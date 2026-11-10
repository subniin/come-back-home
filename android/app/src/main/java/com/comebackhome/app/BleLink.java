package com.comebackhome.app;

import android.annotation.SuppressLint;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.BluetoothStatusCodes;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanFilter;
import android.bluetooth.le.ScanResult;
import android.bluetooth.le.ScanSettings;
import android.content.Context;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelUuid;

import com.comebackhome.app.core.LineProtocol;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.ArrayDeque;
import java.util.Collections;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.UUID;

/**
 * 허리 ESP32와 BLE 연결. 폰이 Central. ESP32 펌웨어(main.cpp)의 Nordic UART Service(NUS)를 쓴다.
 * 스캔 → 연결 → 서비스 탐색 → TX Notify 구독 → TRACK ON → 20ms 루프로 명령/IMU 송신.
 *  - 앱→기기(RX 특성): '\n'으로 끝나는 텍스트 한 줄. 사용자 명령이 IMU보다 먼저 나간다.
 *  - 기기→앱(TX 특성): 20바이트 조각으로 오므로 '\n'까지 모아 한 줄로 만들어 리스너에 넘긴다.
 * 연결이 의도치 않게 끊기면 같은 기기로 재연결을 반복한다.
 * 모든 상태 변경은 메인 스레드에서 한다.
 */
