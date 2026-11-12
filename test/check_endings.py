#!/usr/bin/env python3
"""Checks what test/endings.sh's run of test/shell.tcl saw (AGENTS.md §10, M12), and draws it.
    check_endings.py DUMP GEN SHOT_PREFIX

The three endings, one after the other from a new machine: each job's title
line, its reveal (its own amulet's quarters and verse), its closing scene's
words and prompt and the amulet in Mr Jones's hands, the roll's top (the job's
own "the end"), Mr Jones's offer of the next job, and the last job's ride home:
its horses where the Java EndingScreen puts them, tick for tick. Words are
read with the ROM's fonts (check_shell.find); figures are compared with
scenes_img.bin's and shell_jobs.bin's pixels where they are not clear.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_boot  # noqa: E402
import check_shell  # noqa: E402
from cmacro import value  # noqa: E402

GROUND = check_shell.GROUND


def images(blob):
    """An image blob (x, line, byte width, h, then two pixels a byte): each image's rows of pixels."""
    out, at = [], 0
    while at < len(blob):
        bw, h = blob[at + 3], blob[at + 4]
        at += 5
        rows = []
        for _ in range(h):
            row = []
            for b in blob[at:at + bw // 2]:
                row += [b >> 4, b & 15]
            rows.append(row)
            at += bw // 2
        out.append(rows)
    return out


class Scenes:
    def __init__(self, gen, gen_dir):
        self.gen = gen
        self.sc = gen.sc
        self.img = images(open(f"{gen_dir}/scenes_img.bin", "rb").read())
        self.band = self.sc["SCENE_BAND"]

    def table(self, idx):
        at = self.sc["SCENE_IMG_TABLE"] + idx * 7
        b = self.gen.scenes[at:at + 7]
        return b[3], b[4], b[5], b[6]   # w, h, origin x, y

    def word(self, at):
        return int.from_bytes(self.gen.scenes[at:at + 2], "little", signed=True)

    def figure(self, px, idx, fx, fy):
        """(matching, all) of figure idx's solid pixels with its origin at (fx, fy), clipped to the band."""
        w, h, ox, oy = self.table(idx)
        left, top = fx - ox, fy - oy
        good = total = 0
        for y in range(h):
            for x in range(w):
                c = self.img[idx][y][x]
                sx, sy = left + x, top + y
                if c == 0 or not (0 <= sx < 256 and 0 <= sy < self.band):
                    continue
                total += 1
                good += px[sy][sx] == c
        return good, total


def main(dump, gen_dir, shot):
    gen = check_shell.Gen(gen_dir)
    sc = Scenes(gen, gen_dir)
    screens, logs, end = check_shell.read_dump(dump)
    name = os.path.basename(dump)
    problems = []
    if end != "ok":
        problems.append(f"the openMSX script stopped: {end}")
    s, d = gen.str, gen.sc
    big, small = True, False
    game_h, scenes_h = f"{gen_dir}/game_data.h", f"{gen_dir}/scenes_data.h"
    jobs = gen.g["JOBS"]
    ride_job = d["ENDING_RIDE_JOB"]

    def at_scene(n):
        return gen.scene_str(n)[0]

    want = {}
    for j in range(jobs):
        # The title: before a win, the game's tagline alone; after, the job opened under START.
        want[f"title{j}"] = [(s("STR_START"), big)] + ([(s("STR_TAGLINE"), small)] if j == 0 else
                                                       [(s(f"STR_JOB_LABEL_{j}"), small)])
        if j > 0 and f"STR_JOB_TAGLINE_{j}" in gen.s:
            want[f"title{j}"].append((s(f"STR_JOB_TAGLINE_{j}"), small))
        verse = [s(f"STR_VERSE_0_{l}") if j == 0 or f"STR_JOB_VERSE_{j}_0_{l}" not in gen.s
                 else s(f"STR_JOB_VERSE_{j}_0_{l}") for l in range(gen.s["VERSE_LINES"])]
        want[f"reveal{j}"] = [(v, big) for v in verse]
        lines = "ENDING_LINES" if j == 0 else f"ENDING_LINES_J{j}"
        want[f"ending{j}"] = [(r, big) for r in gen.rows_at(d[lines], d[lines + "_N"])] \
            + [(at_scene(d["STR_ENDING_PROMPT"]), big)]
        want[f"roll{j}"] = [(at_scene(d[f"STR_THE_END_{j}"]), big)]
        if j + 1 < jobs:
            offer = f"OFFER_LINES_J{j + 1}"
            want[f"offer{j}"] = [(r, big) for r in gen.rows_at(d[offer], d[offer + "_N"])] \
                + [(at_scene(d["STR_ENDING_PROMPT"]), big)]
    want["titlelast"] = [(s(f"STR_JOB_LABEL_{jobs - 1}"), small)]

    for tag, items in want.items():
        if tag not in screens:
            problems.append(f"no screen '{tag}'")
            continue
        scr = screens[tag]
        scroll = scr["regs"][23] if tag.startswith("roll") else 0
        check_boot.write_png(f"{shot}-{tag}.png", check_shell.shown_bytes(scr["vram"], scroll), scr["palette"])
        px = check_shell.pixels(scr["vram"], scroll)
        for text, font in items:
            if check_shell.find(gen, px, text, font) is None:
                problems.append(f"{tag}: \"{text}\" is not on screen ({'big' if font else 'small'} font)")

    # The job's own quarters at the reveal: shell_jobs.bin's (shell_img.bin's first for the game's).
    shell_img = images(open(f"{gen_dir}/shell_img.bin", "rb").read())
    job_img = images(open(f"{gen_dir}/shell_jobs.bin", "rb").read())
    n = gen.s["SHELL_JOB_IMAGES"]
    qw, qh = gen.s["IMG_QUARTER_W"], gen.s["IMG_QUARTER_H"]
    big_line = gen.s.get("BIG_LINE", 10)
    for j in range(jobs):
        tag = f"reveal{j}"
        if tag not in screens:
            continue
        px = check_shell.pixels(screens[tag]["vram"])
        mask = value(game_h, "JOB_MASK", j, 0) or 1
        size = 2 * qw
        block = size + big_line + (gen.s["VERSE_LINES"] + 1) * big_line
        top, left = (gen.c["PLAYFIELD_H"] - block) >> 1, (256 - size) >> 1
        for slot in range(n):
            if not mask & (1 << slot):
                continue
            pic = shell_img[slot] if j == 0 else job_img[(j - 1) * n + slot]
            bad = sum(1 for y in range(qh) for x in range(qw) if pic[y][x]
                      and px[top + (slot >> 1) * qh + y][left + (slot & 1) * qw + x] != pic[y][x])
            if bad:
                problems.append(f"{tag}: quarter {slot} is not job {j}'s amulet ({bad} pixels differ)")

    # The amulet in Mr Jones's hands at the prompt: the job's own quarters, at half size.
    for j in range(jobs):
        tag = f"ending{j}"
        if tag not in screens or j == ride_job:
            continue
        px = check_shell.pixels(screens[tag]["vram"])
        first = value(scenes_h, "SCI_QUARTER", j)
        half = d["ENDING_HALF"]
        for slot in range(4):
            good, total = sc.figure(px, first + slot, d["ENDING_HOLD_X"] - half + (half if slot & 1 else 0),
                                    d["ENDING_HOLD_Y"] - half + (half if slot & 2 else 0))
            if good != total:
                problems.append(f"{tag}: Mr Jones does not hold job {j}'s quarter {slot} ({total - good} of {total})")

    # The ride home (EndingScreen.paintHorses), at the ticks logged: within a tick or two of the dump.
    def horses(t):
        out = []
        step = t // d["RIDE_EVERY"]

        def frame(first, moving):
            return first + (step % d["RIDE_FRAMES"] if moving else 0)
        if t < d["RIDE_MOUNT_AT"]:
            k = t - d["RIDE_COME_AT"]
            coming = k < d["RIDE_COME_TICKS"]
            vx = sc.word(d["RIDE_COME_PATH"] + 4 * k) if coming else d["ENDING_VALE_AT"]
            jx = sc.word(d["RIDE_COME_PATH"] + 4 * k + 2) if coming else d["ENDING_JONES_X"]
            out.append((frame(d["SCI_HORSE_VALE"], coming), vx))
            out.append((frame(d["SCI_HORSE_JONES"], coming), jx))
        else:
            riding = t >= d["RIDE_AT"]
            shift = sc.word(d["RIDE_SHIFT_PATH"] + 2 * min(t - d["RIDE_AT"], d["RIDE_TICKS"])) if riding else 0
            out.append((frame(d["SCI_RIDE_JONES"], riding), d["ENDING_JONES_X"] + shift))
            out.append((frame(d["SCI_RIDE_VALE"], riding), d["ENDING_VALE_AT"] + shift))
        return out

    rides = [l for l in logs if l[0] == "ride"]
    if ride_job >= 0 and len(rides) != 3:
        problems.append(f"the ride: {len(rides)} of its 3 moments seen")
    for l in rides:
        want_t, got_t = int(l[2]), int(l[3])
        tag = f"ride{want_t}"
        if tag not in screens:
            problems.append(f"no screen '{tag}'")
            continue
        check_boot.write_png(f"{shot}-{tag}.png", screens[tag]["vram"], screens[tag]["palette"])
        px = check_shell.pixels(screens[tag]["vram"])
        best = None
        for t in (got_t, got_t - 1, got_t - 2):
            score = [sc.figure(px, idx, x, d["ENDING_GROUND"]) for idx, x in horses(t)]
            seen = all(total == 0 or good >= 0.9 * total for good, total in score)
            if seen and any(total for _, total in score):
                best = t
                break
        if best is None:
            problems.append(f"{tag}: the horses are not where EndingScreen has them at tick {got_t}")

    # What the shell did: each job played in turn, each win offering the next, the title keeping it.
    for l in logs:
        if l[0] == "playing" and l[2] != l[3]:
            problems.append(f"the game after job {int(l[2]) - 1}'s win is job {l[3]}, not {l[2]}")
        if l[0] == "roll":
            j, offered = int(l[2]), int(l[3])
            expect = j + 1 if j + 1 < jobs else 0
            if offered != expect:
                problems.append(f"job {j}'s win offers job {offered}, not {expect}")
        if l[0] == "title":
            j, job, unlocked = (int(v) for v in l[2:5])
            expect = min(j, jobs - 1)
            if job != expect or unlocked != expect:
                problems.append(f"title after {j} wins: job {job}, opened {unlocked}; want {expect}")

    for p in problems:
        print(f"FAIL {name}: {p}")
    if not problems:
        print(f"ok   {name}: {jobs} endings played through, each job's words and amulet, the ride home, "
              f"each win offering the next job")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
