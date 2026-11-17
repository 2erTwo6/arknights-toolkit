package com.u8.sdk;
public interface IApplicationListener {
    void onProxyAttachBaseContext(android.content.Context c);
    void onProxyConfigurationChanged(android.content.res.Configuration cfg);
    void onProxyCreate();
    void onProxyTerminate();
}
