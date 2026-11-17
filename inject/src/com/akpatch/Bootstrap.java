package com.akpatch;

import android.content.Context;
import android.util.Log;

import java.io.File;

/**
 * APK 内的静态引导：只负责从 app 私有目录加载 lab.dex 并调用 com.akpatch.Lab.start(Context)。
 * 这样后续所有实验逻辑（lab.dex / liblab.so）都可以用 scp 热替换，无需重打包 1.9GB 的 APK。
 */
public class Bootstrap {
    static final String TAG = "AKPATCH";

    public static void start(Context c) {
        try {
            File dir = c.getFilesDir();
            File dex = new File(dir, "lab.dex");
            if (!dex.exists()) {
                Log.i(TAG, "bootstrap: no lab.dex, idle");
                return;
            }
            dalvik.system.DexClassLoader cl = new dalvik.system.DexClassLoader(
                    dex.getAbsolutePath(),
                    c.getCodeCacheDir().getAbsolutePath(),
                    null,
                    Bootstrap.class.getClassLoader());
            Class<?> k = Class.forName("com.akpatch.Lab", true, cl);
            k.getMethod("start", Context.class).invoke(null, c);
            Log.i(TAG, "bootstrap: lab loaded");
        } catch (Throwable t) {
            Log.e(TAG, "bootstrap failed", t);
        }
    }
}
