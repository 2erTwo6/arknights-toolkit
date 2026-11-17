#!/bin/bash
# Keep the phone screen awake; wake it if it ever sleeps. Never sends touch input.
while true; do
  ssh -F "$HOME/.ssh/config" -o ConnectTimeout=10 -o ServerAliveInterval=20 -o ServerAliveCountMax=3 phone \
    'while true; do
       w=$(dumpsys power | grep -m1 "mWakefulness=" )
       case "$w" in
         *Asleep*|*Dozing*) input keyevent KEYCODE_WAKEUP; echo "woke: $w";;
       esac
       sleep 20
     done' 2>&1
  echo "[keepawake] ssh dropped, retry in 5s $(date -Is)" >> tmp_wake.log
  sleep 5
done
