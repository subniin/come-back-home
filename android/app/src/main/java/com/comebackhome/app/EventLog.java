package com.comebackhome.app;

import android.os.SystemClock;

import java.io.File;
import java.io.IOException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * 시험 이벤트 로그. events.csv: t_ns,type,a,b,c,d,e
 * t_ns는 sensors.csv, gps.csv와 같은 elapsedRealtimeNanos 기준이다.
 * 어느 스레드에서 호출해도 되고, 파일은 전용 스레드 1개에서만 쓴다.
 *
 * type별 열:
 *   pkt: a=seq, b=turnAngle, c=distanceCm, d=state, e=쓰기 성공(1)/실패(0)
 *   btn: a=eventType
 *   ble: a=사건 이름, b=부가 정보 (쉼표 금지)
 *   mode: 예약 (2-3에서 사용)
 */
final class EventLog {
    private final ExecutorService exec = Executors.newSingleThreadExecutor();
    private final CsvLog out;

    EventLog(File dir) throws IOException {
        if (!dir.mkdirs() && !dir.isDirectory()) throw new IOException("mkdir failed: " + dir);
        out = new CsvLog(new File(dir, "events.csv"), "t_ns,type,a,b,c,d,e");
    }

    /** close() 이후 호출하면 RejectedExecutionException. 호출하는 쪽이 close 뒤에 참조를 버린다 */
    void log(String type, Object... cols) {
        long t = SystemClock.elapsedRealtimeNanos();
        exec.execute(() -> {
            Object[] row = new Object[cols.length + 2];
            row[0] = t;
            row[1] = type;
            System.arraycopy(cols, 0, row, 2, cols.length);
            out.row(row);
        });
    }

    /** 이미 큐에 들어온 이벤트를 다 쓴 뒤 닫는다 */
    void close() {
        exec.execute(out::close);
        exec.shutdown();
    }

    String error() {
        return out.error;
    }
}
