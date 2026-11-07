package com.comebackhome.app.core;

/**
 * 실행: javac -d out GuidancePacket.java GuidancePacketTest.java && java -ea -cp out com.comebackhome.app.core.GuidancePacketTest
 * Gradle 테스트 프레임워크 없이 assert로만 검사한다.
 */
public final class GuidancePacketTest {
    public static void main(String[] args) {
        // turnAngle -30 = 0xFFE2, distanceCm 1500 = 0x05DC, LE 순서
        byte[] a = GuidancePacket.encode(-30, 1500, 1, 7);
        assert a.length == 6;
        assert a[0] == (byte) 0xE2 && a[1] == (byte) 0xFF;
        assert a[2] == (byte) 0xDC && a[3] == 0x05;
        assert a[4] == 1 && a[5] == 7;

        // 경계값: turnAngle 180, distanceCm 65535, state 3, seq 255
        byte[] b = GuidancePacket.encode(180, 65535, 3, 255);
        assert b[0] == (byte) 0xB4 && b[1] == 0x00;
        assert b[2] == (byte) 0xFF && b[3] == (byte) 0xFF;
        assert b[4] == 3 && b[5] == (byte) 0xFF;

        assertThrows(() -> GuidancePacket.encode(181, 0, 0, 0));
        assertThrows(() -> GuidancePacket.encode(0, 65536, 0, 0));
        assertThrows(() -> GuidancePacket.encode(0, 0, 4, 0));
        assertThrows(() -> GuidancePacket.encode(0, 0, 0, 256));

        System.out.println("GuidancePacketTest OK");
    }

    private static void assertThrows(Runnable r) {
        try {
            r.run();
        } catch (IllegalArgumentException e) {
            return;
        }
        throw new AssertionError("IllegalArgumentException expected");
    }
}
