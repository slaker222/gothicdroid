#!/bin/bash
# Замер FPS и времени проходов GPU на телефоне по adb. Одна и та же сцена каждый раз: начало новой игры (-nomenu).
#
#   DIR=/storage/emulated/0/GothicSmall scripts/android_perf.sh "имя|ini-ключи|debug-ключи" ...
#
#   имя          — подпись строки результата (и имя скриншота)
#   ini-ключи    — через пробел, меняют значения Gothic.ini поверх базового набора ниже, напр. "vidResIndex=0.000000"
#                  ("x=0" — ничего не менять)
#   debug-ключи  — через пробел, дописываются в debug.cfg, напр. "no_cmd_cull=1" (необязательно)
#
# Примеры:
#   scripts/android_perf.sh "res75|x=0" "res100|vidResIndex=0.000000" "без отсечения|x=0|no_cmd_cull=1"
#
# Переменные:
#   DIR        папка игры на телефоне (по умолчанию .../com.opengothic/files/Gothic1)
#   OUT        куда класть скриншоты (по умолчанию /tmp/og_perf)
#   ANDROID_SERIAL  какой телефон, если подключено несколько (adb devices)
#   TIMERS=1   включить таймеры проходов GPU (по умолчанию выключены: стоят ~2,7 FPS, без них нет строки GPU passes)
#   WARM, SAMPLE    прогрев и окно замера в секундах (20 и 30)
#
# Скрипт сам: делает копию Gothic.ini / debug.cfg владельца, в конце ЗАКРЫВАЕТ ИГРУ, удаляет .og_args и возвращает
# настройки обратно (так же при Ctrl+C).
#
# Вывод по каждой конфигурации:
#   [имя] GPU passes (avg of N): TOTAL=.., GBuffer=.., ...   — мс на проход (только где работают таймеры: Adreno да, Mali нет)
#   [имя] lost=0 n=.. fps=.. frame=..ms tick=.. draw=.. sub=.. pres=.. geo(draws,clusters,tris)=..
#       tick  — игровая логика (CPU), draw — запись команд кадра (CPU), pres — ожидание вывода (большое = упор в GPU)
#       lost  — сколько раз драйвер потерял устройство (должно быть 0)
set -u
D=${DIR:-/storage/emulated/0/Android/data/com.opengothic/files/Gothic1}
OUT=${OUT:-/tmp/og_perf}
WARM=${WARM:-20}
SAMPLE=${SAMPLE:-30}
mkdir -p "$OUT"
BAK=$(mktemp -d)

adb pull "$D/Gothic.ini" "$BAK/" >/dev/null 2>&1
adb pull "$D/debug.cfg"  "$BAK/" >/dev/null 2>&1

cleanup() {
  adb shell am force-stop com.opengothic >/dev/null 2>&1          # игру закрываем всегда
  adb shell "rm -f $D/.og_args" >/dev/null 2>&1
  # было у владельца - вернуть, не было - удалить то, что создал скрипт
  if [ -f "$BAK/Gothic.ini" ]; then adb push "$BAK/Gothic.ini" "$D/Gothic.ini" >/dev/null 2>&1; else adb shell "rm -f $D/Gothic.ini" >/dev/null 2>&1; fi
  if [ -f "$BAK/debug.cfg"  ]; then adb push "$BAK/debug.cfg"  "$D/debug.cfg"  >/dev/null 2>&1; else adb shell "rm -f $D/debug.cfg"  >/dev/null 2>&1; fi
  rm -rf "$BAK"
}
trap cleanup EXIT INT TERM

adb shell "echo '-nomenu -novideo' > $D/.og_args"

