#!/usr/bin/env python3
"""Checks what test/saves.sh saw (AGENTS.md §10, M11), and draws it.
    check_saves.py DUMP GEN SHOT_PREFIX

Each screen's words, read off it with the ROM's fonts (check_shell.find),
must be shell.bin's, its templates filled as MenuScreen fills them; and the
game continued from its slot must be the game that was left: the same room,
place, score, lives, quarters, rooms walked and treasures, its clock a few
ticks on at most.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_boot  # noqa: E402
import check_shell  # noqa: E402


def filled(gen, name, n, slot=None):
    """Template name filled for slot n (slot: its quarters and score), tidied as MenuScreen does."""
    at = gen.s[name]
    raw = gen.bin[at + 1:at + 1 + gen.bin[at]]
    out = ""
    for c in raw:
        if c == gen.s["TF_N"]:
            out += str(n)
        elif c == gen.s["TF_MODE"]:
            out += gen.str("STR_MODE_NAME_HARD")
        elif c == gen.s["TF_JOB"]:
            out += gen.str("STR_JOB_0")
        elif c == gen.s["TF_QUARTERS"]:
            out += str(slot["quarters"])
        elif c == gen.s["TF_SCORE"]:
            out += str(slot["score"])
        elif c == gen.s["TF_ROOM"]:
            out += f"{slot['room'] & 15},{slot['room'] >> 4}"
        elif c == gen.s["TF_LINE"]:
            out += filled(gen, "STR_T_SLOT", n, slot)
        elif c < 32:
            pass
        else:
            out += chr(c)
    while "   " in out:
        out = out.replace("   ", "  ")
    return out.strip()


def main(dump, gen_dir, shot):
    gen = check_shell.Gen(gen_dir)
    screens, logs, end = check_shell.read_dump(dump)
    problems = []
    if end != "ok":
        problems.append(f"the openMSX script stopped: {end}")
    state = {l[0]: [int(v) for v in l[2:]] for l in logs if l[0] in ("quit", "loaded")}
    big, small = True, False
    s = gen.str
    quit = state.get("quit")
    slot2 = {"quarters": quit[6], "score": quit[3], "room": quit[0]} if quit else None
    start = {"quarters": 0, "score": 0, "room": 0}
    want = {
        "title0": [(s("STR_START"), big), (s("STR_OPTIONS"), big)],
        "newgame": [(s("STR_NEW_GAME_HEADING"), big)]
                   + [(filled(gen, "STR_T_EMPTY", n), big) for n in (1, 2, 3)],
        "savepage": [(s("STR_SAVE_HEADING"), big), (filled(gen, "STR_T_SLOT", 1, start), big),
                     (filled(gen, "STR_T_EMPTY", 2), big)],
        "saved": [(filled(gen, "STR_T_SAVED", 2), small)],
        "title1": [(s("STR_CONTINUE_LINE"), big), (s("STR_NEW_GAME"), big), (s("STR_OPTIONS"), big)]
                  + ([(filled(gen, "STR_T_CONTINUE_DETAIL", 2, slot2), small)] if slot2 else []),
        "loaded": [(filled(gen, "STR_T_LOADED", 2), small)],
        "damaged": [(s("STR_WHY_DAMAGED"), small)],
    }
    for tag, items in want.items():
        if tag not in screens:
            problems.append(f"no screen '{tag}'")
            continue
        sc = screens[tag]
        check_boot.write_png(f"{shot}-{tag}.png", sc["vram"], sc["palette"])
        px = check_shell.pixels(sc["vram"])
        for text, font in items:
            if check_shell.find(gen, px, text, font) is None:
                problems.append(f"{tag}: \"{text}\" is not on screen ({'big' if font else 'small'} font)")
    if "quit" not in state or "loaded" not in state:
        problems.append("the game left and the game loaded were not both seen")
    else:
        q, l = state["quit"], state["loaded"]
        names = ["room", "x", "y", "score", "lives", "tick", "quarters", "rooms walked", "treasures"]
        for i, n in enumerate(names):
            if n == "tick":
                if not 0 <= l[i] - q[i] <= 20:
                    problems.append(f"loaded: the clock is at {l[i]}, the game left at {q[i]}")
            elif q[i] != l[i]:
                problems.append(f"loaded: {n} {l[i]}, the game left with {q[i]}")
    if not any(l[0] == "saves" for l in logs):
        problems.append("the run did not finish")
    for p in problems:
        print(f"FAIL saves: {p}")
    if not problems:
        print(f"ok   saves: {len(want)} screens; a game saved, left and continued as it was "
              f"(room {quit[0]}, score {quit[3]}, clock {quit[5]}); a bad slot refused")
    return 1 if problems else 0


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    sys.exit(main(*sys.argv[1:]))
