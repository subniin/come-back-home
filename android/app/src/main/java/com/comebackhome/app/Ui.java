package com.comebackhome.app;

import android.content.Context;
import android.content.res.ColorStateList;
import android.content.res.Configuration;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.TextView;

/**
 * 시험 화면의 색과 모양. 라이트/다크는 시스템 설정을 따른다.
 * 강조색은 하나, 모서리 반경은 하나로 통일한다. 버튼 높이는 터치하기 쉬운 48dp.
 */
final class Ui {
    static final int NEUTRAL = 0, ACCENT = 1, DANGER = 2;
    private static final int RADIUS_DP = 12;

    final int bg, surface, text, textSub, border, track, accent, onAccent, danger, dangerBg, chipBg;
    final boolean night;
    private final Context ctx;
    private final float density;

    Ui(Context ctx) {
        this.ctx = ctx;
        density = ctx.getResources().getDisplayMetrics().density;
        night = (ctx.getResources().getConfiguration().uiMode & Configuration.UI_MODE_NIGHT_MASK)
                == Configuration.UI_MODE_NIGHT_YES;
        bg = c(night ? "#0F1214" : "#F3F4F6");
        surface = c(night ? "#1A1E21" : "#FFFFFF");
        text = c(night ? "#ECEFF1" : "#111827");
        textSub = c(night ? "#A7B0B7" : "#4B5563");
        border = c(night ? "#2B3238" : "#D1D5DB");
        track = c(night ? "#3B444B" : "#9CA3AF"); // 슬라이더 빈 구간은 테두리보다 진하게
        accent = c(night ? "#5EEAD4" : "#0F766E");
        onAccent = c(night ? "#06201C" : "#FFFFFF");
        danger = c(night ? "#FDA29B" : "#B42318");
        dangerBg = c(night ? "#3A1B18" : "#FEE4E2");
        chipBg = c(night ? "#262C31" : "#E5E7EB");
    }

    private static int c(String hex) {
        return Color.parseColor(hex);
    }

    int dp(int v) {
        return Math.round(v * density);
    }

    private GradientDrawable shape(int fill, int stroke) {
        GradientDrawable d = new GradientDrawable();
        d.setCornerRadius(dp(RADIUS_DP));
        d.setColor(fill);
        if (stroke != 0) d.setStroke(dp(1), stroke);
        return d;
    }

    TextView label(String s, float sp, int color, boolean bold) {
        TextView t = new TextView(ctx);
        t.setText(s);
        t.setTextSize(sp);
        t.setTextColor(color);
        if (bold) t.setTypeface(Typeface.DEFAULT_BOLD);
        return t;
    }

    /** 숫자와 로그 표시용 고정폭 글꼴 */
    TextView mono(String s, float sp, int color) {
        TextView t = label(s, sp, color, false);
        t.setTypeface(Typeface.MONOSPACE);
        return t;
    }

    /** 제목이 있는 세로 카드 */
    LinearLayout card(String title) {
        LinearLayout c = new LinearLayout(ctx);
        c.setOrientation(LinearLayout.VERTICAL);
        c.setBackground(shape(surface, border));
        c.setPadding(dp(16), dp(16), dp(16), dp(16));
        c.addView(label(title, 17, text, true));
        return c;
    }

    LinearLayout row() {
        LinearLayout r = new LinearLayout(ctx);
        r.setOrientation(LinearLayout.HORIZONTAL);
        return r;
    }

    Button button(String label, boolean filled) {
        Button b = new Button(ctx);
        b.setText(label);
        b.setAllCaps(false);
        b.setTextSize(16);
        b.setMinHeight(dp(48));
        b.setMinimumHeight(dp(48));
        b.setMinWidth(0);
        b.setMinimumWidth(0);
        b.setPadding(dp(4), 0, dp(4), 0);
        b.setStateListAnimator(null); // 기본 그림자 제거
        setFilled(b, filled);
        return b;
    }

    /** 채움 버튼은 선택됨/주 동작, 테두리 버튼은 보조 동작 */
    void setFilled(Button b, boolean filled) {
        b.setTextColor(filled ? onAccent : text);
        b.setBackground(new RippleDrawable(ColorStateList.valueOf(0x33808080),
                filled ? shape(accent, 0) : shape(surface, border), shape(0xFF000000, 0)));
    }

    TextView chip() {
        TextView t = label("", 15, text, true);
        t.setPadding(dp(12), dp(6), dp(12), dp(6));
        t.setGravity(Gravity.CENTER);
        return t;
    }

    void setChip(TextView t, String s, int kind) {
        t.setText(s);
        t.setTextColor(kind == ACCENT ? onAccent : kind == DANGER ? danger : text);
        t.setBackground(shape(kind == ACCENT ? accent : kind == DANGER ? dangerBg : chipBg, 0));
    }

    TextView banner(String s) {
        TextView t = label(s, 15, danger, true);
        t.setPadding(dp(12), dp(10), dp(12), dp(10));
        t.setBackground(shape(dangerBg, 0));
        return t;
    }

    SeekBar seek(int max) {
        SeekBar s = new SeekBar(ctx);
        s.setMax(max);
        s.setPadding(dp(16), dp(12), dp(16), dp(12)); // 손가락으로 잡기 쉽게 세로 영역을 키운다
        s.setThumbTintList(ColorStateList.valueOf(accent));
        s.setProgressTintList(ColorStateList.valueOf(accent));
        s.setProgressBackgroundTintList(ColorStateList.valueOf(track));
        return s;
    }

    LinearLayout.LayoutParams full(int topDp) {
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        p.topMargin = dp(topDp);
        return p;
    }

    /** cell()이 칸마다 가진 바깥 여백만큼 줄을 넓혀, 첫 칸과 끝 칸이 카드 안쪽 여백에 맞게 한다 */
    LinearLayout.LayoutParams rowOf(int topDp, int gapDp) {
        LinearLayout.LayoutParams p = full(topDp);
        p.setMarginStart(-dp(gapDp) / 2);
        p.setMarginEnd(-dp(gapDp) / 2);
        return p;
    }

    LinearLayout.LayoutParams wrap(int topDp) {
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        p.topMargin = dp(topDp);
        return p;
    }

    /** 한 줄에서 같은 너비를 나눠 갖는 칸 */
    LinearLayout.LayoutParams cell(int gapDp) {
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
        p.setMarginStart(dp(gapDp) / 2);
        p.setMarginEnd(dp(gapDp) / 2);
        return p;
    }
}
