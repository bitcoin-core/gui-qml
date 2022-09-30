// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

package org.bitcoincore.qt;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.content.Intent;
import android.util.Log;
import org.qtproject.qt5.android.bindings.QtService;

public class BitcoinQtService extends QtService
{

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

        Notification notification = new Notification.Builder(this, "bitcoin_channel_id")
            .setSmallIcon(R.drawable.bitcoin)
            .setContentTitle("Running bitcoin")
            .setOngoing(true)
            .build();

        startForeground(1, notification);
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        super.onStartCommand(intent, flags, startId);
        return START_NOT_STICKY;
    }
}
