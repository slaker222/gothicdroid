#!/usr/bin/env python3
"""Pages of the Android app, built from web/shell.html (the browser version's page):
  assets/ui.html        - the page over the running game (touch controls, settings menu)
  assets/launcher.html  - the main menu of the app: the game, its status, the same settings menu, start
"""
import os, re
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
src = open(os.path.join(root, 'web/shell.html'), encoding='utf-8').read()
out = os.path.join(root, 'android/app/src/main/assets')
os.makedirs(out, exist_ok=True)

# the page over the game
open(os.path.join(out, 'ui.html'), 'w', encoding='utf-8').write(src.replace('{{{ SCRIPT }}}', ''))

# the main menu
style = re.search(r'<style>(.*?)</style>', src, re.S).group(1)
panel = re.search(r'(<div id="settings-panel".*?<div class="set-note" id="settings-note">.*?</div>\s*</div>)', src, re.S).group(1)
presets = re.search(r'(const PRESETS = \{.*?\n      \};)', src, re.S).group(1)
# the donate window (Ko-fi, crypto addresses) is written once in shell.html, between these two marks
donate = re.search(r'<!--DONATE-BEGIN-->(.*?)<!--DONATE-END-->', src, re.S).group(1)
# the definitions of the touch buttons (positions, icons, names): the layout editor of the main menu uses the same ones
tdefs = re.search(r'// TOUCH-DEFS-BEGIN.*?\n(.*?)// TOUCH-DEFS-END', src, re.S).group(1)
tkeys = re.search(r'// TOUCH-KEYS-BEGIN[^\n]*\n(.*?)// TOUCH-KEYS-END', src, re.S).group(1)
ticons = re.search(r'// TOUCH-ICONS-BEGIN\n(.*?)// TOUCH-ICONS-END', src, re.S).group(1)
tpl = open(os.path.join(root, 'web/android_launcher.tpl.html'), encoding='utf-8').read()
page = tpl.replace('{{STYLE}}', style).replace('{{PANEL}}', panel).replace('{{PRESETS}}', presets).replace('{{DONATE}}', donate).replace('{{TOUCHDEFS}}', tdefs).replace('{{TOUCHICONS}}', ticons).replace('{{TOUCHKEYS}}', tkeys)
open(os.path.join(out, 'launcher.html'), 'w', encoding='utf-8').write(page)

# the layout editor of the touch buttons: one file for both pages
import shutil
shutil.copy(os.path.join(root, 'web/touch_editor.js'), os.path.join(out, 'touch_editor.js'))
