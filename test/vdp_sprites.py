"""Sprite mode 2 as the V9938 draws it, and what the ROM's sprites should look like (AGENTS.md §10, M5).

render() draws the sprites from the attribute, colour and pattern tables:
- a sprite shows one line below its Y; a Y of 216 ends the list;
- on each line only the first 8 sprites on it are drawn; the rest are dropped;
- a line's colour byte gives its colour (0 transparent) and the early clock
  (bit 7: drawn 32 px to the left);
- a sprite with the CC bit (bit 6) is drawn only on a line where a sprite
  without it, numbered before it, is too; where it overlaps the sprites
  before it in its run of CC sprites, their colours are ORed;
- the first sprite with a pixel at a point is in front.

Gen reads the converter's sprite data from build/gen/ and draws what a frame of
Vale (with his blade), or a frame of the sprite bank, should look like:
expected() returns the same {(x, y): colour} for comparing.
"""
import os
import re

W = 256
EC = 0x80
CC = 0x40
TERMINATOR = 216


def tables(regs, vram, sat_at=None):
    """(attributes, colours, patterns) from VDP registers and video RAM from address 0."""
    sat = ((regs[5] & 0xFC) << 7) | (regs[11] << 15) if sat_at is None else sat_at
    return vram[sat:sat + 128], vram[sat - 512:sat], vram[regs[6] << 11:(regs[6] << 11) + 2048]


def render(sat, colours, patterns, lines=212):
    """{(x, y): colour number} of the sprites, as the V9938 draws them."""
    sprites = []
    for i in range(32):
        y = sat[4 * i]
        if y == TERMINATOR:
            break
        sprites.append((i, (y + 1) & 0xFF, sat[4 * i + 1], sat[4 * i + 2] & 0xFC))
    out = {}
    for line in range(lines):
        on = []
        for i, top, x, pat in sprites:
            row = (line - top) & 0xFF
            if row < 16:
                on.append((i, row, x, pat))
                if len(on) == 8:
                    break
        if not on:
            continue
        drawn = []   # (colour byte, left, pattern row bits as 16-bit int), in priority order
        plain_before = False
        for i, row, x, pat in on:
            cb = colours[16 * i + row]
            bits = patterns[pat * 8 + row] << 8 | patterns[pat * 8 + 16 + row]
            cc = bool(cb & CC)
            if cc and not plain_before:
                drawn.append(None)   # not shown on this line
                continue
            plain_before |= not cc
            drawn.append((cb, x - (32 if cb & EC else 0), bits, cc))
        lo = min(d[1] for d in drawn if d)
        hi = max(d[1] for d in drawn if d) + 16
        for px in range(max(0, lo), min(W, hi)):
            for k, d in enumerate(drawn):
                if d is None:
                    continue
                cb, left, bits, cc = d
                if not (0 <= px - left < 16 and bits & (0x8000 >> (px - left))):
                    continue
                c = cb & 15
                for e in drawn[k + 1:]:
                    if e is None or not e[3]:
                        break
                    if 0 <= px - e[1] < 16 and e[2] & (0x8000 >> (px - e[1])):
                        c |= e[0] & 15
                if c:
                    out[(px, line)] = c
                    break
    return out


def defines(path):
    text = open(path, encoding="utf-8").read()
    return {m[0]: int(m[1], 0) for m in re.findall(r"#define (\w+)\t+\(?(-?(?:0x[0-9A-Fa-f]+|\d+))[UL]*\)?", text)}


def table(gen, name):
    """A table of tables.h, read from tables.bin: (type, offset, count) from its #define."""
    text = open(gen + "/tables.h", encoding="utf-8").read()
    t, off, n = re.search(r"#define " + name + r"\t\(\(const (\w+)\*\)\(g_Tables \+ (\d+)\)\)\t// \[(\d+)\]", text).groups()
    blob = open(gen + "/tables.bin", "rb").read()
    off, n = int(off), int(n)
    size = 2 if t in ("u16", "i16") else 1
    out = []
    for i in range(n):
        v = int.from_bytes(blob[off + size * i:off + size * (i + 1)], "little", signed=t.startswith("i"))
        out.append(v)
    return out


