#!/usr/bin/env python3
"""The sprites on the MSX2 (AGENTS.md §10, M5): plans what test/sprites.tcl shows, then checks it.

    check_sprites.py plan GEN > PLAN
    check_sprites.py check GEN PLAN DUMP

The plan is pages of the ROM's test gallery (wulfquest.c, g_TestGallery):
"page VFRAME BLADE VX VY N FRAME X Y ...": Vale in a frame of player_frames.bin
with a blade shape (-1 none) at VX, VY (the frame's top-left on the screen),
and N frames of the sprite bank at their top-lefts. Between them they show
every frame of Vale, every blade shape, every frame of the bank, Vale partly
off the left edge (the early clock) and partly over the panel (the clip).
Then "turns K ...": one layout held for K frames, Vale, his blade and five
small creatures on the same lines, 14 sprites where the V9938 draws 8.

DUMP is sprites.tcl's: per page or turn frame a line "shown" and the sprite
tables in DUMP.spr (attributes 128 bytes, colours 512, patterns 2048), and
"bench OP W H US" lines. Each page, drawn as the V9938 draws it
(vdp_sprites.py), must be exactly what the converter cut (build/gen/); in
the turns, Vale and his blade must be whole in every frame, and each creature
whole in some of them, taking turns; and the ROM must have counted the
crowded frames.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vdp_sprites  # noqa: E402

TABLES = 128 + 512 + 2048
# The ROM's slots (src/sprites.h): SPR_SMALL of one block, then SPR_BIG of up to 2x2.
_SPRITES_H = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "sprites.h")).read()
SMALL = int(re.search(r"#define SPR_SMALL\s+(\d+)", _SPRITES_H).group(1))
BIG = int(re.search(r"#define SPR_BIG\s+(\d+)", _SPRITES_H).group(1))
TURN_FRAMES = 10


def plan(gen):
    g = vdp_sprites.Gen(gen)
    frames = g.player["PLAYER_FRAMES"]
    shapes = g.blade["BLADE_SHAPES"]
    bank = g.bank["SPRITE_FRAMES"]
    hand = [f for f in range(frames) if g.hand[2 * f] >= 0]
    pages = []
    # Every frame of Vale, a blade with each that holds one; then every shape the blade has.
    for f in range(frames):
        pages.append([f, f % shapes if f in hand else -1, 100, 12])
    for s in range(shapes):
        pages.append([hand[s % len(hand)], s, 100, 12])
    # The bank's frames under Vale on each page: one-block ones in the small slots, 3 a row
    # (6 sprites a line), bigger ones in the 5 big slots, 2 a row (8 a line), below, as many
    # as the 32 sprites hold: more would take turns, and a page is one frame.
    small = [i for i in range(bank) if g.blocks[i] == 0x11]
    big = [i for i in range(bank) if g.blocks[i] != 0x11]
    p = 0
    while small or big:
        if p >= len(pages):
            pages.append([0, -1, 100, 12])
        objs = []
        for k in range(SMALL):
            if small:
                objs.append((small.pop(0), 16 + (k % 3) * 40, 54 + (k // 3) * 18))
        while len(objs) < SMALL:
            objs.append(None)
        # As many as the 32 sprites hold with Vale's 4, the blade's 2 and the small slots' 12.
        room = 32 - 4 - 2 - 2 * SMALL
        for k in range(BIG):
            n = 2 * (g.blocks[big[0]] >> 4) * (g.blocks[big[0]] & 15) if big else 0
            if big and n <= room:
                room -= n
                objs.append((big.pop(0), 16 + (k % 2) * 48, 112 + (k // 2) * 30))
        pages[p] += [o for o in objs]
        p += 1
    # Partly off the left edge, and partly over the panel.
    pages.append([hand[0], 0, -6, 60, (0, -9, 100), (1, 230, 166)])
    pages.append([0, -1, 40, 160, (2, -15, 120)])
    pages.append([hand[1], 5, 200, 150])
    out = []
    for p in pages:
        head, objs = p[:4], p[4:]
        # An empty small slot: a frame held far off the screen.
        objs = [o if o is not None else (0, -200, -200) for o in objs]
        while objs and objs[-1] == (0, -200, -200):
            objs.pop()
        out.append("page " + " ".join(map(str, head)) + f" {len(objs)} " + " ".join(f"{f} {x} {y}" for f, x, y in objs))
    # Turns: 14 sprites on lines 88..103.
    objs = [(i, 80 + 24 * i, 88) for i in range(5)]
    out.append(f"turns {TURN_FRAMES} {hand[2]} 9 20 80 5 " + " ".join(f"{f} {x} {y}" for f, x, y in objs))
    return "\n".join(out) + "\n"


def parse(line):
    f = line.split()
    vf, blade, vx, vy, n = map(int, f[1:6])
    objs = [tuple(map(int, f[6 + 3 * i:9 + 3 * i])) for i in range(n)]
    return vf, blade, vx, vy, objs


def check(gen, plan_path, dump):
    g = vdp_sprites.Gen(gen)
    lines = open(plan_path, encoding="utf-8").read().split("\n")
    pages = [parse(x) for x in lines if x.startswith("page ")]
    turns = [x for x in lines if x.startswith("turns ")]
    out = open(dump, encoding="utf-8").read().split("\n")
    tables = open(dump + ".spr", "rb").read()
    problems, notes = [], []
    if not any(x == "end ok" for x in out):
        problems.append(f"the run did not finish: {[x for x in out if x.startswith('end')]}")
    shown = [x for x in out if x.startswith("shown")]
    ph = g.c["PLAYFIELD_H"]

    def got(i):
        t = tables[i * TABLES:(i + 1) * TABLES]
        return vdp_sprites.render(t[:128], t[128:640], t[640:], lines=212)

    def want(vf, blade, vx, vy, objs):
        w = {}
        for f, x, y in reversed(objs):
            w.update(g.bank_frame(f, x, y))
        w.update(g.vale(vf, blade, vx, vy))
        return g.clip(w)

    for i, p in enumerate(pages):
        if i >= len(shown):
            problems.append(f"page {i} was not shown")
            break
        a, b = got(i), want(*p)
        if a != b:
            right = len(set(a.items()) & set(b.items()))
            problems.append(f"page {i} (Vale {p[0]}, blade {p[1]} at {p[2]},{p[3]}; bank {[o[0] for o in p[4]]}): "
                            f"{len(a)} pixels shown, {len(b)} wanted, {right} right")
    frames = g.player["PLAYER_FRAMES"]
    bank = {o[0] for p in pages for o in p[4]}
    blades = {p[1] for p in pages if p[1] >= 0}
    notes.append(f"{len(pages)} pages: Vale's {len({p[0] for p in pages})} of {frames} frames, "
                 f"{len(blades)} of {g.blade['BLADE_SHAPES']} blade shapes, "
                 f"{len(bank)} of {g.bank['SPRITE_FRAMES']} bank frames, the early clock and the clip, each as cut")

    # Turns.
    for t in turns:
        k = int(t.split()[1])
        vf, blade, vx, vy, objs = parse(" ".join(t.split()[1:]))
        base = len(pages)
        whole = [0] * len(objs)
        vale_ok = 0
        for j in range(k):
            if base + j >= len(shown):
                problems.append("the turns were not all shown")
                break
            a = got(base + j)
            v = g.clip(g.vale(vf, blade, vx, vy))
            vale_ok += all(a.get(xy) == c for xy, c in v.items())
            for n, (f, x, y) in enumerate(objs):
                w = g.clip(g.bank_frame(f, x, y))
                whole[n] += all(a.get(xy) == c for xy, c in w.items())
        if vale_ok != k:
            problems.append(f"turns: Vale and his blade whole in {vale_ok} of {k} frames, not all")
        if min(whole) < 2:
            problems.append(f"turns: the creatures were whole in {whole} of {k} frames; each should be in turn")
        crowded = [int(x.split()[1]) for x in out if x.startswith("crowded")]
        if not crowded or crowded[0] < k:
            problems.append(f"turns: the ROM counted {crowded[0] if crowded else 0} crowded frames of {k}")
        notes.append(f"14 sprites on a line: Vale and the blade whole in every frame, the 5 creatures whole in "
                     f"{whole} of {k} frames, taking turns")

    bench = [x.split() for x in out if x.startswith("bench")]
    if bench:
        notes.append("the blitter: " + ", ".join(f"{'restore' if b[1] == '0' else 'draw'} {b[2]}x{b[3]} {b[4]} us"
                                                for b in bench))
    for p in problems[:30]:
        print(f"FAIL sprites: {p}")
    print(f"{'FAIL' if problems else 'ok  '} sprites: " + "; ".join(notes))
    return 1 if problems else 0


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "plan":
        sys.stdout.write(plan(sys.argv[2]))
    elif len(sys.argv) == 5 and sys.argv[1] == "check":
        sys.exit(check(*sys.argv[2:]))
    else:
        sys.exit(__doc__)
