#!/usr/bin/env python3
"""Pack the language folders for the web build (see tools/lang_manifest.py).

    tools/pack_langs.py <lang dir> <out dir> [--base <mirror folder url>]...

Makes, with tools/pack_web.py, one pack per piece the page can ask for:

    <out>/base/manifest.json               gothic_base: everything shared by all languages
    <out>/base_compact/manifest.json       gothic_base_compact (if present): the same, smaller, lossy pictures
    <out>/<code>/text/manifest.json        text of a language (lang.json files.text)
    <out>/<code>/voice/manifest.json       voice of a language (lang.json files.voice)
    <out>/languages.json                   index the page reads: names, sizes, pack paths

The text and the voice language are chosen independently on the page, so they are
separate packs. Mirror folders (--base) must keep the same layout under <out>.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))


def pack(files, src_root, dst, bases):
    """Pack files of src_root into dst: all of it (files is None), a list of paths, or
    a dict {path in the pack: file on disk}."""
    stage = None
    src = src_root
    if files is not None:
        # pack_web.py packs a folder: a tree of symlinks selects the files
        stage = tempfile.mkdtemp(prefix='stage_', dir=os.environ.get('PACK_TMP'))
        if not isinstance(files, dict):
            files = {rel: os.path.join(src_root, rel) for rel in files}
        for rel, path in files.items():
            os.makedirs(os.path.dirname(os.path.join(stage, rel)), exist_ok=True)
            os.symlink(path, os.path.join(stage, rel))
        src = stage
    cmd = [sys.executable, os.path.join(HERE, 'pack_web.py'), src, dst]
    for b in bases:
        cmd += ['--base', b]
    out = subprocess.run(cmd, check=True, capture_output=True, text=True).stdout
    if stage:
        shutil.rmtree(stage)
    with open(os.path.join(dst, 'manifest.json')) as f:
        man = json.load(f)
    return man, sum(c['size'] for c in man['chunks']), out.splitlines()[0]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('src')
    ap.add_argument('dst')
    ap.add_argument('--base', action='append', default=[])
    args = ap.parse_args()

    def mirror(sub):
        return [b.rstrip('/') + '/' + sub + '/' for b in args.base]

    index = {'version': 1, 'base': None, 'languages': []}
    man, size, line = pack(None, os.path.join(args.src, 'gothic_base'), os.path.join(args.dst, 'base'), mirror('base'))
    index['base'] = {'path': 'base/manifest.json', 'size': size, 'files': len(man['files'])}
    print('base:', line)
    # a smaller base with reduced-colour pictures (tools/compact_vdf.py); needs the palette+tRNS PNG fix
    compact = os.path.join(args.src, 'gothic_base_compact')
    if os.path.isdir(compact):
        man, size, line = pack(None, compact, os.path.join(args.dst, 'base_compact'), mirror('base_compact'))
        index['baseCompact'] = {'path': 'base_compact/manifest.json', 'size': size, 'files': len(man['files'])}
        print('base_compact:', line)

    # a language without voice-over only lists what differs from English: its text pack
    # is the English text with the language's own files laid over it
    def load(code):
        for name in os.listdir(args.src):
            if re.fullmatch(f'gothic_{code}_[01][01]', name):
                folder = os.path.join(args.src, name)
                with open(os.path.join(folder, 'lang.json'), encoding='utf-8') as f:
                    return folder, json.load(f)

    en_folder, en = load('en')
    for name in sorted(os.listdir(args.src)):
        m = re.fullmatch(r'gothic_([a-z]{2})_[01][01]', name)
        if not m:
            continue
        folder = os.path.join(args.src, name)
        with open(os.path.join(folder, 'lang.json'), encoding='utf-8') as f:
            lang = json.load(f)
        if not lang['hasVoice']:
            merged = {f.lower(): (f, os.path.join(en_folder, f)) for f in en['files']['text']}
            for f in lang['files']['text']:
                merged[f.lower()] = (merged.get(f.lower(), (f,))[0], os.path.join(folder, f))
            lang['files']['text'] = dict(merged.values())
        entry = {'id': lang['id'], 'name': lang['name'], 'hasText': lang['hasText'], 'hasVoice': lang['hasVoice']}
        for kind in ('text', 'voice'):
            files = lang['files'][kind]
            if not files:
                continue
            sub = f"{lang['id']}/{kind}"
            man, size, line = pack(files, folder, os.path.join(args.dst, sub), mirror(sub))
            entry[kind] = {'path': sub + '/manifest.json', 'size': size, 'files': len(man['files'])}
            print(f'{sub}: {line}')
        index['languages'].append(entry)

    with open(os.path.join(args.dst, 'languages.json'), 'w', encoding='utf-8') as f:
        json.dump(index, f, ensure_ascii=False, indent=1)
    total = index['base']['size'] + sum(e[k]['size'] for e in index['languages'] for k in ('text', 'voice') if k in e)
    print(f'languages.json written, {total/1e6:.0f} MB in all packs')


if __name__ == '__main__':
    main()
