package com.akpatch;

public class LabNative {
    public static native String dump(String filter, String path, int withMethods);
    public static native String fields(String ns, String cls);
    public static native String find(String ns, String cls, String method, int paramCount);
    public static native String scan(String needle);
    public static native String base();
    public static native String listMethods(String ns, String cls);
    public static native String trampTest(String rvaHex);
    public static native String hookSlot(int idx, String ns, String cls, String method, int pc);
    public static native String hookUrlSlot(int idx, String ns, String cls, String method, int pc, int on);
    public static native String hookUrl(String ns, String cls, String method, int pc, int on);
    public static native String hookSetResp(String rvaHex, int on);
    public static native String hookLog8(String rvaHex);
    public static native String hookParse(String rvaHex, int on);
    public static native String tap(int idx, String ns, String cls, String method, int pc);
    public static native String hookInt(String rvaHex, int value);
    public static native String netcfg();
    public static native String hookRet(String rvaHex, String value);
    public static native String hookRva(String rvaHex);
    public static native String hookLog(String ns, String cls, String method, int pc);
    public static native String hookGet(String ns, String cls, String method, int paramCount, String value);
}
