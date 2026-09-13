#!/usr/bin/env python3
"""从 Linux 控制台 PSF 字体生成 8x16 ASCII 点阵头文件（32~126）。"""
import gzip, struct, sys

src = sys.argv[1] if len(sys.argv) > 1 else "/usr/lib/kbd/consolefonts/default8x16.psfu.gz"
data = gzip.open(src).read() if src.endswith(".gz") else open(src, "rb").read()

if data[:2] == b"\x36\x04":                      # PSF1
    mode, h = data[2], data[3]
    w, count, hdr = 8, (512 if mode & 1 else 256), 4
    glyphs = [data[hdr + i*h: hdr + (i+1)*h] for i in range(count)]
    table = data[hdr + count*h:] if mode & 2 else b""
    cmap = {}
    if table:
        pos = 0
        for g in range(count):
            while pos + 1 < len(table):
                u = struct.unpack_from("<H", table, pos)[0]; pos += 2
                if u == 0xFFFF: break
                if u != 0xFFFE: cmap.setdefault(u, g)
elif data[:4] == b"\x72\xb5\x4a\x86":            # PSF2
    _, _, hdr, flags, count, size, h, w = struct.unpack_from("<8I", data)
    glyphs = [data[hdr + i*size: hdr + (i+1)*size] for i in range(count)]
    cmap = {}
else:
    sys.exit("unknown font format")

assert (w, h) == (8, 16), f"font is {w}x{h}, need 8x16"
out = ["/* 自动生成: scripts/gen-font.py  来源: " + src.split('/')[-1] + " */",
       "#ifndef FONT8X16_H", "#define FONT8X16_H", "#include <stdint.h>",
       "#define FONT_W 8", "#define FONT_H 16", "#define FONT_FIRST 32", "#define FONT_LAST 126",
       "static const uint8_t font8x16[95][16] = {"]
for c in range(32, 127):
    g = glyphs[cmap.get(c, c)]
    out.append("    {" + ",".join(f"0x{b:02X}" for b in g) + "}, /* '" + (chr(c) if c not in (92,) else "\\\\") + "' */")
out += ["};", "#endif"]
open("src/font8x16.h", "w").write("\n".join(out) + "\n")
print("ok:", src)
