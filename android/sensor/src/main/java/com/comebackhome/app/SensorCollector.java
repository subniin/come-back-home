package com.comebackhome.app;

import android.annotation.SuppressLint;
import android.content.Context;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.SystemClock;

import java.io.File;
import java.io.IOException;
import java.util.Locale;

import com.comebackhome.app.core.Heading;

/**
 * 센서(50Hz)와 GPS(1Hz)를 전용 스레드에서 수집해 CSV로 기록한다.
 * 로그 파일은 이 스레드에서만 쓴다.
 *
 * sensors.csv: t_ns,type,v0,v1,v2,v3
 *   type = acc(m/s²) | gyro(rad/s) | mag(µT) | rv | grv (rv·grv는 회전 벡터 x,y,z,w)
 * gps.csv: t_ns,lat,lon,acc_m,speed_mps,bearing_deg,alt_m
 * t_ns는 두 파일 모두 elapsedRealtimeNanos 기준이라 같은 시간축으로 비교할 수 있다.
 */
final class SensorCollector implements SensorEventListener, LocationListener {
    private static final int PERIOD_US = 20_000; // 50Hz
    /**
     * ESP32로 보내는 헤딩 = GAME_ROTATION_VECTOR(자이로 기반, 실내 자기장 왜곡 없음) + 시작 직후 자북과 맞춘 고정 오프셋.
     * 정렬은 시작 후 ALIGN_SAMPLES개(약 2초) 동안 ROTATION_VECTOR와 비교해 한 번만 한다.
     * 그래서 가능하면 건물 밖, 금속에서 떨어진 곳에서 시작한다. GAME_ROTATION_VECTOR가 없는 기기는 ROTATION_VECTOR를 그대로 쓴다.
     */
    private static final int ALIGN_SAMPLES = 100;
    private static final int[] TYPES = {
            Sensor.TYPE_ACCELEROMETER, Sensor.TYPE_GYROSCOPE, Sensor.TYPE_MAGNETIC_FIELD,
            Sensor.TYPE_ROTATION_VECTOR, Sensor.TYPE_GAME_ROTATION_VECTOR};

    final File dir;
    private final SensorManager sensors;
    private final LocationManager location;
    private final HandlerThread thread = new HandlerThread("sensors");
    private final CsvLog sensorLog, gpsLog;

    private final long startNs = SystemClock.elapsedRealtimeNanos();
    private final Heading.Aligner aligner = new Heading.Aligner(ALIGN_SAMPLES);
    private double rvDeg = Double.NaN; // 센서 스레드 전용
    private boolean hasGrv;

    // 화면 표시용: 센서 스레드가 쓰고 메인 스레드가 읽는다
    private volatile long accCount;
    private volatile float azimuthDeg = Float.NaN;
    // ESP32 송신용: 가속도 크기(g)와 헤딩(0~360, 시계방향). 아직 없으면 NaN
    private volatile float accG = Float.NaN, headingDeg = Float.NaN;
    private volatile Location lastFix;
    private volatile String missing = "";

    SensorCollector(Context ctx, File dir) throws IOException {
        this.dir = dir;
        if (!dir.mkdirs() && !dir.isDirectory()) throw new IOException("mkdir failed: " + dir);
        sensors = (SensorManager) ctx.getSystemService(Context.SENSOR_SERVICE);
        location = (LocationManager) ctx.getSystemService(Context.LOCATION_SERVICE);
        sensorLog = new CsvLog(new File(dir, "sensors.csv"), "t_ns,type,v0,v1,v2,v3");
        gpsLog = new CsvLog(new File(dir, "gps.csv"), "t_ns,lat,lon,acc_m,speed_mps,bearing_deg,alt_m");
    }

    @SuppressLint("MissingPermission") // 위치 권한은 MainActivity에서 확인한 뒤 서비스를 시작한다
    void start() {
        thread.start();
        Handler handler = new Handler(thread.getLooper());
        StringBuilder miss = new StringBuilder();
        for (int type : TYPES) {
            Sensor s = sensors.getDefaultSensor(type);
            if (s == null) miss.append(name(type)).append(' ');
            else sensors.registerListener(this, s, PERIOD_US, handler);
        }
        missing = miss.toString();
        hasGrv = sensors.getDefaultSensor(Sensor.TYPE_GAME_ROTATION_VECTOR) != null;
        location.requestLocationUpdates(LocationManager.GPS_PROVIDER, 1000, 0f, this, thread.getLooper());
    }

