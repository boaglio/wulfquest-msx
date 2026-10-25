#!/usr/bin/env python3
"""Checks what test/rooms.tcl saw against the Java game's drawing (AGENTS.md §10, M2).

    check_rooms.py DUMP EXPECTED CONSTANTS_H PALETTE_H SHOT_PREFIX

DUMP.txt has a line a drawing ("room frames page") and an "end" line;
DUMP.bin the shown page of each, 212 lines of 128 bytes. EXPECTED is
RenderRooms's output: the 256 playfields, 176 lines each. The machine is a
50 Hz MSX2, so a frame is 20 ms.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_boot import palette_from_header, write_png  # noqa: E402

LINE = 128
SHOWN = 212
FRAME_MS = 20
LIMIT_MS = 500


def constants(path):
    text = open(path, encoding="utf-8").read()
    return {m[0]: int(m[1].strip("()")) for m in re.findall(r"#define (\w+)\t(\(?-?\d+\)?)", text)}


def main(dump, expected_path, constants_h, palette_h, shot):
    c = constants(constants_h)
    field = c["PLAYFIELD_H"] * LINE
    grid_w = c["GRID_W"]
    rooms = grid_w * c["GRID_H"]
    start = c["START_ROW"] * grid_w + c["START_COL"]
    expected = open(expected_path, "rb").read()
    lines = open(dump + ".txt", encoding="utf-8").read().split("\n")
    shown = open(dump + ".bin", "rb").read()
    palette = palette_from_header(palette_h)
    name = lambda r: f"{r % grid_w},{r // grid_w}"

    problems = []
    end = [l for l in lines if l.startswith("end ")]
    if end != ["end ok"]:
        problems.append(f"the run did not finish: {end}")
    draws = [tuple(map(int, l.split())) for l in lines if l and not l.startswith("end ")]
    if len(expected) != rooms * field:
        problems.append(f"{expected_path} is {len(expected)} bytes, not {rooms} rooms")
    if draws and draws[0][0] != start:
        problems.append(f"the first room shown is {name(draws[0][0])}, not the start room {name(start)}")

    seen, wrong = set(), []
    for i, (room, frames, page) in enumerate(draws):
        vram = shown[i * SHOWN * LINE:(i + 1) * SHOWN * LINE]
        want = expected[room * field:(room + 1) * field]
        if vram[:field] != want:
            wrong.append(room)
            if len(wrong) == 1:
                at = next(k for k in range(field) if vram[k] != want[k])
                problems.append(f"room {name(room)} differs from the Java game's from pixel "
                                f"{at % LINE * 2},{at // LINE}")
                write_png(f"{shot}-{room % grid_w}-{room // grid_w}-msx.png", vram, palette, SHOWN)
                write_png(f"{shot}-{room % grid_w}-{room // grid_w}-java.png", want, palette, c["PLAYFIELD_H"])
        if any(vram[field:]):
            problems.append(f"room {name(room)}: the panel area below the playfield is not blank")
        seen.add(room)
        if room == start:
            write_png(f"{shot}-{room % grid_w}-{room // grid_w}-msx.png", vram, palette, SHOWN)
            write_png(f"{shot}-{room % grid_w}-{room // grid_w}-java.png", want, palette, c["PLAYFIELD_H"])
    if wrong:
        problems.append(f"{len(wrong)} drawings differ from the Java game's")
    if len(seen) != rooms:
        problems.append(f"only {len(seen)} of {rooms} rooms were drawn")

    worst = max(draws, key=lambda d: d[1]) if draws else (0, 0, 0)
    ms = worst[1] * FRAME_MS
    if ms >= LIMIT_MS:
        problems.append(f"room {name(worst[0])} takes {ms} ms to draw; the limit is {LIMIT_MS}")
    times = sorted(d[1] for d in draws)
    summary = (f"{len(seen)} rooms, {len(draws) - len(wrong)} of {len(draws)} drawings equal to the Java "
               f"game's; slowest {name(worst[0])} at {ms} ms ({worst[1]} frames), "
               f"median {times[len(times) // 2] * FRAME_MS if times else 0} ms")
    for p in problems:
        print(f"FAIL rooms: {p}")
    print(f"{'FAIL' if problems else 'ok  '} rooms: {summary}")
    return 1 if problems else 0


if __name__ == "__main__":
    if len(sys.argv) != 6:
        sys.exit(__doc__)
    sys.exit(main(*sys.argv[1:]))
