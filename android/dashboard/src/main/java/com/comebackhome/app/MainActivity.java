package com.comebackhome.app;

import android.Manifest;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.View;
import android.view.Window;
import android.view.WindowInsetsController;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import com.comebackhome.app.core.LineProtocol;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;

/** 대시보드: ESP32 연결, 모드 명령, 기록 경로와 복귀 안내, 수신 로그. IMU는 센싱 앱이 보낸다. */
public class MainActivity extends Activity {
    private static final long REFRESH_MS = 250;
    private static final int MAX_LOG_LINES = 50;

    private final Handler main = new Handler(Looper.getMainLooper());
    private BleLink ble;
    private Ui u;

    private TextView bleChip, bleDetail;
    private TextView pathStatus, logView;
    private PathView path;
    private final ArrayDeque<String> logLines = new ArrayDeque<>();
    private boolean logDirty;
    private String phaseText = "대기", guideText = "";

    private final Runnable refresh = new Runnable() {
        @Override
        public void run() {
            String[] b = ble.status().split("\n", 2); // 첫 줄은 상태, 나머지는 로그 경로와 오류
            u.setChip(bleChip, b[0], chipKind(b[0]));
            bleDetail.setText(b.length > 1 ? b[1] : "");
            bleDetail.setVisibility(b.length > 1 ? View.VISIBLE : View.GONE); // 비었을 때 빈 공간 방지
            pathStatus.setText(guideText.isEmpty() ? phaseText : phaseText + "\n" + guideText);
            if (logDirty) { // 줄마다 갱신하지 않고 주기마다 한 번만 그린다
                logDirty = false;
                logView.setText(String.join("\n", logLines));
            }
            main.postDelayed(this, REFRESH_MS);
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        requestWindowFeature(Window.FEATURE_NO_TITLE); // 기본 액션바 대신 화면 안의 제목을 쓴다
        ble = new BleLink(this);
        u = new Ui(this);

        // ESP32 연결
        Button bleConnect = u.button("연결", true);
        bleConnect.setOnClickListener(v -> connectBle());
        Button bleDisconnect = u.button("해제", false);
        bleDisconnect.setOnClickListener(v -> ble.disconnect());
        bleChip = u.chip();
        u.setChip(bleChip, "정지됨", Ui.NEUTRAL);
        bleDetail = u.mono("", 13, u.textSub);

        LinearLayout bleBtns = u.row();
        bleBtns.addView(bleConnect, u.cell(8));
        bleBtns.addView(bleDisconnect, u.cell(8));

        LinearLayout bleCard = u.card("ESP32 연결");
        bleCard.addView(bleChip, u.wrap(12));
        bleCard.addView(bleBtns, u.rowOf(12, 8));
        bleCard.addView(bleDetail, u.full(12));

        // 모드 명령 (ESP32 펌웨어 명령, 개행은 BleLink가 붙인다)
        LinearLayout modeRows = new LinearLayout(this);
        modeRows.setOrientation(LinearLayout.VERTICAL);
        modeRows.addView(cmdRow("START", "RETURN", "CANCEL"), u.rowOf(0, 8));
        modeRows.addView(cmdRow("ENV IN", "ENV OUT", "AUTO"), u.rowOf(8, 8));
        modeRows.addView(cmdRow("DIR 0", "DIR 1", "DIR 2", "DIR 3"), u.rowOf(8, 8));
        modeRows.addView(cmdRow("DIR 4", "DIR 5", "DIR 6", "DIR 7"), u.rowOf(8, 8));
        modeRows.addView(cmdRow("OFF", "STATUS"), u.rowOf(8, 8));

        LinearLayout mode = u.card("모드");
        mode.addView(modeRows, u.full(12));
        mode.addView(u.banner("RETURN과 DIR은 모터가 진동합니다"), u.full(12));

        // 기록 경로 + 복귀 안내
        pathStatus = u.mono("", 14, u.text);
        path = new PathView(this, u);
        LinearLayout pathCard = u.card("경로");
        pathCard.addView(pathStatus, u.full(12));
        pathCard.addView(path, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, u.dp(280)));

        // 수신 로그
        logView = u.mono("", 12, u.textSub);
        logView.setTextIsSelectable(true);
        LinearLayout logCard = u.card("수신 로그");
        logCard.addView(logView, u.full(12));

        ble.setLineListener(this::onLine);

        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding(u.dp(16), u.dp(16), u.dp(16), u.dp(24));
        col.addView(u.label("ComeBackHome", 26, u.text, true));
        col.addView(u.label("대시보드", 15, u.textSub, false), u.wrap(2));
        col.addView(bleCard, u.full(16));
        col.addView(mode, u.full(12));
        col.addView(pathCard, u.full(12));
        col.addView(logCard, u.full(12));

        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(u.bg);
        scroll.setFitsSystemWindows(true); // 상태바와 내비게이션 바 아래로 내용이 들어가지 않게
        scroll.addView(col);
        setContentView(scroll);
        if (Build.VERSION.SDK_INT >= 30) { // 밝은 배경에서는 상태바 글자를 어둡게
            int light = WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS
                    | WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS;
            getWindow().getInsetsController().setSystemBarsAppearance(u.night ? 0 : light, light);
        }
        requestNeededPermissions();
    }

