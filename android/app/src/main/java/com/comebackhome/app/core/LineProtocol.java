package com.comebackhome.app.core;

import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** 펌웨어 NUS 텍스트 줄(STEP/GUIDE/PHASE) 파서, 조각 줄 조립기, IMU 줄 포맷. Android 의존 없음. */
public final class LineProtocol {
    private LineProtocol() {}

    public static final class Step {
        public final long t;
        public final double x, y, heading;

        Step(long t, double x, double y, double heading) {
            this.t = t; this.x = x; this.y = y; this.heading = heading;
        }
    }

    public static final class Guide {
        public final int dir, wp;
        public final double remain;

        Guide(int dir, int wp, double remain) {
            this.dir = dir; this.wp = wp; this.remain = remain;
        }
    }

    public static final class Phase {
        public final String name, raw;

        Phase(String name, String raw) {
            this.name = name; this.raw = raw;
        }
    }

    private static final Pattern GUIDE = Pattern.compile(
            "GUIDE,dir=(\\d+),wp=(-?\\d+),remain=(-?\\d+(?:\\.\\d+)?)m.*", Pattern.DOTALL);
    private static final Pattern PHASE = Pattern.compile("PHASE,([^\\s,]+).*", Pattern.DOTALL);

    /** 형식이 틀리면 null. 예외를 던지지 않는다. */
    public static Step parseStep(String line) {
        if (line == null) return null;
        String[] p = line.trim().split(",", -1);
        if (p.length != 5 || !p[0].equals("STEP")) return null;
        try {
            return new Step(Long.parseLong(p[1]), Double.parseDouble(p[2]),
                    Double.parseDouble(p[3]), Double.parseDouble(p[4]));
        } catch (NumberFormatException e) {
            return null;
        }
    }

    public static Guide parseGuide(String line) {
        if (line == null) return null;
        Matcher m = GUIDE.matcher(line.trim());
        if (!m.matches()) return null;
        try {
            return new Guide(Integer.parseInt(m.group(1)), Integer.parseInt(m.group(2)),
                    Double.parseDouble(m.group(3)));
        } catch (NumberFormatException e) {
            return null;
        }
    }

    public static Phase parsePhase(String line) {
        if (line == null) return null;
        String s = line.trim();
        Matcher m = PHASE.matcher(s);
        return m.matches() ? new Phase(m.group(1), s) : null;
    }

    /** 끝 개행 없음. 호출자가 붙인다. */
    public static String imuLine(double accG, double headDeg) {
        return String.format(Locale.US, "IMU %.3f %.1f", accG, headDeg);
    }

    /** UTF-8 바이트 조각을 누적해 '\n'마다 완성된 줄을 돌려준다. '\r' 무시, 빈 줄 버림. */
    public static final class LineAssembler {
        static final int MAX = 512;
        private final ByteArrayOutputStream buf = new ByteArrayOutputStream();
        private boolean skipping; // 상한 초과 후 다음 '\n'까지 버린다

        /** 연결이 끊기면 조립 중이던 조각을 버린다 */
        public void reset() {
            buf.reset();
            skipping = false;
        }

        public List<String> feed(byte[] chunk) {
            List<String> out = new ArrayList<>();
            for (byte b : chunk) {
                if (b == '\n') {
                    if (!skipping && buf.size() > 0) out.add(new String(buf.toByteArray(), StandardCharsets.UTF_8));
                    buf.reset();
                    skipping = false;
                } else if (b != '\r' && !skipping) {
                    if (buf.size() >= MAX) { buf.reset(); skipping = true; }
                    else buf.write(b);
                }
            }
            return out;
        }
    }
}
