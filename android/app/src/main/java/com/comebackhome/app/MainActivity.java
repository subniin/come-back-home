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
import android.widget.SeekBar;
import android.widget.TextView;

import java.util.ArrayList;
import java.util.List;

/** 시험 화면: 센서 기록, ESP32 연결, 진동 시험 값 송신. */
public class MainActivity extends Activity {
    private static final long REFRESH_MS = 250;
    private static final String[] STATE_SHORT = {"0 대기", "1 아웃", "2 기록", "3 복귀"};
    private static final String[] STATE_DESC = {"대기, 진동 없음", "아웃사이드, 진동", "인사이드 기록, 진동 없음", "백트래킹, 진동"};

    private final Handler main = new Handler(Looper.getMainLooper());
    private BleLink ble;
    private Ui u;

    private TextView recChip, recDetail, bleChip, bleDetail;
    private TextView angleVal, distVal, stateDesc, warn;
    private final Button[] stateBtns = new Button[4];
    // 시험 송신 값 (BleLink로 전달). 기본은 진동 없는 state 0
    private int testAngle, testDist = 1500, testState;

    private final Runnable refresh = new Runnable() {
        @Override
        public void run() {
            SensorCollector c = GuideService.collector;
            String err = GuideService.lastError;
            if (c != null) {
                u.setChip(recChip, "기록 중", Ui.ACCENT);
                recDetail.setText(c.status());
            } else {
                u.setChip(recChip, "정지됨", err != null ? Ui.DANGER : Ui.NEUTRAL);
                recDetail.setText(err != null ? "오류: " + err : "기록을 시작하면 센서와 GPS 상태가 여기에 표시됩니다");
            }
            String[] b = ble.status().split("\n", 2); // 첫 줄은 상태, 나머지는 로그 경로와 오류
            u.setChip(bleChip, b[0], chipKind(b[0]));
            bleDetail.setText(b.length > 1 ? b[1] : "");
            bleDetail.setVisibility(b.length > 1 ? View.VISIBLE : View.GONE); // 비었을 때 빈 공간 방지
            main.postDelayed(this, REFRESH_MS);
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        requestWindowFeature(Window.FEATURE_NO_TITLE); // 기본 액션바 대신 화면 안의 제목을 쓴다
        ble = new BleLink(this);
        u = new Ui(this);

        // 센서 기록
        Button start = u.button("기록 시작", true);
        start.setOnClickListener(v -> startSession());
        Button stop = u.button("기록 종료", false);
        stop.setOnClickListener(v -> stopService(new Intent(this, GuideService.class)));
        recChip = u.chip();
        u.setChip(recChip, "정지됨", Ui.NEUTRAL);
        recDetail = u.mono("", 13, u.textSub);

        LinearLayout recBtns = u.row();
        recBtns.addView(start, u.cell(8));
        recBtns.addView(stop, u.cell(8));

        LinearLayout rec = u.card("센서 기록");
        rec.addView(recChip, u.wrap(12));
        rec.addView(recBtns, u.rowOf(12, 8));
        rec.addView(recDetail, u.full(12));

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

        // 진동 시험: turnAngle(-180~180), distanceCm(0~3000, 10cm 단위), state(0~3)
        SeekBar angleBar = u.seek(360);
        angleBar.setProgress(testAngle + 180);
        angleBar.setOnSeekBarChangeListener(new Change() {
            @Override
            void onChange(int progress) {
                testAngle = progress - 180;
                pushTest();
            }
        });
        angleVal = u.mono("", 28, u.text);

        int[][] presetRows = {{0, 45, 90, 135}, {180, -135, -90, -45}}; // 8방향 모터 시험용
        LinearLayout presets = new LinearLayout(this);
        presets.setOrientation(LinearLayout.VERTICAL);
        for (int[] angles : presetRows) {
            LinearLayout r = u.row();
            for (int a : angles) {
                Button b = u.button(a + "°", false);
                b.setOnClickListener(v -> angleBar.setProgress(a + 180));
                r.addView(b, u.cell(8));
            }
            presets.addView(r, u.rowOf(8, 8));
        }

        SeekBar distBar = u.seek(300);
        distBar.setProgress(testDist / 10);
        distBar.setOnSeekBarChangeListener(new Change() {
            @Override
            void onChange(int progress) {
                testDist = progress * 10;
                pushTest();
            }
        });
        distVal = u.mono("", 22, u.text);

        LinearLayout stateRow = u.row();
        for (int i = 0; i < stateBtns.length; i++) {
            int state = i;
            stateBtns[i] = u.button(STATE_SHORT[i], false);
            stateBtns[i].setOnClickListener(v -> {
                testState = state;
                pushTest();
            });
            stateRow.addView(stateBtns[i], u.cell(8));
        }
        stateDesc = u.label("", 14, u.textSub, false);
        warn = u.banner("이 모드에서는 모터가 진동합니다");

        LinearLayout test = u.card("진동 시험");
        test.addView(u.label("방향 turnAngle", 14, u.textSub, false), u.wrap(16));
        test.addView(angleVal, u.wrap(0));
        test.addView(angleBar, u.full(0));
        test.addView(presets, u.full(4));
        test.addView(u.label("거리 distance", 14, u.textSub, false), u.wrap(20));
        test.addView(distVal, u.wrap(0));
        test.addView(distBar, u.full(0));
        test.addView(u.label("모드 state", 14, u.textSub, false), u.wrap(20));
        test.addView(stateRow, u.rowOf(8, 8));
        test.addView(stateDesc, u.full(8));
        test.addView(warn, u.full(12));

        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding(u.dp(16), u.dp(16), u.dp(16), u.dp(24));
        col.addView(u.label("ComeBackHome", 26, u.text, true));
        col.addView(u.label("시험 도구", 15, u.textSub, false), u.wrap(2));
        col.addView(rec, u.full(16));
        col.addView(bleCard, u.full(12));
        col.addView(test, u.full(12));

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
        pushTest();

        requestNeededPermissions();
    }

    private static int chipKind(String s) {
        if (s.startsWith("연결됨")) return Ui.ACCENT;
        if (s.contains("실패") || s.contains("없음") || s.contains("꺼짐") || s.contains("끊김")) return Ui.DANGER;
        return Ui.NEUTRAL;
    }

    private void startSession() {
        if (checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED) {
            requestNeededPermissions();
            return;
        }
        startForegroundService(new Intent(this, GuideService.class));
    }

    private void connectBle() {
        if (!hasBlePermission()) {
            requestNeededPermissions();
            return;
        }
        ble.connect();
    }

    /** 시험 값을 BleLink에 넘기고 화면에 표시한다 */
    private void pushTest() {
        ble.setTestValues(testAngle, testDist, testState);
        angleVal.setText(testAngle + "°");
        distVal.setText(testDist + " cm");
        for (int i = 0; i < stateBtns.length; i++) u.setFilled(stateBtns[i], i == testState);
        stateDesc.setText(STATE_DESC[testState]);
        warn.setVisibility(testState == 1 || testState == 3 ? View.VISIBLE : View.GONE);
    }

    /** SeekBar 값 변경만 받는 리스너 */
    private abstract static class Change implements SeekBar.OnSeekBarChangeListener {
        abstract void onChange(int progress);

        @Override
        public void onProgressChanged(SeekBar bar, int progress, boolean fromUser) {
            onChange(progress);
        }

        @Override
        public void onStartTrackingTouch(SeekBar bar) {
        }

        @Override
        public void onStopTrackingTouch(SeekBar bar) {
        }
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
        p.add(Manifest.permission.ACCESS_FINE_LOCATION);
        p.add(Manifest.permission.ACCESS_COARSE_LOCATION);
        if (Build.VERSION.SDK_INT >= 31) {
            p.add(Manifest.permission.BLUETOOTH_SCAN);
            p.add(Manifest.permission.BLUETOOTH_CONNECT);
        }
        if (Build.VERSION.SDK_INT >= 33) {
            p.add(Manifest.permission.POST_NOTIFICATIONS);
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
