#!/usr/bin/env python3
"""Write release.json: one file that lists every mirror and a link to every archive.

    tools/make_release.py <archives dir> -o release.json \
        --mirror m1 "Mirror 1" https://mirror1.example/gothic/ \
        --mirror m2 "Mirror 2" https://huggingface.co/.../resolve/main/ \
        [--map m2 links-m2.json]

<archives dir> holds gothic1/ and gothic2/ made by tools/pack_langs.py (only archives and manifest.json files:
that is all a mirror needs; keep languages.json out of it and pass the folder with --meta). A mirror is a
base link: the archive gothic1/ru/voice/voice-ru-01.bin is expected at <base>gothic1/ru/voice/voice-ru-01.bin.
A mirror whose links can not be derived (a host with random per-file links) gets --map:
a JSON {"gothic1/base/base-01.bin": "https://...", ...} (also manifest.json files);
paths missing from the map fall back to the base link when the mirror has one, else the
mirror has no link for that file.

Mirrors are listed in order of preference: the page tries the first one and moves on to
the next when it fails (the mirror that failed is tried last by the following requests).

release.json layout (links of a file: one per mirror, in the order of "mirrors"):
    {"format": "gothic-release", "version": 1,
     "mirrors": [{"id": "m1", "name": "Mirror 1"}, ...],
     "games": {"g1": {"base":      PACK,
                      "languages": [{"id": "ru", "name": "...", "hasText": true, "hasVoice": true,
                                     "text": PACK, "voice": PACK}, ...]}, "g2": ...}}
    PACK = {"size": bytes, "files": n, "manifest": [url per mirror], "chunks": [[url per mirror], ...]}
"""
import argparse
import json
import os
import sys

GAMES = {'gothic1': 'g1', 'gothic2': 'g2'}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('archives')
    ap.add_argument('-o', '--out', default='release.json')
    ap.add_argument('--mirror', nargs=3, action='append', metavar=('ID', 'NAME', 'BASE'), required=True)
    ap.add_argument('--meta', help='folder with <game>/languages.json (made by pack_langs.py); default: the archives folder itself')
    ap.add_argument('--map', nargs=2, action='append', default=[], metavar=('ID', 'FILE'))
    args = ap.parse_args()

    mirrors = [{'id': i, 'name': n} for i, n, _ in args.mirror]
    bases = {i: (b if b.endswith('/') else b + '/') if b else '' for i, _, b in args.mirror}
    maps = {}
    for i, f in args.map:
        with open(f, encoding='utf-8') as fh:
            maps[i] = json.load(fh)
    for i in maps:
        if i not in bases:
            sys.exit('--map %s: no such mirror' % i)

    def links(rel):
        out = []
        for m in mirrors:
            i = m['id']
            url = maps.get(i, {}).get(rel) or (bases[i] + rel if bases[i] else None)
            out.append(url)
        if not any(out):
            sys.exit('no mirror has a link for ' + rel)
        return out

    def pack(game_dir, info):
        sub = os.path.dirname(info['path'])
        with open(os.path.join(args.archives, game_dir, info['path']), encoding='utf-8') as f:
            man = json.load(f)
        rel = lambda name: '/'.join(p for p in (game_dir, sub, name) if p)
        return {'size': info['size'], 'files': info['files'],
                'manifest': links(rel('manifest.json')),
                'chunks': [links(rel(c['name'])) for c in man['chunks']]}

    games = {}
    for game_dir, gid in GAMES.items():
        idx_path = os.path.join(args.meta or args.archives, game_dir, 'languages.json')
        if not os.path.exists(idx_path):
            continue
        with open(idx_path, encoding='utf-8') as f:
            idx = json.load(f)
        langs = []
        for l in idx['languages']:
            e = {'id': l['id'], 'name': l['name'], 'hasText': l['hasText'], 'hasVoice': l['hasVoice']}
            for kind in ('text', 'voice'):
                if kind in l:
                    e[kind] = pack(game_dir, l[kind])
            langs.append(e)
        games[gid] = {'base': pack(game_dir, idx['base']), 'languages': langs}
        n = sum(len(p['chunks']) for p in [games[gid]['base']] + [l[k] for l in langs for k in ('text', 'voice') if k in l])
        print(f'{gid}: {len(langs)} languages, {n} archives')

    out = {'format': 'gothic-release', 'version': 1, 'mirrors': mirrors, 'games': games}
    with open(args.out, 'w', encoding='utf-8') as f:
        json.dump(out, f, ensure_ascii=False, indent=1)
    print(f'{args.out}: {os.path.getsize(args.out) / 1024:.0f} KB, {len(mirrors)} mirrors')


if __name__ == '__main__':
    main()
