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

import com.comebackhome.app.core.GuidancePacket;

import java.io.File;
import java.io.IOException;
import java.text.SimpleDateFormat;
import java.util.Collections;
import java.util.Date;
import java.util.Locale;
import java.util.UUID;

/**
 * 허리 ESP32와 BLE 연결 (정의서 3장). 폰이 Central.
 * 스캔 → 연결 → 서비스 탐색 → 버튼 Notify 구독 → 10Hz 쓰기.
 * 연결이 의도치 않게 끊기면 같은 기기로 재연결을 반복한다.
 * 모든 상태 변경은 메인 스레드에서 한다.
 *
 * 무수신 시 모터 정지(0.5초)는 ESP32 펌웨어 규칙이다 (기준안 5.2). 이 클래스는 끊기면 송신을 멈출 뿐이다.
 */
@SuppressLint("MissingPermission") // 권한은 MainActivity에서 확인한 뒤 호출한다
final class BleLink {
    static final UUID SERVICE = UUID.fromString("8c5a8775-542e-4447-a6d6-572d509d4938");
    static final UUID GUIDANCE_CHAR = UUID.fromString("8b2e38a4-d5bb-41a2-9378-7dd07e3d64dc");
    static final UUID BUTTON_CHAR = UUID.fromString("383de663-08ec-437e-b633-58292aec7c29");
    private static final UUID CCCD = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");
    private static final long PERIOD_MS = 100; // 10Hz (정의서 3.3)
    private static final long RETRY_MS = 1000; // 재연결 간격 (제안)

    private final Context ctx;
    private final Handler main = new Handler(Looper.getMainLooper());
    private BluetoothLeScanner scanner;
    private ScanCallback scanCb;
    private BluetoothDevice device;
    private BluetoothGatt gatt;
    private BluetoothGattCharacteristic guidance, button;
    private EventLog events;
    private File logDir;
    private boolean wantConnected; // true인 동안 끊기면 재연결
    private String fatal;          // 재연결해도 소용없는 실패 사유
    private String logError;
    private int seq, sent, failed, buttons, retries;

    // 시험용 송신 값. MainActivity 슬라이더가 바꾼다 (둘 다 메인 스레드). 실제 안내 값은 2-1에서 연결
    // 기본 state 0 = 진동 없음 (실기 안전)
    private int turnDeg, distCm = 1500, state;

    /** 다음 패킷부터 적용한다 */
    void setTestValues(int turnDeg, int distCm, int state) {
        this.turnDeg = turnDeg;
        this.distCm = distCm;
        this.state = state;
    }

    /** 화면 표시용. 메인 스레드가 쓰고 메인 스레드가 읽는다 */
    private volatile String status = "정지됨";

    BleLink(Context ctx) {
        this.ctx = ctx;
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
        seq = sent = failed = buttons = retries = 0;
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
                    guidance = null;
                    button = null;
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
                BluetoothGattCharacteristic c = svc == null ? null : svc.getCharacteristic(GUIDANCE_CHAR);
                if (gattStatus != BluetoothGatt.GATT_SUCCESS || c == null) {
                    fatal = "서비스 없음: 펌웨어 확인";
                    wantConnected = false; // 재연결해도 같은 결과
                    ev("ble", "no_service");
                    g.disconnect();
                    return;
                }
                c.setWriteType(BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE);
                guidance = c;
                button = svc.getCharacteristic(BUTTON_CHAR);
                subscribeButton(g);
            });
        }

        @Override
        public void onDescriptorWrite(BluetoothGatt g, BluetoothGattDescriptor d, int gattStatus) {
            main.post(() -> {
                if (g != gatt || guidance == null) return;
                ev("ble", gattStatus == BluetoothGatt.GATT_SUCCESS ? "notify_on" : "notify_fail");
                startSending(); // 구독 쓰기가 끝난 뒤에 송신 시작 (GATT 작업은 한 번에 하나)
            });
        }

        // Android 13 이상은 값을 인자로 받고, 12 이하는 characteristic에서 읽는다
        @Override
        public void onCharacteristicChanged(BluetoothGatt g, BluetoothGattCharacteristic c, byte[] value) {
            byte[] v = value.clone();
            main.post(() -> onButton(v));
        }

        @Override
        @SuppressWarnings("deprecation")
        public void onCharacteristicChanged(BluetoothGatt g, BluetoothGattCharacteristic c) {
            byte[] v = c.getValue();
            if (v == null) return;
            byte[] copy = v.clone();
            main.post(() -> onButton(copy));
        }
    };

    /** 버튼 Notify를 켠다. 못 켜면 버튼 없이 송신만 시작한다 */
    private void subscribeButton(BluetoothGatt g) {
        BluetoothGattDescriptor cccd = button == null ? null : button.getDescriptor(CCCD);
        if (cccd == null || !g.setCharacteristicNotification(button, true) || !writeCccd(g, cccd)) {
            ev("ble", "no_button_notify");
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
        main.removeCallbacks(sendLoop);
        main.post(sendLoop);
    }

    /** 정의서 6장: eventType 0 = 짧게 누름, 1 = 길게 누름. 모드 전환 연결은 2-3 */
    private void onButton(byte[] v) {
        if (v.length < 1) return;
        buttons++;
        ev("btn", v[0] & 0xFF);
    }

    private final Runnable sendLoop = new Runnable() {
        @Override
        public void run() {
            if (guidance == null) return;
            int s = seq;
            byte[] pkt = GuidancePacket.encode(turnDeg, distCm, state, s);
            seq = (seq + 1) & 0xFF;
            boolean ok = write(pkt);
            if (ok) sent++;
            else failed++;
            ev("pkt", s, turnDeg, distCm, state, ok ? 1 : 0);
            status = "연결됨: 전송 " + sent + ", 실패 " + failed + ", 버튼 " + buttons;
            main.postDelayed(this, PERIOD_MS);
        }
    };

    private boolean write(byte[] pkt) {
        if (Build.VERSION.SDK_INT >= 33) {
            return gatt.writeCharacteristic(guidance, pkt, BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE)
                    == BluetoothStatusCodes.SUCCESS;
        }
        guidance.setValue(pkt);
        return gatt.writeCharacteristic(guidance);
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
