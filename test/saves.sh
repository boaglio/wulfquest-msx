#!/usr/bin/env bash
# AGENTS.md §10, M11: the saves, on the tests' medium (save.h: video RAM, which
# a test gives the machine through g_TestMedium; the disk's are test/disk.sh's).
# test/shell.tcl walks it: a new game picks its slot and is saved there at once;
# the pause menu saves it to another slot; leaving for the title keeps it there,
# and the title offers it first (CONTINUE); continuing loads it back to the same
# room, place, score, lives, quarters, rooms walked and clock; a slot made bad
# is refused, saying why. check_saves.py reads each screen's words (shell.bin's)
# and the game's state before and after.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

rom=build/wulfquest.rom
map=build/msxgl/wulfquest.map
[[ -f "$rom" && -f "$map" ]] || { echo "saves.sh: no $rom (run ./build.sh)" >&2; exit 1; }
command -v openmsx >/dev/null || { echo "saves.sh: openMSX is needed" >&2; exit 1; }
mkdir -p build/test build/shots

syms=""
for n in g_Shell g_ShellTick g_TestMedium g_ValeRoom g_ValeX g_ValeY g_Score g_ValeLives g_SimTick \
         g_QuestPieces g_Visited g_TreasuresTaken; do
    a=$(awk -v s="_$n" '$2 == s { print "0x" substr($1, length($1) - 3) }' "$map")
    [[ -n "$a" ]] || { echo "saves.sh: $n is not in $map" >&2; exit 1; }
    syms+="$n $a "
done
joy=$(awk '$2 == "_Data_JoyPortA" { print "0x" substr($1, length($1) - 3) }' "$map")

UP="key 8 0x20"; DOWN="key 8 0x40"; SPACE="key 8 0x01"; ESC="key 7 0x04"; F1="key 6 0x20"
STATE='{[peek g_ValeRoom] [peek g_ValeX 4] [peek g_ValeY 4] [peek g_Score 4] [peek g_ValeLives] [peek g_SimTick 4] [peek g_QuestPieces] [peek g_Visited 4] [peek g_TreasuresTaken 2]}'
# The tests' medium: slot 3's record, at 7000h + 3 * 256 (save.c), made bad: its mark, then nonsense.
BAD="vpoke 0x7300 {87 81 1 71 9 9 9 9 9 9 9 9}"
# Shell states (shell.h): title 0, options 3, playing 4, paused 5, save 6, load 7, new game 13.
plan=(
    "wait 0" "poke g_TestMedium 1" "pause 0.5" "wait 0" "dump title0"
    # A new game: its slot first (the first empty), then saved there at once.
    "$SPACE" "wait 13" "dump newgame" "pause 0.6" "$SPACE" "wait 4" "pause 0.5"
    # Walking; then the pause menu's save, to slot 2.
    "hold 8 0x80 1.2" "pause 0.5"
    "$F1" "wait 5" "$DOWN" "$SPACE" "wait 6" "dump savepage" "$DOWN" "$SPACE" "wait 4" "pause 1.6" "dump saved"
    # Leaving for the title keeps the game in its slot; the title offers it first.
    "$ESC" "wait 0" "log quit $STATE" "pause 0.3" "dump title1"
    # Continuing loads it back.
    "$SPACE" "wait 4" "log loaded $STATE" "pause 1.6" "dump loaded"
    # A slot made bad is refused, saying why: the title's options, LOAD GAME, slot 3.
    "$ESC" "wait 0" "$BAD" "$DOWN" "$DOWN" "$SPACE" "wait 3" "pause 0.6"
    "$DOWN" "$DOWN" "$DOWN" "$SPACE" "wait 7" "pause 0.6" "$DOWN" "$DOWN" "$SPACE" "pause 0.3" "dump damaged"
    "log saves done"
)
plan_tcl=""
for s in "${plan[@]}"; do plan_tcl+="{$s} "; done

out=build/test/saves.txt
SHELL_OUT="$PWD/$out" SHELL_SYMS="$syms" SHELL_JOY="$joy" SHELL_PLAN="$plan_tcl" timeout 900 \
    env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
    openmsx -machine C-BIOS_MSX2_EU -carta "$rom" -script test/shell.tcl >"$out.log" 2>&1 \
    || { echo "saves.sh: openMSX failed (see $out.log)" >&2; exit 1; }
python3 -I test/check_saves.py "$out" build/gen build/shots/saves
