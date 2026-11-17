package com.akpatch;

import android.content.Context;
import android.util.Log;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;

/**
 * 可迭代实验入口：APK 内只有 Bootstrap，真正的逻辑全部从 files/lab.dex + files/liblab.so 加载。
 * 命令文件: files/ak_cmd (每行一条)，输出: files/ak_out (追加)。
 */
public class Lab {
    static final String TAG = "AKLAB";
    static File sDir;
    static Context sCtx;

    public static void start(Context c) {
        sCtx = c;
        sDir = c.getFilesDir();
        // 先起 SDK 重定向，保证即使 native 库有问题也不影响登录
        startSdkPatch();
        try {
            loadNative(c);
            File cf = new File(sDir, "ak_cmd");
            if (!cf.exists()) cf.createNewFile();
            append("ak_out", "\n=== lab up (pid " + android.os.Process.myPid() + ") ===\n");
            Log.i(TAG, "lab started dir=" + sDir);
        } catch (Throwable t) {
            Log.e(TAG, "native load failed", t);
            append("ak_out", "native load failed: " + t + "\n");
            return;
        }
        Thread t = new Thread(new Runnable() {
            public void run() { loop(); }
        }, "aklab-cmd");
        t.setDaemon(true);
        t.start();


    }

    static void startSdkPatch() {
        Thread boot = new Thread(new Runnable() {
            public void run() {
                try {
                    File f = new File(sDir, "ak_boot");
                    if (!f.exists() || f.length() == 0) return;
                    byte[] b = readAll(f);
                    String script = new String(b, "UTF-8");
                    long t0 = System.currentTimeMillis();
                    String res = exec(script);
                    append("ak_out", "### BOOT " + script.trim().replace('\n', ';') + "\n" + res
                            + "### boot done in " + (System.currentTimeMillis() - t0) + "ms\n");
                } catch (Throwable t) {
                    Log.e(TAG, "boot failed", t);
                }
            }
        }, "aklab-boot");
        boot.setDaemon(true);
        boot.start();

        Thread sdk = new Thread(new Runnable() {
            public void run() {
                for (int i = 0; i < 120; i++) {
                    try { SdkPatch.patch(); } catch (Throwable ignored) { }
                    try { Thread.sleep(500); } catch (InterruptedException ignored) { }
                }
            }
        }, "aklab-sdk");
        sdk.setDaemon(true);
        sdk.start();
    }

    static void loadNative(Context c) throws Exception {
        File src = new File(sDir, "liblab.so");
        File dst = new File(c.getCodeCacheDir(), "liblab.so");
        copy(src, dst);
        System.load(dst.getAbsolutePath());
    }

    static void copy(File a, File b) throws Exception {
        InputStream in = new FileInputStream(a);
        FileOutputStream out = new FileOutputStream(b);
        byte[] buf = new byte[65536];
        int n;
        while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
        in.close();
        out.close();
    }

    static byte[] readAll(File f) throws Exception {
        ByteArrayOutputStream bos = new ByteArrayOutputStream();
        InputStream in = new FileInputStream(f);
        byte[] buf = new byte[65536];
        int n;
        while ((n = in.read(buf)) > 0) bos.write(buf, 0, n);
        in.close();
        return bos.toByteArray();
    }

    static synchronized void append(String name, String text) {
        try {
            Log.i(TAG, "append " + name + " len=" + text.length());
            FileOutputStream o = new FileOutputStream(new File(sDir, name), true);
            o.write(text.getBytes("UTF-8"));
            o.close();
        } catch (Throwable t) {
            Log.e(TAG, "append failed for " + name, t);
        }
    }

    static void loop() {
        File cmd = new File(sDir, "ak_cmd");
        while (true) {
            try {
                if (cmd.exists() && cmd.length() > 0) {
                    byte[] b = readAll(cmd);
                    // 立即清空，避免重复执行；root 写文件时用 > 覆盖
                    new FileOutputStream(cmd).close();
                    String script = new String(b, "UTF-8");
                    Log.i(TAG, "cmd recv len=" + b.length + " text=" + script.replace('\n',';'));
                    long t0 = System.currentTimeMillis();
                    String res = exec(script);
                    append("ak_out", "### " + script.trim().replace('\n', ';') + "\n"
                            + res + "### done in " + (System.currentTimeMillis() - t0) + "ms\n");
                }
                Thread.sleep(200);
            } catch (Throwable t) {
                Log.e(TAG, "loop exception", t);
                try { Thread.sleep(500); } catch (InterruptedException ignored) { }
            }
        }
    }

