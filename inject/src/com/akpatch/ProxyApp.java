package com.akpatch;

import android.content.Context;
import android.content.res.Configuration;
import android.util.Log;
import com.u8.sdk.IApplicationListener;

public class ProxyApp implements IApplicationListener {
    static { Log.i("AKPATCH", "static init"); }
    public ProxyApp() { Log.i("AKPATCH", "ctor"); }
    public void onProxyAttachBaseContext(Context c) {
        Log.i("AKPATCH", "onProxyAttachBaseContext");
        try { Bootstrap.start(c); } catch (Throwable t) { Log.e("AKPATCH", "attach err", t); }
    }
    public void onProxyConfigurationChanged(Configuration cfg) { }
    public void onProxyCreate() {
        Log.i("AKPATCH", "onProxyCreate");
        try { AkPatchEntry.onCreate(); } catch (Throwable t) { Log.e("AKPATCH", "create err", t); }
    }
    public void onProxyTerminate() { }
}
