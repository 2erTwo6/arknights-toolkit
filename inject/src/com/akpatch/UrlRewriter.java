package com.akpatch;

import android.util.Log;

import java.io.IOException;

import okhttp3.HttpUrl;
import okhttp3.Interceptor;
import okhttp3.Request;
import okhttp3.Response;

/** Rewrites SDK requests that target Hypergryph servers to the private server over plain HTTP. */
public class UrlRewriter implements Interceptor {
    static final String TAG = "AKPATCH-NET";
    static final String SRV = "http://127.0.0.1:10029";  // 改成你的后端地址

    static boolean target(String host) {
        if (host == null) return false;
        if (host.equals("as.hypergryph.com") || host.equals("user.hypergryph.com")) return true;
        if (host.startsWith("as-") ) return true;
        if (host.equals("launcher.hypergryph.com")) return true;
        if (host.startsWith("game-config.")) return true;
        if (host.startsWith("core-api-account")) return true;
        return false;
    }

    @Override
    public Response intercept(Chain chain) throws IOException {
        Request req = chain.request();
        try {
            HttpUrl u = req.url();
            String host = u.host();
            if (target(host)) {
                String path = u.encodedPath();
                String q = u.encodedQuery();
                String url = SRV + path + (q != null ? "?" + q : "");
                Log.i(TAG, "rewrite " + host + path + " -> " + url);
                Request nr = req.newBuilder().url(url).build();
                return chain.proceed(nr);
            }
        } catch (Throwable t) {
            Log.e(TAG, "rewrite failed", t);
        }
        return chain.proceed(req);
    }
}
