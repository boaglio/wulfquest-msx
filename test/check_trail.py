#!/usr/bin/env python3
"""Checks what test/trail.sh's run of test/shell.tcl saw (AGENTS.md §10, M12), and draws it.
    check_trail.py DUMP GEN SHOT_PREFIX MARCH_TICKS

The map over the demo's game with the looking glass's way showing: every dot
of the trail the converter's Trail finds (demo_trail.txt: trail.c's
reference), MapScreen.paintTrail's every third pixel, of one phase; the cross
where it ends when his room's frame shows; no trail ink anywhere else but
where the Java game's map has that ink too; and the second dump, a little
later, a march step or two on.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_boot  # noqa: E402
import check_shell  # noqa: E402


def main(dump, gen_dir, shot, march):
    march = int(march)
    gen = check_shell.Gen(gen_dir)
    screens, logs, end = check_shell.read_dump(dump)
    name = os.path.basename(dump)
    problems = []
    if end != "ok":
        problems.append(f"the openMSX script stopped: {end}")
    lines = open(f"{gen_dir}/demo_trail.txt", encoding="ascii").read().split("\n")
    head = lines[0].split()
    info = dict(zip(head[1::2], (int(v) for v in head[2::2])))
    route = [tuple(int(v) for v in l.split()) for l in lines[1:] if l.strip()]
    ink = gen.s["INK_MAP_TRAIL"]
    every = gen.s["MAP_TRAIL_DOT_EVERY"]
    fh = gen.c["PLAYFIELD_H"]
    W = 256
    found = next((l for l in logs if l[0] == "found"), None)
    if found is None:
        problems.append("the trail was not found")
    else:
        ticks, n, whole = (int(v) for v in found[2:5])
        if n != len(route) or whole != info["whole"]:
            problems.append(f"the ROM found {n} pixels (whole {whole}); Trail finds {len(route)} (whole {info['whole']})")
    # Left mid-search: his room's cells as they were; opened again: the openings' steps kept.
    got = {l[0]: l for l in logs}
    if "cells0" not in got or "cells1" not in got:
        problems.append("the map was not left mid-search")
    else:
        if got["cells0"][2] != got["cells1"][2]:
            problems.append("left mid-search, Vale's room's cells are not as they were")
        if got["cells1"][3] != "0":
            problems.append(f"left mid-search, the search did not stop (stage {got['cells1'][3]})")
    if "again" not in got or int(got["again"][2]) < 3:
        problems.append(f"opened again, the openings' steps were worked out again ({got.get('again')})")
    java_map = [[gen.demo_map[y * W + x] for x in range(W)] for y in range(fh)]
    phases = []
    for tag in ("trail1", "trail2"):
        if tag not in screens:
            problems.append(f"no screen '{tag}'")
            continue
        check_boot.write_png(f"{shot}-{tag}.png", screens[tag]["vram"], screens[tag]["palette"])
        px = check_shell.pixels(screens[tag]["vram"])
        best = max(range(every), key=lambda p: sum(px[v][u] == ink for i, (u, v) in enumerate(route) if i % every == p))
        phases.append(best)
        want = {(u, v) for i, (u, v) in enumerate(route) if i % every == best}
        u, v = route[-1]
        cross = {(u + d, v) for d in range(-2, 3)} | {(u, v + d) for d in range(-2, 3)}
        cross = {(x, y) for x, y in cross if 0 <= x < W and 0 <= y < fh}
        cross_on = info["whole"] and all(px[y][x] == ink for x, y in cross)
        if cross_on:
            want |= cross
        missing = [(x, y) for x, y in want if px[y][x] != ink]
        # His room's frame is drawn over the trail.
        here = gen.s["INK_MAP_HERE"]
        missing = [(x, y) for x, y in missing if px[y][x] != here]
        if missing:
            problems.append(f"{tag}: {len(missing)} of the trail's {len(want)} dots are not there, first {missing[0]}")
        stray = [(x, y) for y in range(fh) for x in range(W)
                 if px[y][x] == ink and (x, y) not in want and (x, y) not in cross and java_map[y][x] != ink]
        if stray:
            problems.append(f"{tag}: {len(stray)} pixels of the trail's ink off it, first {stray[0]}")
    if len(phases) == 2 and phases[0] == phases[1]:
        problems.append(f"the trail did not march: phase {phases[0]} in both dumps")
    for p in problems:
        print(f"FAIL {name}: {p}")
    if not problems:
        print(f"ok   {name}: the trail Trail finds, {len(route)} pixels to the quarter, dot for dot and marching "
              f"(found in {ticks} ticks); left mid-search and opened again; {100 * info['shared'] // max(1, info['java'])}% of the Java game's way "
              f"({info['java']} pixels)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
