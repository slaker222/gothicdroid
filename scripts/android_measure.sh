#!/bin/bash
# measure on the phone: android_measure.sh RENDER_SCALE SIMPLE(true|false) [wait s] [GAME_DIR]
# sets the launcher settings, starts a new game, prints the gpu time per pass and the fps
D="$(cd "$(dirname "$0")" && pwd)"
$D/android_setprefs.sh "${4:-/storage/emulated/0/GothicSmall}" "$1" "$2"
adb logcat -c
adb shell input keyevent KEYCODE_WAKEUP
adb shell am start -n com.opengothic/.MainActivity > /dev/null
$D/android_newgame.sh "${3:-70}"
echo "== scale=$1 simple=$2 $(adb shell cat ${4:-/storage/emulated/0/GothicSmall}/debug.cfg | tr '\n' ' ' | grep -o 'render_scale=[0-9]* \|simple_render=[01]' | tr '\n' ' ')"
adb logcat -d -s OpenGothic:I | grep "RENDERER_SETTINGS" | tail -1 | grep -o "internalRes=[0-9x]*"
adb logcat -d -s OpenGothic:I | grep "GPU_PASS" | tail -1 | cut -c34-600
adb logcat -d -s OpenGothic:I | grep "\[PERF\]" | tail -3 | cut -c34-200
