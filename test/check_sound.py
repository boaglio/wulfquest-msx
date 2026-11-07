#!/usr/bin/env python3
"""Checks what test/sound.tcl heard against the converter's tables (AGENTS.md §10, M9).

    check_sound.py DUMP SOUND_BIN SOUND_DATA_H HZ

Each sound, played alone, must come out of the PSG as its entries say, a frame
each: an effect on channel A (the first free voice), a tune on channel C; the
period (or the noise's), the tone or noise in the mixer, the volume. The
entries themselves are held to the Java game's synth by convert's SoundTest.
"""
import re
import sys


def entries(blob, i):
    at = blob[4 * i] | blob[4 * i + 1] << 8
    n = blob[4 * i + 2] | blob[4 * i + 3] << 8
    out = []
    for k in range(n):
        lo, hi, vol, frames = blob[at + 4 * k:at + 4 * k + 4]
        out += [(lo | (hi & 15) << 8, hi >> 7, vol)] * frames
    return out


def main(dump, sound_bin, header, hz):
    blob = open(sound_bin, "rb").read()
    defs = dict(re.findall(r"#define (\w+)\s+(\d+)\b", open(header).read()))
    sfx = int(defs["SFX_COUNT"])
    got = {}
    final = None
    cur = None
    for line in open(dump).read().split("\n"):
        f = line.split()
        if not f:
            continue
        if f[0] == "sound":
            cur = int(f[1])
            got[cur] = []
        elif f[0] == "end":
            final = " ".join(f[1:])
        else:
            got[cur].append(list(map(int, f)))
    problems = []
    if final != "ok":
        problems.append(f"the run did not finish: {final}")
    names = {int(v): k for k, v in defs.items() if k.startswith(("SFX_", "TUNE_")) and k not in ("SFX_COUNT",)}
    for i in sorted(got):
        want = entries(blob, i)
        ch = 0 if i < sfx else 2
        frames = got[i]
        # It starts the frame after the ROM takes the request: from its first sounding frame on.
        start = next((k for k, r in enumerate(frames) if r[8 + ch] != 0), None)
        if start is None:
            problems.append(f"{names.get(i, i)}: never heard")
            continue
        first_sounding = next(k for k, w in enumerate(want) if w[2] != 0)
        start -= first_sounding
        for k, (period, noise, vol) in enumerate(want):
            if start + k >= len(frames):
                problems.append(f"{names.get(i, i)}: {len(frames) - start} frames recorded of {len(want)}")
                break
            r = frames[start + k]
            if r[8 + ch] != vol:
                problems.append(f"{names.get(i, i)} frame {k}: volume {r[8 + ch]}, the table's {vol}")
                break
            if vol == 0:
                continue
            mixer = r[7]
            if noise:
                ok = r[6] == period and not mixer & (8 << ch)
            else:
                ok = (r[2 * ch] | r[2 * ch + 1] << 8) == period and not mixer & (1 << ch)
            if not ok:
                problems.append(f"{names.get(i, i)} frame {k}: the PSG {r}, the table {(period, noise, vol)}")
                break
    for p in problems[:30]:
        print(f"FAIL sound {hz} Hz: {p}")
    print(f"{'FAIL' if problems else 'ok  '} sound {hz} Hz: {len(got)} sounds, each frame as its table")
    return 1 if problems else 0


if __name__ == "__main__":
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    sys.exit(main(*sys.argv[1:]))
