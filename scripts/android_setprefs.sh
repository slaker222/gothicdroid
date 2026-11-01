#!/bin/bash
# launcher settings of the app on the phone: android_setprefs.sh GAME_DIR RENDER_SCALE SIMPLE(true|false) [DRAW_DIST 0..4] [WORLD_DIST m] [SHADOWS 0..3] [SSAO true|false] [REFLECTIONS true|false]
adb shell am force-stop com.gothic.droid
adb shell "run-as com.gothic.droid sh -c 'mkdir -p shared_prefs; cat > shared_prefs/opengothic_launcher.xml'" <<X
<?xml version='1.0' encoding='utf-8' standalone='yes' ?>
<map>
    <string name="game_dir">${1:-/storage/emulated/0/GothicSmall}</string>
    <int name="render_scale" value="${2:-100}" />
    <boolean name="simple_render" value="${3:-true}" />
    <int name="draw_dist" value="${4:-4}" />
    <int name="world_dist" value="${5:-150}" />
    <int name="shadows" value="${6:-1}" />
    <boolean name="ssao" value="${7:-false}" />
    <boolean name="reflections" value="${8:-false}" />
</map>
X
