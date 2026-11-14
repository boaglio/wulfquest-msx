#!/usr/bin/env python3
"""The sampling profiler's report (AGENTS.md §10, M13).

    tools/profile.py build/test/jungle-C-BIOS_MSX2_EU.txt.prof

test/moves.tcl, run with MOVES_PROFILE=<scenario>, counts the program counter
every few microseconds of that scenario as "COUNT SEGMENT PC" lines (SEGMENT is
bank 1's for 8000h-BFFFh, the banked code; else 0). This names each sample's
function, static ones too: the linker's map places each module's areas, and
the assembler's .sym files give every label's offset in its area.
"""
import bisect
import collections
import glob
import os
import re
import sys

BUILD = "build/msxgl"
CYCLES_PER_S = 3579545


def symbols():
    """[(absolute address with segment << 16, name, module)], sorted."""
    placed = {}   # global name -> absolute address, from the map
    for line in open(os.path.join(BUILD, "wulfquest.map")):
        m = re.match(r"\s+([0-9A-F]{8})\s+(\S+)\s+(\S+)\s*$", line)
        if m:
            placed[m.group(2)] = (int(m.group(1), 16), m.group(3))
    out = []
    for sym in glob.glob(os.path.join(BUILD, "*.sym")):
        module = os.path.basename(sym)[:-4]
        labels = []   # (area, offset, name, global)
        for line in open(sym, errors="replace"):
            m = re.match(r"\s*(\d+)\s+(\S+)\s+([0-9A-F]{8})\s+(\w*)", line)
            if m and "R" in m.group(4):
                labels.append((int(m.group(1)), int(m.group(3), 16), m.group(2), "G" in m.group(4)))
        base = {}
        for area, off, name, glob_ in labels:
            # Only a module the linker took (a stale .sym of an old build names its globals too).
            if glob_ and placed.get(name, (0, ""))[1] == module and area not in base:
                base[area] = placed[name][0] - off
        for area, off, name, glob_ in labels:
            if area in base and not name.startswith(".") and not re.match(r"^\d+\$$", name):
                out.append((base[area] + off, name, module))
    # The library's modules have no .sym here: their globals from the map.
    have = {a for a, _, _ in out}
    for name, (addr, module) in placed.items():
        if addr not in have and name.startswith("_") and addr >= 0x4000:
            out.append((addr, name, module))
    out.sort()
    return out


def lines(prof, function, calls):
    """--lines PROF [MODULE:]FUNCTION [CALLS]: its C lines' cycles a call
    (CALLS: how many calls the scenario made, from MOVES_TIME), from the
    module's listing (";./file.c:LINE:" comments before each line's code;
    in inline assembly, by its local label too)."""
    module, _, fn = function.rpartition(":")
    syms = symbols()
    hits = [(a, m) for a, n, m in syms if n == "_" + fn and (not module or m == module)]
    if len(hits) != 1:
        sys.exit(f"profile.py: {function}: {len(hits)} places")
    start, module = hits[0]
    nxt = min((a for a, _, m in syms if a > start), default=start + 0x10000)
    off = next(int(m.group(1), 16) for m in (re.match(r"\s*\d+\s+_" + fn + r"\s+([0-9A-F]{8})", l)
                                             for l in open(os.path.join(BUILD, module + ".sym"))) if m)
    base = start - off
    entries, cur, src, lab = [], None, None, ""
    for l in open(os.path.join(BUILD, module + ".lst"), errors="replace"):
        m = re.search(r";\.?/?([\w./-]+\.c):(\d+):", l)
        if m:
            src, cur, lab = m.group(1), int(m.group(2)), ""
            continue
        m = re.search(r"\s(\d{5}\$):", l)
        if m:
            lab = m.group(1)
        m = re.match(r"\s*([0-9A-F]{8})\s", l)
        if m and cur:
            entries.append((int(m.group(1), 16) + base, src, cur, lab))
    entries.sort()
    addrs = [a for a, *_ in entries]
    per = collections.Counter()
    for line in open(prof):
        if line[0] in "cft":
            continue
        n, seg, pc = map(int, line.split())
        key = seg << 16 | pc if start >> 16 else pc
        if start <= key < nxt:
            i = bisect.bisect_right(addrs, key - 1) - 1   # a sample's pc is the next instruction's
            if i >= 0:
                per[entries[i][1:]] += n
    period = CYCLES_PER_S * float(os.environ.get("MOVES_PROFILE_US", "137")) / 1e6
    total = sum(per.values())
    print(f"{fn}: {total * period / calls:.0f} cycles a call")
    texts = {}
    for (f, ln, lab), n in sorted(per.items(), key=lambda x: -x[1])[:30]:
        if f not in texts:
            path = next((p for p in (f, os.path.join("src", os.path.basename(f))) if os.path.exists(p)), None)
            texts[f] = open(path).read().split("\n") if path else []
        t = texts[f][ln - 1].strip() if ln <= len(texts[f]) else ""
        print(f"{n * period / calls:7.0f}  {os.path.basename(f)}:{ln}: {lab + ' ' if lab else ''}{t[:90]}")


