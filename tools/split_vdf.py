#!/usr/bin/env python3
"""Split a Gothic VDF archive in two: what a language patch replaces, and the rest.

A language version overrides some files of the shared archives (menu, chapter and
loading screens, fonts ...) with its own copies in extra VDFs (textures_russian.vdf,
fonts.vdf ...). The originals stay inside the shared archive, so every language would
carry them. This moves every file whose name occurs in the --like archives (and the
.TGA source of a -C.TEX texture) into a second archive, keeping the directory tree.

    tools/split_vdf.py textures.VDF --like textures_russian.vdf --like fonts.vdf \\
        --rest textures_base.VDF --moved textures_moved.VDF

The result is checked: both archives together hold exactly the files of the source,
byte for byte, and the directory tree of each is read back.
"""
import argparse
import os
import struct
import sys

HEADER_SIZE = 296
ENTRY_SIZE = 80
DIR = 0x80000000
LAST = 0x40000000


class Node:
    def __init__(self, raw_name, attr, is_dir):
        self.raw_name = raw_name
        self.attr = attr
        self.is_dir = is_dir
        self.data = b''
        self.children = []

    @property
    def name(self):
        return self.raw_name.rstrip(b' \0').decode('latin1')


def read_vdf(path):
    with open(path, 'rb') as f:
        header = f.read(HEADER_SIZE)
        count, files, ts, size, root, version = struct.unpack('<6I', header[272:296])
        f.seek(root)
        raw = [struct.unpack('<64s4I', f.read(ENTRY_SIZE)) for _ in range(count)]

        def listing(start):
            nodes, i = [], start
            while True:
                name, off, sz, typ, attr = raw[i]
                n = Node(name, attr, bool(typ & DIR))
                if n.is_dir:
                    n.children = listing(off)
                else:
                    f.seek(off)
                    n.data = f.read(sz)
                nodes.append(n)
                if typ & LAST:
                    return nodes
                i += 1

        return header, listing(0)


def walk(nodes, prefix=''):
    for n in nodes:
        if n.is_dir:
            yield from walk(n.children, prefix + n.name + '/')
        else:
            yield prefix + n.name, n


def filtered(nodes, keep):
    out = []
    for n in nodes:
        if n.is_dir:
            c = filtered(n.children, keep)
            if c:
                d = Node(n.raw_name, n.attr, True)
                d.children = c
                out.append(d)
        elif keep(n):
            out.append(n)
    return out


def write_vdf(path, header, nodes):
    entries = []  # [node, off, last]

    def emit(level):
        start = len(entries)
        for n in level:
            entries.append([n, 0, False])
        entries[-1][2] = True
        for k, n in enumerate(level):
            if n.is_dir:
                entries[start + k][1] = len(entries)
                emit(n.children)

    emit(nodes)
    files = [e for e in entries if not e[0].is_dir]
    pos = HEADER_SIZE + len(entries) * ENTRY_SIZE
    data_start = pos
    for e in files:
        e[1] = pos
        pos += len(e[0].data)
    count, _, ts, _, root, version = struct.unpack('<6I', header[272:296])
    with open(path, 'wb') as f:
        f.write(header[:272])
        f.write(struct.pack('<6I', len(entries), len(files), ts, pos - data_start, HEADER_SIZE, version))
        for n, off, last in entries:
            if n.is_dir:
                f.write(struct.pack('<64s4I', n.raw_name, off, 0, DIR | (LAST if last else 0), n.attr))
            else:
                f.write(struct.pack('<64s4I', n.raw_name, off, len(n.data), LAST if last else 0, n.attr))
        for e in files:
            f.write(e[0].data)
    return len(entries), len(files)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('src')
    ap.add_argument('--like', action='append', required=True, help='archive whose file names are moved out (repeatable)')
    ap.add_argument('--rest', required=True, help='output: the shared rest')
    ap.add_argument('--moved', required=True, help='output: the files the language patches replace')
    args = ap.parse_args()

    header, tree = read_vdf(args.src)
    like = set()
    for p in args.like:
        for path, _ in walk(read_vdf(p)[1]):
            like.add(os.path.basename(path).upper())
    names = {os.path.basename(p).upper() for p, _ in walk(tree)}
    move = {n for n in like if n in names}
    # the .TGA source of a compiled texture is the same picture
    for n in list(move):
        if n.endswith('-C.TEX') and n[:-6] + '.TGA' in names:
            move.add(n[:-6] + '.TGA')

    def is_moved(n):
        return n.name.upper() in move

    rest = filtered(tree, lambda n: not is_moved(n))
    moved = filtered(tree, is_moved)
    n1 = write_vdf(args.rest, header, rest)
    n2 = write_vdf(args.moved, header, moved)

    # check: the two archives together are the source, byte for byte
    src = {p: n.data for p, n in walk(tree)}
    got = {}
    for out in (args.rest, args.moved):
        for p, n in walk(read_vdf(out)[1]):
            assert p not in got, 'duplicate ' + p
            got[p] = n.data
    assert got == src, 'content differs'
    print(f'{args.src}: {len(src)} files, {os.path.getsize(args.src)/1e6:.1f} MB')
    print(f'  rest : {n1[1]} files, {os.path.getsize(args.rest)/1e6:.2f} MB  -> {args.rest}')
    print(f'  moved: {n2[1]} files, {os.path.getsize(args.moved)/1e6:.2f} MB  -> {args.moved}')
    print('  check: both archives hold exactly the source files')


if __name__ == '__main__':
    sys.exit(main())
