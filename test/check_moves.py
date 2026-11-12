#!/usr/bin/env python3
"""Checks what test/moves.tcl saw against the Java game (AGENTS.md §10, M3 and M4).

    check_moves.py DUMP EXPECTED PLAN CONSTANTS_H HZ ROOMS_EXPECTED GEN

DUMP is moves.tcl's output (and DUMP.bin, the shown page at each flip, and
DUMP.spr, the sprite tables then), EXPECTED MoveTraces's, PLAN the plan both
played; both traces have a line a tick, "X_FP Y_FP FACING SWING WALK SX SY SW
SH ROOM FREEZE FRAME BLADE": the frame and the blade Vale is drawn with too,
as the Java game's PlayerPainter picks them. Every tick must be equal. On top, each scenario's own check, from the plan:

  across     at the top of the run-up, 85 ticks holding right move 255-257 px
  down       at the top of the run-up, 59 ticks holding down move 176-178 px
  slide      down-right along a wall: the wall stops x, y goes at full speed
  route:N:T  the walk crosses at least N edges between rooms and ends in room T
  seam:F:T   Vale walks from room F into room T and back
  -          in a room (not the open world): he walks into a wall at least once

The loop must never come late to a frame (a flip is a hitch, not lateness),
and in a scenario without flips must run 50 ticks a second: a tick a frame at
50 Hz, five in six frames at 60 Hz. Every frame must be put on screen while
the screen is blank; at every flip, the page shown must be the new room as
the Java game draws it (ROOMS_EXPECTED, RenderRooms's), and the sprites on it,
drawn as the V9938 draws them (vdp_sprites.py), must be Vale in that tick's
frame and blade, where he stands, as the converter cut them (GEN, build/gen/).
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_rooms import constants  # noqa: E402
import vdp_sprites  # noqa: E402

RUN_UP = 24          # MovementFeelTest.RUN_UP: long enough to reach top speed
ACROSS_TICKS = 85
DOWN_TICKS = 59
LINE = 128           # bytes a line of SCREEN 5
SPRITE_TABLES = 128 + 512 + 2048   # a flip's attribute, colour and pattern tables in DUMP.spr


def read(path):
    """name -> [ticks, end fields or None, flips]; the order; the final end line; busiest frames."""
    scenarios, order, final, busiest = {}, [], None, {}
    name = None
    for line in open(path, encoding="utf-8").read().split("\n"):
        f = line.split()
        if not f:
            continue
        if f[0] == "scenario":
            name = f[1]
            order.append(name)
            scenarios[name] = [[], None, [], [], [], []]
        elif f[0] == "busiest":
            busiest[f[1]] = (int(f[2]), int(f[3]))
        elif f[0] == "flip":
            scenarios[name][2].append(tuple(map(int, f[1:])))
        elif f[0] == "J":
            scenarios[name][3].append(" ".join(f))
        elif f[0] == "Q":
            scenarios[name][4].append(" ".join(f))
        elif f[0] == "M":
            scenarios[name][5].append(" ".join(f))
        elif f[0] == "end":
            if len(f) == 8 and f[1] == name:
                scenarios[name][1] = tuple(map(int, f[2:]))
            else:
                final = " ".join(f[1:])
        else:
            scenarios[name][0].append(tuple(map(int, f)))
    return scenarios, order, final, busiest


def p_check(plan, name):
    return plan[name]["check"]


def read_plan(path):
    plan = {}
    lines = open(path, encoding="utf-8").read().split("\n")
    for head, keys in zip(lines[0::2], lines[1::2]):
        _, name, room, open_, x, y, check, seed, level, endless, easy, job = head.split()
        plan[name] = {"open": open_ == "1", "check": check, "keys": list(map(int, keys.split()[1:]))}
    return plan


def main(dump, expected_path, plan_path, constants_h, hz, rooms_expected, gen):
    c = constants(constants_h)
    player_h = open(os.path.join(os.path.dirname(constants_h), "player.h"), encoding="utf-8").read()
    c.update({m[0]: int(m[1]) for m in re.findall(r"#define (\w+)\s+(-?\d+)\b", player_h)})
    hz = int(hz)
    got, order, final, busiest = read(dump)
    want, want_order, _, _ = read(expected_path)
    plan = read_plan(plan_path)
    field = c["PLAYFIELD_H"] * LINE
    rooms_pic = open(rooms_expected, "rb").read()
    shown = open(dump + ".bin", "rb").read()
    tables = open(dump + ".spr", "rb").read()
    gen_sprites = vdp_sprites.Gen(gen)
    game_h = vdp_sprites.defines(gen + "/game_data.h")
    untouchable = vdp_sprites.table(gen, "g_OrchidUntouchable")
    vale_inked = vdp_sprites.table(gen, "g_ValeInked")
    grid_w = c["GRID_W"]
    plan_name = os.path.basename(plan_path).split("_")[0]
    tag = f"{plan_name} {hz} Hz"
    # Plans whose timing is known to slip (AGENTS.md §10, M6): lateness is
    # reported, not failed, until the tuning milestone closes it.
    slipped = plan_name in os.environ.get("MOVES_TIMING_SLIPPED", "").split()
    slips = []
    problems, notes = [], []
    flips_seen, flip_frames, seams, unblanked = 0, [], 0, 0

    if final != "ok":
        problems.append(f"the run did not finish: {final}")
    for name in want_order:
        if name not in got:
            problems.append(f"{name}: not played")
            continue
        g, end, flips, gj, gq, gm = got[name]
        wm = want[name][5]
        if wm:
            # The rest of the game: every tick's line equal to the Java game's.
            if len(gm) != len(wm):
                problems.append(f"{name}: {len(gm)} game lines recorded, the Java game has {len(wm)}")
            for t, (a, b) in enumerate(zip(gm, wm)):
                if a != b:
                    problems.append(f"{name}: the game differs from the Java game's after tick {t + 1}:\n"
                                    f"    MSX2 {a[:300]}\n    Java {b[:300]}")
                    break
            notes.append(f"{name}: score {wm[-1].split()[1]}")
        w = want[name][0]
        wj = want[name][3]
        wq = want[name][4]
        if wq:
            # The quest and the Wulf: every tick's line equal to the Java game's.
            if len(gq) != len(wq):
                problems.append(f"{name}: {len(gq)} quest lines recorded, the Java game has {len(wq)}")
            for t, (a, b) in enumerate(zip(gq, wq)):
                if a != b:
                    problems.append(f"{name}: the quest differs from the Java game's after tick {t + 1}:\n"
                                    f"    MSX2 {a[:300]}\n    Java {b[:300]}")
                    break
            last = wq[-1].split()
            notes.append(f"{name}: amulet set {last[1]}, quarters {bin(int(last[2])).count('1')}"
                         + (", won" if wj and wj[-1].split()[1] == "5" else ""))
        if wj:
            # The jungle: every tick's line equal to the Java game's.
            if len(gj) != len(wj):
                problems.append(f"{name}: {len(gj)} jungle lines recorded, the Java game has {len(wj)}")
            for t, (a, b) in enumerate(zip(gj, wj)):
                if a != b:
                    problems.append(f"{name}: the jungle differs from the Java game's after tick {t + 1}:\n"
                                    f"    MSX2 {a[:300]}\n    Java {b[:300]}")
                    break
            modes = [int(x.split()[1]) for x in wj]
            deaths = sum(1 for t in range(1, len(modes)) if modes[t - 1] == 0 and modes[t] == 1)
            most = max(int(x.split()[5]) for x in wj)
            notes.append(f"{name}: {deaths} deaths, up to {most} creatures in the room"
                         + (", game over" if 3 in modes else "") + f" ({p_check(plan, name)})")
        if len(g) != len(w):
            problems.append(f"{name}: {len(g)} ticks recorded, the Java game has {len(w)}")
        for t, (a, b) in enumerate(zip(g, w)):
            if a != b:
                problems.append(f"{name}: differs from the Java game after tick {t + 1}: MSX2 {a}, Java {b}")
                break

        x = [s[0] for s in g]
        y = [s[1] for s in g]
        room = [s[9] for s in g]
        g_frame = [s[11] for s in g]
        g_blade = [s[12] for s in g]
        p = plan[name]
        check = p["check"]
        crossings = [t for t in range(1, len(room)) if room[t] != room[t - 1]]
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
            held = next(i for i, k in enumerate(p["keys"]) if k != p["keys"][0])
            bad = [t for t in range(RUN_UP, held)
                   if x[t] != x[t - 1] or y[t] - y[t - 1] != c["PLAYER_SPEED_Y_FP"]]
            notes.append(f"wall slide: {held - RUN_UP - len(bad)} of {held - RUN_UP} ticks at full speed down")
            if bad:
                problems.append(f"{name}: not a full-speed slide down the wall after tick {bad[0] + 1}")
        if check.startswith("route:"):
            _, n, target = check.split(":")
            n, target = int(n), int(target)
            ends = f"{room[-1] % grid_w},{room[-1] // grid_w}" if room else "nowhere"
            notes.append(f"route: {len(crossings)} edges crossed, {len(set(room))} rooms, {len(g)} ticks, "
                         f"ends in {ends} at {x[-1] / 256:g},{y[-1] / 256:g}" if g else "route: nothing")
            if len(crossings) < n:
                problems.append(f"{name}: crosses {len(crossings)} edges between rooms, not {n}")
            if not room or room[-1] != target:
                problems.append(f"{name}: ends in room {ends}, not {target % grid_w},{target // grid_w}")
        if check.startswith("seam:"):
            seams += 1
            _, a, b = check.split(":")
            if int(b) not in room or not room or room[-1] != int(a):
                problems.append(f"{name}: does not walk from room {a} into {b} and back")
        if check == "-" and not p["open"]:
            keys = p["keys"]
            stops = 0
            for t in range(8, len(g)):
                k = keys[t]
                if all(keys[u] == k for u in range(t - 8, t + 1)) and g[t][3] < 0 and g[t][10] == 0 and (
                        (k & 12 and k & 12 != 12 and x[t] == x[t - 1])
                        or (k & 3 and k & 3 != 3 and y[t] == y[t - 1])):
                    stops += 1
            notes.append(f"{name}: {stops} ticks pressed against a wall")
            if stops == 0:
                problems.append(f"{name}: never walks into a wall, so it checks none")

        # Every flip: the new room on screen, whole, and Vale on it.
        if len(flips) != len(crossings):
            problems.append(f"{name}: {len(flips)} flips shown, the Java game changes rooms {len(crossings)} times")
        for (ticks, page, frames), t in zip(flips, crossings):
            vram = shown[flips_seen * field:(flips_seen + 1) * field]
            spr = tables[flips_seen * SPRITE_TABLES:(flips_seen + 1) * SPRITE_TABLES]
            flips_seen += 1
            flip_frames.append(frames)
            r = room[t]
            where = f"room {r % grid_w},{r // grid_w}"
            if ticks != t + 1:
                problems.append(f"{name}: the room after tick {t + 1} was shown after tick {ticks}")
            if vram != rooms_pic[r * field:(r + 1) * field]:
                problems.append(f"{name}: after tick {t + 1}, page {page} is not {where} as the Java game draws it")
            # Vale in this tick's frame and blade, his frame's top-left where he stands.
            sx = (x[t] >> 8) - (r % grid_w) * c["PLAYFIELD_W"] - c["PLAYER_ORIGIN_X"]
            sy = (y[t] >> 8) - (r // grid_w) * c["PLAYFIELD_H"] - c["PLAYER_ORIGIN_Y"]
            frame, blade, ink = g_frame[t], g_blade[t], 255
            if wm:
                # M8 (PlayerPainter): knocked down he sits, bladeless; an untouchable flower
                # inks him, flashing back to himself near its end.
                mf = wm[t].split()
                kind, orchid, left = int(mf[4]), int(mf[5]), int(mf[6])
                if kind == game_h["EFFECT_KNOCKDOWN"]:
                    frame = c["PLAYER_FRAME_SIT"] + (1 if g[t][2] >= 5 else 0)
                    blade = -1
                if orchid >= 0 and untouchable[orchid] and not (
                        left <= game_h["ORCHID_TINT_WARN_TICKS"] and (left // game_h["ORCHID_TINT_FLASH_TICKS"]) % 2 == 0):
                    ink = vale_inked[orchid]
            want_px = gen_sprites.clip(gen_sprites.vale(frame, blade, sx, sy, ink))
            if wj:
                # PlayerPainter.visible: after a death he blinks, half a period shown.
                jf = wj[t].split()
                mode, invuln, period = int(jf[1]), int(jf[4]), c["PLAYER_SPAWN_BLINK_TICKS"]
                if mode == 0 and invuln > 0 and invuln % period >= (period + 1) // 2:
                    want_px = {}
            got_px = vdp_sprites.render(spr[:128], spr[128:640], spr[640:], lines=c["PLAYFIELD_H"])
            if wj:
                # With the jungle, other sprites too: Vale's must all be there, in front
                # (blinked out, none to look for).
                got_px = {k: v for k, v in got_px.items() if k in want_px}
            if got_px != want_px:
                right = len(set(got_px.items()) & set(want_px.items()))
                problems.append(f"{name}: after tick {t + 1}, the sprites are not Vale in frame {frame}, "
                                f"blade {blade}, ink {ink}, at {sx},{sy} in {where}: {len(got_px)} pixels shown, "
                                f"{len(want_px)} wanted, {right} right")
        if end is None:
            continue
        late, ticks, frames, nflips, nflipframes, blank = end
        unblanked += blank
        if blank:
            problems.append(f"{name}: {blank} frames put on screen while it was not blank")
        timing = []
        if late:
            timing.append(f"{name}: the loop came late to {late} frames")
        if nflips == 0:
            want_ticks = frames if hz == 50 else frames * 5 / 6
            if abs(ticks - want_ticks) > 1:
                timing.append(f"{name}: {ticks} ticks in {frames} frames at {hz} Hz; 50 a second is {want_ticks:g}")
        (slips if slipped else problems).extend(timing)

    if seams:
        notes.append(f"{seams} edges between rooms crossed both ways")
    if flip_frames:
        f = sorted(flip_frames)
        ms = 1000 // hz
        notes.append(f"{len(f)} flips, each the whole new room swapped in while the screen was blank, "
                     f"Vale on it; the hitch {f[len(f) // 2] * ms} ms median, {f[-1] * ms} ms worst")
    if busiest:
        worst = max(busiest, key=lambda n: busiest[n][0])
        cycles, tick = busiest[worst]
        frame = 3579545 // hz
        notes.append(f"busiest frame {cycles} cycles ({worst}, tick {tick}), {cycles * 100 // frame}% of a frame")
    for p in slips:
        print(f"SLIP {tag}: {p}")
    if slips:
        notes.append(f"timing slipped in {len(slips)} checks (AGENTS.md §10, M6), not failed")
    for p in problems[:40]:
        print(f"FAIL {tag}: {p}")
    if len(problems) > 40:
        print(f"FAIL {tag}: ... and {len(problems) - 40} more")
    ticks = sum(len(got[n][0]) for n in got)
    print(f"{'FAIL' if problems else 'ok  '} {tag}: {len(got)} scenarios, {ticks} ticks"
          + ("" if problems else " equal to the Java game's") + "; " + "; ".join(notes))
    return 1 if problems else 0


if __name__ == "__main__":
    if len(sys.argv) != 8:
        sys.exit(__doc__)
    sys.exit(main(*sys.argv[1:]))
