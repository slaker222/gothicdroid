#!/usr/bin/env python3
"""Make the archives of gothic_base_compact: smaller, with a loss nobody should notice.

    tools/compact_vdf.py Textures.vdf [more.vdf ...] [--colors 256] [--min-psnr 40]

The pictures stored under a *.TGA name (plain TGA or the PNG written by tools/recode_vdf_png.py:
loading screens, maps ...) are reduced to a palette of at most --colors colours (libimagequant,
Floyd-Steinberg dithering, the same as pngquant) and stored as a palette PNG. The resolution
is never changed. Transparency goes to the PNG tRNS chunk; Tempest reads that since the
palette+tRNS fix in PixmapCodecPng, so the compact base needs an engine with that fix.
A picture whose PSNR against the original (over mid-grey, so alpha counts) comes out below
--min-psnr stays as it was. The archives are rewritten in place; run it on a copy.
"""
import argparse
import io
import math
import os
import sys
from concurrent.futures import ProcessPoolExecutor

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import split_vdf as sv


def psnr(a, b):
    ba, bb = a.tobytes(), b.tobytes()
    se = sum((x - y) ** 2 for x, y in zip(ba, bb))
    return 99.0 if se == 0 else 10 * math.log10(255.0 * 255.0 * len(ba) / se)


def over_grey(im):
    return Image.alpha_composite(Image.new('RGBA', im.size, (128, 128, 128, 255)), im).convert('RGB')


def reduce(job):
    data, colors, min_psnr = job
    try:
        im = Image.open(io.BytesIO(data))
        im.load()
        rgba = im.convert('RGBA')
        q = rgba.quantize(colors=colors, method=Image.Quantize.LIBIMAGEQUANT, dither=Image.Dither.FLOYDSTEINBERG)
        out = io.BytesIO()
        q.save(out, 'PNG', optimize=True)
        res = out.getvalue()
        back = Image.open(io.BytesIO(res))
        back.load()
        if back.mode != 'P' or back.size != im.size:
            return None, 0.0
        p = psnr(over_grey(rgba), over_grey(back.convert('RGBA')))
    except Exception:
        return None, 0.0
    if p < min_psnr or len(res) >= len(data):
        return None, p
    return res, p


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('vdf', nargs='+')
    ap.add_argument('--colors', type=int, default=256)
    ap.add_argument('--min-psnr', type=float, default=40.0)
    args = ap.parse_args()
    for path in args.vdf:
        header, tree = sv.read_vdf(path)
        todo = [(p, n) for p, n in sv.walk(tree) if p.upper().endswith('.TGA')]
        with ProcessPoolExecutor() as pool:
            res = list(pool.map(reduce, [(n.data, args.colors, args.min_psnr) for _, n in todo], chunksize=1))
        before = after = kept = 0
        worst = 99.0
        for (p, n), (data, ps) in zip(todo, res):
            before += len(n.data)
            if data is None:
                kept += 1
                after += len(n.data)
                continue
            worst = min(worst, ps)
            n.data = data
            after += len(data)
        tmp = path + '.tmp'
        sv.write_vdf(tmp, header, tree)
        os.replace(tmp, path)
        print(f'{path}: {len(todo)} pictures, {before/1e6:.1f} MB -> {after/1e6:.1f} MB, '
              f'reduced {len(todo) - kept}, left as they were {kept}, lowest PSNR of the reduced {worst:.1f} dB')


if __name__ == '__main__':
    main()
