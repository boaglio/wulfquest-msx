#!/usr/bin/env python3
"""Checks what test/shell.tcl saw (AGENTS.md §10, M10), and draws it.
    check_shell.py DUMP GEN SHOT_PREFIX

Each screen's words are read off it with the ROM's own fonts (font.bin): a
string is on screen where each of its characters' glyphs is, in one ink on
black. The strings looked for are shell.bin's (the data's, by shell_data.h's
offsets), so a word on screen that is not the data's is not found. The demo
must end as the Java game's does (demo_check.txt) and its map must be the
Java game's MapScreen, pixel for pixel (demo_map.bin). Pictures of every
screen go to SHOT_PREFIX-TAG.png.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_boot  # noqa: E402

W, H = 256, 212
GROUND = 15


def defines(path):
    text = open(path, encoding="utf-8").read()
    return {m[0]: int(m[1]) for m in re.findall(r"#define (\w+)\t+\(?(-?\d+)L?\)?(?=\s|$)", text)}


class Gen:
    def __init__(self, gen):
        self.p = defines(f"{gen}/panel_data.h")
        self.s = defines(f"{gen}/shell_data.h")
        self.c = defines(f"{gen}/constants.h")
        self.g = defines(f"{gen}/game_data.h")
        self.font = open(f"{gen}/font.bin", "rb").read()
        self.bin = open(f"{gen}/shell.bin", "rb").read()
        self.palette = check_boot.palette_from_header(f"{gen}/palette.h")
        self.demo = open(f"{gen}/demo_check.txt", encoding="ascii").read().split()
        self.demo_map = open(f"{gen}/demo_map.bin", "rb").read()
        self.sc = defines(f"{gen}/scenes_data.h")
        self.scenes = open(f"{gen}/scenes.bin", "rb").read()

    def scene_str(self, at):
        return self.scenes[at + 1:at + 1 + self.scenes[at]].decode("ascii"), at + 1 + self.scenes[at]

    def ending_rows(self):
        """The closing scene's lines' rows, as scenes.bin has them (wrapped to the screen)."""
        at, rows = self.sc["ENDING_LINES"], []
        for _ in range(self.sc["ENDING_LINES_N"]):
            n = self.scenes[at + 5]
            at += 6
            for _ in range(n):
                row, at = self.scene_str(at)
                rows.append(row)
        return rows

    def intro_rows(self):
        at, rows = self.sc["INTRO_TEXT"], []
        for _ in range(self.sc["INTRO_ROWS"]):
            row, at = self.scene_str(at)
            rows.append(row)
        return rows

    def roll_rows(self):
        """The credits roll's rows: (text, big font)."""
        at, rows = self.sc["ROLL_TEXT"], []
        for _ in range(self.sc["ROLL_ROWS"]):
            body = self.scenes[at]
            row, at = self.scene_str(at + 1)
            rows.append((row, not body))
        return rows

    def str(self, name):
        at = self.s[name]
        return self.bin[at + 1:at + 1 + self.bin[at]].decode("ascii")

    def glyph(self, ch, big):
        """The glyph's rows as sets of x: the big font's 8x8 or the small's 4x6."""
        if big:
            g = ord(ch) - self.p["FONT_BIG_FIRST"]
            g = g if 0 <= g < self.p["FONT_BIG_GLYPHS"] else 0
            rows = self.font[g * 8:g * 8 + 8]
            return [[x for x in range(8) if r & (0x80 >> x)] for r in rows], 8
        g = ord(ch) - self.p["FONT_SMALL_FIRST"]
        g = g if 0 <= g < self.p["FONT_SMALL_GLYPHS"] else 0
        at = self.p["FONT_SMALL_AT"] + g * 6
        rows = self.font[at:at + 6]
        return [[x for x in range(4) if r & (0x80 >> x)] for r in rows], 4


