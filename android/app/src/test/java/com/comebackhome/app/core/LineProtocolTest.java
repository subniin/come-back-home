package com.comebackhome.app.core;

import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.List;

/**
 * 실행: javac -d out LineProtocol.java LineProtocolTest.java && java -ea -cp out com.comebackhome.app.core.LineProtocolTest
 */
public final class LineProtocolTest {
    public static void main(String[] args) {
        LineProtocol.Step s = LineProtocol.parseStep("STEP,1234,1.50,-2.00,90.0");
        check(s != null && s.t == 1234 && s.x == 1.5 && s.y == -2.0 && s.heading == 90.0, "step");

        LineProtocol.Guide g = LineProtocol.parseGuide("GUIDE,dir=3,wp=5,remain=12.3m");
        check(g != null && g.dir == 3 && g.wp == 5 && g.remain == 12.3, "guide");
        g = LineProtocol.parseGuide("GUIDE,dir=3,wp=-1,remain=12.3m,GPS");
        check(g != null && g.wp == -1 && g.remain == 12.3, "guide tail");

        LineProtocol.Phase p = LineProtocol.parsePhase("PHASE,RETURNING,from=2,goal=0");
        check(p != null && p.name.equals("RETURNING") && p.raw.equals("PHASE,RETURNING,from=2,goal=0"), "phase1");
        p = LineProtocol.parsePhase("PHASE,IDLE (복귀 취소)");
        check(p != null && p.name.equals("IDLE") && p.raw.equals("PHASE,IDLE (복귀 취소)"), "phase2");

        check(LineProtocol.parseStep("STEP,1,2,3") == null, "step short");
        check(LineProtocol.parseStep("STEP,a,1,2,3") == null, "step nan");
        check(LineProtocol.parseStep("STEP,1,2,3,4,5") == null, "step long");
        check(LineProtocol.parseStep("GUIDE,1,2,3,4") == null, "step wrong tag");
        check(LineProtocol.parseStep(null) == null, "step null");
        check(LineProtocol.parseGuide("GUIDE,dir=x,wp=5,remain=1.0m") == null, "guide bad dir");
        check(LineProtocol.parseGuide("GUIDE,dir=3,wp=5,remain=12.3") == null, "guide no m");
        check(LineProtocol.parseGuide("STEP,1,2,3,4") == null, "guide wrong tag");
        check(LineProtocol.parsePhase("PHASE,") == null, "phase empty");
        check(LineProtocol.parsePhase("PHASE") == null, "phase none");
        check(LineProtocol.parsePhase("hello") == null, "phase wrong");

        // 한 줄이 3조각
        LineProtocol.LineAssembler a = new LineProtocol.LineAssembler();
        check(a.feed(b("STEP,12")).isEmpty(), "c1");
        check(a.feed(b("34,1.50,-2.")).isEmpty(), "c2");
        check(a.feed(b("00,90.0\n")).equals(Arrays.asList("STEP,1234,1.50,-2.00,90.0")), "c3");

        // 한 조각에 2줄, \r\n, 빈 줄, 미완성 꼬리 유지
        a = new LineProtocol.LineAssembler();
        check(a.feed(b("A\r\nB\n\n\r\nC")).equals(Arrays.asList("A", "B")), "two lines");
        check(a.feed(b("\n")).equals(Arrays.asList("C")), "tail kept");

        // 한글 바이트 중간 분할 ("복귀" = 6바이트)
        byte[] k = "PHASE,IDLE (복귀 취소)\n".getBytes(StandardCharsets.UTF_8);
        for (int cut = 1; cut < k.length; cut++) {
            a = new LineProtocol.LineAssembler();
            List<String> r = a.feed(Arrays.copyOfRange(k, 0, cut));
            r.addAll(a.feed(Arrays.copyOfRange(k, cut, k.length)));
            check(r.equals(Arrays.asList("PHASE,IDLE (복귀 취소)")), "korean cut " + cut);
        }

        // 상한 초과: 그 줄은 버리고 다음 줄은 정상
        a = new LineProtocol.LineAssembler();
        char[] big = new char[600];
        Arrays.fill(big, 'x');
        check(a.feed(b(new String(big) + "\nOK\n")).equals(Arrays.asList("OK")), "overflow");

        check(LineProtocol.imuLine(1.0234, 87.45).equals("IMU 1.023 87.5")
                || LineProtocol.imuLine(1.0234, 87.45).equals("IMU 1.023 87.4"), "imu fmt");
        check(LineProtocol.imuLine(1.023, 87.5).equals("IMU 1.023 87.5"), "imu");
        check(LineProtocol.imuLine(0, 0).equals("IMU 0.000 0.0"), "imu zero");

        System.out.println("ALL PASSED");
    }

    private static byte[] b(String s) {
        return s.getBytes(StandardCharsets.UTF_8);
    }

    private static void check(boolean ok, String name) {
        if (!ok) throw new AssertionError(name);
    }
}
