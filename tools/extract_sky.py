#!/usr/bin/env python3
"""Pull the sky textures of both games out of their Textures*.vdf into the app's assets (the "Sky" setting).

    tools/extract_sky.py --g1 <Gothic 1 folder> --g2 <Gothic 2 folder> [--out android/app/src/main/assets/sky]

The clouds (day / night, per world of Gothic 2), the sun and the moon go to <out>/g1 and <out>/g2 as the
compiled *-C.TEX files the engine reads. The files are game data: the folder is in .gitignore, run the
script before building the app.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import split_vdf as sv

WANT = ('SKYDAY_', 'SKYNIGHT_', 'UNSUN', 'MOON')


def vdfs(folder):
    data = next((os.path.join(folder, d) for d in os.listdir(folder) if d.lower() == 'data'), None)
    if data is None:
        sys.exit(f'{folder}: no Data folder')
    out = [os.path.join(data, f) for f in sorted(os.listdir(data))
           if f.lower().startswith('textures') and f.lower().endswith('.vdf')]
    # a later (patch) archive replaces the earlier ones: the order of the names is good enough for the sky
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--g1', required=True)
    ap.add_argument('--g2', required=True)
    ap.add_argument('--out', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..',
                                                  'android', 'app', 'src', 'main', 'assets', 'sky'))
    a = ap.parse_args()
    for tag, folder in (('g1', a.g1), ('g2', a.g2)):
        dst = os.path.join(a.out, tag)
        os.makedirs(dst, exist_ok=True)
        found = {}
        for f in vdfs(folder):
            _, tree = sv.read_vdf(f)
            for p, n in sv.walk(tree):
                name = p.split('/')[-1].upper()
                if name.endswith('-C.TEX') and name.startswith(WANT):
                    found[name] = n.data
        for name, data in found.items():
            with open(os.path.join(dst, name), 'wb') as fo:
                fo.write(data)
        print(f'{tag}: {len(found)} files, {sum(map(len, found.values())) // 1024} KB')


main()
