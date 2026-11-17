#!/bin/bash
# 一条龙：重启客户端 → 自动挂载探针(ak_boot) → 点完整流程 → 抓日志
# 前置：lab.dex / liblab.so 已放到 /data/data/com.hypergryph.arknights/files/
#       ak_boot 内容为要自动执行的 lab 命令（每行一条）
set -u
DEV=${DEVICE:-phone}
PH="ssh -F $HOME/.ssh/config $DEV"
$PH 'D=/data/data/com.hypergryph.arknights/files; am force-stop com.hypergryph.arknights; sleep 1; rm -f $D/ak_out; am start -n com.hypergryph.arknights/com.u8.sdk.U8UnityContext >/dev/null 2>&1'
for i in $(seq 1 30); do
  F=$($PH 'dumpsys window 2>/dev/null | grep -m1 mCurrentFocus' 2>/dev/null)
  case "$F" in *arknights*) break;; esac
  sleep 1
done
sleep 12
$PH 'input tap 983 697; sleep 0.3; input tap 1380 891'   # 语音弹窗：勾选+确认（无弹窗时无害）
sleep 7
$PH 'input tap 1380 1268'                                # 加载页 START
sleep 7
$PH 'input tap 1380 925'                                 # 登录页 开始唤醒
sleep 16
echo "== focus =="; $PH 'dumpsys window 2>/dev/null | grep -m1 mCurrentFocus'
echo "== pid ==";   $PH 'pidof com.hypergryph.arknights'
echo "== 探针原文 =="; $PH 'tail -20 /data/data/com.hypergryph.arknights/files/ak_out'
echo "== 改写/打点 =="; $PH 'logcat -d -s AKLAB-N 2>&1 | grep -aE "CSHARP|^.*CS[0-3] |H8 |TAP" | tail -15'
$PH 'screencap -p /data/local/tmp/flow.png' 2>/dev/null
