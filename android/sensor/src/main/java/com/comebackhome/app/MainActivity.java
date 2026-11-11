package com.comebackhome.app;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
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

import java.util.ArrayList;
import java.util.List;

/** 센싱 앱: 몸에 붙이는 폰. 시작을 누르면 센서를 기록하고 ESP32에 IMU를 보낸다. 경로와 모드는 대시보드 앱이 맡는다. */
public class MainActivity extends Activity {
    private static final long REFRESH_MS = 250;

    private final Handler main = new Handler(Looper.getMainLooper());
    private Ui u;
    private TextView recChip, recDetail, bleChip, bleDetail;

    private final Runnable refresh = new Runnable() {
        @Override
        public void run() {
            SensorCollector c = GuideService.collector;
            String err = GuideService.lastError;
            if (c != null) {
                u.setChip(recChip, "센싱 중", Ui.ACCENT);
                recDetail.setText(c.status());
            } else {
                u.setChip(recChip, "정지됨", err != null ? Ui.DANGER : Ui.NEUTRAL);
                recDetail.setText(err != null ? "오류: " + err : "시작을 누르면 센서와 GPS를 기록하고 ESP32에 연결합니다");
            }
            BleLink ble = GuideService.ble;
            String[] b = (ble != null ? ble.status() : "정지됨").split("\n", 2); // 첫 줄은 상태, 나머지는 로그 경로와 오류
            u.setChip(bleChip, b[0], chipKind(b[0]));
            bleDetail.setText(b.length > 1 ? b[1] : "");
            bleDetail.setVisibility(b.length > 1 ? View.VISIBLE : View.GONE);
            main.postDelayed(this, REFRESH_MS);
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        u = new Ui(this);

        Button start = u.button("시작", true);
        start.setOnClickListener(v -> startSession());
        Button stop = u.button("종료", false);
        stop.setOnClickListener(v -> stopService(new Intent(this, GuideService.class)));
        LinearLayout btns = u.row();
        btns.addView(start, u.cell(8));
        btns.addView(stop, u.cell(8));

        recChip = u.chip();
        u.setChip(recChip, "정지됨", Ui.NEUTRAL);
        recDetail = u.mono("", 13, u.textSub);
        bleChip = u.chip();
        u.setChip(bleChip, "정지됨", Ui.NEUTRAL);
        bleDetail = u.mono("", 13, u.textSub);

        LinearLayout rec = u.card("센서 기록");
        rec.addView(recChip, u.wrap(12));
        rec.addView(recDetail, u.full(12));
        LinearLayout bleCard = u.card("ESP32 연결");
        bleCard.addView(bleChip, u.wrap(12));
        bleCard.addView(bleDetail, u.full(12));

        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding(u.dp(16), u.dp(16), u.dp(16), u.dp(24));
        col.addView(u.label("ComeBackHome", 26, u.text, true));
        col.addView(u.label("센싱", 15, u.textSub, false), u.wrap(2));
        col.addView(btns, u.rowOf(16, 8));
        col.addView(rec, u.full(16));
        col.addView(bleCard, u.full(12));

        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(u.bg);
        scroll.setFitsSystemWindows(true);
        scroll.addView(col);
        setContentView(scroll);
        if (Build.VERSION.SDK_INT >= 30) {
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