    void stop() {
        sensors.unregisterListener(this);
        location.removeUpdates(this);
        // 이미 큐에 들어온 이벤트를 다 쓴 뒤 닫는다
        new Handler(thread.getLooper()).post(() -> {
            sensorLog.close();
            gpsLog.close();
        });
        thread.quitSafely();
    }

    @Override
    public void onSensorChanged(SensorEvent e) {
        int type = e.sensor.getType();
        float[] v = e.values;
        sensorLog.row(e.timestamp, name(type), v[0], v[1], v[2], v.length > 3 ? v[3] : "");
        if (type == Sensor.TYPE_ACCELEROMETER) {
            accCount++;
            accG = (float) (Math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) / SensorManager.GRAVITY_EARTH);
        } else if (type == Sensor.TYPE_ROTATION_VECTOR || type == Sensor.TYPE_GAME_ROTATION_VECTOR) {
            // 폰을 눕히든 세우든(화면이 몸 쪽) 몸 정면 헤딩이 나온다 (core/Heading 참고)
            double deg = Heading.forwardDeg(v[0], v[1], v[2], v.length > 3 ? v[3] : Double.NaN);
            if (Double.isNaN(deg)) return; // 정면을 정할 수 없는 자세 → 직전 값 유지
            if (type == Sensor.TYPE_ROTATION_VECTOR) {
                rvDeg = deg;
                azimuthDeg = (float) deg; // 화면 확인용 (자북)
                if (!hasGrv) headingDeg = (float) deg;
            } else {
                aligner.add(rvDeg, deg);
                if (aligner.ready()) headingDeg = (float) aligner.apply(deg);
            }
        }
    }

    @Override
    public void onAccuracyChanged(Sensor sensor, int accuracy) {
    }

    @Override
    public void onLocationChanged(Location l) {
        lastFix = l;
        gpsLog.row(l.getElapsedRealtimeNanos(), l.getLatitude(), l.getLongitude(), l.getAccuracy(),
                l.getSpeed(), l.getBearing(), l.getAltitude());
    }

    // Android 10에는 아래 세 메서드의 기본 구현이 없어서 생략하면 호출 시 크래시가 난다
    @Override
    public void onStatusChanged(String provider, int status, Bundle extras) {
    }

    @Override
    public void onProviderEnabled(String provider) {
    }

    @Override
    public void onProviderDisabled(String provider) {
    }

    float accG() {
        return accG;
    }

    float headingDeg() {
        return headingDeg;
    }

    String status() {
        long now = SystemClock.elapsedRealtimeNanos();
        double sec = (now - startNs) / 1e9;
        Location f = lastFix;
        String gps = f == null ? "수신 없음"
                : String.format(Locale.US, "±%.1fm (%.0f초 전)",
                        f.getAccuracy(), (now - f.getElapsedRealtimeNanos()) / 1e9);
        String err = sensorLog.error != null ? sensorLog.error : gpsLog.error;
        return String.format(Locale.US,
                "기록 %.0f초\n가속도 평균 %.1fHz\n방위각 %.0f° / 전송 헤딩 %s\nGPS %s\n미지원 센서: %s\n오류: %s\n저장: %s",
                sec, accCount / sec, azimuthDeg,
                Float.isNaN(headingDeg) ? "정렬 중" : String.format(Locale.US, "%.0f°", headingDeg), gps,
                missing.isEmpty() ? "없음" : missing, err == null ? "없음" : err, dir);
    }

    private static String name(int type) {
        switch (type) {
            case Sensor.TYPE_ACCELEROMETER: return "acc";
            case Sensor.TYPE_GYROSCOPE: return "gyro";
            case Sensor.TYPE_MAGNETIC_FIELD: return "mag";
            case Sensor.TYPE_ROTATION_VECTOR: return "rv";
            case Sensor.TYPE_GAME_ROTATION_VECTOR: return "grv";
            default: return String.valueOf(type);
        }
    }
}
