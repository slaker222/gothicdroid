#!/usr/bin/env python3
"""Pack a game folder into chunks for the web build.

Every chunk is self-contained: it holds whole "pieces" (up to 4 MB of one
file, deflate-raw or stored), so the browser can unpack a chunk the moment it
arrives, while other chunks are still downloading. manifest.json lists the
chunks (size + sha256), the files, and where each piece of a file lives.

Files are laid out by priority: everything the game needs to start comes
first ("core"), speech and the remaining cutscenes follow; the shell may start
the game once the first `coreChunks` chunks are installed.

    tools/pack_web.py <game dir> <out dir> [--chunk-mb 30] [--name base] [--base <mirror folder url>]...

--base lists folders that hold a copy of the pack (chunks under the same
names); the shell falls back to them when a download fails.
"""
import argparse
import hashlib
import json
import os
import zlib
from concurrent.futures import ProcessPoolExecutor

PIECE = 4 * 1024 * 1024

# lower rank = earlier in the download; core = rank < LATE
LATE = 100
SKIP = ('saves/', 'saves_', 'launcher/', 'directx')


def rank(rel):
    r = rel.lower()
    if '/video/' in r:
        return 10 if os.path.basename(r) in ('intro.bik', 'logo1.bik') else LATE + 10
    if r.startswith('data/speech'):
        return LATE
    if r.startswith('data/textures') or r.startswith('data/fonts'):
        return 3
    if r.startswith('data/'):
        return 4
    if '/music/' in r:
        return 5
    return 1  # scripts, system, presets, small stuff


def compress(job):
    path, at, size = job
    with open(path, 'rb') as f:
        f.seek(at)
        raw = f.read(size)
    c = zlib.compressobj(9, zlib.DEFLATED, -15, 9)
    comp = c.compress(raw) + c.flush()
    # already compressed payloads (opus, mp4) do not shrink: store them
    if len(comp) >= len(raw) * 0.95:
        return 'store', raw, zlib.crc32(raw)
    return 'deflate-raw', comp, zlib.crc32(raw)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('src')
    ap.add_argument('dst')
    ap.add_argument('--chunk-mb', type=float, default=30)
    ap.add_argument('--name', default='', help='readable chunk names: <name>-01.bin (default: c000-<hash>.bin)')
    ap.add_argument('--base', action='append', default=[], help='mirror folder url (repeatable)')
    args = ap.parse_args()
    limit = int(args.chunk_mb * 1000 * 1000)

    files = []
    for dirpath, _, names in os.walk(args.src):
        for n in names:
            full = os.path.join(dirpath, n)
            rel = os.path.relpath(full, args.src).replace(os.sep, '/')
            if rel.lower().startswith(SKIP):
                continue
            files.append((rank(rel), rel, full, os.path.getsize(full)))
    files.sort(key=lambda f: (f[0], f[1].lower()))

    jobs, owners = [], []
    for fi, (_, rel, full, size) in enumerate(files):
        for at in range(0, max(size, 1), PIECE):
            jobs.append((full, at, min(PIECE, size - at)))
            owners.append((fi, at))

    os.makedirs(args.dst, exist_ok=True)
    chunks, pieces = [], []
    cur, cur_core = bytearray(), True
    core_chunks = None

    def flush():
        nonlocal cur
        if not cur:
            return
        digest = hashlib.sha256(cur).hexdigest()
        name = f'{args.name}-{len(chunks) + 1:02d}.bin' if args.name else f'c{len(chunks):03d}-{digest[:10]}.bin'
        with open(os.path.join(args.dst, name), 'wb') as f:
            f.write(cur)
        chunks.append({'name': name, 'size': len(cur), 'sha256': digest})
        cur = bytearray()

    with ProcessPoolExecutor() as pool:
        for (fi, at), job, (method, data, crc) in zip(owners, jobs, pool.map(compress, jobs, chunksize=4)):
            core = files[fi][0] < LATE
            # core and late data never share a chunk, so "core installed" is a chunk count
            if (cur and len(cur) + len(data) > limit) or (cur and cur_core and not core):
                flush()
            if not core and core_chunks is None:
                core_chunks = len(chunks)
            cur_core = core
            pieces.append({'file': fi, 'at': at, 'raw': job[2], 'chunk': len(chunks),
                           'off': len(cur), 'len': len(data), 'method': method, 'crc': crc})
            cur += data
        flush()

    manifest = {
        'version': 1,
        'coreChunks': core_chunks if core_chunks is not None else len(chunks),
        'bases': [b if b.endswith('/') else b + '/' for b in args.base],
        'chunks': chunks,
        'files': [{'path': rel, 'size': size} for _, rel, _, size in files],
        'pieces': pieces,
    }
    with open(os.path.join(args.dst, 'manifest.json'), 'w') as f:
        json.dump(manifest, f, separators=(',', ':'))

    raw_total = sum(f[3] for f in files)
    dl_total = sum(c['size'] for c in chunks)
    core_dl = sum(c['size'] for c in chunks[:manifest['coreChunks']])
    print(f'{len(files)} files, {raw_total/1e6:.1f} MB -> {len(chunks)} chunks, {dl_total/1e6:.1f} MB to download')
    print(f'core (game can start): {manifest["coreChunks"]} chunks, {core_dl/1e6:.1f} MB')
    for i, c in enumerate(chunks):
        print(f'  {c["name"]}  {c["size"]/1e6:5.1f} MB' + ('   <- core ends' if i + 1 == manifest['coreChunks'] else ''))


if __name__ == '__main__':
    main()
