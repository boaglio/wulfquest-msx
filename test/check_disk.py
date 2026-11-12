#!/usr/bin/env python3
"""Checks what test/disk.sh saw (AGENTS.md §10, M11), and draws it.
    check_disk.py DUMP GEN SHOT_PREFIX

Two runs of the machine with one disk: the first must have found the disk
(g_DiskReady), saved and left a game; the second, after the power cycle, must
offer it first on the title, with its line as the first run's title had it,
and load it back as it was left. Words are read off the screens as
check_shell.py reads them, and must be shell.bin's (check_saves.filled).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_boot  # noqa: E402
import check_saves  # noqa: E402
import check_shell  # noqa: E402


def main(dump, gen_dir, shot):
    gen = check_shell.Gen(gen_dir)
    screens, logs, end = check_shell.read_dump(dump)
    problems = []
    if end != "ok":
        problems.append(f"the openMSX script stopped: {end}")
    ready = [l[2] for l in logs if l[0] == "ready"]
    if ready != ["1", "1"]:
        problems.append(f"the disk was not found ready in both runs: {ready}")
    state = {l[0]: [int(v) for v in l[2:]] for l in logs if l[0] in ("quit", "loaded")}
    quit = state.get("quit")
    slot2 = {"quarters": quit[6], "score": quit[3], "room": quit[0]} if quit else None
    s = gen.str
    big, small = True, False
    offered = [(s("STR_CONTINUE_LINE"), big), (s("STR_NEW_GAME"), big)] \
        + ([(check_saves.filled(gen, "STR_T_CONTINUE_DETAIL", 2, slot2), small)] if slot2 else [])
    want = {
        "title0": [(s("STR_START"), big), (s("STR_OPTIONS"), big)],
        "newgame": [(s("STR_NEW_GAME_HEADING"), big)]
                   + [(check_saves.filled(gen, "STR_T_EMPTY", n), big) for n in (1, 2, 3)],
        "saved": [(check_saves.filled(gen, "STR_T_SAVED", 2), small)],
        "title1": offered,
        "title2": offered,
        "loaded": [(check_saves.filled(gen, "STR_T_LOADED", 2), small)],
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
    for p in problems:
        print(f"FAIL disk: {p}")
    if not problems:
        print(f"ok   disk: the NMS 8245's drive: a game saved to the disk, the machine off and on, "
              f"offered first and loaded as it was (room {quit[0]}, score {quit[3]}, clock {quit[5]})")
    return 1 if problems else 0


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    sys.exit(main(*sys.argv[1:]))
