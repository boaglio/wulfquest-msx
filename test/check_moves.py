#!/usr/bin/env python3
"""Checks what test/moves.tcl saw against the Java game (AGENTS.md §10, M3).

    check_moves.py DUMP EXPECTED SCENARIOS_JSON CONSTANTS_H HZ

DUMP is moves.tcl's output, EXPECTED MoveTraces's; both have a line a tick,
and DUMP the cycles of each scenario's busiest frame,
"X_FP Y_FP FACING SWING WALK SX SY SW SH". Every tick must be equal. On top,
the scenarios' own checks (the Java game's MovementFeelTest numbers):

  across  at the top of the run-up, 85 ticks holding right move 255-257 px
  down    at the top of the run-up, 59 ticks holding down move 176-178 px
  slide   down-right along a wall: the wall stops x, y goes at full speed

and every room scenario must walk into a wall at least once. The loop must
never come late to a frame, and must run 50 ticks a second: a tick a frame
at 50 Hz, five in six frames at 60 Hz.
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_rooms import constants  # noqa: E402

RUN_UP = 24          # MovementFeelTest.RUN_UP: long enough to reach top speed
ACROSS_TICKS = 85
DOWN_TICKS = 59
KEY_BITS = {"U": 1, "D": 2, "L": 4, "R": 8, "F": 16}


def read(path):
    """name -> (list of tick tuples, end fields or None); and the final end line."""
    scenarios, order, final, busiest = {}, [], None, {}
    name = None
    for line in open(path, encoding="utf-8").read().split("\n"):
        f = line.split()
        if not f:
            continue
        if f[0] == "scenario":
            name = f[1]
            order.append(name)
            scenarios[name] = ([], None)
        elif f[0] == "busiest":
            busiest[f[1]] = int(f[2])
        elif f[0] == "end":
            if len(f) == 5 and f[1] == name:
                scenarios[name] = (scenarios[name][0], tuple(map(int, f[2:])))
            else:
                final = " ".join(f[1:])
        else:
            scenarios[name][0].append(tuple(map(int, f)))
    return scenarios, order, final, busiest


def keys_of(s):
    out = []
    for held, n in s["keys"]:
        b = 0
        for ch in held:
            b |= KEY_BITS[ch]
        out += [b] * n
    return out


def main(dump, expected_path, scenarios_json, constants_h, hz):
    c = constants(constants_h)
    hz = int(hz)
    got, order, final, busiest = read(dump)
    want, want_order, _, _ = read(expected_path)
    plan = {s["name"]: s for s in json.load(open(scenarios_json, encoding="utf-8"))["scenarios"]}
    tag = f"moves {hz} Hz"
    problems, notes = [], []

    if final != "ok":
        problems.append(f"the run did not finish: {final}")
    for name in want_order:
        if name not in got:
            problems.append(f"{name}: not played")
            continue
        g, end = got[name]
        w, _ = want[name]
        if len(g) != len(w):
            problems.append(f"{name}: {len(g)} ticks recorded, the Java game has {len(w)}")
        for t, (a, b) in enumerate(zip(g, w)):
            if a != b:
                problems.append(f"{name}: differs from the Java game after tick {t + 1}: MSX2 {a}, Java {b}")
                break

        x = [s[0] for s in g]
        y = [s[1] for s in g]
        check = plan[name].get("check")
        if check == "across" and len(x) >= RUN_UP + ACROSS_TICKS:
            px = (x[RUN_UP + ACROSS_TICKS - 1] - x[RUN_UP - 1]) / 256
            notes.append(f"{ACROSS_TICKS} ticks right at full speed: {px:g} px")
            if not 255 <= px <= 257:
                problems.append(f"{name}: {ACROSS_TICKS} ticks right moved {px:g} px, not 256 +-1")
        if check == "down" and len(y) >= RUN_UP + DOWN_TICKS:
            px = (y[RUN_UP + DOWN_TICKS - 1] - y[RUN_UP - 1]) / 256
            notes.append(f"{DOWN_TICKS} ticks down at full speed: {px:g} px")
            if not 176 <= px <= 178:
                problems.append(f"{name}: {DOWN_TICKS} ticks down moved {px:g} px, not 176 +-1")
        if check == "slide":
            held = plan[name]["keys"][0][1]
            bad = [t for t in range(RUN_UP, held)
                   if x[t] != x[t - 1] or y[t] - y[t - 1] != c["PLAYER_SPEED_Y_FP"]]
            notes.append(f"wall slide: {held - RUN_UP - len(bad)} of {held - RUN_UP} ticks at full speed down")
            if bad:
                problems.append(f"{name}: not a full-speed slide down the wall after tick {bad[0] + 1}")
        if plan[name].get("open") is not True:
            keys = keys_of(plan[name])
            stops = 0
            for t in range(8, len(g)):
                k = keys[t]
                if all(keys[u] == k for u in range(t - 8, t + 1)) and g[t][3] < 0 and (
                        (k & 12 and k & 12 != 12 and x[t] == x[t - 1])
                        or (k & 3 and k & 3 != 3 and y[t] == y[t - 1])):
                    stops += 1
            notes.append(f"{name}: {stops} ticks pressed against a wall")
            if stops == 0:
                problems.append(f"{name}: never walks into a wall, so it checks none")

        if end is None:
            continue
        late, ticks, frames = end
        if late:
            problems.append(f"{name}: the loop came late to {late} frames")
        want_ticks = frames if hz == 50 else frames * 5 / 6
        if abs(ticks - want_ticks) > 1:
            problems.append(f"{name}: {ticks} ticks in {frames} frames at {hz} Hz; 50 a second is {want_ticks:g}")

    if busiest:
        worst = max(busiest, key=busiest.get)
        frame = 3579545 // hz
        notes.append(f"busiest frame {busiest[worst]} cycles ({worst}), {busiest[worst] * 100 // frame}% of a frame")
    for p in problems:
        print(f"FAIL {tag}: {p}")
    ticks = sum(len(got[n][0]) for n in got)
    print(f"{'FAIL' if problems else 'ok  '} {tag}: {len(got)} scenarios, {ticks} ticks"
          + ("" if problems else " equal to the Java game's") + "; " + "; ".join(notes))
    return 1 if problems else 0


if __name__ == "__main__":
    if len(sys.argv) != 6:
        sys.exit(__doc__)
    sys.exit(main(*sys.argv[1:]))
