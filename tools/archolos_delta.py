#!/usr/bin/env python3
"""Archolos language downloads: hash an install, copy only what differs from a hashed reference.

    tools/archolos_delta.py hash <install dir> <out.json>
    tools/archolos_delta.py copy <install dir> <reference.json> <dst dir>

The Steam install is 7 GB and most of it is the same in every language, so a language copy keeps only the
files that are new or different from the reference (the Russian install, hashed before the language was changed).
"""
import hashlib
import json
import os
import shutil
import sys
from concurrent.futures import ThreadPoolExecutor

SKIP = ('saves', 'saves_thechroniclesofmyrtana', 'directx', 'gd3d11', '_work/tools')


def walk(root):
    for d, _, fs in os.walk(root):
        for n in fs:
            p = os.path.join(d, n)
            rel = os.path.relpath(p, root).replace(os.sep, '/')
            if not rel.lower().startswith(SKIP):
                yield rel, p


def md5(p):
    h = hashlib.md5()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(1 << 22), b''):
            h.update(b)
    return h.hexdigest()


def hashes(root):
    items = list(walk(root))
    with ThreadPoolExecutor(4) as ex:
        return {rel: [os.path.getsize(p), h] for (rel, p), h in zip(items, ex.map(lambda t: md5(t[1]), items))}


if sys.argv[1] == 'hash':
    json.dump(hashes(sys.argv[2]), open(sys.argv[3], 'w'))
else:
    ref = json.load(open(sys.argv[3]))
    n = tot = 0
    for rel, p in walk(sys.argv[2]):
        if rel in ref and ref[rel][0] == os.path.getsize(p) and ref[rel][1] == md5(p):
            continue
        d = os.path.join(sys.argv[4], rel)
        os.makedirs(os.path.dirname(d), exist_ok=True)
        shutil.copy2(p, d)
        n += 1
        tot += os.path.getsize(p)
    print(f'copied {n} files, {tot/1e6:.0f} MB')
