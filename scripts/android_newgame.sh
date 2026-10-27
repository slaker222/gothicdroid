#!/bin/bash
# starts the game from the launcher of the phone (the chosen game) and begins a new game
sleep 3; adb shell input tap 540 1476; sleep 28
adb shell input tap 120 846; sleep 1       # UP: "new game"
adb shell input tap 2268 908; sleep ${1:-50}
