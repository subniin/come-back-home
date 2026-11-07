package com.comebackhome.app;

import java.io.BufferedWriter;
import java.io.Closeable;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;

/**
 * CSV 파일 하나. 한 스레드에서만 쓴다.
 * 1초마다 flush해서 강제 종료 시 손실을 1초 이내로 제한한다.
 */
final class CsvLog implements Closeable {
    private static final long FLUSH_INTERVAL_NS = 1_000_000_000L;

    private final BufferedWriter out;
    private long lastFlushNs = System.nanoTime();
    /** 첫 쓰기 오류. 화면 표시용 */
    volatile String error;

    CsvLog(File file, String header) throws IOException {
        out = new BufferedWriter(new FileWriter(file));
        out.write(header);
        out.newLine();
    }

    void row(Object... cols) {
        if (error != null) return;
        try {
            for (int i = 0; i < cols.length; i++) {
                if (i > 0) out.write(',');
                out.write(String.valueOf(cols[i]));
            }
            out.newLine();
            long now = System.nanoTime();
            if (now - lastFlushNs >= FLUSH_INTERVAL_NS) {
                out.flush();
                lastFlushNs = now;
            }
        } catch (IOException e) {
            error = e.toString();
        }
    }

    @Override
    public void close() {
        try {
            out.close();
        } catch (IOException e) {
            if (error == null) error = e.toString();
        }
    }
}
