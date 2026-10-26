#!/usr/bin/env bash
# Re-encode Gothic cutscenes (*.bik) to MP4 for the web build.
# Output files keep the original *.bik name (scripts reference them); the engine
# detects the MP4 payload and hands it to the browser's <video> element.
#
#   tools/repack_video.sh <src video dir> <dst video dir> [av1|h264]
#
# av1  (default): smallest; Chrome, Edge, Firefox, Android browsers
# h264          : ~2x larger; plays in every browser, including older Safari/iOS
set -euo pipefail

SRC="$1"
DST="$2"
CODEC="${3:-av1}"
mkdir -p "$DST"

case "$CODEC" in
  av1)  VIDEO=(-c:v libsvtav1 -preset 6 -crf 32 -g 250) ;;
  h264) VIDEO=(-c:v libx264 -preset slow -crf 23 -profile:v high) ;;
  *)    echo "unknown codec: $CODEC" >&2; exit 1 ;;
esac

for f in "$SRC"/*.[bB][iI][kK]; do
  out="$DST/$(basename "$f")"
  ffmpeg -v error -y -i "$f" "${VIDEO[@]}" -pix_fmt yuv420p \
    -c:a libopus -b:a 64k -map_metadata -1 -movflags +faststart -f mp4 "$out"
  printf '%-20s %6.1f MB -> %5.1f MB\n' "$(basename "$f")" \
    "$(echo "$(stat -c %s "$f")/1000000" | bc -l)" "$(echo "$(stat -c %s "$out")/1000000" | bc -l)"
done
