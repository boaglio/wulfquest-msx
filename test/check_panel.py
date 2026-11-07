#!/usr/bin/env python3
"""Checks the panel test/panel.tcl saw against the panel as it should be (AGENTS.md §10, M9).

    check_panel.py DUMP GEN

For each case, the panel's lines from video RAM must be, pixel for pixel, the
panel drawn here from the converter's font.bin and panel.bin, as panel.c lays
it out: SCORE and the score in bright white, HI and the best in white, LIVES
and a head a life (five at most, then X and the count), EASY, the amulet's
quarters (held or dim), and the bottom line (a message centred, or the
flower's bar, its fill what is left of it).
"""
import os
import re
import sys

W = 256


def defines(path):
    return {k: int(v) for k, v in re.findall(r"#define (\w+)\s+(-?\d+)\b", open(path).read())}


class Panel:
    def __init__(self, gen):
        self.d = defines(gen + "/panel_data.h")
        self.c = defines(gen + "/constants.h")
        self.font = open(gen + "/font.bin", "rb").read()
        self.blob = open(gen + "/panel.bin", "rb").read()
        self.inks = list(map(int, re.search(r"#define ORCHID_INKS\s+([\d, ]+)", open(gen + "/panel_data.h").read())
                             .group(1).split(",")))
        self.hints = re.search(r"#define STR_HINTS\s+(.+)", open(gen + "/panel_data.h").read()).group(1).split(", ")
        self.top = self.c["PLAYFIELD_H"]
        self.px = [[self.d["PANEL_GROUND"]] * W for _ in range(212 - self.top)]

    def string(self, at):
        n = self.blob[at]
        return self.blob[at + 1:at + 1 + n].decode("ascii")

    def glyph(self, x, y, ch, ink, big=True):
        d = self.d
        if big:
            g = ord(ch) - d["FONT_BIG_FIRST"]
            g = g if 0 <= g < d["FONT_BIG_GLYPHS"] else 0
            rows, w, at = 8, 8, g * 8
        else:
            g = ord(ch) - d["FONT_SMALL_FIRST"]
            g = g if 0 <= g < d["FONT_SMALL_GLYPHS"] else 0
            rows, w, at = d["FONT_SMALL_H"], 4, d["FONT_SMALL_AT"] + g * d["FONT_SMALL_H"]
        for r in range(rows):
            b = self.font[at + r]
            for i in range(w):
                self.px[y - self.top + r][x + i] = ink if b & (0x80 >> i) else d["PANEL_GROUND"]

    def text(self, x, y, s, ink, big=True):
        for i, ch in enumerate(s):
            self.glyph(x + i * (8 if big else 4), y, ch, ink, big)

    def image(self, x, y, index):
        at = index * 32
        for r in range(8):
            for i in range(8):
                b = self.blob[at + r * 4 + i // 2]
                self.px[y - self.top + r][x + i] = b >> 4 if i % 2 == 0 else b & 15

    def fill(self, x, y, w, h, colour):
        for r in range(h):
            for i in range(w):
                self.px[y - self.top + r][x + i] = colour

    def draw(self, s):
        d, top = self.d, self.top
        a, b, c = top + 2, top + 14, top + 28
        self.text(4, a, self.string(d["STR_SCORE"]) + str(s["score"]).zfill(d["SCORE_DIGITS"]), d["PANEL_BRIGHT"])
        self.text(4 + 21 * 8, a, self.string(d["STR_HI"]) + str(s["best"]).zfill(d["SCORE_DIGITS"]), d["PANEL_INK"])
        self.text(4, b, self.string(d["STR_LIVES"]), d["PANEL_INK"])
        for i in range(min(s["lives"], d["LIVES_SHOWN"])):
            self.image(48 + 8 * i, b, d["IMAGE_HEAD"])
        if s["lives"] > d["LIVES_SHOWN"]:
            self.text(88, b, "X" + str(s["lives"]), d["PANEL_INK"])
        if s["easy"]:
            self.text(220, b, self.string(d["STR_EASY"]), d["PANEL_EASY_INK"])
        for k in range(4):
            held = s["slots"] & (1 << k)
            self.image((W - 16) // 2 + (k & 1) * 8, top + 10 + (k >> 1) * 8,
                       (d["IMAGE_QUARTER"] if held else d["IMAGE_QUARTER_DIM"]) + k)
        msg = None
        if s.get("over"):
            msg = self.string(d["STR_GAME_OVER"])
        elif s.get("hint") is not None:
            msg = self.string(d[self.hints[s["hint"]]])
        if msg is not None:
            x = ((W - len(msg) * 4) // 2) & 0xFE
            self.text(x, c, msg, d["PANEL_BRIGHT"], big=False)
        elif s.get("orchid") is not None:
            w = d["EFFECT_BAR_W"]
            x = (W - w) // 2
            self.fill(x, c, w, 6, d["PANEL_DIM"])
            self.fill(x + 1, c + 1, w - 2, 4, d["PANEL_GROUND"])
            per_mille = min(1000, s["left"] * 1000 // s["total"])
            fill = (w - 2) * per_mille // 1000
            self.fill(x + 1, c + 1, fill, 4, self.inks[s["orchid"]])
        return self.px


def main(dump, gen, cases_json):
    import json
    cases = json.loads(cases_json)
    names = [l.split()[1] for l in open(dump).read().split("\n") if l.startswith("case ")]
    final = [l for l in open(dump).read().split("\n") if l.startswith("end ")]
    raw = open(dump + ".bin", "rb").read()
    problems = []
    if not final or final[0] != "end ok":
        problems.append(f"the run did not finish: {final}")
    p0 = Panel(gen)
    size = (212 - p0.top) * 128
    for i, name in enumerate(names):
        page = raw[i * size:(i + 1) * size]
        if len(page) < size:
            break
        got = [[(page[r * 128 + x // 2] >> (0 if x % 2 else 4)) & 15 for x in range(W)] for r in range(212 - p0.top)]
        want = Panel(gen).draw(cases[name])
        bad = sum(1 for r in range(len(got)) for x in range(W) if got[r][x] != want[r][x])
        if bad:
            first = next((x, r + p0.top) for r in range(len(got)) for x in range(W) if got[r][x] != want[r][x])
            problems.append(f"{name}: {bad} pixels differ, the first at {first}")
    for p in problems:
        print(f"FAIL panel: {p}")
    print(f"{'FAIL' if problems else 'ok  '} panel: {len(names)} cases, each the panel as it should be")
    return 1 if problems else 0


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    sys.exit(main(*sys.argv[1:]))
