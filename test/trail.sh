#!/usr/bin/env bash
# AGENTS.md §10, M12: the looking glass's trail on the map, on a 50 Hz and a
# 60 Hz MSX2. The attract demo is played to its end (where the Java game's
# ends, test/shell.sh checks); then the glass is given to Vale (g_WayTicks)
# and the map opened over the demo's game. test/shell.tcl waits while the ROM
# finds the trail (trail.c, over some ticks) and dumps the map twice, a march
# step apart. check_trail.py wants on screen, dot for dot, the trail the
# converter's Trail (trail.c's reference) finds for that game (demo_trail.txt),
# marching, and says how much of the Java game's own way it is.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

rom=build/wulfquest.rom
map=build/msxgl/wulfquest.map
gen=build/gen
[[ -f "$rom" && -f "$map" ]] || { echo "trail.sh: no $rom (run ./build.sh)" >&2; exit 1; }
command -v openmsx >/dev/null || { echo "trail.sh: openMSX is needed" >&2; exit 1; }
mkdir -p build/test build/shots

syms=""
for n in g_Booted g_Shell g_ShellTick g_DemoTicks g_TestMapOpen g_WayTicks g_TrailStage g_TrailN g_TrailWhole g_TestMapHold; do
    a=$(awk -v s="_$n" '$2 == s { print "0x" substr($1, length($1) - 3) }' "$map")
    [[ -n "$a" ]] || { echo "trail.sh: $n is not in $map" >&2; exit 1; }
    syms+="$n $a "
done
joy=$(awk '$2 == "_Data_JoyPortA" { print "0x" substr($1, length($1) - 3) }' "$map")
def() { awk -v n="$2" '$1 == "#define" && $2 == n { print $3 }' "$gen/$1"; }
# Vale's room's cells (vale.c's, ram.h's RAM_CELLS: the lowest of the big arrays), which the search borrows.
cells=$(awk '$2 == "_g_RamLow" { print "0x" substr($1, length($1) - 3) }' "$map")
margin=$(awk '$1 == "#define" && $2 == "CELLS_MARGIN" { print $3 }' src/data.h)
cells_n=$(( ($(def constants.h CELL_COLS) + 2 * margin) * ($(def constants.h CELL_ROWS) + 2 * margin) ))   # data.h's CELLS_W * CELLS_H
march=$(def shell_data.h MAP_TRAIL_MARCH_TICKS)
glass=$(def game_data.h GLASS_WAY_TICKS)

# Shell states (shell.h): title 0, attract 1, playing 4, map 9. The trail's stages
# (trail.h): the openings 2, the trail 3, done 4.
SPACE="key 8 0x01"; M="key 4 0x04"
grab="[binary encode hex [debug read_block memory $cells $cells_n]]"
plan=(
    "wait 0" "until {[peek g_ShellTick 2] >= 2}"
    # A game, the glass, the map; left while the trail is being found (M held: a tick of the
# search is long), his room's cells back.
    "$SPACE" "wait 4" "pause 0.5" "log cells0 {$grab}"
    "poke g_WayTicks $glass 2" "$M" "until {[peek g_Shell] == 9 && [peek g_TrailStage] == 3}"
    "hold 4 0x04 1" "wait 4" "pause 0.2" "log cells1 {$grab [peek g_TrailStage]}"
    # Opened again: the openings' steps kept, straight to the trail; then left, the game ended.
    "$M" "until {[peek g_Shell] == 9 && [peek g_ShellTick 2] >= 1}" "log again {[peek g_TrailStage]}"
    "hold 7 0x04 1" "wait 0" "until {[peek g_ShellTick 2] >= 2}"
    # Left alone: the intro, then the demo, to its end.
    "until {[peek g_Shell] == 1 && [peek g_DemoTicks 2] >= 100}"
    "until {[peek g_Shell] != 1 || [peek g_DemoTicks 2] >= 1500}"
    "until {[peek g_Shell] == 0 && [peek g_ShellTick 2] >= 2}"
    # The glass, and the map.
    "poke g_WayTicks $glass 2" "poke g_TestMapOpen 1" "until {[peek g_Shell] == 9}"
    "until {[peek g_TrailStage] == 4 || [peek g_ShellTick 2] > 3000}"
    "log found {[peek g_ShellTick 2] [peek g_TrailN 2] [peek g_TrailWhole]}"
    # Each dump with the map held between two of its ticks; a march step or two between them.
    "poke g_TestMapHold 1" "until {[peek g_TestMapHold] == 2}" "dump trail1" "log at1 {[peek g_ShellTick 2]}" "poke g_TestMapHold 0"
    "pause 0.3" "poke g_TestMapHold 1" "until {[peek g_TestMapHold] == 2}" "dump trail2" "log at2 {[peek g_ShellTick 2]}"
)
plan_tcl=""
for s in "${plan[@]}"; do plan_tcl+="{$s} "; done

fail=0
for m in C-BIOS_MSX2_EU:50 C-BIOS_MSX2_JP:60; do
    machine=${m%%:*}
    out=build/test/trail-$machine.txt
    SHELL_OUT="$PWD/$out" SHELL_SYMS="$syms" SHELL_JOY="$joy" SHELL_PLAN="$plan_tcl" timeout 900 \
        env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
        openmsx -machine "$machine" -carta "$rom" -script test/shell.tcl >"$out.log" 2>&1 \
        || { echo "trail.sh: openMSX failed on $machine (see $out.log)" >&2; fail=1; continue; }
    python3 -I test/check_trail.py "$out" "$gen" "build/shots/trail-$machine" "$march" || fail=1
done
exit $fail
