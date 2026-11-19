package com.comebackhome.app.core;

/**
 * 실행: javac -d out Heading.java HeadingTest.java && java -ea -cp out com.comebackhome.app.core.HeadingTest
 */
public final class HeadingTest {
    public static void main(String[] args) {
        // 눕힘(화면 위), 폰 위쪽이 북 → 0°
        near(Heading.forwardDeg(0, 0, 0, 1), 0, "flat north");
        for (int yaw = 0; yaw < 360; yaw += 30) {
            double[] q = mul(z(-yaw), new double[]{0, 0, 0, 1}); // 시계방향 yaw
            near(Heading.forwardDeg(q[0], q[1], q[2], q[3]), yaw, "flat " + yaw);
            // 앞뒤로 30°, 60°, 90°(세움, 화면이 몸 쪽) 기울여도 같은 헤딩
            for (int pitch : new int[]{30, 60, 90, 110}) {
                double[] p = mul(z(-yaw), x(pitch));
                near(Heading.forwardDeg(p[0], p[1], p[2], p[3]), yaw, "pitch " + pitch + " yaw " + yaw);
            }
        }
        // w 생략(3개 값만 오는 기기)
        double[] q = mul(z(-45), x(90));
        near(Heading.forwardDeg(q[0] * Math.signum(q[3]), q[1] * Math.signum(q[3]), q[2] * Math.signum(q[3]), Double.NaN), 45, "no w");

        // 정렬: rv = grv + 100° 이면 이후 grv에 100°를 더해 돌려준다 (360 경계 포함)
        Heading.Aligner a = new Heading.Aligner(3);
        a.add(350, 250);
        check(!a.ready() && Double.isNaN(a.apply(0)), "not ready");
        a.add(5, 265);
        a.add(10, 270);
        check(a.ready(), "ready");
        near(a.apply(300), 40, "aligned wrap");
        a.add(0, 0); // 정렬 후 입력은 무시
        near(a.apply(0), 100, "frozen");

        System.out.println("ALL PASSED");
    }

    private static double[] z(double deg) {
        double h = Math.toRadians(deg) / 2;
        return new double[]{0, 0, Math.sin(h), Math.cos(h)};
    }

    private static double[] x(double deg) {
        double h = Math.toRadians(deg) / 2;
        return new double[]{Math.sin(h), 0, 0, Math.cos(h)};
    }

    /** 해밀턴 곱 a*b, 순서 (x, y, z, w) */
    private static double[] mul(double[] a, double[] b) {
        return new double[]{
                a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
                a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
                a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
                a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]};
    }

    private static void near(double got, double want, String name) {
        double d = Math.abs(((got - want) % 360 + 540) % 360 - 180);
        if (!(d < 0.5)) throw new AssertionError(name + ": got " + got + " want " + want);
    }

    private static void check(boolean ok, String name) {
        if (!ok) throw new AssertionError(name);
    }
}
