#!/usr/bin/env python3
"""Archolos: the other languages (steam_raw_archolos/<code>, tools/steam_archolos_langs.sh) -> lang_archolos/gothic_<code>_<TV>.

    tools/build_archolos_langs.py <steam_raw_archolos dir> <lang_archolos dir> [<russian install dir>]

  text-only (en de it es la cs hu uk): the KM_*<CODE>.mod files (scripts, localization, fonts) and the font
                   textures of English; the cutscenes of a text-only language (burned-in subtitles) are not kept
  pl (voiced):     the same + KM_Speech1-6PL.mod (-> Opus 24k mono), IntroKm / OutroKm (-> AV1), and a patch
                   Textures_pl.vdf / Textures_Addon_pl.vdf with the pictures that differ from the Russian install
                   (needs <russian install dir>: the original Textures*.vdf)
Only the files the download kept are used, so the language folders hold what differs from the shared base.
"""
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import split_vdf as sv

raw, out = os.path.expanduser(sys.argv[1]), os.path.expanduser(sys.argv[2])
ru_install = os.path.expanduser(sys.argv[3]) if len(sys.argv) > 3 else None
TEXT = ('en', 'de', 'it', 'es', 'la', 'cs', 'hu', 'uk')
VOICED = ('pl',)
TMP = f'{out}/.tmp'
os.makedirs(TMP, exist_ok=True)


def run(*cmd):
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)


def put(src, dst):
    if os.path.exists(dst):
        return
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    low = os.path.basename(dst).lower()
    part = dst + '.part'
    if re.match(r'km_speech\d+', low):
        run(sys.executable, f'{HERE}/repack_audio.py', src, part, '--mono', '24k')
    elif low.endswith('.bik'):
        t = f'{TMP}/{code}'
        os.makedirs(t, exist_ok=True)
        if os.path.lexists(f'{t}/{low}'):
            os.remove(f'{t}/{low}')
        os.symlink(src, f'{t}/{os.path.basename(dst)}')
        run(f'{HERE}/repack_video.sh', t, t + '/out')
        shutil.move(f'{t}/out/{os.path.basename(dst)}', part)
    elif low.startswith('textures'):
        shutil.copy2(src, part)
        run(sys.executable, f'{HERE}/recode_vdf_png.py', part)
    else:
        shutil.copy2(src, part)
    os.replace(part, dst)
    print(f'  {dst[len(out):]}: {os.path.getsize(src)/1e6:.1f} MB -> {os.path.getsize(dst)/1e6:.1f} MB', flush=True)


def patch(src, base, dst):
    """a VDF with the files of src that differ from base"""
    if os.path.exists(dst):
        return
    hdr, ts = sv.read_vdf(src)
    _, tb = sv.read_vdf(base)
    old = {p: n.data for p, n in sv.walk(tb)}
    changed = {p for p, n in sv.walk(ts) if old.get(p) != n.data}
    names = {os.path.basename(p) for p in changed}
    tree = sv.filtered(ts, lambda n: n.name in names)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    sv.write_vdf(dst, hdr, tree)
    run(sys.executable, f'{HERE}/recode_vdf_png.py', dst)
    print(f'  {dst[len(out):]}: {len(changed)} changed files, {os.path.getsize(dst)/1e6:.1f} MB', flush=True)


for code in TEXT + VOICED:
    voiced = code in VOICED
    dest = f'{out}/gothic_{code}_{"11" if voiced else "10"}'
    print(f'{code}:', flush=True)
    for d, _, fs in os.walk(f'{raw}/{code}'):
        for n in sorted(fs):
            p = os.path.join(d, n)
            rel = os.path.relpath(p, f'{raw}/{code}').replace(os.sep, '/')
            low = rel.lower()
            if low.startswith('data/km_') and low.endswith('.mod'):
                put(p, f'{dest}/{rel}')
            elif low.startswith('data/textures_') and low.endswith('.vdf') and not low.startswith(('data/textures.', 'data/textures_addon.')):
                put(p, f'{dest}/{rel}')                      # English font textures
            elif voiced and low.startswith('_work/data/video/') and low.endswith('.bik'):
                put(p, f'{dest}/{rel}')
    if voiced and ru_install:
        for n in ('Textures', 'Textures_Addon'):
            patch(f'{raw}/{code}/Data/{n}.vdf', f'{ru_install}/Data/{n}.vdf', f'{dest}/Data/{n}_{code}.vdf')
shutil.rmtree(TMP, ignore_errors=True)
print('done')
