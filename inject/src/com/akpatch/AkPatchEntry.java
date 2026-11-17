package com.akpatch;

import android.content.Context;
import android.util.Log;

public class AkPatchEntry {
    static final String TAG = "AKPATCH";

    public static void onAttach(Context c) {
        Log.i(TAG, "onAttach " + c);
        Thread t = new Thread(new Runnable() {
            public void run() { work(); }
        }, "akpatch-work");
        t.setDaemon(true);
        t.start();
    }

    public static void onCreate() {
        Log.i(TAG, "onCreate");
    }

    static void work() {
        Log.i(TAG, "work start");
        try {
            Native.startRvaHook();
            Native.startUrlLog();
            Native.startConfigPatch();
            Log.i(TAG, "startRvaHook called");
        } catch (Throwable t) {
            Log.e(TAG, "startRvaHook failed", t);
        }
        for (int i = 0; i < 40; i++) {
            try {
                SdkPatch.patch();
            } catch (Throwable t) {
                Log.e(TAG, "SdkPatch failed", t);
            }
            try { Thread.sleep(500); } catch (InterruptedException e) {}
        }

        Log.i(TAG, "work done");
    }
}