def main():
    args = sys.argv[1:]
    if args[:1] == ["--lines"]:
        lines(args[1], args[2], int(args[3]) if len(args) > 3 else 1)
        return
    if args[:1] == ["--addr"]:
        # name=0xADDR for MOVES_COUNT and MOVES_TIME, static functions too
        # (module:name when two modules have one); past 8000h with @SEGMENT,
        # bank 1's segment, which test/moves.tcl checks.
        found = {}
        for addr, name, module in symbols():
            found.setdefault(name[1:], []).append((addr, module))
            found.setdefault(f"{module}:{name[1:]}", []).append((addr, module))
        out = []
        for a in args[1:]:
            # FUNCTION/LABEL: a local label in an assembly function (00072$), from the listing.
            a, _, label = a.partition("/")
            if label:
                hits = found.get(a, [])
                if len(hits) != 1:
                    sys.exit(f"profile.py: {a}: {len(hits)} places")
                start, module = hits[0]
                fn = a.split(":")[-1]
                lst = open(os.path.join(BUILD, module + ".lst"), errors="replace").read().split("\n")
                i = next(k for k, l in enumerate(lst) if re.search(r"\s_" + fn + r"::?\s*$", l))
                fa = int(lst[i].split()[0], 16)
                at = next(int(l.split()[0], 16) for l in lst[i:] if re.search(r"\s" + re.escape(label) + r"\$:", l))
                addr = start + at - fa
                seg = addr >> 16 if addr >> 16 else 1
                out.append(f"{fn}_{label}=0x{addr & 0xFFFF:04X}" + (f"@{seg}" if addr & 0xFFFF >= 0x8000 else ""))
                continue
            hits = found.get(a, [])
            if len(hits) != 1:
                sys.exit(f"profile.py: {a}: {len(hits)} places ({', '.join(m for _, m in hits)})")
            addr = hits[0][0]
            seg = addr >> 16 if addr >> 16 else 1   # the main code past 8000h is in segment 1
            out.append(f"{a.split(':')[-1]}=0x{addr & 0xFFFF:04X}" + (f"@{seg}" if addr & 0xFFFF >= 0x8000 else ""))
        print(" ".join(out))
        return
    if len(args) != 1:
        sys.exit(__doc__)
    syms = symbols()
    addrs = [a for a, _, _ in syms]
    by_fn = collections.Counter()
    total = 0
    calls = []
    times = []
    froms = []
    def where(seg, pc):
        i = bisect.bisect_right(addrs, seg << 16 | pc) - 1
        if not (i >= 0 and (addrs[i] >> 16) == seg):
            # Bank 1's own segment holds the main code past 8000h: the map has it unsegmented.
            i = bisect.bisect_right(addrs, pc) - 1
        if pc < 0x4000:
            return "BIOS (interrupt, slot calls)"
        return f"{syms[i][1]} ({syms[i][2]})" if i >= 0 else f"? {seg:02X}:{pc:04X}"
    for line in open(args[0]):
        if line.startswith("from "):
            _, name, n, seg, pc = line.split()
            froms.append((name, int(n), where(int(seg), int(pc)), int(pc)))
            continue
        if line.startswith("calls "):
            calls.append(line.split()[1:])
            continue
        if line.startswith("time "):
            _, name, k, cyc = line.split()
            times.append((name, int(k), int(cyc)))
            continue
        n, seg, pc = map(int, line.split())
        total += n
        by_fn[where(seg, pc)] += n
    print(f"{total} samples")
    for name, n in calls:
        print(f"calls {name}: {n}")
    for name, n, fn, pc in sorted(froms, key=lambda t: -t[1]):
        print(f"  from {fn} at {pc:04X}: {n}")
    for name, k, cyc in sorted(times, key=lambda t: -t[2]):
        print(f"time {name}: {k} calls, {cyc} cycles, {cyc // max(k, 1)} a call")
    by_module = collections.Counter()
    for name, n in by_fn.items():
        by_module[name.split("(")[-1].rstrip(")")] += n
    print("by module: " + ", ".join(f"{m} {100.0 * n / total:.1f}%" for m, n in by_module.most_common(12)))
    for name, n in by_fn.most_common(int(os.environ.get("PROFILE_TOP", "40"))):
        print(f"{100.0 * n / total:6.2f}%  {name}")


if __name__ == "__main__":
    main()
