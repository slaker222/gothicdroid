#!/bin/bash
# The Android app shows pages of the browser version: web/shell.html over the game (WebView) and the main menu
# built from the same page (web/android_launcher.tpl.html). The page notices the app by the AndroidBridge object (NATIVE in shell.html).
exec python3 "$(dirname "$0")/make_android_ui.py"
