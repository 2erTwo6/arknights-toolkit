#!/bin/bash
# usage: variant_region.sh <tag> <spec.json> <changed_entry> [wait]
set -e
cd /var/home/liu/文档/deepseek-harness/MRFZ
export JAVA_HOME=$PWD/tmp/tools/jdk-17.0.21+9
export PATH=$JAVA_HOME/bin:$PATH
TAG=$1; SPEC=$2; ENTRY=$3; WAIT=${4:-45}
mkdir -p tmp/work/reg_$TAG
python3 tmp/zipedit.py "tmp/work/ak_stripped.apk" "tmp/work/ak_$TAG.apk" "$SPEC" >/dev/null
tmp/tools/android-14/apksigner sign --ks tmp/work/test.jks --ks-pass pass:testpass --key-pass pass:testpass \
  --v1-signing-enabled false --v2-signing-enabled true --v3-signing-enabled true "tmp/work/ak_$TAG.apk" >/dev/null
INFO=$(python3 tmp/region_push.py "tmp/work/ak_base2.apk" "tmp/work/ak_$TAG.apk" "$ENTRY" "tmp/work/reg_$TAG")
echo "$INFO"
A=$(echo "$INFO" | python3 -c "import json,sys;print(json.load(sys.stdin)['A'])")
LOLD=$(echo "$INFO" | python3 -c "import json,sys;print(json.load(sys.stdin)['L_old'])")
LNEW=$(echo "$INFO" | python3 -c "import json,sys;print(json.load(sys.stdin)['L_new'])")
SUFA=$(echo "$INFO" | python3 -c "import json,sys;print(json.load(sys.stdin)['suffix_start'])")
SUFL=$(echo "$INFO" | python3 -c "import json,sys;print(json.load(sys.stdin)['suffix_len'])")
SIZE=$(echo "$INFO" | python3 -c "import json,sys;print(json.load(sys.stdin)['size'])")
scp -F ~/.ssh/config -q "tmp/work/reg_$TAG/region.bin" "tmp/work/reg_$TAG/tail.bin" phone:/data/local/tmp/
ssh -F ~/.ssh/config phone "cd /data/local/tmp && head -c $A ak_base2.apk > ak_$TAG.apk && cat region.bin >> ak_$TAG.apk && tail -c +$((SUFA+1)) ak_base2.apk | head -c $SUFL >> ak_$TAG.apk && cat tail.bin >> ak_$TAG.apk && truncate -s $SIZE ak_$TAG.apk && sha256sum ak_$TAG.apk | cut -d' ' -f1 && pm install -r -d -g /data/local/tmp/ak_$TAG.apk 2>&1 | tail -1"
echo "local sha: $(sha256sum tmp/work/ak_$TAG.apk | cut -d' ' -f1)"
ssh -F ~/.ssh/config phone "logcat -c; am force-stop com.hypergryph.arknights; am start -n com.hypergryph.arknights/com.u8.sdk.U8UnityContext >/dev/null 2>&1; sleep $WAIT; if pidof com.hypergryph.arknights >/dev/null; then echo ALIVE_$TAG; else echo DIED_$TAG; fi; screencap -p /data/local/tmp/$TAG.png"
scp -F ~/.ssh/config -q phone:/data/local/tmp/$TAG.png "tmp/work/$TAG.png"