for spec in "$@"; do
  name=${spec%%|*}; rest=${spec#*|}; kvs=${rest%%|*}; dbg=""
  [[ "$rest" == *"|"* ]] && dbg=${rest#*|}
  adb shell am force-stop com.opengothic
  # базовый набор: лёгкий пресет «Ultra low», тени выключены, без ограничения FPS
  adb shell "cat > $D/Gothic.ini" <<EOF
[INTERNAL]
vidResIndex=1.000000
webSun=1
webFog=1
webShadows=0
webDrawDist=4
webWorldDistM=30
webWaterQuality=1
webWaterPhysics=0
webSkipFarAnim=1
webBlood=0
webTexQuality=1
webFpsLimit=0
[KEYS]
keyAction=0c022a00
[ENGINE]
zCloudShadowScale=0
zEnvMappingEnabled=0
EOF
  for kv in $kvs; do k=${kv%%=*}; adb shell "sed -i 's/^$k=.*/$kv/' $D/Gothic.ini"; done
  adb shell "printf 'safe_mode=0\nlog_pipelines=0\ngpu_timers=${TIMERS:-0}\n' > $D/debug.cfg"
  for kv in $dbg; do adb shell "echo $kv >> $D/debug.cfg"; done
  adb logcat -c
  adb shell am start -n com.opengothic/.GameActivity --es extra_game_dir "$D" >/dev/null

  # ждём мир (запись кадра >= 2 мс и есть геометрия); вступительные ролики пропускаем касанием (на MIUI касания
  # заблокированы без «Отладка по USB (настройки безопасности)» — тогда ролик придётся дождаться)
  for i in $(seq 1 120); do
    sleep 2
    d=$(adb logcat -d | grep 'PERF\] frame' | tail -1 | sed -n 's/.*draw=\([0-9]*\).*/\1/p')
    n=$(adb logcat -d | grep -c 'PERF_GEO\] DrawCalls=[1-9]')
    if [ "${n:-0}" -ge 1 ] && [ "${d:-0}" -ge 2 ]; then break; fi
    if [ "$i" -gt 6 ]; then adb shell input tap 1000 500 >/dev/null 2>&1; fi
  done
  sleep "$WARM"; adb logcat -c; sleep "$SAMPLE"
  adb logcat -d | grep -E "PERF\] frame|PERF_GEO|GPU_PASS" > "$OUT/raw.txt"
  adb exec-out screencap -p > "$OUT/shot_${name// /_}.png"
  lost=$(adb logcat -d | grep -c "device is lost")
  python3 -I - "$name" "$lost" "$OUT/raw.txt" <<'PY'
import re,sys
name,lost,f=sys.argv[1],sys.argv[2],sys.argv[3]
L=open(f).read().splitlines()
fr=[];geo=None
for l in L:
    m=re.search(r'frame=(\d+)ms \(tick=(\d+) wait=(\d+) draw=(\d+) sub=(\d+) gpu=(\d+) pres=(\d+)\) fps=([\d.]+)',l)
    if m: fr.append(tuple(float(x) for x in m.groups()))
    g=re.search(r'DrawCalls=(\d+).*Clusters=(\d+) Triangles=(\d+)',l)
    if g: geo=g.groups()
gp={}; cnt=0
for l in L:
    if 'GPU_PASS] ms per frame:' in l:
        cnt+=1
        for k,v in re.findall(r'([A-Za-z0-9#\- ]+?)=([\d.]+)',l.split('ms per frame:')[1]):
            gp.setdefault(k.strip(),[]).append(float(v))
if gp:
    tops=sorted(((sum(v)/cnt,k) for k,v in gp.items()),reverse=True)[:12]
    print(f"[{name}] GPU passes (avg of {cnt}): "+", ".join(f"{k}={a:.2f}" for a,k in tops))
if not fr:
    print(f"[{name}] no frames, lost={lost}"); sys.exit()
av=lambda i:sum(x[i] for x in fr)/len(fr)
print(f"[{name}] lost={lost} n={len(fr)} fps={av(7):.1f} frame={av(0):.1f}ms tick={av(1):.1f} wait={av(2):.1f} draw={av(3):.1f} sub={av(4):.1f} pres={av(6):.1f} geo(draws,clusters,tris)={geo}")
PY
done
