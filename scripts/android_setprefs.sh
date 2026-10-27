#!/bin/bash
# launcher settings of the app on the phone: android_setprefs.sh GAME_DIR RENDER_SCALE SIMPLE(true|false) [DRAW_DIST 0..4] [WORLD_DIST m]
adb shell am force-stop com.opengothic
adb shell "run-as com.opengothic sh -c 'mkdir -p shared_prefs; cat > shared_prefs/opengothic_launcher.xml'" <<X
<?xml version='1.0' encoding='utf-8' standalone='yes' ?>
<map>
    <string name="game_dir">${1:-/storage/emulated/0/GothicSmall}</string>
    <int name="render_scale" value="${2:-100}" />
    <boolean name="simple_render" value="${3:-true}" />
    <int name="draw_dist" value="${4:-4}" />
    <int name="world_dist" value="${5:-150}" />
</map>
X
