#!/bin/bash
# Downloads The Chronicles Of Myrtana: Archolos in other languages from the Steam client of this machine; only files that differ from the Russian install (steam_raw_archolos/ref_ru.json, tools/archolos_delta.py) are kept in steam_raw_archolos/<code>:
#
#   tools/steam_archolos_langs.sh "polish pl" "french fr" ...     (Steam language, short code)
#
# For each language: stops Steam, sets "language" in appmanifest_39510.acf, starts Steam (the AppImage, not
# "steam -silent": a silent Steam ignores steam://validate), repeats steam://validate/39510 until the update
# runs, waits for "Fully Installed" in logs/content_log.txt and copies the game folder. At the end the
# manifest of the first run (appmanifest_39510.acf.bak-orig, copy it before the first run) is put back.
# Steam languages of Archolos: english german polish italian spanish latam czech hungarian ukrainian russian.
A=1467450; APPS=~/.local/share/Steam/steamapps; RAW=~/Gothic_Dev/steam_raw_archolos; HERE=$(dirname "$(readlink -f "$0")")
LOG=~/.local/share/Steam/logs/content_log.txt; ST=~/.local/share/Steam/ubuntu12_32/steam
IMG=/home/alex/Downloads/Steam-1.0.0.87-3-anylinux-x86_64.AppImage
stopsteam(){ $ST -shutdown >/dev/null 2>&1; for i in $(seq 1 40); do pgrep -f 'ubuntu12_32/steam ' >/dev/null || break; sleep 1; done; sleep 8; }
for pair in "$@"; do
  set -- $pair; L=$1; S=$2
  [ -d $RAW/$S ] && { echo "skip $S"; continue; }
  pgrep -f 'ubuntu12_32/steam ' >/dev/null && stopsteam
  sed -i -E "s/(\"language\"\s+)\"[a-z]+\"/\1\"$L\"/" $APPS/appmanifest_$A.acf
  : > /dev/null
  T0=$(date +%s)
  (cd ~ && setsid nohup $IMG >/dev/null 2>&1 &)
  sleep 60
  until pgrep -f steamwebhelper >/dev/null; do sleep 5; done
  # retry validate until the download starts
  for i in $(seq 1 60); do
    $ST steam://validate/$A >/dev/null 2>&1
    sleep 40
    grep "AppID $A" $LOG | tail -40 | grep -q -E 'download [0-9]+/[0-9]+.*stage|Downloading|Update Started' && break
  done
  for i in $(seq 1 2880); do
    last=$(grep "AppID $A state changed" $LOG | tail -1 | tr -d "\r")
    echo "$last" | grep -q -E 'changed : Fully Installed,$' && [ "$(date -d "$(echo "$last" | cut -c2-20)" +%s)" -gt "$T0" ] && break
    sleep 5
  done
  sleep 5
  python3 -I $HERE/archolos_delta.py copy "$APPS/common/TheChroniclesOfMyrtana" $RAW/ref_ru.json $RAW/$S.tmp && mv $RAW/$S.tmp $RAW/$S
  echo "DONE $L $(du -sh $RAW/$S | cut -f1)"
done
stopsteam
cp $APPS/appmanifest_$A.acf.bak-orig $APPS/appmanifest_$A.acf
(cd ~ && setsid nohup $IMG >/dev/null 2>&1 &)
echo CHAIN_DONE
