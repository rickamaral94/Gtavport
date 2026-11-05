package com.gtavrecomp.app;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.net.Uri;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.os.PowerManager;
import android.util.Log;

/**
 * Gtavport (Fase 1): a build no aparelho como serviço em primeiro plano (dataSync), para o Android não
 * encerrá-la com a tela apagada ou o app em segundo plano. Progresso na notificação; a tela
 * Odin2DeviceBuildActivity lê o mesmo estado (Odin2DeviceBuild). Os discos chegam como URIs do
 * Storage Access Framework (permissão persistida pela tela) e vão ao nativo como /proc/self/fd/N.
 */
public class Odin2BuildService extends Service {
    static final String EXTRA_URIS = "uris";
    private static final String CHANNEL = "gtavport_build";
    private static final int NOTIFICATION = 7301;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private PowerManager.WakeLock wake;

    @Override
    public IBinder onBind(Intent i) {
        return null;
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        NotificationManager nm = getSystemService(NotificationManager.class);
        nm.createNotificationChannel(new NotificationChannel(CHANNEL, "Compilação do jogo", NotificationManager.IMPORTANCE_LOW));
        Notification n = notification("Preparando…", 0);
        if (Build.VERSION.SDK_INT >= 29) startForeground(NOTIFICATION, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC);
        else startForeground(NOTIFICATION, n);
        if (Odin2DeviceBuild.running) return START_NOT_STICKY;  // já rodando: só reaparece a notificação
        String[] uris = intent != null ? intent.getStringArrayExtra(EXTRA_URIS) : null;
        if (uris == null || uris.length != 3) {
            stopSelf();
            return START_NOT_STICKY;
        }
        wake = getSystemService(PowerManager.class).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "gtavport:build");
        wake.acquire(4 * 60 * 60 * 1000L);
        Odin2DeviceBuild.running = true;  // antes da thread: a tela não vê um "parado" entre os dois
        new Thread(() -> work(uris), "gtavport-build").start();
        tick();
        return START_NOT_STICKY;
    }

    private void work(String[] uris) {
        ParcelFileDescriptor[] fds = new ParcelFileDescriptor[3];
        String err;
        try {
            String[] paths = new String[3];
            for (int i = 0; i < 3; i++) {
                fds[i] = getContentResolver().openFileDescriptor(Uri.parse(uris[i]), "r");
                if (fds[i] == null) throw new IllegalStateException("não abriu " + uris[i]);
                paths[i] = "/proc/self/fd/" + fds[i].getFd();
            }
            err = Odin2DeviceBuild.run(this, paths[0], paths[1], paths[2]);
        } catch (Exception e) {
            Log.e(Odin2DeviceBuild.TAG, "work", e);
            err = String.valueOf(e);
            Odin2DeviceBuild.error = err;
            Odin2DeviceBuild.running = false;
            Odin2DeviceBuild.finished = true;
        } finally {
            for (ParcelFileDescriptor f : fds) {
                if (f != null) try { f.close(); } catch (Exception ignored) { }
            }
        }
        final String e = err;
        ui.post(() -> {
            NotificationManager nm = getSystemService(NotificationManager.class);
            stopForeground(STOP_FOREGROUND_DETACH);
            nm.notify(NOTIFICATION, notification(e == null ? "Pronto: o jogo foi compilado neste aparelho."
                    : (Odin2DeviceBuild.cancelled.get() ? "Cancelado (continua de onde parou)." : "Parou: " + e), -1));
            if (wake != null && wake.isHeld()) wake.release();
            stopSelf();
        });
    }

    private void tick() {
        if (!Odin2DeviceBuild.running) return;
        getSystemService(NotificationManager.class).notify(NOTIFICATION,
                notification(Odin2DeviceBuild.stage, (int) (Odin2DeviceBuild.progress * 1000)));
        ui.postDelayed(this::tick, 1500);
    }

    private Notification notification(String text, int progress) {
        Intent open = new Intent(this, Odin2DeviceBuildActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        PendingIntent pi = PendingIntent.getActivity(this, 0, open, PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        Notification.Builder b = new Notification.Builder(this, CHANNEL)
                .setSmallIcon(android.R.drawable.stat_sys_download)
                .setContentTitle("GTA V — compilando no aparelho")
                .setContentText(text == null || text.isEmpty() ? "…" : text)
                .setContentIntent(pi)
                .setOnlyAlertOnce(true);
        if (progress >= 0) b.setProgress(1000, progress, progress == 0).setOngoing(true);
        return b.build();
    }

    @Override
    public void onTimeout(int startId, int fgsType) {
        // targetSdk 35+ (dataSync tem limite diário): parar com a build retomável.
        Odin2DeviceBuild.cancelled.set(true);
    }
}
