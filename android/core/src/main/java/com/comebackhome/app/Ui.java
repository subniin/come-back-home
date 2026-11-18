package com.comebackhome.app;

import android.content.Context;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

/**
 * 두 앱 공용 색과 모양. 현장과 야간에 잘 보이도록 항상 어두운 고대비 테마다.
 * 상태색은 둘뿐이다: 초록(accent)은 정상, 주황(warn)은 복귀 중. 모서리 반경은 하나, 버튼은 장갑 낀 손도 누르는 56dp.
 */
public final class Ui {
    public static final int NEUTRAL = 0, ACCENT = 1, DANGER = 2;
    private static final int RADIUS_DP = 16;
    private static final int BUTTON_DP = 56;

    public final int bg, surface, text, textSub, border, accent, onAccent, warn, danger, dangerBg, chipBg;
    public final boolean night = true; // 항상 어둡다 (상태바 글자색 결정에 쓴다)
    private final Context ctx;
    private final float density;

    public Ui(Context ctx) {
        this.ctx = ctx;
        density = ctx.getResources().getDisplayMetrics().density;
        bg = c("#000000");
        surface = c("#141414");
        text = c("#FFFFFF");
        textSub = c("#A8A8A8");
        border = c("#2A2A2A");
        accent = c("#3DDC84");
        onAccent = c("#000000");
        warn = c("#FFB020");
        danger = c("#FF6B6B");
        dangerBg = c("#3A1414");
        chipBg = c("#262626");
    }

    private static int c(String hex) {
        return Color.parseColor(hex);
    }

    public int dp(int v) {
        return Math.round(v * density);
    }

    private GradientDrawable shape(int fill, int stroke) {
        GradientDrawable d = new GradientDrawable();
        d.setCornerRadius(dp(RADIUS_DP));
        d.setColor(fill);
        if (stroke != 0) d.setStroke(dp(1), stroke);
        return d;
    }

    public TextView label(String s, float sp, int color, boolean bold) {
        TextView t = new TextView(ctx);
        t.setText(s);
        t.setTextSize(sp);
        t.setTextColor(color);
        if (bold) t.setTypeface(Typeface.DEFAULT_BOLD);
        return t;
    }

    /** 숫자와 로그 표시용 고정폭 글꼴 */
    public TextView mono(String s, float sp, int color) {
        TextView t = label(s, sp, color, false);
        t.setTypeface(Typeface.MONOSPACE);
        return t;
    }

    /** 세로 카드. title이 null이면 제목 없이 만든다 */
    public LinearLayout card(String title) {
        LinearLayout c = new LinearLayout(ctx);
        c.setOrientation(LinearLayout.VERTICAL);
        c.setBackground(shape(surface, 0));
        c.setPadding(dp(16), dp(16), dp(16), dp(16));
        if (title != null) c.addView(label(title, 17, text, true));
        return c;
    }

    public LinearLayout row() {
        LinearLayout r = new LinearLayout(ctx);
        r.setOrientation(LinearLayout.HORIZONTAL);
        return r;
    }

    public Button button(String label, boolean filled) {
        Button b = new Button(ctx);
        b.setText(label);
        b.setAllCaps(false);
        b.setTextSize(18);
        b.setTypeface(Typeface.DEFAULT_BOLD);
        b.setMinHeight(dp(BUTTON_DP));
        b.setMinimumHeight(dp(BUTTON_DP));
        b.setMinWidth(0);
        b.setMinimumWidth(0);
        b.setPadding(dp(4), 0, dp(4), 0);
        b.setStateListAnimator(null); // 기본 그림자 제거
        setFilled(b, filled);
        return b;
    }

    /** 채움 색을 직접 정하는 큰 버튼. 모드 명령처럼 한 번에 눌러야 하는 동작용 */
    public Button big(String label, int fill, int textColor) {
        Button b = button(label, false);
        b.setTextSize(20);
        b.setTextColor(textColor);
        b.setBackground(new RippleDrawable(ColorStateList.valueOf(0x33808080), shape(fill, 0), shape(0xFF000000, 0)));
        return b;
    }

    /** 색 점. 상태 표시용 */
    public View dot(int color, int sizeDp) {
        GradientDrawable d = new GradientDrawable();
        d.setShape(GradientDrawable.OVAL);
        d.setColor(color);
        View v = new View(ctx);
        v.setBackground(d);
        v.setLayoutParams(new LinearLayout.LayoutParams(dp(sizeDp), dp(sizeDp)));
        return v;
    }

    /** 누르면 body를 펼치고 접는 영역. 처음에는 접혀 있다 */
    public LinearLayout collapsible(String title, View body) {
        LinearLayout box = new LinearLayout(ctx);
        box.setOrientation(LinearLayout.VERTICAL);
        Button head = button("▸ " + title, false);
        head.setTextSize(15);
        head.setMinHeight(dp(48));
        head.setMinimumHeight(dp(48));
        body.setVisibility(View.GONE);
        head.setOnClickListener(v -> {
            boolean open = body.getVisibility() != View.VISIBLE;
            body.setVisibility(open ? View.VISIBLE : View.GONE);
            head.setText((open ? "▾ " : "▸ ") + title);
        });
        box.addView(head, full(0));
        box.addView(body, full(8));
        return box;
    }

    /** 채움 버튼은 선택됨/주 동작, 테두리 버튼은 보조 동작 */
    public void setFilled(Button b, boolean filled) {
        b.setTextColor(filled ? onAccent : text);
        b.setBackground(new RippleDrawable(ColorStateList.valueOf(0x33808080),
                filled ? shape(accent, 0) : shape(chipBg, 0), shape(0xFF000000, 0)));
    }

    public TextView chip() {
        TextView t = label("", 15, text, true);
        t.setPadding(dp(12), dp(8), dp(12), dp(8));
        t.setGravity(Gravity.CENTER);
        return t;
    }

    public void setChip(TextView t, String s, int kind) {
        t.setText(s);
        t.setTextColor(kind == ACCENT ? onAccent : kind == DANGER ? danger : text);
        t.setBackground(shape(kind == ACCENT ? accent : kind == DANGER ? dangerBg : chipBg, 0));
    }

    public TextView banner(String s) {
        TextView t = label(s, 15, danger, true);
        t.setPadding(dp(12), dp(10), dp(12), dp(10));
        t.setBackground(shape(dangerBg, 0));
        return t;
    }

    public LinearLayout.LayoutParams full(int topDp) {
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        p.topMargin = dp(topDp);
        return p;
    }

    /** cell()이 칸마다 가진 바깥 여백만큼 줄을 넓혀, 첫 칸과 끝 칸이 카드 안쪽 여백에 맞게 한다 */
    public LinearLayout.LayoutParams rowOf(int topDp, int gapDp) {
        LinearLayout.LayoutParams p = full(topDp);
        p.setMarginStart(-dp(gapDp) / 2);
        p.setMarginEnd(-dp(gapDp) / 2);
        return p;
    }

    public LinearLayout.LayoutParams wrap(int topDp) {
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        p.topMargin = dp(topDp);
        return p;
    }

    /** 한 줄에서 같은 너비를 나눠 갖는 칸 */
    public LinearLayout.LayoutParams cell(int gapDp) {
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
        p.setMarginStart(dp(gapDp) / 2);
        p.setMarginEnd(dp(gapDp) / 2);
        return p;
    }
}
