#!/bin/bash
# starts the game (the launcher starts it itself: --ez autostart true) and begins a new game
sleep 1
for i in 1 2 3 4 5 6; do
  adb shell dumpsys activity activities | grep -m1 topResumed | grep -q GameActivity && break
  sleep 2
done
sleep ${DELAY:-24}
adb shell input tap 120 846; sleep 1       # UP: "new game"
adb shell input tap 2268 908; sleep ${1:-50}
