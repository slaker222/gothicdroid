#!/usr/bin/env python3
"""Icon of a Windows exe (the first icon group) -> .ico and the largest image as .png.
   python3 tools/extract_exe_icon.py GOTHIC.EXE out_dir/name"""
import struct, sys, io
import pefile
from PIL import Image

pe = pefile.PE(sys.argv[1], fast_load=True)
pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_RESOURCE']])
RT_ICON, RT_GROUP = 3, 14
res = {}
for t in pe.DIRECTORY_ENTRY_RESOURCE.entries:
    if t.id in (RT_ICON, RT_GROUP):
        res[t.id] = t
def data(entry):
    leaf = entry.directory.entries[0].data.struct
    return pe.get_data(leaf.OffsetToData, leaf.Size)
icons = {e.id: data(e) for e in res[RT_ICON].directory.entries}
grp = data(res[RT_GROUP].directory.entries[0])
_, _, count = struct.unpack('<HHH', grp[:6])
entries = []
for i in range(count):
    w, h, cc, _, planes, bpp, size, iid = struct.unpack('<BBBBHHIH', grp[6 + i*14: 6 + i*14 + 14])
    entries.append((w or 256, h or 256, cc, planes, bpp, icons[iid]))
# .ico
out = struct.pack('<HHH', 0, 1, len(entries))
off = 6 + 16 * len(entries)
body = b''
for w, h, cc, planes, bpp, d in entries:
    out += struct.pack('<BBBBHHII', w % 256, h % 256, cc, 0, planes, bpp, len(d), off + len(body))
    body += d
open(sys.argv[2] + '.ico', 'wb').write(out + body)
ico = Image.open(sys.argv[2] + '.ico')
size = max(ico.ico.sizes())
im = ico.ico.getimage(size).convert('RGBA')
im.save(sys.argv[2] + '.png')
print(sys.argv[1], sorted(ico.ico.sizes()), '->', im.size)
