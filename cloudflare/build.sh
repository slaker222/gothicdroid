#!/bin/bash
# Собирает cloudflare/public из dist-web (+ web/landing.html). Запуск: cloudflare/build.sh
# По умолчанию онлайн-установка включена; OFFLINE_ONLY=1 cloudflare/build.sh — только папка/ZIP.
set -e
cd "$(dirname "$0")"
ROOT=$(cd .. && pwd)
OUT=public
rm -rf "$OUT"; mkdir -p "$OUT"
if [ "${OFFLINE_ONLY:-0}" = "1" ]; then
  # только Select folder / Select ZIP (онлайн-установка отключена патчем)
  python3 patch_game.py "$ROOT/dist-web/game.html" "$OUT/game.html"
else
  # онлайн-установка с зеркал (Hugging Face): game.html как есть + release.json
  cp "$ROOT/dist-web/game.html" "$OUT/game.html"
  cp "$ROOT/dist-web/release.json" "$OUT/release.json"
fi
cp "$ROOT/web/landing.html" "$OUT/index.html"
cp "$ROOT"/dist-web/opengothic.{js,wasm,worker.js} "$OUT/"
cp -rL "$ROOT/dist-web/img" "$OUT/img"
cp _headers "$OUT/_headers"
# лимит Cloudflare — 25 МиБ на файл
find "$OUT" -type f -size +25M -print | while read f; do echo "СЛИШКОМ БОЛЬШОЙ ФАЙЛ: $f"; exit 1; done
du -sh "$OUT"; find "$OUT" -type f | sort
