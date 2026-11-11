#!/bin/bash
# Native android build on a connected phone: libopengothic.so -> apk -> install -> optional start.
#   scripts/android_deploy.sh [--run]        (build dir: build-android-arm64, sdk: ~/android-sdk)
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
ninja -C build-android-arm64 > build-android.log 2>&1 || { grep -E "error:|FAILED:" build-android.log | head; exit 1; }
cp build-android-arm64/opengothic/libopengothic.so android/app/src/main/jniLibs/arm64-v8a/libopengothic.so
cp build-android-arm64/opengothic/lib{hook_impl,main_hook,file_redirect_hook,gsl_alloc_hook}.so android/app/src/main/jniLibs/arm64-v8a/
( cd android && ANDROID_HOME="${ANDROID_HOME:-$HOME/android-sdk}" ./gradlew assembleDebug --offline > ../gradle.log 2>&1 ) || { tail -20 gradle.log; exit 1; }
adb install -r android/app/build/outputs/apk/debug/app-debug.apk | tail -1
if [ "$1" = "--run" ]; then
  adb shell input keyevent KEYCODE_WAKEUP
  adb logcat -c
  adb shell am force-stop com.gothic.droid
  adb shell am start -n com.gothic.droid/com.opengothic.MainActivity > /dev/null
fi
