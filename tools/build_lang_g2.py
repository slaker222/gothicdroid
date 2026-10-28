#!/usr/bin/env python3
"""Split the Gothic II Steam installs (steam_raw/g2_<code>) into lang_g2/, like lang/ of Gothic 1.

    tools/build_lang_g2.py <steam_raw dir> <out dir> base|<code>|all

  gothic_base          everything that does not depend on the language (taken from g2_en);
                       sound VDFs -> Ogg Opus, cutscenes -> AV1 (tools/repack_audio.py, repack_video.sh)
  gothic_<code>_11     voiced language (en de pl ru): the files that differ between the voiced
                       installs or exist in some only; speech -> Opus 24k mono, cutscenes -> AV1.
                       A textures*/sounds* VDF that differs from the English one becomes a patch
                       <name>_<code>.vdf with the changed files only (loaded after the original)
  gothic_<code>_10     text-only language (fr it es): the files that differ from the English
                       install (their speech is the English one), cutscenes -> AV1

Only the game data is split: Data/, the compiled scripts, the dialogue lines (ou.*) and the
cutscenes. Executables, system/ settings, saves, Spine, DirectX and the tools are not copied.
Run tools/lang_manifest.py on the result to write lang.json into every language folder.
"""
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import split_vdf as sv

raw, out, what = os.path.expanduser(sys.argv[1]), os.path.expanduser(sys.argv[2]), sys.argv[3]
VOICED = ('en', 'de', 'pl', 'ru')
TEXT = ('fr', 'it', 'es')
TMP = os.path.join(out, '.tmp')

# base also gets these from the English install (small, shared)
BASE_ROOTS = ('data/', 'miles/', '_work/data/anims/', '_work/data/music/', '_work/data/presets/',
              '_work/data/video/', '_work/data/textures/', '_work/data/meshes/', 'system/',
              '_work/data/scripts/_compiled/', '_work/data/scripts/content/cutscene/')
NOT_COPIED = ('data/steam', 'data/modvdf/', 'system/gothic.ini', 'system/g2_classic.ini', 'system/gothicgame.rtf')
NOT_COPIED_EXT = ('.exe', '.dll', '.rtf', '.ico', '.bak', '.scc', '.pdf', '.htm', '.txt')
PATCHED = ('data/textures', 'data/sounds')  # a differing VDF of these becomes a patch


def in_scope(low):
    """game data that can differ between languages"""
    return (low.startswith('data/') and not low.startswith('data/steam')) \
        or low.startswith('_work/data/scripts/_compiled/') \
        or low.startswith('_work/data/scripts/content/cutscene/') \
        or low.startswith('_work/data/video/')


def md5(p):
    h = hashlib.md5()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


def files(code):
    """lower-case path -> real relative path"""
    root = f'{raw}/g2_{code}'
    res = {}
    for d, _, fs in os.walk(root):
        for n in fs:
            rel = os.path.relpath(os.path.join(d, n), root)
            low = rel.lower().replace(os.sep, '/')
            if low.endswith('.scc'):
                continue
            res[low] = rel
    return res


F = {}
HASH = {}


def fl(code):
    if code not in F:
        F[code] = files(code)
    return F[code]


def h(code, low):
    k = (code, low)
    if k not in HASH:
        HASH[k] = md5(f'{raw}/g2_{code}/{fl(code)[low]}')
    return HASH[k]


def hashes(codes, lows):
    todo = [(c, l) for c in codes for l in lows if l in fl(c) and (c, l) not in HASH]
    with ThreadPoolExecutor(4) as ex:
        for (c, l), v in zip(todo, ex.map(lambda t: md5(f'{raw}/g2_{t[0]}/{fl(t[0])[t[1]]}'), todo)):
            HASH[(c, l)] = v


def video(src, dst):
    tmp = tempfile.mkdtemp(dir=TMP)
    os.symlink(src, f'{tmp}/{os.path.basename(dst)}')
    subprocess.run([f'{HERE}/repack_video.sh', tmp, tmp + '/out'], check=True, capture_output=True)
    shutil.move(f'{tmp}/out/{os.path.basename(dst)}', dst)
    shutil.rmtree(tmp)


