package org.bitcoincore.qt;

import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.system.ErrnoException;
import android.system.Os;
import android.view.WindowManager;

import org.qtproject.qt5.android.bindings.QtActivity;

import java.io.File;

public class BitcoinQtActivity extends QtActivity
{
    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        final File bitcoinDir = new File(getFilesDir().getAbsolutePath() + "/.bitcoin");
        if (!bitcoinDir.exists()) {
            bitcoinDir.mkdir();
        }

        Intent intent = new Intent(this, BitcoinQtService.class);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                startForegroundService(intent);
        } else {
                startService(intent);
        }

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        super.onCreate(savedInstanceState);
    }
}
