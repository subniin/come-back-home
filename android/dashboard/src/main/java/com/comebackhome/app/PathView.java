package com.comebackhome.app;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.view.View;

import com.comebackhome.app.core.LineProtocol;

import java.util.ArrayList;
import java.util.List;

/**
 * 기록된 경로(STEP 점)를 선으로 그리고, 복귀 중에는 GUIDE 방향을 화살표로 보여 준다.
 * 좌표는 펌웨어 PDR 기준: x = 동쪽, y = 북쪽(미터). 화면은 위쪽이 +y.
 * 화살표: dir 0 = 몸 정면(화면 위), 시계 방향으로 45도씩 (펌웨어 모터 번호와 같음).
 * 메인 스레드에서만 쓴다.
 */
final class PathView extends View {
    private static final int MAX_POINTS = 5000; // ponytail: 넘으면 새 점을 버린다. 필요하면 앞쪽 점을 솎아 낸다
    private static final double MIN_SPAN_M = 5; // 점이 몇 개 없어도 너무 확대하지 않는다

    private final List<double[]> pts = new ArrayList<>();
    private LineProtocol.Guide guide;
    private final Paint line = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint dot = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint ring = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint arrow = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path tmp = new Path();
    private final float dp;

    PathView(Context ctx, Ui u) {
        super(ctx);
        dp = ctx.getResources().getDisplayMetrics().density;
        line.setStyle(Paint.Style.STROKE);
        line.setStrokeWidth(3 * dp);
        line.setStrokeJoin(Paint.Join.ROUND);
        line.setColor(u.text);
        dot.setColor(u.warn);
        ring.setStyle(Paint.Style.STROKE);
        ring.setStrokeWidth(2 * dp);
        ring.setColor(u.textSub);
        arrow.setColor(u.warn);
        setBackground(null);
    }

    void clear() {
        pts.clear();
        guide = null;
        invalidate();
    }

    void addStep(LineProtocol.Step s) {
        if (pts.size() < MAX_POINTS) pts.add(new double[]{s.x, s.y});
        invalidate();
    }

    /** null이면 화살표를 숨긴다 */
    void setGuide(LineProtocol.Guide g) {
        guide = g;
        invalidate();
    }

    @Override
    protected void onDraw(Canvas c) {
        int w = getWidth(), h = getHeight();
        float pad = 24 * dp;
        if (!pts.isEmpty()) {
            double minX = Double.MAX_VALUE, maxX = -Double.MAX_VALUE, minY = Double.MAX_VALUE, maxY = -Double.MAX_VALUE;
            for (double[] p : pts) {
                minX = Math.min(minX, p[0]);
                maxX = Math.max(maxX, p[0]);
                minY = Math.min(minY, p[1]);
                maxY = Math.max(maxY, p[1]);
            }
            double spanX = Math.max(maxX - minX, MIN_SPAN_M), spanY = Math.max(maxY - minY, MIN_SPAN_M);
            double scale = Math.min((w - 2 * pad) / spanX, (h - 2 * pad) / spanY);
            double cx = (minX + maxX) / 2, cy = (minY + maxY) / 2;
            tmp.reset();
            float lastX = 0, lastY = 0;
            for (int i = 0; i < pts.size(); i++) {
                double[] p = pts.get(i);
                lastX = (float) (w / 2.0 + (p[0] - cx) * scale);
                lastY = (float) (h / 2.0 - (p[1] - cy) * scale);
                if (i == 0) {
                    tmp.moveTo(lastX, lastY);
                    c.drawCircle(lastX, lastY, 7 * dp, ring); // 시작점
                } else {
                    tmp.lineTo(lastX, lastY);
                }
            }
            c.drawPath(tmp, line);
            c.drawCircle(lastX, lastY, 6 * dp, dot); // 현재 위치
        }
        if (guide != null && guide.dir >= 0 && guide.dir <= 7) drawArrow(c, w - 40 * dp, 40 * dp, 28 * dp, guide.dir * 45f);
    }

    private void drawArrow(Canvas c, float cx, float cy, float r, float deg) {
        c.save();
        c.rotate(deg, cx, cy);
        tmp.reset();
        tmp.moveTo(cx, cy - r);                 // 머리
        tmp.lineTo(cx + r * 0.6f, cy + r * 0.7f);
        tmp.lineTo(cx, cy + r * 0.3f);
        tmp.lineTo(cx - r * 0.6f, cy + r * 0.7f);
        tmp.close();
        c.drawPath(tmp, arrow);
        c.restore();
    }
}