def put(src, dst, voiced_pack=True, sound=False):
    """copy src to dst, recoding what the web build recodes"""
    low = os.path.basename(dst).lower()
    rel = dst[len(out):].lower()
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    if os.path.exists(dst) and os.path.getsize(dst) > 0:
        print(f'  {rel}: already done', flush=True)
        return
    sz = os.path.getsize(src)
    part = dst + '.part'
    if low.endswith('.vdf') and low.startswith('speech') and sz > 5_000_000:
        subprocess.run([sys.executable, f'{HERE}/repack_audio.py', src, part, '--mono', '24k'], check=True)
        os.replace(part, dst)
    elif low.endswith('.vdf') and low.startswith('sounds') and sz > 5_000_000:
        subprocess.run([sys.executable, f'{HERE}/repack_audio.py', src, part, '--mono', '48k', '--stereo', '64k'], check=True)
        os.replace(part, dst)
    elif low.endswith('.bik'):
        video(src, dst)
    else:
        shutil.copy2(src, dst)
    print(f'  {rel}: {sz/1e6:.1f} MB -> {os.path.getsize(dst)/1e6:.1f} MB', flush=True)


def patch(src, base_src, dst):
    """a VDF with the files of src that differ from base_src"""
    if os.path.exists(dst):
        print(f'  {dst[len(out):]}: already done', flush=True)
        return
    hdr, ts = sv.read_vdf(src)
    _, tb = sv.read_vdf(base_src)
    old = {p: n.data for p, n in sv.walk(tb)}
    changed = {p for p, n in sv.walk(ts) if old.get(p) != n.data}
    names = {os.path.basename(p) for p in changed}
    tree = sv.filtered(ts, lambda n: n.name in names)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    sv.write_vdf(dst, hdr, tree)
    print(f'  {dst[len(out):]}: {len(changed)} changed files, {os.path.getsize(dst)/1e6:.1f} MB', flush=True)


def lang_set():
    """paths (lower case) that belong to the voiced languages, not to base"""
    scope = set()
    for c in VOICED:
        scope |= {l for l in fl(c) if in_scope(l)}
    hashes(VOICED, scope)
    res = set()
    for l in scope:
        present = [c for c in VOICED if l in fl(c)]
        if l.startswith(PATCHED) and l.endswith('.vdf') and len(present) == len(VOICED):
            continue  # in every language: patched per language, the English file stays in base
        if len(present) < len(VOICED) or len({HASH[(c, l)] for c in present}) > 1:
            res.add(l)
    return res


def build_base():
    L = lang_set()
    dest = f'{out}/gothic_base'
    n = 0
    for low, rel in sorted(fl('en').items()):
        if not low.startswith(BASE_ROOTS) or low in L:
            continue
        # of the scripts only the compiled ones (the shared MUSIC.DAT ...) are game data; sources (*.d) are not
        if low.startswith('_work/data/scripts/') and not in_scope(low):
            continue
        if low.startswith(NOT_COPIED) or low.endswith(NOT_COPIED_EXT):
            continue
        put(f'{raw}/g2_en/{rel}', f'{dest}/{rel}')
        n += 1
    print(f'base: {n} files', flush=True)


def build_voiced(code):
    L = lang_set()
    dest = f'{out}/gothic_{code}_11'
    mine = [l for l in sorted(L) if l in fl(code)]
    print(f'{code}: {len(mine)} language files', flush=True)
    for low in mine:
        rel = fl(code)[low]
        put(f'{raw}/g2_{code}/{rel}', f'{dest}/{rel}')
    # patches: textures / sounds that differ from the English archive
    hashes(('en', code), [l for l in fl('en') if l.startswith(PATCHED) and l.endswith('.vdf')])
    if code != 'en':
        for low in sorted(fl('en')):
            if low.startswith(PATCHED) and low.endswith('.vdf') and low in fl(code) and h(code, low) != h('en', low):
                rel = fl(code)[low]
                name = os.path.splitext(os.path.basename(rel))[0]
                patch(f'{raw}/g2_{code}/{rel}', f'{raw}/g2_en/{fl("en")[low]}',
                      f'{dest}/{os.path.dirname(rel)}/{name}_{code}.vdf')


def build_text(code):
    dest = f'{out}/gothic_{code}_10'
    scope = [l for l in sorted(fl(code)) if in_scope(l)]
    hashes(('en', code), scope)
    mine = [l for l in scope if l not in fl('en') or h(code, l) != h('en', l)]
    print(f'{code}: {len(mine)} language files', flush=True)
    for low in mine:
        rel = fl(code)[low]
        put(f'{raw}/g2_{code}/{rel}', f'{dest}/{rel}')


os.makedirs(TMP, exist_ok=True)
jobs = [what] if what != 'all' else ['base', *VOICED, *TEXT]
for j in jobs:
    if j == 'base':
        build_base()
    elif j in VOICED:
        build_voiced(j)
    elif j in TEXT:
        build_text(j)
    else:
        sys.exit(f'unknown: {j}')
shutil.rmtree(TMP, ignore_errors=True)
print('DONE', what, flush=True)
