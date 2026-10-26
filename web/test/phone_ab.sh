#!/bin/bash
# A/B of two builds on the phone (adb): phone_ab.sh N   (A = :8323, B = :8322); settings in SET
N=${1:-3}; cd "$(dirname "$0")"
SET=${SET:-resScale:3,shadows:0,ssao:0,reflections:0,drawDist:4,worldDist:150,skipFarAnim:1}
for i in $(seq $N); do for pair in "A 8323" "B 8322"; do set -- $pair; 
  r=$(timeout 400 node gtest.mjs --device --url=http://127.0.0.1:$2/ --nomenu --uncapped --nolimit --set=$SET --warm=${WARM:-30} --measure=${MEASURE:-20} 2>&1 | grep '^{"url')
  echo "$1 $(echo "$r" | sed 's/.*"fps":\([0-9.]*\).*"frameMs":\([0-9.]*\),"tickMs":\([0-9.]*\),"drawMs":\([0-9.]*\).*/fps=\1 frame=\2 tick=\3 draw=\4/')"
done; done | tee /dev/stderr | awk '{split($2,f,"="); s[$1]+=f[2]; n[$1]++} END{for(k in s) printf "%s mean fps %.1f (n=%d)\n", k, s[k]/n[k], n[k]}'
