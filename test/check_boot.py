#!/usr/bin/env python3
"""Checks what test/boot.tcl saw (AGENTS.md §10, M0, M2), and draws it.

    check_boot.py DUMP WANT PALETTE_H SHOT_PREFIX

WANT is 50 or 60 (an MSX2 at that rate) or msx1. For an MSX2 it writes
SHOT_PREFIX-before.png and -after.png: the screen as the VDP holds it,
256x212, in the colours it holds, before and after Space.
"""
import re
import struct
import sys
import zlib

W, H = 256, 212


def read_dump(path):
    seen = {}
    with open(path, encoding="ascii") as f:
        for line in f:
            tag, what, data = line.rstrip("\n").split(" ", 2)
            if tag == "error":
                sys.exit(f"FAIL {path}: the openMSX script stopped: {data}")
            seen[(tag, what)] = bytes.fromhex(data)
    return seen


def palette_from_header(path):
    text = open(path, encoding="utf-8").read()
    body = text[text.index("{"):text.index("}")]
    return bytes(int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{2})", body))


def rgb(palette, i):
    rb, g = palette[2 * i], palette[2 * i + 1]
    scale = lambda v: v * 255 // 7
    return scale(rb >> 4 & 7), scale(g & 7), scale(rb & 7)


def write_png(path, vram, palette, height=H):
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(W):
            byte = vram[y * 128 + x // 2]
            rows.extend(rgb(palette, byte >> 4 if x % 2 == 0 else byte & 15))
    def chunk(kind, data):
        c = kind + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", W, height, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(rows), 9)))
        f.write(chunk(b"IEND", b""))


def lit(vram, y0, y1, x0=0, x1=W):
    """How many pixels in the box are not colour 0."""
    n = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            byte = vram[y * 128 + x // 2]
            n += (byte >> 4 if x % 2 == 0 else byte & 15) != 0
    return n


def main(dump, want, palette_h, shot):
    seen = read_dump(dump)
    name = dump.rsplit("/", 1)[-1]
    problems = []
    if want == "msx1":
        screen = seen[("before", "vram")][:960].decode("latin-1")
        if "MSX2" not in screen:
            problems.append("an MSX1 does not say an MSX2 is needed")
    else:
        regs = seen[("before", "regs")]
        if regs[0] & 0x0E != 0x06 or regs[1] & 0x18 != 0:
            problems.append(f"not SCREEN 5 (R#0={regs[0]:02X} R#1={regs[1]:02X})")
        hz = 50 if regs[9] & 0x02 else 60
        if hz != int(want):
            problems.append(f"running at {hz} Hz, the machine is {want} Hz")
        palette = palette_from_header(palette_h)
        if seen[("before", "palette")] != palette:
            problems.append("the palette is not the game's (build/gen/palette.h)")
        before, after = seen[("before", "vram")], seen[("after", "vram")]
        write_png(shot + "-before.png", before, palette)
        write_png(shot + "-after.png", after, palette)
        gen = palette_h.rsplit("/", 1)[0]
        PLAYFIELD_H = int(re.search(r"#define PLAYFIELD_H\t(\d+)", open(gen + "/constants.h").read())[1])
        # M2: a room in the playfield, the panel below it blank, and Space
        # shows another room (test/rooms.sh checks the rooms themselves).
        if lit(before, 0, PLAYFIELD_H) == 0:
            problems.append("no room in the playfield")
        if lit(before, PLAYFIELD_H, H) != 0:
            problems.append("the panel area is not blank")
        if after == before:
            problems.append("Space did not show another room")
    for p in problems:
        print(f"FAIL {name}: {p}")
    if not problems:
        print(f"ok   {name}")
    return 1 if problems else 0


if __name__ == "__main__":
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    sys.exit(main(*sys.argv[1:]))
