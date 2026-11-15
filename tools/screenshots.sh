#!/usr/bin/env bash
# Screenshots for a release (AGENTS.md §10, M14), never committed: the
# title, the intro, a game, its pause menu and its map, and two rooms of the
# demo's game and its map with the looking glass's trail (tools/screenshots_demo.tcl), from build/wulfquest.rom
# on openMSX's C-BIOS MSX2, into build/shots/*.png (512x424).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
command -v openmsx >/dev/null || { echo "screenshots.sh: openMSX is needed" >&2; exit 1; }
map=build/msxgl/wulfquest.map
[[ -f build/wulfquest.rom && -f "$map" ]] || { echo "screenshots.sh: build first (./build.sh)" >&2; exit 1; }
addr() { awk -v s="_$1" '$2 == s { print "0x" substr($1, length($1) - 3) }' "$map"; }
rm -rf build/shots
mkdir -p build/shots
if ! SHOTS_DIR="$PWD/build/shots" SHOTS_SHELL="$(addr g_Shell)" SHOTS_TICK="$(addr g_ShellTick)" SHOTS_VISIBLE="$(addr g_ValeVisible)" SHOTS_INVULN="$(addr g_ValeInvuln)" timeout 300 \
    env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
    openmsx -machine C-BIOS_MSX2_EU -carta build/wulfquest.rom -script tools/screenshots.tcl > build/shots/openmsx.log 2>&1; then
    echo "screenshots.sh: openMSX failed ($(openmsx -v 2>&1 | head -1)); its log:" >&2
    tail -30 build/shots/openmsx.log >&2
    exit 1
fi
# Three more from a real game: the attract demo's keys played into it (Vale's
# sabre and a rhino; Vale and the natives by the pool), at those ticks; then
# the map with the looking glass's trail to the nearest quarter.
glass=$(awk '$1 == "#define" && $2 == "GLASS_WAY_TICKS" { print $3 }' build/gen/game_data.h)
syms=""
for n in g_Shell g_ShellTick g_TestRecord g_TestRecordTicks g_RecTicks g_TestSeed g_TestX g_TestY Test_BeforeTick \
         g_WayTicks g_TestMapOpen g_TrailStage g_TestMapHold; do
    a=$(addr "$n"); [[ -n "$a" ]] || { echo "screenshots.sh: $n is not in $map" >&2; exit 1; }
    syms+="$n $a "
done
if ! SHOTS_DIR="$PWD/build/shots" SHOTS_SYMS="$syms" SHOTS_DEMO="$PWD/convert/src/main/resources/wulfmsx/convert/demo.txt" \
    SHOTS_AT="528 sabre 706 natives" SHOTS_MAP_AT=2300 SHOTS_GLASS="$glass" timeout 400 \
    env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
    openmsx -machine C-BIOS_MSX2_EU -carta build/wulfquest.rom -script tools/screenshots_demo.tcl > build/shots/openmsx-demo.log 2>&1; then
    echo "screenshots.sh: openMSX failed on the demo's game; its log:" >&2
    tail -30 build/shots/openmsx-demo.log >&2
    exit 1
fi
python3 tools/screenshots.py build/shots
[[ -n "${SHOTS_KEEP:-}" ]] || rm -f build/shots/*.regs build/shots/*.pal build/shots/*.vram
