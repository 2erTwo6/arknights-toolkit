#!/bin/bash
# usage: variant.sh <tag> <spec.json> [wait_seconds]
set -e
cd /var/home/liu/文档/deepseek-harness/MRFZ
export JAVA_HOME=$PWD/tmp/tools/jdk-17.0.21+9
export PATH=$JAVA_HOME/bin:$PATH
TAG=$1; SPEC=$2; WAIT=${3:-45}
python3 tmp/zipedit.py "arknights-hg-2791原版客户端.apk" "tmp/work/ak_$TAG.apk" "$SPEC"
tmp/tools/android-14/apksigner sign --ks tmp/work/test.jks --ks-pass pass:testpass --key-pass pass:testpass \
  --v1-signing-enabled false --v2-signing-enabled true --v3-signing-enabled true "tmp/work/ak_$TAG.apk"
OUT=$(python3 tmp/push_apk.py "arknights-hg-2791原版客户端.apk" "tmp/work/ak_$TAG.apk" "tmp/work/tail_$TAG.bin")
echo "$OUT"
PREFIX=$(echo "$OUT" | sed -n 's/.*PREFIX=\([0-9]*\).*/\1/p')
scp -F ~/.ssh/config -q "tmp/work/tail_$TAG.bin" phone:/data/local/tmp/tail.bin
ssh -F ~/.ssh/config phone "cd /data/local/tmp && head -c $PREFIX ak_orig.apk > ak_$TAG.apk && cat tail.bin >> ak_$TAG.apk && pm install -r -d -g /data/local/tmp/ak_$TAG.apk 2>&1 | tail -1"
ssh -F ~/.ssh/config phone "logcat -c; am force-stop com.hypergryph.arknights; am start -n com.hypergryph.arknights/com.u8.sdk.U8UnityContext >/dev/null 2>&1; sleep $WAIT; if pidof com.hypergryph.arknights >/dev/null; then echo ALIVE_$TAG; else echo DIED_$TAG; fi; screencap -p /data/local/tmp/$TAG.png"
scp -F ~/.ssh/config -q phone:/data/local/tmp/$TAG.png "tmp/work/$TAG.png"
