#!/bin/bash
# starts the game from the launcher of the phone (the chosen game) and begins a new game
sleep 4
for i in 1 2 3 4 5 6; do
  adb shell input tap 540 1910; sleep 4
  adb shell dumpsys activity activities | grep -m1 topResumed | grep -q GameActivity && break
done
sleep 24
adb shell input tap 120 846; sleep 1       # UP: "new game"
adb shell input tap 2268 908; sleep ${1:-50}
