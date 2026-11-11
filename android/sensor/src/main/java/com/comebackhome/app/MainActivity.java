package com.comebackhome.app;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.View;
import android.view.Window;
import android.view.WindowInsetsController;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.util.ArrayList;
import java.util.List;

/** 센싱 앱: 몸에 붙이는 폰. 시작을 누르면 센서를 기록하고 ESP32에 IMU를 보낸다. 경로와 모드는 대시보드 앱이 맡는다. */
public class MainActivity extends Activity {
    private static final long REFRESH_MS = 250;

    private final Handler main = new Handler(Looper.getMainLooper());
    private Ui u;
    private TextView bigLabel, bigSub, sensorState, bleState, recDetail, bleDetail;
    private View bigDot;

    private final Runnable refresh = new Runnable() {
        @Override
        public void run() {
            SensorCollector c = GuideService.collector;
            String err = GuideService.lastError;
            BleLink ble = GuideService.ble;
            String bleLine = (ble != null ? ble.status() : "정지됨").split("\n", 2)[0];
            boolean linked = bleLine.startsWith("연결됨");

            bigLabel.setText(c != null ? "센싱 중" : "정지됨");
            bigSub.setText(c == null ? "시작을 누르면 센서를 기록합니다" : linked ? "IMU 50Hz 전송" : "ESP32 연결 대기");
            bigDot.getBackground().setTint(c == null ? u.textSub : linked ? u.accent : u.warn);

            sensorState.setText(c != null ? "정상" : err != null ? "오류" : "정지");
            sensorState.setTextColor(c != null ? u.accent : err != null ? u.danger : u.textSub);
            bleState.setText(bleLine.startsWith("연결됨") ? "연결됨" : bleLine);
            bleState.setTextColor(linked ? u.accent : chipDanger(bleLine) ? u.danger : u.textSub);

            recDetail.setText(c != null ? c.status() : err != null ? "오류: " + err : "");
            String[] b = (ble != null ? ble.status() : "").split("\n", 2);
            bleDetail.setText(b.length > 1 ? b[1] : "");
            main.postDelayed(this, REFRESH_MS);
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        u = new Ui(this);

        // 큰 상태
        bigDot = u.dot(u.textSub, 18);
        bigLabel = u.label("정지됨", 28, u.text, true);
        bigSub = u.label("", 14, u.textSub, false);
        LinearLayout big = u.card(null);
        big.setGravity(Gravity.CENTER_HORIZONTAL);
        big.setPadding(u.dp(16), u.dp(28), u.dp(16), u.dp(28));
        big.addView(bigDot);
        big.addView(bigLabel, u.wrap(12));
        big.addView(bigSub, u.wrap(6));

        // 센서와 ESP32 상태 두 줄
        sensorState = u.label("정지", 16, u.textSub, true);
        bleState = u.label("정지됨", 16, u.textSub, true);
        LinearLayout rows = u.card(null);
        rows.addView(stateRow("센서", sensorState));
        rows.addView(stateRow("ESP32", bleState), u.full(12));

        // 시작 / 종료
        Button start = u.big("시작", u.text, u.onAccent);
        start.setOnClickListener(v -> startSession());
        Button stop = u.big("종료", u.dangerBg, u.danger);
        stop.setOnClickListener(v -> stopService(new Intent(this, GuideService.class)));
        LinearLayout btns = u.row();
        btns.addView(start, u.cell(10));
        btns.addView(stop, u.cell(10));

        // 상세: 센서와 GPS 수치, 로그 경로
        recDetail = u.mono("", 13, u.textSub);
        bleDetail = u.mono("", 13, u.textSub);
        LinearLayout detailBody = new LinearLayout(this);
        detailBody.setOrientation(LinearLayout.VERTICAL);
        detailBody.addView(recDetail, u.full(0));
        detailBody.addView(bleDetail, u.full(8));
        LinearLayout detail = u.collapsible("상세", detailBody);

        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding(u.dp(16), u.dp(16), u.dp(16), u.dp(24));
        col.addView(u.label("센싱", 24, u.text, true));
        col.addView(big, u.full(16));
        col.addView(rows, u.full(12));
        col.addView(btns, u.rowOf(12, 10));
        col.addView(detail, u.full(16));

        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(u.bg);
        scroll.setFitsSystemWindows(true);
        scroll.addView(col);
        setContentView(scroll);
        if (Build.VERSION.SDK_INT >= 30) { // 어두운 배경이라 상태바 글자는 밝게 둔다
            getWindow().getInsetsController().setSystemBarsAppearance(0,
                    WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS
                            | WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS);
        }
        requestNeededPermissions();
    }

    private LinearLayout stateRow(String name, TextView state) {
        LinearLayout r = u.row();
        r.addView(u.label(name, 16, u.text, false), new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
        r.addView(state);
        return r;
    }

    private static boolean chipDanger(String s) {
        return s.contains("실패") || s.contains("없음") || s.contains("꺼짐") || s.contains("끊김");
    }

    private void startSession() {
        boolean ok = checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED;
        if (Build.VERSION.SDK_INT >= 31) {
            ok &= checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN) == PackageManager.PERMISSION_GRANTED
                    && checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED;
        }
        if (!ok) {
            requestNeededPermissions();
            return;
        }
        startForegroundService(new Intent(this, GuideService.class));
    }

    private void requestNeededPermissions() {
        List<String> p = new ArrayList<>();
        p.add(Manifest.permission.ACCESS_FINE_LOCATION);
        p.add(Manifest.permission.ACCESS_COARSE_LOCATION);
        if (Build.VERSION.SDK_INT >= 31) {
            p.add(Manifest.permission.BLUETOOTH_SCAN);
            p.add(Manifest.permission.BLUETOOTH_CONNECT);
        }
        if (Build.VERSION.SDK_INT >= 33) p.add(Manifest.permission.POST_NOTIFICATIONS);
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
}
