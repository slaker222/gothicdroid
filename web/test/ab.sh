#!/bin/bash
# A/B of two builds, interleaved: ab.sh N [gtest args]   (A = :8323 base build, B = :8322 current dist-web)
# prints the fps of every run and the means
N=${1:-3}; shift
cd "$(dirname "$0")"
for i in $(seq $N); do
  for pair in "A 8323" "B 8322"; do
    set -- $pair "$@"
    name=$1; port=$2; shift 2
    r=$(timeout 300 node gtest.mjs --url=http://127.0.0.1:$port/ --nomenu --uncapped --nolimit --profile=${PROFILE:-profile2} --set=${SET:-shadows:2,ssao:1,reflections:1,drawDist:0,worldDist:0,resScale:0} --warm=${WARM:-12} --measure=${MEASURE:-20} "$@" 2>&1 | grep '^{"url')
    echo "$name $(echo "$r" | sed 's/.*"fps":\([0-9.]*\).*"frameMs":\([0-9.]*\),"tickMs":\([0-9.]*\),"drawMs":\([0-9.]*\).*/fps=\1 frame=\2 tick=\3 draw=\4/')"
  done
done | tee /dev/stderr | awk '{split($2,f,"="); s[$1]+=f[2]; n[$1]++} END{for(k in s) printf "%s mean fps %.1f (n=%d)\n", k, s[k]/n[k], n[k]}'
