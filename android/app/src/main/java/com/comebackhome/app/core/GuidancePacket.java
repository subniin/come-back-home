package com.comebackhome.app.core;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/**
 * 안내 패킷 v1 (정의서 4장): {int16 turnAngle, uint16 distanceCm, uint8 state, uint8 seq}, little-endian, 6바이트.
 * Android 의존 없음 (순수 Java).
 */
public final class GuidancePacket {
    public static final int SIZE = 6;

    private GuidancePacket() {
    }

    public static byte[] encode(int turnAngleDeg, int distanceCm, int state, int seq) {
        if (turnAngleDeg < -180 || turnAngleDeg > 180) throw new IllegalArgumentException("turnAngle " + turnAngleDeg);
        if (distanceCm < 0 || distanceCm > 0xFFFF) throw new IllegalArgumentException("distanceCm " + distanceCm);
        if (state < 0 || state > 3) throw new IllegalArgumentException("state " + state);
        if (seq < 0 || seq > 0xFF) throw new IllegalArgumentException("seq " + seq);

        return ByteBuffer.allocate(SIZE).order(ByteOrder.LITTLE_ENDIAN)
                .putShort((short) turnAngleDeg)
                .putShort((short) distanceCm)
                .put((byte) state)
                .put((byte) seq)
                .array();
    }
}