    private static int chipKind(String s) {
        if (s.startsWith("연결됨")) return Ui.ACCENT;
        if (s.contains("실패") || s.contains("없음") || s.contains("꺼짐") || s.contains("끊김")) return Ui.DANGER;
        return Ui.NEUTRAL;
    }

    private void connectBle() {
        if (!hasBlePermission()) {
            requestNeededPermissions();
            return;
        }
        ble.connect();
    }

    /** 같은 너비의 명령 버튼 한 줄. 버튼 글자가 곧 보내는 명령이다 */
    private LinearLayout cmdRow(String... cmds) {
        LinearLayout r = u.row();
        for (String cmd : cmds) {
            Button b = u.button(cmd, false);
            b.setOnClickListener(v -> {
                if (!ble.send(cmd)) addLog("! 연결 안 됨: " + cmd);
            });
            r.addView(b, u.cell(8));
        }
        return r;
    }

    /** 기기에서 온 한 줄 (메인 스레드) */
    private void onLine(String line) {
        addLog(line);
        LineProtocol.Step st = LineProtocol.parseStep(line);
        if (st != null) {
            path.addStep(st);
            return;
        }
        LineProtocol.Guide g = LineProtocol.parseGuide(line);
        if (g != null) {
            path.setGuide(g);
            guideText = String.format(java.util.Locale.US, "방향 %d, 경유점 %d, 남은 %.1fm", g.dir, g.wp, g.remain);
            return;
        }
        LineProtocol.Phase ph = LineProtocol.parsePhase(line);
        if (ph != null) {
            phaseText = ph.raw;
            if (ph.name.equals("RECORDING")) path.clear(); // 새 기록 시작
            if (!ph.name.equals("RETURNING")) { // 복귀가 끝나거나 취소되면 안내를 지운다
                path.setGuide(null);
                guideText = "";
            }
        }
    }

    private void addLog(String line) {
        logLines.add(line);
        while (logLines.size() > MAX_LOG_LINES) logLines.poll();
        logDirty = true;
    }

    /** Android 12 이상은 BLUETOOTH_SCAN/CONNECT 런타임 권한, 11 이하는 위치 권한으로 스캔 */
    private boolean hasBlePermission() {
        if (Build.VERSION.SDK_INT >= 31) {
            return checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN) == PackageManager.PERMISSION_GRANTED
                    && checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED;
        }
        return checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED;
    }

    private void requestNeededPermissions() {
        List<String> p = new ArrayList<>();
        if (Build.VERSION.SDK_INT >= 31) {
            p.add(Manifest.permission.BLUETOOTH_SCAN);
            p.add(Manifest.permission.BLUETOOTH_CONNECT);
        } else {
            p.add(Manifest.permission.ACCESS_FINE_LOCATION);
        }
        requestPermissions(p.toArray(new String[0]), 1);
    }

    @Override
    protected void onResume() {
        super.onResume();
        main.post(refresh);
    }

    @Override
    protected void onPause() {
        main.removeCallbacks(refresh);
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        ble.disconnect(); // GATT 누수 방지
        super.onDestroy();
    }
}
