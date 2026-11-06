#!/usr/bin/env python3
"""Checks what test/boot.tcl saw (AGENTS.md §10, M0, M2), and draws it.

    check_boot.py DUMP WANT PALETTE_H SHOT_PREFIX

WANT is 50 or 60 (an MSX2 at that rate) or msx1. For an MSX2 it writes
SHOT_PREFIX-before.png and -after.png: the screen as the VDP holds it,
256x212, in the colours it holds, sprites included, before and after F5 (the
debug key for the next room). And it checks Vale is on screen (M3).
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


def write_png(path, vram, palette, height=H, sprites=None):
    """The screen; with sprites ({(x, y): colour}, from sprite_pixels) drawn over it."""
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(W):
            byte = vram[y * 128 + x // 2]
            c = byte >> 4 if x % 2 == 0 else byte & 15
            if sprites and (x, y) in sprites:
                c = sprites[(x, y)]
            rows.extend(rgb(palette, c))
    def chunk(kind, data):
        c = kind + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", W, height, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(rows), 9)))
        f.write(chunk(b"IEND", b""))


def sprite_pixels(regs, vram):
    """Sprite mode 2, 16x16 sprites, as the V9938 draws them: {(x, y): colour}.

    vram is video RAM from address 0. The first sprite has priority; a Y of
    216 ends the list; a line's colour byte gives its colour and the early
    clock (32 px to the left). The OR bit and the 8-a-line limit are left out:
    the game uses neither yet.
    """
    sat = ((regs[5] & 0xFC) << 7) | (regs[11] << 15)   # R#5 bits 0-1 are always set in mode 2
    colours = sat - 512
    patterns = regs[6] << 11
    out = {}
    shown = []
    for i in range(32):
        y = vram[sat + 4 * i]
        if y == 216:
            break
        shown.append(i)
    for i in reversed(shown):
        y, x, pat = vram[sat + 4 * i], vram[sat + 4 * i + 1], vram[sat + 4 * i + 2] & 0xFC
        for line in range(16):
            cb = vram[colours + 16 * i + line]
            c = cb & 15
            sy = (y + 1 + line) & 0xFF
            if c == 0 or sy >= H:
                continue
            for px in range(16):
                bits = vram[patterns + pat * 8 + (px // 8) * 16 + line]
                sx = x - (32 if cb & 0x80 else 0) + px
                if bits & (0x80 >> (px % 8)) and 0 <= sx < W:
                    out[(sx, sy)] = c
    return out


def header_defines(path):
    text = open(path, encoding="utf-8").read()
    return {m[0]: int(m[1]) for m in re.findall(r"#define (\w+)\t+\(?(-?\d+)L?\)?", text)}


def vale_problems(tag, regs, vram, gen):
    """M3: Vale stands where a new game starts him, facing down, in his first frame, in his colour."""
    p = header_defines(gen + "/player.h")
    c = header_defines(gen + "/constants.h")
    frame = p["PLAYER_VIEW_DOWN"] * p["PLAYER_FRAMES_PER_VIEW"]
    art = open(gen + "/player_frames.bin", "rb").read()[frame * 64:(frame + 1) * 64]
    left = (p["PLAYER_START_X_FP"] >> 8) - c["START_COL"] * c["PLAYFIELD_W"] - p["PLAYER_ORIGIN_X"]
    top = (p["PLAYER_START_Y_FP"] >> 8) - c["START_ROW"] * c["PLAYFIELD_H"] - p["PLAYER_ORIGIN_Y"]
    want = {}
    for y in range(24):
        for x in range(16):
            if art[(y // 16) * 32 + (x // 8) * 16 + y % 16] & (0x80 >> (x % 8)):
                want[(left + x, top + y)] = p["PLAYER_INK"]
    got = sprite_pixels(regs, vram)
    if not want:
        return [f"{tag}: Vale's first frame is empty"]
    if got != want:
        return [f"{tag}: Vale is not on screen as he should be ({len(got)} sprite pixels shown, "
                f"{len(want)} expected at {left},{top}, {len(set(got.items()) & set(want.items()))} of them right)"]
    return []


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
        gen = palette_h.rsplit("/", 1)[0]
        for tag in ("before", "after"):
            regs_t, vram64 = seen[(tag, "regs")], seen[(tag, "vram64")]
            write_png(f"{shot}-{tag}.png", seen[(tag, "vram")], palette, sprites=sprite_pixels(regs_t, vram64))
            # M3: Vale, standing; F5 keeps him at the same place in the next room.
            problems += vale_problems(tag, regs_t, vram64, gen)
        PLAYFIELD_H = int(re.search(r"#define PLAYFIELD_H\t(\d+)", open(gen + "/constants.h").read())[1])
        # M2: a room in the playfield, and F5 shows another room
        # (test/rooms.sh checks the rooms themselves). M4: the debug overlay
        # on the panel below it.
        if lit(before, 0, PLAYFIELD_H) == 0:
            problems.append("no room in the playfield")
        if lit(before, PLAYFIELD_H, H) == 0:
            problems.append("the panel has no debug overlay")
        if after == before:
            problems.append("F5 did not show another room")
    for p in problems:
        print(f"FAIL {name}: {p}")
    if not problems:
        print(f"ok   {name}")
    return 1 if problems else 0


if __name__ == "__main__":
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    sys.exit(main(*sys.argv[1:]))
