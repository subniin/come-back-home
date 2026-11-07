package com.comebackhome.app;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.IBinder;
import android.os.PowerManager;

import java.io.File;
import java.io.IOException;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/** 화면이 꺼져도 수집을 계속하는 Foreground Service. 서비스 1회 실행 = 기록 세션 1개. */
public class GuideService extends Service {
    private static final String CHANNEL = "guide";

    /** 화면 표시용. 기록 중일 때만 non-null */
    static volatile SensorCollector collector;
    /** 마지막 시작 실패 사유. 화면 표시용 */
    static volatile String lastError;

    private PowerManager.WakeLock wakeLock;

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (collector != null) return START_NOT_STICKY; // 이미 기록 중

        getSystemService(NotificationManager.class).createNotificationChannel(
                new NotificationChannel(CHANNEL, "기록", NotificationManager.IMPORTANCE_LOW));
        Notification n = new Notification.Builder(this, CHANNEL)
                .setContentTitle("ComeBackHome 기록 중")
                .setSmallIcon(android.R.drawable.ic_menu_mylocation)
                .setOngoing(true)
                .build();
        startForeground(1, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_LOCATION);

        wakeLock = getSystemService(PowerManager.class)
                .newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "comebackhome:guide");
        wakeLock.acquire();

        String session = new SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(new Date());
        try {
            SensorCollector c = new SensorCollector(this, new File(getExternalFilesDir(null), "logs/" + session));
            c.start();
            collector = c;
            lastError = null;
        } catch (IOException e) {
            lastError = e.toString();
            stopSelf();
        }
        return START_NOT_STICKY;
    }

    @Override
    public void onDestroy() {
        SensorCollector c = collector;
        collector = null;
        if (c != null) c.stop();
        if (wakeLock != null && wakeLock.isHeld()) wakeLock.release();
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }
}
