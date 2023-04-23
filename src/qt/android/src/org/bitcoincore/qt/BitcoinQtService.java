// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

package org.bitcoincore.qt;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Intent;
import android.net.wifi.WifiManager;
import android.util.Log;
import org.qtproject.qt5.android.bindings.QtService;
import android.content.Context;
import android.os.PowerManager;

public class BitcoinQtService extends QtService
{
    private static final String TAG = "BitcoinQtService";
    private static final int NOTIFICATION_ID = 21000000;
    private PowerManager.WakeLock wakeLock;
    private WifiManager.WifiLock wifiLock;
    private Notification.Builder notificationBuilder;
    private boolean connected = false;
    private boolean paused = false;
    private boolean synced = false;
    private int blockHeight = 0;
    private double verificationProgress = 0.0;


    @Override
    public void onCreate() {
        super.onCreate();

        CharSequence name = "Bitcoin Core";
        String description = "Bitcoin Core App notifications";
        // IMPORTANCE_LOW notifications won't make sound
        int importance = NotificationManager.IMPORTANCE_LOW;
        NotificationChannel channel = new NotificationChannel("bitcoin_channel_id", name, importance);
        channel.setDescription(description);

        NotificationManager notificationManager = getSystemService(NotificationManager.class);
        notificationManager.createNotificationChannel(channel);

        Intent intent = new Intent(this, BitcoinQtActivity.class);
        PendingIntent pendingIntent = PendingIntent.getActivity(this, 0, intent, 0);

        notificationBuilder = new Notification.Builder(this, "bitcoin_channel_id")
            .setSmallIcon(R.drawable.bitcoin)
            .setContentTitle("Running bitcoin")
            .setOngoing(true)
            .setContentIntent(pendingIntent);

        startForeground(NOTIFICATION_ID, notificationBuilder.build());
        PowerManager powerManager = (PowerManager) getSystemService(Context.POWER_SERVICE);
        wakeLock = powerManager.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "BitcoinCore::IBD");

        WifiManager wifiManager = (WifiManager) getSystemService(Context.WIFI_SERVICE);
        wifiLock = wifiManager.createWifiLock(WifiManager.WIFI_MODE_FULL_HIGH_PERF, "BitcoinCore::WIFI_LOCK");
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        super.onStartCommand(intent, flags, startId);
        wakeLock.acquire();
        wifiLock.acquire();
        if (register()) {
            Log.d(TAG, "Registered JVM to native module");
        } else {
            Log.e(TAG, "Failed to register JVM to native module");
        }
        return START_NOT_STICKY;
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        if (wakeLock.isHeld()) {
            wakeLock.release(); // Release the wake lock
        }

        if (wifiLock.isHeld()) {
            wifiLock.release(); // Release the WiFi lock
        }
    }

    public void updateBlockTipHeight(int blockHeight) {
        if (this.blockHeight != blockHeight) {
            this.blockHeight = blockHeight;
            if (synced && connected) {
                updateNotification();
            }
        }
     }

    public void updateNumberOfPeers(int numPeers) {
        boolean newConnectedState = numPeers > 0;
        if (connected != newConnectedState) {
            connected = newConnectedState;
            updateNotification();
        }
    }

    public void updatePaused(boolean paused) {
        if (this.paused != paused) {
            this.paused = paused;
            updateNotification();
        }
    }

    public void updateVerificationProgress(double progress) {
        boolean newSyncedState = progress > 0.999;
        boolean needNotificationUpdate = false;
        if (synced != newSyncedState) {
            synced = newSyncedState;
            needNotificationUpdate = true;
        }
        double newProgress = Math.floor(progress * 10000) / 100.0;
        if (verificationProgress != newProgress ) {
            verificationProgress = newProgress;
            needNotificationUpdate = true;
        }
        if (needNotificationUpdate) {
            updateNotification();
        }
    }

    private void updateNotification() {
        NotificationManager notificationManager = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
        if (paused) {
            notificationBuilder.setContentTitle("Paused");
        } else if (!connected) {
            notificationBuilder.setContentTitle("Connecting...");
        } else if (!synced) {
            if (verificationProgress < 0) {
                notificationBuilder.setContentTitle(String.format("%.2f%% loaded...", verificationProgress));
            } else {
                notificationBuilder.setContentTitle(String.format("%.0f%% loaded...", verificationProgress));
            }
        } else {
            // Synced and connected
            notificationBuilder.setContentTitle(String.format("Blocktime %,d", blockHeight));
        }

        notificationManager.notify(NOTIFICATION_ID, notificationBuilder.build());
    }

    public native boolean register();
}