def layered(blob, at, blocks, cols=1):
    """A cut frame (SpriteCut.java) back to {(x, y): colour}: blocks row by row, cols a row."""
    out = {}
    colours = at + blocks * 64
    for b in range(blocks):
        base, over = at + b * 64, at + b * 64 + 32
        for y in range(16):
            a = blob[colours + b * 32 + y] & 15
            c = blob[colours + b * 32 + 16 + y] & 15
            for x in range(16):
                i, bit = (x // 8) * 16 + y, 0x80 >> (x % 8)
                on0, on1 = blob[base + i] & bit, blob[over + i] & bit
                v = (a | c if on1 else a) if on0 else (c if on1 else 0)
                if v:
                    out[((b % cols) * 16 + x, (b // cols) * 16 + y)] = v
    return out


class Gen:
    """The converter's sprite data, from build/gen/."""

    def __init__(self, gen):
        self.player = defines(gen + "/player.h")
        self.blade = defines(gen + "/blade.h")
        self.bank = defines(gen + "/sprite_bank.h")
        self.c = defines(gen + "/constants.h")
        self.frames = open(gen + "/player_frames.bin", "rb").read()
        self.gen = gen
        self._inked = {}
        self.sprites = []   # the bank's chunks, sprites_N.bin
        while os.path.exists(f"{gen}/sprites_{len(self.sprites)}.bin"):
            self.sprites.append(open(f"{gen}/sprites_{len(self.sprites)}.bin", "rb").read())
        self.shapes = open(gen + "/blade.bin", "rb").read()
        self.hand = table(gen, "g_PlayerHand")
        self.blade_at = table(gen, "g_BladeAt")
        info = open(gen + "/frames.bin", "rb").read()   # 8 bytes a frame (RomSprites)
        self.offset = [info[i] | info[i + 1] << 8 for i in range(0, len(info), 8)]
        self.chunk = [info[i + 2] for i in range(0, len(info), 8)]
        self.blocks = [info[i + 3] for i in range(0, len(info), 8)]

    def vale(self, frame, blade, left, top, ink=255):
        """Vale in a frame, his top-left at left, top, and his blade (-1 none) over him; inked
        in a flower's colours from player_inked_<ink>.bin (M8), 255 for his own."""
        out = {}
        n = self.player["PLAYER_FRAME_BYTES"]
        frames = self.frames if ink == 255 else self.inked(ink)
        for (x, y), v in layered(frames, frame * n, 2).items():
            out[(left + x, top + y)] = v
        hx, hy = self.hand[2 * frame], self.hand[2 * frame + 1]
        if blade >= 0 and hx >= 0:
            at = self.blade_at[4 * blade:4 * blade + 4]
            for k, ink in ((1, self.blade["BLADE_HILT"]), (0, self.blade["BLADE_INK"])):
                p = blade * self.blade["BLADE_SHAPE_BYTES"] + 32 * k
                for y in range(16):
                    for x in range(16):
                        if self.shapes[p + (x // 8) * 16 + y] & (0x80 >> (x % 8)):
                            out[(left + hx + at[2 * k] + x, top + hy + at[2 * k + 1] + y)] = ink
        return out

    def inked(self, i):
        if i not in self._inked:
            self._inked[i] = open(f"{self.gen}/player_inked_{i}.bin", "rb").read()
        return self._inked[i]

    def bank_frame(self, frame, left, top):
        cols, rows = self.blocks[frame] >> 4, self.blocks[frame] & 15
        return {(left + x, top + y): v
                for (x, y), v in layered(self.sprites[self.chunk[frame]], self.offset[frame], cols * rows, cols).items()}

    def clip(self, pixels):
        """To the playfield: the screen's width, and above the panel."""
        h = self.c["PLAYFIELD_H"]
        return {(x, y): v for (x, y), v in pixels.items() if 0 <= x < W and 0 <= y < h}
