#!/usr/bin/env python3
"""Repack a Gothic VDF archive, re-encoding every *.WAV inside it to Ogg Opus.

File names stay the same (scripts reference them as *.WAV); the engine detects
the Opus payload by its magic (game/sound/opussound.cpp). Files that ffmpeg
fails to convert, or that would not get smaller, are copied unchanged.

    tools/repack_audio.py speech.vdf speech_opus.vdf --mono 24k
    tools/repack_audio.py sound.VDF  sound_opus.VDF  --mono 48k --stereo 64k
"""
import argparse
import os
import struct
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

HEADER_SIZE = 296
ENTRY_SIZE  = 80
DIR_FLAG    = 0x80000000


def read_vdf(path):
    with open(path, 'rb') as f:
        header = f.read(HEADER_SIZE)
        count, files, timestamp, size, root, version = struct.unpack('<6I', header[272:296])
        f.seek(root)
        entries = [list(struct.unpack('<64s4I', f.read(ENTRY_SIZE))) for _ in range(count)]
        blobs = {}
        for i, (name, off, sz, typ, attr) in enumerate(entries):
            if not typ & DIR_FLAG:
                f.seek(off)
                blobs[i] = f.read(sz)
    return header, entries, blobs


def wav_channels(data):
    if data[:4] != b'RIFF' or data[8:12] != b'WAVE':
        return 0
    pos = 12
    while pos + 8 <= len(data):
        tag, sz = data[pos:pos+4], struct.unpack('<I', data[pos+4:pos+8])[0]
        if tag == b'fmt ':
            return struct.unpack('<H', data[pos+10:pos+12])[0]
        pos += 8 + sz + (sz & 1)
    return 0


def encode(data, bitrate):
    cmd = ['ffmpeg', '-v', 'error', '-f', 'wav', '-i', 'pipe:0',
           '-map_metadata', '-1', '-fflags', '+bitexact', '-flags:a', '+bitexact',
           '-c:a', 'libopus', '-b:a', bitrate, '-f', 'opus', 'pipe:1']
    r = subprocess.run(cmd, input=data, capture_output=True)
    if r.returncode != 0 or r.stdout[:4] != b'OggS':
        return None
    return r.stdout


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('src')
    ap.add_argument('dst')
    ap.add_argument('--mono',   default='24k', help='bitrate for mono files (default 24k)')
    ap.add_argument('--stereo', default='64k', help='bitrate for stereo files (default 64k)')
    ap.add_argument('--jobs',   type=int, default=os.cpu_count())
    args = ap.parse_args()

    header, entries, blobs = read_vdf(args.src)

    def job(i):
        name = entries[i][0].rstrip(b' \0').decode('latin1')
        data = blobs[i]
        ch   = wav_channels(data) if name.upper().endswith('.WAV') else 0
        if ch not in (1, 2):
            return i, data, False
        out = encode(data, args.mono if ch == 1 else args.stereo)
        if out is None or len(out) >= len(data):
            return i, data, False
        return i, out, True

    done = 0
    kept = []
    with ThreadPoolExecutor(args.jobs) as pool:
        for i, data, converted in pool.map(job, list(blobs)):
            blobs[i] = data
            if not converted:
                kept.append(entries[i][0].rstrip(b' \0').decode('latin1'))
            done += 1
            if done % 200 == 0 or done == len(blobs):
                print(f'\r{done}/{len(blobs)}', end='', file=sys.stderr, flush=True)
    print(file=sys.stderr)

    root = HEADER_SIZE
    pos  = root + len(entries) * ENTRY_SIZE
    for i in sorted(blobs):
        entries[i][1] = pos
        entries[i][2] = len(blobs[i])
        pos += len(blobs[i])

    count, files, timestamp, _, _, version = struct.unpack('<6I', header[272:296])
    with open(args.dst, 'wb') as f:
        f.write(header[:272])
        f.write(struct.pack('<6I', count, files, timestamp, pos - root - len(entries) * ENTRY_SIZE, root, version))
        for e in entries:
            f.write(struct.pack('<64s4I', *e))
        for i in sorted(blobs):
            f.write(blobs[i])

    src_size, dst_size = os.path.getsize(args.src), os.path.getsize(args.dst)
    print(f'{args.src}: {src_size/1e6:.1f} MB -> {dst_size/1e6:.1f} MB, {len(kept)} files copied unchanged')
    for name in kept[:20]:
        print('  kept:', name)


if __name__ == '__main__':
    main()
