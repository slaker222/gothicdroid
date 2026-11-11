#!/bin/bash
# A/B on the phone: android_ab.sh LIB.so [SCALE SIMPLE WAIT]
# installs the given libopengothic.so, starts a new game, prints the mean of the last 20 [PERF] frames
D="$(cd "$(dirname "$0")" && pwd)"; ROOT="$D/.."
cp "$1" "$ROOT/android/app/src/main/jniLibs/arm64-v8a/libopengothic.so"
cp "$(dirname "$1")"/lib{hook_impl,main_hook,file_redirect_hook,gsl_alloc_hook}.so "$ROOT/android/app/src/main/jniLibs/arm64-v8a/" 2>/dev/null || true # adrenotools hooks, if next to the lib
( cd "$ROOT/android" && ANDROID_HOME="${ANDROID_HOME:-$HOME/android-sdk}" ./gradlew assembleDebug --offline > ../gradle.log 2>&1 ) || { tail -20 "$ROOT/gradle.log"; exit 1; }
adb install -r "$ROOT/android/app/build/outputs/apk/debug/app-debug.apk" > /dev/null
adb shell am force-stop com.gothic.droid
"$D/android_measure.sh" "${2:-40}" "${3:-true}" "${4:-60}" > /dev/null 2>&1
adb logcat -d -s OpenGothic:I | grep "\[PERF\]" | tail -20 | sed 's/.*frame=\([0-9]*\)ms (tick=\([0-9]*\) wait=[0-9]* draw=\([0-9]*\).*/\1 \2 \3/' \
 | awk '{f+=$1;t+=$2;d+=$3;n++} END{printf "%s fps=%.1f frame=%.1f tick=%.1f draw=%.1f (n=%d)\n", "'"$(basename $1)"'", 1000*n/f, f/n, t/n, d/n, n}'
