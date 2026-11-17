# arknights-toolkit

Android / Unity IL2CPP（arm64-v8a）客户端的**网络层与运行时实验工具集**。

它面向的是几类通用问题，而不是某个具体产品：

- Android 多 dex 加载与运行时扩展：不修改 Manifest，让新增代码参与启动流程；
- OkHttp 层的请求拦截与路由改写；
- IL2CPP 原生方法的内联 hook：近地址跳板、ADRP 重定位、8 寄存器参数透传；
- APK 的增量重建（保留未改条目的原始压缩数据与对齐）与差量推送。

> 仓库不含游戏本体、资源包、日志或官方二进制；示例地址统一为 `127.0.0.1`。
> 仅供技术学习，使用前请阅读 `NOTICE.md`。

## 组成

| 目录 | 内容 |
|---|---|
| `lab/` | 可热替换的实验通道：`lab.dex` + `liblab.so`，命令循环与 native 探针 |
| `inject/` | 引导层：`ProxyApp` / `Bootstrap`，以及编译期 stub |
| `scripts/` | APK 重建、差量推送、变体实验 |
| `specs/` | 打包 spec 与配置模板（第三方密钥已打码） |
| `docs/` | 设计记录、实验通道说明、部署笔记 |

## 构建

依赖：JDK 17、Android build-tools（`d8`）、NDK（`aarch64-linux-android24-clang`）、对应版本的 `android.jar`。

```bash
export ANDROID_HOME=/path/to/android-sdk
export NDK_CC=/path/to/ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android24-clang
lab/build_lab.sh            # 产物：build/out/lab.dex、build/out/liblab.so
```

默认后端地址为 `127.0.0.1:10029`，可在编译期覆盖：

```bash
AK_SERVER_URL=http://127.0.0.1:10029 lab/build_lab.sh
```

## 打包与安装

```bash
# 1) 引导 dex（ProxyApp + Bootstrap + AkPatchEntry）
javac -source 8 -target 8 -nowarn -cp "$ANDROID_JAR:build/stub.jar" -d build/inject/cls $(find inject/src -name '*.java')
d8 --min-api 24 --output build/inject $(find build/inject/cls -name '*.class')

# 2) 按 spec 重建 APK：未改条目连压缩数据一起原样保留
python3 scripts/zipedit.py original.apk patched.apk specs/spec_v2.json

# 3) 自签名后安装
apksigner sign --ks test.jks --ks-pass pass:testpass \
  --v1-signing-enabled false --v2-signing-enabled true --v3-signing-enabled true patched.apk
```

## 运行实验

调试机需要 root 才能把 `lab.dex` 放进应用私有目录（发布形态应把逻辑直接打进 APK）：

```bash
scp -F ~/.ssh/config build/out/lab.dex build/out/liblab.so phone:/data/local/tmp/
ssh -F ~/.ssh/config phone 'D=/data/data/<package>/files; \
  cp /data/local/tmp/lab.dex /data/local/tmp/liblab.so $D/; \
  chown root:root $D/lab.dex $D/liblab.so; chmod 644 $D/lab.dex $D/liblab.so'
```

`files/ak_boot` 每次启动执行一次，`files/ak_cmd` 用于运行中一次性命令，输出写到 `files/ak_out`。命令清单与示例见 `docs/实验通道说明.md`。

## 实现要点

- 目标客户端通常有多套网络栈（Java、C#、UnityWebRequest），只处理其中一层会出现「部分请求生效、关键请求仍走默认路由」的现象；排查时先确认请求由哪套栈发出。
- 部分配置接口会校验来源域名，改内容或换地址都可能被判空；这类场景更适合在网络层改写请求 URL，而不是改配置数据。
- 内联 hook 的跳板必须落在目标 ±1GB 内，否则 `ADRP` 无法重定位；16 字节补丁不能打在短函数上；多目标要各自独立跳板，共用会串。
- 部分环境下 `dlopen` / `dlsym` 拿不到 il2cpp 符号，可直接解析 `/proc/self/maps` 中的 ELF `.dynsym`。

## 不包含

- 游戏本体、资源包、`global-metadata.dat`、日志、截图；
- 任何官方 `.so` / `.dex` / 密钥；
- 服务端实现（本仓库只做客户端侧）。

## 已知限制

- hook 依赖具体版本的类名/方法名/参数个数，客户端更新后需要重新定位；
- 当前仍需在启动时注入，尚未固化为单包；
- 仅验证过 arm64-v8a。

## 免责声明

仅供安全研究与客户端技术学习。请自行确认所在地区的法律与相关服务条款；不得用于商业用途、作弊、破坏公平性或传播受版权保护的资源。详见 `NOTICE.md`。
