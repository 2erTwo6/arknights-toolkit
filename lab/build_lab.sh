#!/bin/bash
# 构建 lab.dex + liblab.so
# 依赖：JDK 17+、Android build-tools(d8)、NDK clang、android.jar
# 可用环境变量：JAVA_HOME ANDROID_HOME ANDROID_JAR D8 NDK_CC AK_SERVER_URL OUT
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-$ROOT/build/out}
CLS=${CLS:-$ROOT/build/cls}
STUB_OUT=$ROOT/build/stub
if [ -n "${JAVA_HOME:-}" ]; then export PATH="$JAVA_HOME/bin:$PATH"; fi
JAVAC=${JAVAC:-javac}
JAR=${JAR:-jar}
ANDROID_JAR=${ANDROID_JAR:-}
if [ -z "$ANDROID_JAR" ] && [ -n "${ANDROID_HOME:-}" ]; then
  ANDROID_JAR=$(ls "$ANDROID_HOME"/platforms/android-*/android.jar 2>/dev/null | sort -V | tail -1)
fi
[ -n "$ANDROID_JAR" ] || { echo "缺少 android.jar：设置 ANDROID_JAR 或 ANDROID_HOME"; exit 1; }
D8=${D8:-}
if [ -z "$D8" ] && [ -n "${ANDROID_HOME:-}" ]; then
  D8=$(ls "$ANDROID_HOME"/build-tools/*/d8 2>/dev/null | sort -V | tail -1)
fi
[ -n "$D8" ] || { echo "缺少 d8：设置 D8 或 ANDROID_HOME"; exit 1; }
NDK_CC=${NDK_CC:-aarch64-linux-android24-clang}

rm -rf "$OUT" "$CLS" "$STUB_OUT"; mkdir -p "$OUT" "$CLS" "$STUB_OUT"

# 1) stub 只用于编译期，不进入 lab.dex
"$JAVAC" -source 8 -target 8 -nowarn -cp "$ANDROID_JAR" -d "$STUB_OUT" $(find "$ROOT/inject/stub" -name "*.java")
"$JAR" cf "$ROOT/build/stub.jar" -C "$STUB_OUT" .

# 2) lab.dex
"$JAVAC" -source 8 -target 8 -nowarn -cp "$ANDROID_JAR:$ROOT/build/stub.jar" -d "$CLS" $(find "$ROOT/lab/src" -name "*.java")
"$D8" --min-api 24 --output "$OUT" $(find "$CLS" -name "*.class")
mv "$OUT/classes.dex" "$OUT/lab.dex"

# 3) native（可用 AK_SERVER_URL 覆盖默认地址）
CFLAGS=""
if [ -n "${AK_SERVER_URL:-}" ]; then CFLAGS="-DAK_SERVER_URL=\"$AK_SERVER_URL\""; fi
"$NDK_CC" -shared -fPIC -O2 -Wall $CFLAGS -o "$OUT/liblab.so" "$ROOT/lab/native/labl.c" -llog -ldl

echo "构建完成："; ls -la "$OUT"
