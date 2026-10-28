#!/usr/bin/env python3
"""Recode the uncompressed *.TGA pictures of a Gothic VDF archive to PNG, without any loss.

    tools/recode_vdf_png.py Textures.vdf [more.vdf ...]

Loading screens, start screens and maps are plain 24/32 bit TGA, 4-6 MB each. The engine picks
the image codec by the file content (Tempest PixmapCodec::testFormat), not by the name, so a
PNG stored under the same *.TGA name loads as before and keeps every pixel.
Each picture is decoded back and compared with the original pixel by pixel; one that does not
match, or does not get smaller, stays as it was. The archive is rewritten in place.
"""
import io
import os
import sys
from concurrent.futures import ProcessPoolExecutor

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import split_vdf as sv


def recode(data):
    """PNG bytes for a TGA, or None if it is not a plain true-colour TGA or not identical"""
    if len(data) < 18 or data[2] != 2 or data[16] not in (24, 32):
        return None
    try:
        im = Image.open(io.BytesIO(data))
        im.load()
        if im.mode not in ('RGB', 'RGBA'):
            return None
        out = io.BytesIO()
        im.save(out, 'PNG', optimize=True, compress_level=9)
        png = out.getvalue()
        back = Image.open(io.BytesIO(png))
        back.load()
        if back.mode != im.mode or back.size != im.size or back.tobytes() != im.tobytes():
            return None
    except Exception:
        return None
    return png if len(png) < len(data) else None


def main():
    for path in sys.argv[1:]:
        header, tree = sv.read_vdf(path)
        todo = [(p, n) for p, n in sv.walk(tree) if p.upper().endswith('.TGA')]
        if not todo:
            print(f'{path}: no TGA')
            continue
        before = sum(len(n.data) for _, n in todo)
        with ProcessPoolExecutor() as pool:
            res = list(pool.map(recode, [n.data for _, n in todo], chunksize=1))
        after = kept = 0
        for (p, n), png in zip(todo, res):
            if png is None:
                kept += 1
                after += len(n.data)
            else:
                n.data = png
                after += len(png)
        tmp = path + '.tmp'
        sv.write_vdf(tmp, header, tree)
        os.replace(tmp, path)
        print(f'{path}: {len(todo)} TGA, {before/1e6:.1f} MB -> {after/1e6:.1f} MB, left as TGA: {kept}')


if __name__ == '__main__':
    main()
