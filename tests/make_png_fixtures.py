#!/usr/bin/env python3
"""Make the PNG test pictures in tests/data/ and the pixels each one must decode to.

For every picture NAME.png there is a line "NAME width height crc32" in manifest.txt: the CRC-32
of the expected 8-bit RGBA pixels, row by row. The small synthetic pictures also have NAME.rgba,
the pixels themselves, to look at when a test fails. The synthetic pictures are written here with
an explicit filter on each row and several compression styles, so that every path of the
reader is used. Where Pillow reads a picture the same way, its result is compared too, so a
mistake here cannot hide a mistake in the reader.

The two noaa_*.png files are real answers from NOAA's radar map service (public domain);
their expected pixels come from Pillow.

Run:  python3 tests/make_png_fixtures.py       (needs Pillow)
"""
import os
import random
import struct
import sys
import zlib

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "data")
random.seed(20261124)


def chunk(kind, body):
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)


def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else (b if pb <= pc else c)


def filter_row(kind, row, prev, bpp):
    out = bytearray()
    for i, x in enumerate(row):
        a = row[i - bpp] if i >= bpp else 0
        b = prev[i]
        c = prev[i - bpp] if i >= bpp else 0
        pred = [0, a, b, (a + b) // 2, paeth(a, b, c)][kind]
        out.append((x - pred) & 0xFF)
    return bytes([kind]) + bytes(out)


def pack_row(samples, depth):
    """Pack a list of samples into bytes, most significant bits first."""
    if depth == 8:
        return bytes(samples)
    if depth == 16:
        return b"".join(struct.pack(">H", s) for s in samples)
    bits = "".join(format(s, "0%db" % depth) for s in samples)
    bits += "0" * (-len(bits) % 8)
    return bytes(int(bits[i:i + 8], 2) for i in range(0, len(bits), 8))


def compress(data, style):
    if style == "stored":
        comp = zlib.compressobj(0)
    elif style == "fixed":
        comp = zlib.compressobj(6, zlib.DEFLATED, 15, 8, zlib.Z_FIXED)
    elif style == "best":
        comp = zlib.compressobj(9)
    elif style == "fast":
        comp = zlib.compressobj(1)
    elif style == "rle":
        comp = zlib.compressobj(6, zlib.DEFLATED, 15, 8, zlib.Z_RLE)
    elif style == "blocks":                              # several deflate blocks
        comp = zlib.compressobj(6)
        out = b""
        for i in range(0, len(data), 97):
            out += comp.compress(data[i:i + 97]) + comp.flush(zlib.Z_FULL_FLUSH)
        return out + comp.flush()
    else:
        comp = zlib.compressobj(6)
    return comp.compress(data) + comp.flush()


def write_png(w, h, ctype, depth, rows, filters, style, plte=None, trns=None, idat_pieces=1):
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    bpp = max(1, channels * depth // 8)
    raw = b""
    prev = bytes(len(rows[0]))
    for y, row in enumerate(rows):
        raw += filter_row(filters[y % len(filters)], row, prev, bpp)
        prev = row
    z = compress(raw, style)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, depth, ctype, 0, 0, 0))
    png += chunk(b"gAMA", struct.pack(">I", 45455))                 # ancillary chunks are skipped
    if plte:
        png += chunk(b"PLTE", plte)
    if trns:
        png += chunk(b"tRNS", trns)
    step = max(1, len(z) // idat_pieces + 1)
    for i in range(0, len(z), step):
        png += chunk(b"IDAT", z[i:i + step])
    png += chunk(b"tEXt", b"Comment\x00made by make_png_fixtures.py")
    return png + chunk(b"IEND", b"")


def scale(v, depth):
    return v >> 8 if depth == 16 else (v * 255 // ((1 << depth) - 1) if depth < 8 else v)


def synth(name, ctype, depth, style, filters, w=23, h=17, trns=None, pieces=1, check_pillow=True):
    """Random samples -> PNG bytes and the RGBA the reader must give."""
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    top = (1 << depth) - 1
    plte = None
    if ctype == 3:
        n = min(top + 1, 256)
        entries = [tuple(random.randrange(256) for _ in range(3)) for _ in range(n)]
        plte = b"".join(bytes(e) for e in entries)
    samples = [[random.randint(0, top) for _ in range(w * channels)] for _ in range(h)]
    if ctype == 0 and trns is not None:                           # make sure some pixels match tRNS
        for row in samples:
            for i in range(0, w, 5):
                row[i] = trns[0]
    if ctype == 2 and trns is not None:
        for row in samples:
            for i in range(0, w, 5):
                row[3 * i:3 * i + 3] = list(trns)
    rows = [pack_row(r, depth) for r in samples]
    if ctype == 3 and trns is not None:
        trns_bytes = bytes(trns)
    elif ctype == 0 and trns is not None:
        trns_bytes = struct.pack(">H", trns[0])
    elif ctype == 2 and trns is not None:
        trns_bytes = b"".join(struct.pack(">H", v) for v in trns)
    else:
        trns_bytes = None
    png = write_png(w, h, ctype, depth, rows, filters, style, plte, trns_bytes, pieces)

    rgba = bytearray()
    for r in samples:
        for x in range(w):
            if ctype == 0:
                v = r[x]
                g = scale(v, depth)
                a = 0 if trns is not None and v == trns[0] else 255
                rgba += bytes([g, g, g, a])
            elif ctype == 2:
                px = r[3 * x:3 * x + 3]
                a = 0 if trns is not None and tuple(px) == tuple(trns) else 255
                rgba += bytes([scale(v, depth) for v in px]) + bytes([a])
            elif ctype == 3:
                e = entries[r[x]]
                a = trns[r[x]] if trns is not None and r[x] < len(trns) else 255
                rgba += bytes(e) + bytes([a])
            elif ctype == 4:
                g = scale(r[2 * x], depth)
                rgba += bytes([g, g, g, scale(r[2 * x + 1], depth)])
            else:
                rgba += bytes([scale(v, depth) for v in r[4 * x:4 * x + 4]])
    # Pillow cannot be the judge everywhere: it clips 16-bit grey, and it does not apply tRNS
    # to grey pictures below 8 bits or to 16-bit RGB (it compares after scaling to 8 bits), so
    # there only the colours are compared
    mode = "full"
    if not check_pillow or (depth == 16 and ctype in (0, 4)):
        mode = "none"
    elif ctype in (0, 2) and trns is not None and (depth == 16 or (ctype == 0 and depth < 8)):
        mode = "colours"
    save(name, png, bytes(rgba), w, h, pillow=mode)


def save(name, png, rgba, w, h, pillow="full", keep_pixels=True):
    path = os.path.join(DATA, name + ".png")
    open(path, "wb").write(png)
    if keep_pixels:
        open(os.path.join(DATA, name + ".rgba"), "wb").write(rgba)
    if pillow != "none":
        im = Image.open(path).convert("RGBA")
        got = im.tobytes()
        if pillow == "colours":                              # leave the alpha out
            got = bytes(b for i, b in enumerate(got) if i % 4 != 3)
            want = bytes(b for i, b in enumerate(rgba) if i % 4 != 3)
        else:
            want = rgba
        if im.size != (w, h) or got != want:
            sys.exit("Pillow disagrees with the expected pixels of " + name)
    manifest.append("%s %d %d %d" % (name, w, h, zlib.crc32(rgba) & 0xFFFFFFFF))


def real(name):
    im = Image.open(os.path.join(DATA, name + ".png")).convert("RGBA")
    manifest.append("%s %d %d %d" % (name, im.size[0], im.size[1], zlib.crc32(im.tobytes()) & 0xFFFFFFFF))


manifest = []
os.makedirs(DATA, exist_ok=True)
ALL = [0, 1, 2, 3, 4]

for depth, style in [(1, "stored"), (2, "fixed"), (4, "best"), (8, "blocks"), (16, "fast")]:
    synth("grey_%d" % depth, 0, depth, style, ALL)
synth("grey_8_trns", 0, 8, "default", ALL, trns=(77,))
synth("grey_4_trns", 0, 4, "fixed", ALL, trns=(9,))
synth("grey_16_trns", 0, 16, "default", ALL, trns=(40000,))
for depth, style in [(8, "default"), (16, "blocks")]:
    synth("rgb_%d" % depth, 2, depth, style, ALL)
synth("rgb_8_trns", 2, 8, "best", ALL, trns=(10, 20, 30))
synth("rgb_16_trns", 2, 16, "default", ALL, trns=(1000, 2000, 3000))
for depth, style in [(1, "default"), (2, "rle"), (4, "blocks"), (8, "best")]:
    synth("palette_%d" % depth, 3, depth, style, ALL, pieces=3)
synth("palette_8_trns", 3, 8, "default", ALL, trns=[0, 128, 255, 10, 200])
synth("palette_4_trns", 3, 4, "fixed", ALL, trns=[255, 0, 77])
for depth, style in [(8, "default"), (16, "stored")]:
    synth("grey_alpha_%d" % depth, 4, depth, style, ALL)
for depth, style in [(8, "fast"), (16, "default")]:
    synth("rgba_%d" % depth, 4 + 2, depth, style, ALL)
for f in range(5):                                          # each filter on its own, wide rows
    synth("filter_%d" % f, 6, 8, "default", [f], w=61, h=9)
synth("pieces_rgba_8", 6, 8, "blocks", ALL, w=40, h=30, pieces=7)
synth("one_pixel", 6, 8, "default", [0], w=1, h=1)
synth("wide_one_row", 2, 8, "fixed", [1], w=500, h=1)

# a bigger picture with long runs and gradients, so copies and several blocks are used
w, h = 200, 100
rows = []
for y in range(h):
    rows.append(bytes(v for x in range(w) for v in ((x * 255 // w), (y * 255 // h), ((x + y) % 7) * 36, 255 if (x // 20 + y // 10) % 3 else 0)))
rgba = b"".join(rows)
save("gradient_rgba", write_png(w, h, 6, 8, rows, [4, 2, 1, 3, 0], "default"), rgba, w, h)

real("noaa_kansas_clear")
real("noaa_seattle_rain")
open(os.path.join(DATA, "manifest.txt"), "w").write("\n".join(manifest) + "\n")
print("wrote %d pictures" % len(manifest))
