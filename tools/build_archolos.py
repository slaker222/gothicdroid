#!/usr/bin/env python3
"""Split the Steam install of The Chronicles Of Myrtana: Archolos (Russian) into lang_archolos/, like lang_g2/.

    tools/build_archolos.py <install dir> <out dir>

  gothic_base      everything that does not depend on the language: Data/*.vdf and KM_*.mod (anims, meshes,
                   textures, worlds, music, sounds), _work/Data (anims, music, presets, FishFood.bik), Miles, system.
                   Sound archives -> Ogg Opus (mono 48k / stereo 64k), *.TGA pictures -> PNG (lossless).
  gothic_ru_11     Russian: KM_Speech1-6RU.mod (voice -> Opus mono 24k), KM_ScriptsRU / KM_LocalizationRU /
                   KM_FontsRU / KM_SoundRU (text), cutscenes IntroKm / OutroKm / CreditsKm (voiced -> AV1+Opus .bik)

Empty placeholders of the Steam install (0-byte files, 376-byte stub VDFs) are copied as they are.
Run tools/archolos_manifest.py on the result, then pack with tools/pack_archolos.py. Restartable: finished files are skipped.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
src, out = os.path.expanduser(sys.argv[1]), os.path.expanduser(sys.argv[2])
BASE, RU = f'{out}/gothic_base', f'{out}/gothic_ru_11'
TMP = f'{out}/.tmp'
os.makedirs(TMP, exist_ok=True)

LANG = re.compile(r'^km_(speech\d+|scripts|localization|fonts|sound)ru\.mod$')
SKIP_ROOT = ('saves', 'saves_thechroniclesofmyrtana', 'directx', 'gd3d11', '_work/tools')
SKIP_EXT = ('.exe', '.dll', '.txt', '.zip', '.license', '.rtf', '.ico', '.bak', '.log', '.rpt', '.vdf.cfg')
SKIP_SYSTEM = ('gothic.ini', 'log.txt', 'gothic_errorlog.txt')
VIDEO_RU = ('introkm.bik', 'outrokm.bik', 'creditskm.bik')


def log(rel, a, b):
    print(f'  {rel}: {a/1e6:.1f} MB -> {b/1e6:.1f} MB', flush=True)


def run(*cmd):
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)


def put(path, rel, dest):
    d = f'{dest}/{rel}'
    low = os.path.basename(rel).lower()
    if os.path.exists(d) and not os.path.exists(d + '.part'):
        print(f'  {rel}: done', flush=True)
        return
    os.makedirs(os.path.dirname(d), exist_ok=True)
    sz = os.path.getsize(path)
    part = d + '.part'
    if low.endswith(('.vdf', '.mod')) and sz > 5_000_000:
        if re.match(r'km_speech\d+ru', low):
            run(sys.executable, f'{HERE}/repack_audio.py', path, part, '--mono', '24k')
        elif low.startswith(('sounds', 'km_sound')):
            run(sys.executable, f'{HERE}/repack_audio.py', path, part, '--mono', '48k', '--stereo', '64k')
        elif low.startswith('textures'):
            shutil.copy2(path, part)
            run(sys.executable, f'{HERE}/recode_vdf_png.py', part)
        else:
            shutil.copy2(path, part)
    elif low.endswith('.bik') and sz > 0:
        t = tempfile.mkdtemp(dir=TMP)
        os.symlink(path, f'{t}/{os.path.basename(rel)}')
        run(f'{HERE}/repack_video.sh', t, t + '/out')
        shutil.move(f'{t}/out/{os.path.basename(rel)}', part)
        shutil.rmtree(t)
    else:
        shutil.copy2(path, part)
    os.replace(part, d)
    log(rel, sz, os.path.getsize(d))


def main():
    base, ru = [], []
    for d, _, fs in os.walk(src):
        for n in fs:
            path = os.path.join(d, n)
            rel = os.path.relpath(path, src)
            low = rel.lower().replace(os.sep, '/')
            if low.startswith(SKIP_ROOT) or low.endswith(SKIP_EXT) or '/' not in low:
                continue
            if low.startswith('system/') and (low.count('/') > 1 or low[7:] in SKIP_SYSTEM
                                              or low.endswith(('.dll', '.exe'))):
                continue
            if LANG.match(os.path.basename(low)) or low.replace('_work/data/video/', '') in VIDEO_RU:
                ru.append((path, rel))
            else:
                base.append((path, rel))
    for what, dest, items in (('base', BASE, base), ('ru', RU, ru)):
        print(f'{what}: {len(items)} files', flush=True)
        for path, rel in sorted(items, key=lambda t: (os.path.getsize(t[0]), t[1])):
            put(path, rel, dest)
    shutil.rmtree(TMP, ignore_errors=True)
    print('done', flush=True)


main()
