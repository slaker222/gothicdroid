#!/usr/bin/env python3
"""Отключает в game.html онлайн-загрузку игры (ВРЕМЕННО): остаются только
«Select folder» и «Select ZIP». Код не удаляется, а закомментирован.
Вернуть онлайн: в build.sh вместо patch_game.py просто скопировать dist-web/game.html.
(game.html в dist-web минифицирован, поэтому патч работает по минифицированному тексту.)"""
import re, sys

src, dst = sys.argv[1], sys.argv[2]
s = open(src, encoding='utf-8').read()

# 1) ссылки на архивы закомментированы, массив пустой
m = re.search(r'ARCHIVE_URLS=\[("https?://[^\]]*)\]', s)
assert m, 'ARCHIVE_URLS not found'
s = s.replace(m.group(0), 'ARCHIVE_URLS=[/*ONLINE DOWNLOAD DISABLED (cloudflare build): ' + m.group(1).replace('*/', '* /') + '*/]')

# 2) downloadGame(): вместо скачивания — подсказка выбрать папку / ZIP
head = 'async function downloadGame(){'
assert s.count(head) == 1, 'downloadGame not found'
s = s.replace(head, head +
    '/*ONLINE DOWNLOAD DISABLED (cloudflare build); the original body follows*/'
    'return progressContainer.style.display="none",statusText.style.color="",'
    'setStatus("Choose the game folder or ZIP: Select folder / Select ZIP in the menu of the folder button (bottom left)."),'
    'void(window.ogOpenFilesMenu&&window.ogOpenFilesMenu());')
open(dst, 'w', encoding='utf-8').write(s)
