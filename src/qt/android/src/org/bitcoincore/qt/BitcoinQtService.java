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
    private PowerManager.WakeLock wakeLock;
    private WifiManager.WifiLock wifiLock;

    @Override
    public void onCreate() {
        super.onCreate();

        CharSequence name = "Bitcoin Core";
        String description = "Bitcoin Core App notifications";
        int importance = NotificationManager.IMPORTANCE_DEFAULT;
        NotificationChannel channel = new NotificationChannel("bitcoin_channel_id", name, importance);
        channel.setDescription(description);

        NotificationManager notificationManager = getSystemService(NotificationManager.class);
        notificationManager.createNotificationChannel(channel);

        Intent intent = new Intent(this, BitcoinQtActivity.class);
        PendingIntent pendingIntent = PendingIntent.getActivity(this, 0, intent, 0);

        Notification notification = new Notification.Builder(this, "bitcoin_channel_id")
            .setSmallIcon(R.drawable.bitcoin)
            .setContentTitle("Running bitcoin")
            .setOngoing(true)
            .setContentIntent(pendingIntent)
            .build();

        startForeground(1, notification);
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
}