def pixels(vram, scroll=0):
    """The screen as shown: line s is the page's line (scroll + s) round 256 (R#23)."""
    rows = []
    for s in range(H):
        y = (scroll + s) & 255
        rows.append([(vram[y * 128 + x // 2] >> (4 if x % 2 == 0 else 0)) & 15 for x in range(W)])
    return rows


def shown_bytes(vram, scroll=0):
    return b"".join(vram[((scroll + s) & 255) * 128:((scroll + s) & 255) * 128 + 128] for s in range(H))


def find(gen, px, text, big):
    """Where text is drawn (x, y), in the font, one ink on black, or None."""
    glyphs = [gen.glyph(ch, big) for ch in text]
    adv = 8 if big else 4
    rows = 8 if big else 6
    first = next((i for i, ch in enumerate(text) if ch != " "), None)
    if first is None:
        return None
    for y in range(H - rows + 1):
        for x in range(0, W - adv * len(text) + 1, 2):
            ok = True
            ink = None
            for i, (g, w) in enumerate(glyphs):
                for gy in range(rows):
                    line = px[y + gy]
                    on = g[gy]
                    for gx in range(w):
                        c = line[x + i * adv + gx]
                        if gx in on:
                            if c == GROUND or (ink is not None and c != ink):
                                ok = False
                                break
                            ink = c
                        elif c != GROUND:
                            ok = False
                            break
                    if not ok:
                        break
                if not ok:
                    break
            if ok:
                return x, y
    return None


def read_dump(path):
    screens, logs, end = {}, [], None
    with open(path, encoding="ascii") as f:
        for line in f:
            parts = line.rstrip("\n").split(" ", 2)
            if parts[0] == "end":
                end = line[4:].strip()
            elif parts[0] == "log":
                logs.append(line[4:].split())
            else:
                screens.setdefault(parts[0], {})[parts[1]] = bytes.fromhex(parts[2])
    return screens, logs, end


def main(dump, gen_dir, shot):
    gen = Gen(gen_dir)
    screens, logs, end = read_dump(dump)
    name = os.path.basename(dump)
    problems = []
    if end != "ok":
        problems.append(f"the openMSX script stopped: {end}")
    s = gen.str
    big, small = True, False
    want = {
        "title": [(s("STR_TAGLINE"), small), (s("STR_START"), big), (s("STR_MODE_HARD"), small),
                  (s("STR_OPTIONS"), big), (s("STR_FOOTER"), small)],
        "options": [(s("STR_OPT_HEADING"), big), (s("STR_OPT_DIFFICULTY") + "  " + s("STR_MODE_HARD"), big),
                    (s("STR_OPT_KEYS") + "  " + s("STR_KEYS_0"), big), (s("STR_OPT_SOUND") + "  " + s("STR_ON"), big),
                    (s("STR_OPT_LOAD"), big), (s("STR_OPT_HISCORES"), big), (s("STR_OPT_BACK"), big),
                    (s("STR_OPT_FOOTER"), small)],
        "hiscores": [(s("STR_HISCORES"), big), (s("STR_PRESS_FIRE"), small)],
        "attract": [(s("STR_WORDMARK"), big), (s("STR_BANNER"), small)],
        "map": [(s("STR_MAP"), small)],
        "entry": [(s("STR_NEW_BEST"), big), (s("STR_ENTRY_PROMPT"), small)],
        "hiscores2": [(s("STR_HISCORES"), big)],
        "pause": [(s("STR_PAUSE_HEADING"), big), (s("STR_PAUSE_FOOTER"), small)]
                 + [(s(n), big) for n in ("STR_PAUSE_RESUME", "STR_PAUSE_SAVE", "STR_PAUSE_LOAD",
                                         "STR_PAUSE_OPTIONS", "STR_PAUSE_QUIT")],
        "ingameopt": [(s("STR_OPT_HEADING"), big), (s("STR_OPT_SOUND") + "  " + s("STR_ON"), big),
                      (s("STR_OPT_BACK"), big)],
        "map2": [(s("STR_MAP"), small)],
        "reveal": [(s(f"STR_VERSE_0_{l}"), big) for l in range(gen.s["VERSE_LINES"])],
        "reveal2": [(s("STR_REVEAL_PROMPT"), big)],
        "gameover": [(s("STR_GAME_OVER"), big), (s("STR_PROGRESS_B"), big), (s("STR_CONTINUE"), big),
                     (s("STR_CONTINUES_A") + str(gen.g["CONTINUES_START"]) + s("STR_CONTINUES_B"), big)],
        "intro": [(r, big) for r in gen.intro_rows()],
        "ending2": [(r, big) for r in gen.ending_rows()] + [(gen.scene_str(gen.sc["STR_ENDING_PROMPT"])[0], big)],
        "joyhiscores": [(s("STR_HISCORES"), big)],
        "joymap": [(s("STR_MAP"), small)],
    }
    # The hi-score table a new machine has, row by row; and the run the demo put in it.
    t = gen.s["HISCORE_DEFAULTS"]
    for i in range(gen.s["HISCORE_SIZE"]):
        row = gen.bin[t + 13 * i:t + 13 * (i + 1)]
        score = int.from_bytes(row[3:7], "little")
        want["hiscores"] += [(row[:3].decode("ascii"), small), (str(score), small)]
    demo = dict(zip(gen.demo[1::2], gen.demo[2::2]))
    alphabet = s("STR_ALPHABET")
    want["hiscores2"] += [(alphabet[2] + alphabet[0] + alphabet[0], small), (demo["score"], small)]
    want["entry"] += [(s("STR_ENTRY_SCORE") + demo["score"] + s("STR_ENTRY_PLACE"), small)]

    # The tally: its heading, each label and its figure, the total (the win's own sums, logged).
    tl = next((l for l in logs if l[0] == "tally"), None)
    if tl:
        before, escape, time, lives, total = (int(v) for v in tl[2:7])
        if before + escape + time + lives != total:
            problems.append(f"tally: {before} + {escape} + {time} + {lives} is not the score {total}")
        labels = [gen.scene_str(gen.sc[f"STR_TALLY_LABEL_{i}"])[0] for i in range(gen.sc["TALLY_LABELS"])]
        want["tally"] = [(gen.scene_str(gen.sc["STR_TALLY_HEADING"])[0], big)] \
            + [(t, big) for t in labels] + [(str(v), big) for v in (before, escape, time, lives, total)] \
            + [(gen.scene_str(gen.sc["STR_TALLY_TOTAL"])[0], big), (gen.scene_str(gen.sc["STR_TALLY_PROMPT"])[0], big)]
        # TallyScreen: "a new best" when the total is the table's best or more (the table a new machine has).
        t = gen.s["HISCORE_DEFAULTS"]
        best = int.from_bytes(gen.bin[t + 3:t + 7], "little")
        if total >= best:
            want["tally"].append((gen.scene_str(gen.sc["STR_TALLY_NEW_BEST"])[0], big))
    else:
        problems.append("the tally's sums were not logged")
    for tag, items in want.items():
        if tag not in screens:
            problems.append(f"no screen '{tag}'")
            continue
        sc = screens[tag]
        check_boot.write_png(f"{shot}-{tag}.png", sc["vram"], sc["palette"])
        px = pixels(sc["vram"])
        for text, font in items:
            if find(gen, px, text, font) is None:
                problems.append(f"{tag}: \"{text}\" is not on screen ({'big' if font else 'small'} font)")
    # The credits roll: every row of it seen on screen as it rose (each dump as shown, R#23).
    rolls = sorted((t for t in screens if t.startswith("roll")), key=lambda t: int(t[4:]))
    if not rolls:
        problems.append("no screen of the credits roll")
    unseen = [r for r in gen.roll_rows() if r[0].strip()]
    for tag in rolls:
        sc = screens[tag]
        scroll = sc["regs"][23]
        check_boot.write_png(f"{shot}-{tag}.png", shown_bytes(sc["vram"], scroll), sc["palette"])
        px = pixels(sc["vram"], scroll)
        unseen = [r for r in unseen if find(gen, px, r[0], r[1]) is None]
    for text, font in unseen:
        problems.append(f"roll: \"{text}\" was never on screen ({'big' if font else 'small'} font)")
    for tag in ("title", "game"):
        if tag in screens:
            check_boot.write_png(f"{shot}-{tag}.png", screens[tag]["vram"], screens[tag]["palette"])

    # The attract demo: the palette dimmed, and the game where the Java game's demo ends.
    if "attract" in screens and screens["attract"]["palette"] == gen.palette:
        problems.append("attract: the palette is not dimmed")
    if "title" in screens and screens["title"]["palette"] != gen.palette:
        problems.append("title: the palette is not the game's")
    got = next((l for l in logs if l[0] == "demo"), None)
    if got is None:
        problems.append("the demo's end was not seen")
    else:
        ticks, room, x, y, score, lives = (int(v) for v in got[2:8])
        x = x - (1 << 32) if x >= 1 << 31 else x
        y = y - (1 << 32) if y >= 1 << 31 else y
        mine = {"ticks": ticks, "room": room, "x": x, "y": y, "score": score, "lives": lives}
        for k, v in mine.items():
            if str(v) != demo[k]:
                problems.append(f"demo: {k} {v}, the Java game's demo ends with {demo[k]}")
    # The map over the demo's game: the Java game's MapScreen.
    if "map" in screens:
        px = pixels(screens["map"]["vram"])
        fh = gen.c["PLAYFIELD_H"]
        bad = [(x, y) for y in range(fh) for x in range(W) if px[y][x] != gen.demo_map[y * W + x]]
        if bad:
            problems.append(f"map: {len(bad)} pixels differ from the Java game's map, first at {bad[0]}")
    cont = next((l for l in logs if l[0] == "continued"), None)
    if cont is None or cont[2] != "1":
        problems.append(f"game over: the continue was not taken ({cont})")
    for what in ("keys", "joystick"):
        if not any(l[0] == what for l in logs):
            problems.append(f"the {what} run did not finish")

    for p in problems:
        print(f"FAIL {name}: {p}")
    if not problems:
        print(f"ok   {name}: {len(want)} screens, the demo ({demo['ticks']} ticks) and its map as the Java game's; "
              f"the intro, the ending, the tally, every row of the credits roll ({len(gen.roll_rows())} rows)")
    return 1 if problems else 0


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    sys.exit(main(*sys.argv[1:]))
