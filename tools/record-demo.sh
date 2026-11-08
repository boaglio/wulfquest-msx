#!/usr/bin/env bash
# Records the attract demo on the MSX (AGENTS.md §7, M10). The converter's
# DemoRoute plays the game as it ships, Hard, from the start, to the quarters
# (QuestRoute, a swing every SWING ticks) for TICKS ticks, from the first seed
# in SEEDS whose game lasts that long; tools/record_demo.tcl plays those keys
# into the ROM's keyboard in openMSX with the ROM recording what it reads;
# what it read becomes convert/src/main/resources/wulfmsx/convert/demo.txt,
# which the converter puts in the ROM as the demo. The ROM must be built.
#   tools/record-demo.sh [SEEDS [TICKS [SWING]]]     (default 10 3000 40)
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
seeds=${1:-10} ticks=${2:-3000} swing=${3:-40}
rom=build/wulfquest.rom
map=build/msxgl/wulfquest.map
[[ -f "$rom" && -f "$map" ]] || { echo "record-demo.sh: no $rom (run ./build.sh)" >&2; exit 1; }
mkdir -p build/test

mvn -q ${MAVEN_ARGS:-} -pl convert -am package -DskipTests exec:java@demo-route \
    -Ddemo.seed="$seeds" -Ddemo.ticks="$ticks" -Ddemo.swing="$swing" > build/test/demo-route.log 2>&1 \
    || { tail -20 build/test/demo-route.log >&2; echo "record-demo.sh: DemoRoute failed" >&2; exit 1; }
grep "demo route" build/test/demo-route.log | tail -1

syms=""
for n in g_Shell g_ShellTick g_TestRecord g_TestRecordTicks g_RecTicks g_RecRuns g_TestSeed g_TestX g_TestY \
         Test_BeforeTick; do
    a=$(awk -v s="_$n" '$2 == s { print "0x" substr($1, length($1) - 3) }' "$map")
    [[ -n "$a" ]] || { echo "record-demo.sh: $n is not in $map" >&2; exit 1; }
    syms+="$n $a "
done
out=build/test/demo-recorded.txt
REC_SYMS="$syms" REC_ROUTE="$PWD/build/test/demo_route.txt" REC_OUT="$PWD/$out" timeout 900 \
    env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
    openmsx -machine C-BIOS_MSX2_EU -carta "$rom" -script tools/record_demo.tcl > "$out.log" 2>&1 \
    || { echo "record-demo.sh: openMSX failed (see $out.log)" >&2; exit 1; }

python3 -I - "$out" build/test/demo_route.txt convert/src/main/resources/wulfmsx/convert/demo.txt <<'PY'
import sys
rec, route, dest = sys.argv[1:]
lines = open(rec).read().split("\n")
end = [l for l in lines if l.startswith("end")]
if not end or not end[0].startswith("end ok"):
    sys.exit(f"record-demo.sh: the recording stopped: {end}")
runs = [tuple(map(int, l.split())) for l in lines if l and not l.startswith("end")]
played = [k for n, k in runs for _ in range(n)]
r = open(route).read().split("\n")
seed, start = r[0], r[1]
keys = [int(k) for k in r[2:] if k]
if played != keys:
    first = next(i for i in range(min(len(played), len(keys))) if played[i] != keys[i]) if played[:len(keys)] != keys[:len(played)] else min(len(played), len(keys))
    sys.exit(f"record-demo.sh: the MSX read other keys than were pressed, from tick {first} ({len(played)} read, {len(keys)} pressed)")
with open(dest, "w") as f:
    f.write("# The attract demo, recorded on the MSX (AGENTS.md §7, M10) by tools/record-demo.sh:\n")
    f.write("# the keys the ROM read each tick of a new game, Hard, from the start, as runs\n")
    f.write("# \"COUNT KEYS\" (up 1, down 2, left 4, right 8, fire 16).\n")
    f.write(seed + "\n" + start + "\n")
    for n, k in runs:
        f.write(f"{n} {k}\n")
print(f"record-demo.sh: {len(played)} ticks in {len(runs)} runs, as pressed, to {dest}")
PY
