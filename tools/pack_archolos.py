#!/usr/bin/env python3
"""Pack lang_archolos/ (tools/build_archolos.py, build_archolos_langs.py) for the web build, like tools/pack_langs.py.

    tools/pack_archolos.py <lang_archolos dir> <out dir> [--base <mirror folder url>]... [--only code,code]

Every gothic_<code>_<TV> folder gets a lang.json (voice = KM_Speech*.mod and, for a voiced language, its
cutscenes; text = the rest; cutscenes of a text-only language are subtitled, not packed) and packs
<out>/base, <out>/<code>/text, <out>/<code>/voice, plus <out>/languages.json (same layout as the other games).
"""
import argparse
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pack_langs import pack
from lang_manifest import NAMES

NAMES.update({'la': 'Español (Latinoamérica)', 'es': 'Español (España)', 'hu': 'Magyar', 'uk': 'Українська'})
ORDER = ['ru', 'en', 'pl', 'de', 'it', 'es', 'la', 'cs', 'hu', 'uk']


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src')
    ap.add_argument('dst')
    ap.add_argument('--base', action='append', default=[])
    ap.add_argument('--only', default='')
    a = ap.parse_args()
    mirror = lambda sub: [b.rstrip('/') + '/' + sub + '/' for b in a.base]
    only = set(filter(None, a.only.split(',')))

    langs = {}
    for name in os.listdir(a.src):
        m = re.fullmatch(r'gothic_([a-z]{2})_([01])([01])', name)
        if m:
            langs[m.group(1)] = (os.path.join(a.src, name), m.group(2) == '1', m.group(3) == '1')
    idx_path = os.path.join(a.dst, 'languages.json')
    index = json.load(open(idx_path, encoding='utf-8')) if os.path.exists(idx_path) else {'version': 1, 'base': None, 'languages': []}

    if not only or 'base' in only:
        man, size, line = pack(None, os.path.join(a.src, 'gothic_base'), os.path.join(a.dst, 'base'), mirror('base'), 'base')
        index['base'] = {'path': 'base/manifest.json', 'size': size, 'files': len(man['files'])}
        print('base:', line, flush=True)
    for code in sorted(langs, key=lambda c: ORDER.index(c) if c in ORDER else 99):
        if only and code not in only:
            continue
        folder, has_text, has_voice = langs[code]
        files = sorted((os.path.relpath(os.path.join(d, f), folder).replace(os.sep, '/')
                        for d, _, fs in os.walk(folder) for f in fs if f != 'lang.json'), key=str.lower)
        is_video = lambda r: r.lower().endswith('.bik')
        voice = [f for f in files if re.match(r'data/km_speech\d+\w*\.mod$', f.lower()) or (has_voice and is_video(f))]
        subtitled = [f for f in files if f not in voice and is_video(f)]
        text = [f for f in files if f not in voice and f not in subtitled]
        with open(os.path.join(folder, 'lang.json'), 'w', encoding='utf-8') as f:
            json.dump({'id': code, 'name': NAMES.get(code, code), 'hasText': has_text, 'hasVoice': has_voice,
                       'files': {'text': text, 'voice': voice, 'subtitledVideo': subtitled}}, f, ensure_ascii=False, indent=1)
        entry = {'id': code, 'name': NAMES.get(code, code), 'hasText': has_text, 'hasVoice': has_voice}
        for kind, fl in (('text', text), ('voice', voice)):
            if not fl:
                continue
            sub = f'{code}/{kind}'
            man, size, line = pack(fl, folder, os.path.join(a.dst, sub), mirror(sub), f'{kind}-{code}')
            entry[kind] = {'path': sub + '/manifest.json', 'size': size, 'files': len(man['files'])}
            print(f'{sub}:', line, flush=True)
        index['languages'] = [e for e in index['languages'] if e['id'] != code] + [entry]
    index['languages'].sort(key=lambda e: ORDER.index(e['id']) if e['id'] in ORDER else 99)
    with open(idx_path, 'w', encoding='utf-8') as f:
        json.dump(index, f, ensure_ascii=False, indent=1)
    print('languages.json written', flush=True)


main()
