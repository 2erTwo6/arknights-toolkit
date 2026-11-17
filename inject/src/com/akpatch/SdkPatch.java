package com.akpatch;

import android.util.Log;

import java.lang.reflect.Field;
import java.lang.reflect.Method;

public class SdkPatch {
    static final String TAG = "AKPATCH-SDK";

    static boolean patchClientClass(String cls) {
        try {
            Class<?> c = Class.forName(cls);
            Field f = c.getDeclaredField("okHttpClient");
            f.setAccessible(true);
            Object client = f.get(null);
            if (client == null) return false;
            if (client.getClass().getName().contains("AkClient")) return true;
            Method newBuilder = client.getClass().getMethod("newBuilder");
            Object b = newBuilder.invoke(client);
            Class<?> interceptorCls = Class.forName("okhttp3.Interceptor");
            Object interceptor = Class.forName("com.akpatch.UrlRewriter").newInstance();
            Method add = b.getClass().getMethod("addInterceptor", interceptorCls);
            add.invoke(b, interceptor);
            Method build = b.getClass().getMethod("build");
            Object nc = build.invoke(b);
            f.set(null, nc);
            Log.i(TAG, "patched okHttpClient of " + cls);
            return true;
        } catch (ClassNotFoundException e) {
            return false;
        } catch (Throwable t) {
            Log.e(TAG, "patch failed for " + cls, t);
            return false;
        }
    }

    public static void patch() {
        boolean a = patchClientClass("com.hypergryph.platform.hgsdk.http.NetworkService");
        boolean b = patchClientClass("com.hypergryph.platform.hguseragreement.commom.http.NetworkService");
        Log.i(TAG, "sdk patch done hgsdk=" + a + " agreement=" + b);
    }
}
