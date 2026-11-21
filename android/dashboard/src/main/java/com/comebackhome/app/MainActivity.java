package com.comebackhome.app;

import android.Manifest;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Looper;
import android.view.Gravity;
import android.view.View;
import android.view.Window;
import android.view.WindowInsetsController;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import com.comebackhome.app.core.LineProtocol;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;

/** 대시보드: ESP32 연결, 복귀 모드 명령, 기록 경로와 복귀 안내. 시험용 명령과 수신 로그는 개발자 메뉴에 접어 둔다. IMU는 센싱 앱이 보낸다. */
public class MainActivity extends Activity {
    private static final long REFRESH_MS = 250;
    private static final int MAX_LOG_LINES = 50;

    private final Handler main = new Handler(Looper.getMainLooper());
    private BleLink ble;
    private Ui u;

    private TextView bleChip, bleDetail, bleToggle;
    private TextView phaseView, metaView, logView, envIn, envOut;
    private View phaseDot;
    private PathView path;
    private final ArrayDeque<String> logLines = new ArrayDeque<>();
    private boolean logDirty;
    private String phaseName = "IDLE", guideText = "";

    private final Runnable refresh = new Runnable() {
        @Override
        public void run() {
            String[] b = ble.status().split("\n", 2); // 첫 줄은 상태, 나머지는 로그 경로와 오류
            u.setChip(bleChip, b[0], chipKind(b[0]));
            bleToggle.setText(isIdle(b[0]) ? "연결" : "해제");
            bleDetail.setText(b.length > 1 ? b[1] : "");
            bleDetail.setVisibility(b.length > 1 ? View.VISIBLE : View.GONE); // 비었을 때 빈 공간 방지
            int color = phaseColor(phaseName);
            phaseView.setText(phaseLabel(phaseName));
            phaseView.setTextColor(color);
            phaseDot.getBackground().setTint(color);
            metaView.setText(guideText);
            metaView.setVisibility(guideText.isEmpty() ? View.GONE : View.VISIBLE);
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

        // 머리줄: 제목, 연결 상태, 연결/해제
        bleChip = u.chip();
        u.setChip(bleChip, "정지됨", Ui.NEUTRAL);
        Button toggle = u.button("연결", false);
        bleToggle = toggle;
        toggle.setOnClickListener(v -> {
            if (isIdle(ble.status().split("\n", 2)[0])) connectBle();
            else ble.disconnect();
        });
        LinearLayout head = u.row();
        head.setGravity(Gravity.CENTER_VERTICAL);
        head.addView(u.label("대시보드", 24, u.text, true), new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
        head.addView(bleChip, u.wrap(0));
        LinearLayout.LayoutParams tp = new LinearLayout.LayoutParams(u.dp(84), LinearLayout.LayoutParams.WRAP_CONTENT);
        tp.setMarginStart(u.dp(8));
        head.addView(toggle, tp);

        // 경로 + 복귀 안내
        phaseDot = u.dot(u.text, 10);
        phaseView = u.label("대기", 20, u.text, true);
        metaView = u.label("", 14, u.textSub, false);
        LinearLayout phaseRow = u.row();
        phaseRow.setGravity(Gravity.CENTER_VERTICAL);
        phaseRow.addView(phaseDot);
        LinearLayout.LayoutParams np = u.wrap(0);
        np.setMarginStart(u.dp(8));
        phaseRow.addView(phaseView, np);
        path = new PathView(this, u);
        LinearLayout pathCard = u.card(null);
        pathCard.addView(phaseRow);
        pathCard.addView(metaView, u.full(4));
        pathCard.addView(path, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, u.dp(300)));

        // 모드 명령 (ESP32 펌웨어 명령, 개행은 BleLink가 붙인다)
        LinearLayout modes = u.row();
        modes.addView(modeButton("시작", u.text, u.onAccent, "START"), u.cell(10));
        modes.addView(modeButton("복귀", u.warn, u.onAccent, "RETURN"), u.cell(10));
        modes.addView(modeButton("취소", u.chipBg, u.text, "CANCEL"), u.cell(10));

        // 실내/실외: 토글 하나. 고르면 ENV IN 또는 ENV OUT을 보낸다
        envIn = envSegment("실내", "ENV IN");
        envOut = envSegment("실외", "ENV OUT");
        selectEnv(true);
        LinearLayout env = u.row();
        env.setBackground(pill(u.surface, 14));
        env.setPadding(u.dp(4), u.dp(4), u.dp(4), u.dp(4));
        env.addView(envIn, new LinearLayout.LayoutParams(0, u.dp(48), 1f));
        env.addView(envOut, new LinearLayout.LayoutParams(0, u.dp(48), 1f));

        // 개발자 메뉴: 시험용 명령, 연결 상세, 수신 로그
        bleDetail = u.mono("", 13, u.textSub);
        LinearLayout devRows = new LinearLayout(this);
        devRows.setOrientation(LinearLayout.VERTICAL);
        devRows.addView(cmdRow("AUTO", "OFF", "STATUS"), u.rowOf(0, 8));
        devRows.addView(cmdRow("DIR 0", "DIR 1", "DIR 2", "DIR 3"), u.rowOf(8, 8));
        devRows.addView(cmdRow("DIR 4", "DIR 5", "DIR 6", "DIR 7"), u.rowOf(8, 8));
        devRows.addView(u.banner("RETURN과 DIR은 모터가 진동합니다"), u.full(12));
        devRows.addView(bleDetail, u.full(12));
        logView = u.mono("", 12, u.textSub);
        logView.setTextIsSelectable(true);
        devRows.addView(logView, u.full(12));
        LinearLayout dev = u.collapsible("개발자 메뉴", devRows);

        ble.setLineListener(this::onLine);

        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding(u.dp(16), u.dp(16), u.dp(16), u.dp(24));
        col.addView(head, u.full(0));
        col.addView(pathCard, u.full(16));
        col.addView(modes, u.rowOf(12, 10));
        col.addView(env, u.full(12));
        col.addView(dev, u.full(16));

        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(u.bg);
        scroll.setFitsSystemWindows(true); // 상태바와 내비게이션 바 아래로 내용이 들어가지 않게
        scroll.addView(col);
        setContentView(scroll);
        if (Build.VERSION.SDK_INT >= 30) { // 어두운 배경이라 상태바 글자는 밝게 둔다
            getWindow().getInsetsController().setSystemBarsAppearance(0,
                    WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS
                            | WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS);
        }
        requestNeededPermissions();
    }

    private GradientDrawable pill(int fill, int radiusDp) {
        GradientDrawable d = new GradientDrawable();
        d.setCornerRadius(u.dp(radiusDp));
        d.setColor(fill);
        return d;
    }

    private Button modeButton(String label, int fill, int textColor, String cmd) {
        Button b = u.big(label, fill, textColor);
        b.setOnClickListener(v -> send(cmd));
        return b;
    }

    private TextView envSegment(String label, String cmd) {
        TextView t = u.label(label, 16, u.textSub, false);
        t.setGravity(Gravity.CENTER);
        t.setOnClickListener(v -> {
            if (send(cmd)) selectEnv(cmd.equals("ENV IN"));
        });
        return t;
    }

    private void selectEnv(boolean indoor) {
        style(envIn, indoor);
        style(envOut, !indoor);
    }

    private void style(TextView t, boolean on) {
        t.setBackground(on ? pill(u.chipBg, 10) : null);
        t.setTextColor(on ? u.text : u.textSub);
        t.setTypeface(on ? Typeface.DEFAULT_BOLD : Typeface.DEFAULT);
    }

    private boolean send(String cmd) {
        if (ble.send(cmd)) return true;
        Toast.makeText(this, "ESP32에 연결되지 않았습니다", Toast.LENGTH_SHORT).show();
        addLog("! 연결 안 됨: " + cmd);
        return false;
    }

    /** 연결 시도 중이 아닌 상태(처음, 해제, 실패)면 true */
    private static boolean isIdle(String s) {
        return s.startsWith("정지됨") || s.contains("실패") || s.contains("꺼짐") || s.contains("서비스 없음");
    }

    private static String phaseLabel(String name) {
        switch (name) {
            case "RECORDING": return "기록 중";
            case "RETURNING": return "복귀 중";
            case "ARRIVED": return "도착";
            case "NO_ROUTE": return "경로 없음";
            default: return "대기";
        }
    }

    private int phaseColor(String name) {
        switch (name) {
            case "RETURNING": return u.warn;
            case "RECORDING": case "ARRIVED": return u.accent;
            case "NO_ROUTE": return u.danger;
            default: return u.text;
        }
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
            b.setOnClickListener(v -> send(cmd));
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
            guideText = String.format(java.util.Locale.US, "경유점 %d · 남은 %.1fm", g.wp, g.remain);
            return;
        }
        LineProtocol.Phase ph = LineProtocol.parsePhase(line);
        if (ph != null) {
            phaseName = ph.name;
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
