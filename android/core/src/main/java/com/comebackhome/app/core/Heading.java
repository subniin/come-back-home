package com.comebackhome.app.core;

/**
 * 회전 벡터(쿼터니언)에서 몸 정면 헤딩을 구한다. Android API를 쓰지 않아 PC에서도 테스트할 수 있다.
 *
 * 정면은 폰 위쪽(+Y)과 뒷면 카메라 쪽(-Z)을 더한 방향을 수평면에 투영한 것이다.
 * 그래서 폰을 눕혀 달든(화면 위, 폰 위쪽이 앞) 세워 달든(화면이 몸 쪽, 카메라가 앞),
 * 그 사이로 앞뒤로 기울든 같은 헤딩이 나온다. getOrientation()처럼 폰이 서면 값이 무너지지 않는다.
 * 화면이 바깥(앞)을 보게 세우면 헤딩이 180° 뒤집히므로 그렇게 달지 않는다.
 */
public final class Heading {
    private static final double MIN_HORIZONTAL = 0.3; // 정면 벡터의 수평 성분이 이보다 작으면 판단 불가

    private Heading() {
    }

    /**
     * Android 회전 벡터 값(x, y, z, w) → 0~360° (시계방향, 0 = 센서 기준 북).
     * w가 없으면 NaN을 넘기면 된다. 폰 자세상 정면을 정할 수 없으면 NaN.
     */
    public static double forwardDeg(double x, double y, double z, double w) {
        if (Double.isNaN(w)) w = Math.sqrt(Math.max(0, 1 - x * x - y * y - z * z));
        // 회전행렬 R(기기 → 세계: 동, 북, 위)의 2열(+Y) - 3열(+Z)
        double east = 2 * (x * y - z * w) - 2 * (x * z + y * w);
        double north = 1 - 2 * (x * x + z * z) - 2 * (y * z - x * w);
        if (Math.hypot(east, north) < MIN_HORIZONTAL) return Double.NaN;
        return norm(Math.toDegrees(Math.atan2(east, north)));
    }

    public static double norm(double deg) {
        deg %= 360;
        return deg < 0 ? deg + 360 : deg;
    }

    /**
     * GAME_ROTATION_VECTOR(자이로 기반, 자기장 영향 없음, 시작 방향 임의)를 북 기준으로 맞춘다.
     * 시작 직후 일정 개수 동안 ROTATION_VECTOR(자북 기준) 헤딩과의 차이를 원형 평균해 고정 오프셋으로 쓴다.
     * 이후에는 자기장이 틀어진 실내에서도 자이로 헤딩을 그대로 따라간다. 오래 쓰면 자이로 드리프트는 남는다.
     */
    public static final class Aligner {
        private final int need;
        private int n;
        private double s, c, offset = Double.NaN;

        public Aligner(int samples) {
            need = samples;
        }

        /** 같은 시점의 두 헤딩을 넣는다. 정렬이 끝나면 더 넣어도 무시한다. */
        public void add(double rvDeg, double grvDeg) {
            if (!Double.isNaN(offset) || Double.isNaN(rvDeg) || Double.isNaN(grvDeg)) return;
            double d = Math.toRadians(rvDeg - grvDeg);
            s += Math.sin(d);
            c += Math.cos(d);
            if (++n >= need) offset = Math.toDegrees(Math.atan2(s, c));
        }

        public boolean ready() {
            return !Double.isNaN(offset);
        }

        /** 정렬 전에는 NaN */
        public double apply(double grvDeg) {
            return ready() ? norm(grvDeg + offset) : Double.NaN;
        }
    }
}
