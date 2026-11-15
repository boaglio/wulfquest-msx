#!/usr/bin/env python3
"""tools/screenshots.py DIR: each DIR/NAME.{regs,pal,vram} (tools/screenshots.tcl's
dumps) drawn as DIR/NAME.png: the page on show (R#2), the sprites over it as
the V9938 draws them (test/vdp_sprites.py), each pixel doubled (512x424)."""
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "test"))
import vdp_sprites  # noqa: E402

W, H = 256, 212


def rgb(pal, i):
    rb, g = pal[2 * i], pal[2 * i + 1]
    scale = lambda v: v * 255 // 7
    return bytes((scale(rb >> 4 & 7), scale(g & 7), scale(rb & 7)))


def png(path, pixels):
    rows = bytearray()
    for line in pixels:
        row = b"".join(c + c for c in line)
        rows += b"\x00" + row + b"\x00" + row
    def chunk(kind, data):
        c = kind + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", 2 * W, 2 * H, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(rows), 9)))
        f.write(chunk(b"IEND", b""))


def main(d):
    names = sorted(n[:-5] for n in os.listdir(d) if n.endswith(".vram"))
    for name in names:
        regs = open(os.path.join(d, name + ".regs"), "rb").read()
        pal = open(os.path.join(d, name + ".pal"), "rb").read()
        vram = open(os.path.join(d, name + ".vram"), "rb").read()
        page = (regs[2] >> 5) & 3
        screen = vram[page * 32768:(page + 1) * 32768]
        sprites = {}
        if not regs[8] & 2:   # R#8's SPD: sprites on
            sprites = vdp_sprites.render(*vdp_sprites.tables(regs, vram))
        colours = [rgb(pal, i) for i in range(16)]
        pixels = []
        for y in range(H):
            line = []
            for x in range(W):
                b = screen[y * 128 + x // 2]
                c = sprites.get((x, y), b >> 4 if x % 2 == 0 else b & 15)
                line.append(colours[c])
            pixels.append(line)
        png(os.path.join(d, name + ".png"), pixels)
        print(f"screenshots: {name}.png")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
