package com.akpatch;

public class Native {
    static {
        System.loadLibrary("akpatch");
    }
    public static native int patchUrl(String oldUrl, String newUrl, boolean wide, boolean doPatch);
    public static native int listTypes(String filter);
    public static native void startHook();
    public static native void startRvaHook();
    public static native void startConfigPatch();
    public static native void startUrlLog();
    public static native int scanUrls(String needle);
    public static native long findMethod(String ns, String cls, String method, int paramCount);
    public static native int listFields(String ns, String cls);
    public static native int classInfo(String ns, String cls);
}