@SuppressLint("MissingPermission") // 권한은 MainActivity에서 확인한 뒤 호출한다
final class BleLink {
    static final UUID SERVICE = UUID.fromString("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
    static final UUID RX_CHAR = UUID.fromString("6E400002-B5A3-F393-E0A9-E50E24DCCA9E"); // 앱 → 기기
    static final UUID TX_CHAR = UUID.fromString("6E400003-B5A3-F393-E0A9-E50E24DCCA9E"); // 기기 → 앱
    private static final UUID CCCD = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");
    private static final long PERIOD_MS = 20;  // 50Hz IMU (펌웨어 걸음 감지 기준)
    private static final long RETRY_MS = 1000; // 재연결 간격 (제안)
    private static final int MAX_QUEUE = 32;

    /** 기기에서 온 한 줄. 메인 스레드에서 호출된다 */
    interface LineListener {
        void onLine(String line);
    }

    private final Context ctx;
    private final Handler main = new Handler(Looper.getMainLooper());
    private BluetoothLeScanner scanner;
    private ScanCallback scanCb;
    private BluetoothDevice device;
    private BluetoothGatt gatt;
    private BluetoothGattCharacteristic rx, tx;
    private EventLog events;
    private File logDir;
    private boolean wantConnected; // true인 동안 끊기면 재연결
    private String fatal;          // 재연결해도 소용없는 실패 사유
    private String logError;
    private int sent, failed, lines, retries;

    private LineListener listener;
    private final ArrayDeque<String> commands = new ArrayDeque<>();
    private final LineProtocol.LineAssembler assembler = new LineProtocol.LineAssembler();

    /** 화면 표시용. 메인 스레드가 쓰고 메인 스레드가 읽는다 */
    private volatile String status = "정지됨";

    BleLink(Context ctx) {
        this.ctx = ctx;
    }

    void setLineListener(LineListener l) {
        listener = l;
    }

    /**
     * 명령 한 줄을 보낸다 (개행은 자동으로 붙인다). 메인 스레드에서 호출한다.
     * 연결 전이면 버리고 false. 쓰기 실패한 명령은 재시도하지 않는다.
     */
    boolean send(String cmd) {
        if (rx == null || commands.size() >= MAX_QUEUE) return false;
        commands.add(cmd);
        return true;
    }

    String status() {
        String err = logError != null ? logError : events != null ? events.error() : null;
        return status
                + (logDir != null ? "\n로그: " + logDir : "")
                + (err != null ? "\n로그 오류: " + err : "");
    }

    void connect() {
        if (wantConnected) return; // 이미 스캔 중, 연결 중, 재연결 대기 중
        BluetoothAdapter adapter = ctx.getSystemService(BluetoothManager.class).getAdapter();
        if (adapter == null || !adapter.isEnabled()) {
            status = "블루투스 꺼짐 또는 미지원";
            return;
        }
        wantConnected = true;
        fatal = null;
        sent = failed = lines = retries = 0;
        openLog();
        scanner = adapter.getBluetoothLeScanner();
        scanCb = new ScanCallback() {
            @Override
            public void onScanResult(int callbackType, ScanResult r) {
                main.post(() -> {
                    if (scanCb == null) return; // 이미 찾음 또는 중지됨
                    stopScan();
                    device = r.getDevice();
                    ev("ble", "found", device.getAddress());
                    connectDevice();
                });
            }

            @Override
            public void onScanFailed(int errorCode) {
                main.post(() -> {
                    scanCb = null;
                    wantConnected = false;
                    status = "스캔 실패: " + errorCode;
                    ev("ble", "scan_failed", errorCode);
                    closeLog();
                });
            }
        };
        ScanFilter filter = new ScanFilter.Builder().setServiceUuid(new ParcelUuid(SERVICE)).build();
        ScanSettings settings = new ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build();
        scanner.startScan(Collections.singletonList(filter), settings, scanCb);
        status = "스캔 중";
        ev("ble", "scan");
    }

    void disconnect() {
        wantConnected = false;
        main.removeCallbacks(reconnect);
        main.removeCallbacks(sendLoop);
        stopScan();
        if (gatt != null) {
            gatt.disconnect(); // 끊김 콜백에서 close하고 로그를 닫는다
        } else {
            status = "정지됨";
            closeLog();
        }
    }

    private void stopScan() {
        if (scanCb == null) return;
        scanner.stopScan(scanCb);
        scanCb = null;
    }

    private void connectDevice() {
        status = retries == 0 ? "연결 중" : "재연결 중 (" + retries + "회째)";
        ev("ble", "connecting", retries);
        gatt = device.connectGatt(ctx, false, gattCb, BluetoothDevice.TRANSPORT_LE);
    }

    private final Runnable reconnect = new Runnable() {
        @Override
        public void run() {
            if (wantConnected && device != null) connectDevice();
        }
    };

    private final BluetoothGattCallback gattCb = new BluetoothGattCallback() {
        @Override
        public void onConnectionStateChange(BluetoothGatt g, int gattStatus, int newState) {
            main.post(() -> {
                if (g != gatt) { // 이미 정리한 연결의 늦은 콜백
                    g.close();
                    return;
                }
                if (gattStatus == BluetoothGatt.GATT_SUCCESS && newState == BluetoothProfile.STATE_CONNECTED) {
                    status = "서비스 탐색 중";
                    g.discoverServices();
                } else if (newState == BluetoothProfile.STATE_DISCONNECTED || gattStatus != BluetoothGatt.GATT_SUCCESS) {
                    main.removeCallbacks(sendLoop);
                    rx = null;
                    tx = null;
                    commands.clear();
                    assembler.reset();
                    gatt = null;
                    g.close();
                    ev("ble", "disconnected", gattStatus);
                    if (wantConnected) {
                        retries++;
                        status = "끊김, 재연결 대기 (" + retries + "회째)";
                        main.postDelayed(reconnect, RETRY_MS);
                    } else {
                        status = fatal != null ? fatal : "정지됨";
                        closeLog();
                    }
                }
            });
        }

        @Override
        public void onServicesDiscovered(BluetoothGatt g, int gattStatus) {
            main.post(() -> {
                if (g != gatt) return;
                BluetoothGattService svc = g.getService(SERVICE);
                BluetoothGattCharacteristic r = svc == null ? null : svc.getCharacteristic(RX_CHAR);
                BluetoothGattCharacteristic t = svc == null ? null : svc.getCharacteristic(TX_CHAR);
                if (gattStatus != BluetoothGatt.GATT_SUCCESS || r == null || t == null) {
                    fatal = "서비스 없음: 펌웨어 확인";
                    wantConnected = false; // 재연결해도 같은 결과
                    ev("ble", "no_service");
                    g.disconnect();
                    return;
                }
                r.setWriteType(BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE);
                rx = r;
                tx = t;
                subscribeTx(g);
            });
        }

        @Override
        public void onDescriptorWrite(BluetoothGatt g, BluetoothGattDescriptor d, int gattStatus) {
            main.post(() -> {
                if (g != gatt || rx == null) return;
                ev("ble", gattStatus == BluetoothGatt.GATT_SUCCESS ? "notify_on" : "notify_fail");
                startSending(); // 구독 쓰기가 끝난 뒤에 송신 시작 (GATT 작업은 한 번에 하나)
            });
        }

        // Android 13 이상은 값을 인자로 받고, 12 이하는 characteristic에서 읽는다
        @Override
        public void onCharacteristicChanged(BluetoothGatt g, BluetoothGattCharacteristic c, byte[] value) {
            byte[] v = value.clone();
            main.post(() -> onChunk(v));
        }

        @Override
        @SuppressWarnings("deprecation")
        public void onCharacteristicChanged(BluetoothGatt g, BluetoothGattCharacteristic c) {
            byte[] v = c.getValue();
            if (v == null) return;
            byte[] copy = v.clone();
            main.post(() -> onChunk(copy));
        }
    };

    /** TX Notify를 켠다. 못 켜면 수신 없이 송신만 시작한다 */
    private void subscribeTx(BluetoothGatt g) {
        BluetoothGattDescriptor cccd = tx.getDescriptor(CCCD);
        if (cccd == null || !g.setCharacteristicNotification(tx, true) || !writeCccd(g, cccd)) {
            ev("ble", "no_tx_notify");
            startSending();
        }
    }

    private boolean writeCccd(BluetoothGatt g, BluetoothGattDescriptor d) {
        if (Build.VERSION.SDK_INT >= 33) {
            return g.writeDescriptor(d, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
                    == BluetoothStatusCodes.SUCCESS;
        }
        d.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);
        return g.writeDescriptor(d);
    }

    private void startSending() {
        retries = 0;
        status = "연결됨";
        ev("ble", "connected");
        commands.clear();
        commands.add("TRACK ON"); // 걸음마다 STEP,t,x,y,heading 로그를 받는다
        main.removeCallbacks(sendLoop);
        main.post(sendLoop);
    }

    private void onChunk(byte[] chunk) {
        List<String> got = assembler.feed(chunk);
        for (String line : got) {
            lines++;
            ev("rx", line);
            if (listener != null) listener.onLine(line);
        }
    }

    private final Runnable sendLoop = new Runnable() {
        @Override
        public void run() {
            if (rx == null) return;
            String cmd = commands.poll();
            String out = null;
            if (cmd != null) {
                out = cmd;
                ev("tx", cmd);
            } else if (GuideService.collector != null) { // 센서 기록 중일 때만 IMU 송신
                float acc = GuideService.collector.accG(), head = GuideService.collector.headingDeg();
                if (!Float.isNaN(acc) && !Float.isNaN(head)) out = LineProtocol.imuLine(acc, head);
            }
            if (out != null) {
                if (write((out + "\n").getBytes(StandardCharsets.UTF_8))) sent++;
                else {
                    failed++; // ponytail: 실패한 명령은 재시도하지 않고 로그만 남긴다
                    ev("tx_fail", out);
                }
            }
            status = "연결됨: 송신 " + sent + ", 실패 " + failed + ", 수신 " + lines + "줄";
            main.postDelayed(this, PERIOD_MS);
        }
    };

    private boolean write(byte[] data) {
        if (Build.VERSION.SDK_INT >= 33) {
            return gatt.writeCharacteristic(rx, data, BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE)
                    == BluetoothStatusCodes.SUCCESS;
        }
        rx.setValue(data);
        return gatt.writeCharacteristic(rx);
    }

    private void openLog() {
        File base = ctx.getExternalFilesDir(null);
        if (base == null) base = ctx.getFilesDir();
        String session = new SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(new Date());
        logDir = new File(base, "logs/" + session);
        try {
            events = new EventLog(logDir);
            logError = null;
        } catch (IOException e) {
            events = null; // 로그 없이도 연결은 진행한다
            logError = e.toString();
        }
    }

    private void closeLog() {
        if (events == null) return;
        events.close();
        events = null;
    }

    private void ev(String type, Object... cols) {
        if (events != null) events.log(type, cols);
    }
}