    static String outPath() { return new File(sDir, "ak_out").getAbsolutePath(); }

    static boolean waitReady(int seconds) {
        for (int i = 0; i < seconds; i++) {
            try {
                String b = LabNative.base();
                if (b != null && !b.contains("base=0x0")) return true;
            } catch (Throwable ignored) { }
            try { Thread.sleep(1000); } catch (InterruptedException ignored) { }
        }
        return false;
    }

    static String exec(String script) {
        StringBuilder sb = new StringBuilder();
        if (!waitReady(120)) {
            return "il2cpp not ready after 120s\n";
        }
        for (String line : script.split("\n")) {
            line = line.trim();
            if (line.length() == 0) continue;
            String[] p = line.split("\\|", -1);
            String cmd = p[0];
            Log.i(TAG, "exec cmd=" + cmd);
            try {
                if (cmd.equals("classes")) {
                    sb.append(LabNative.dump(p.length > 1 ? p[1] : "", outPath(), 0));
                } else if (cmd.equals("dump")) {
                    sb.append(LabNative.dump(p.length > 1 ? p[1] : "", outPath(), 1));
                } else if (cmd.equals("fields")) {
                    sb.append(LabNative.fields(p[1], p[2]));
                } else if (cmd.equals("find")) {
                    sb.append(LabNative.find(p[1], p[2], p[3], Integer.parseInt(p[4])));
                } else if (cmd.equals("scan")) {
                    sb.append(LabNative.scan(p[1]));
                } else if (cmd.equals("base")) {
                    sb.append(LabNative.base());
                } else if (cmd.equals("tramptest")) {
                    sb.append(LabNative.trampTest(p[1]));
                } else if (cmd.equals("hookslot")) {
                    sb.append(LabNative.hookSlot(Integer.parseInt(p[1]), p[2], p[3], p[4], Integer.parseInt(p[5])));
                } else if (cmd.equals("hookurlslot")) {
                    sb.append(LabNative.hookUrlSlot(Integer.parseInt(p[1]), p[2], p[3], p[4], Integer.parseInt(p[5]), Integer.parseInt(p[6])));
                } else if (cmd.equals("hookurl")) {
                    sb.append(LabNative.hookUrl(p[1], p[2], p[3], Integer.parseInt(p[4]), Integer.parseInt(p[5])));
                } else if (cmd.equals("hooksetresp")) {
                    sb.append(LabNative.hookSetResp(p[1], Integer.parseInt(p[2])));
                } else if (cmd.equals("hooklog8")) {
                    sb.append(LabNative.hookLog8(p[1]));
                } else if (cmd.equals("hookparse")) {
                    sb.append(LabNative.hookParse(p[1], Integer.parseInt(p[2])));
                } else if (cmd.equals("tap")) {
                    sb.append(LabNative.tap(Integer.parseInt(p[1]), p[2], p[3], p[4], Integer.parseInt(p[5])));
                } else if (cmd.equals("hookint")) {
                    sb.append(LabNative.hookInt(p[1], Integer.parseInt(p[2])));
                } else if (cmd.equals("netcfg")) {
                    sb.append(LabNative.netcfg());
                } else if (cmd.equals("hookret")) {
                    sb.append(LabNative.hookRet(p[1], p[2]));
                } else if (cmd.equals("hookrva")) {
                    sb.append(LabNative.hookRva(p[1]));
                } else if (cmd.equals("hooklog")) {
                    sb.append(LabNative.hookLog(p[1], p[2], p[3], Integer.parseInt(p[4])));
                } else if (cmd.equals("hookget")) {
                    sb.append(LabNative.hookGet(p[1], p[2], p[3], Integer.parseInt(p[4]),
                            p.length > 5 ? p[5] : ""));
                } else if (cmd.equals("listmethods")) {
                    sb.append(LabNative.listMethods(p[1], p[2]));
                } else if (cmd.equals("echo")) {
                    sb.append("echo: ").append(line).append("\n");
                } else {
                    sb.append("unknown cmd: ").append(cmd).append("\n");
                }
            } catch (Throwable t) {
                sb.append("cmd failed: ").append(line).append(" -> ").append(t).append("\n");
            }
        }
        return sb.toString();
    }
}
