#!/usr/bin/env bash
# AGENTS.md §10, M10: the shell. On a 50 Hz and a 60 Hz MSX2, test/shell.tcl
# walks every screen from the title and back, with the keys and then with
# joystick 1: the options, the hi-scores, the attract demo to its end, the
# map over its game, a hi-score's three letters, a game's pause menu, its
# options and its map, the reveal, game over and a continue, the win's sum.
# check_shell.py reads each screen's words with the ROM's own font and wants
# each one shell.bin's (the data's), the demo to end where the Java game's
# does, and its map to be the Java game's MapScreen pixel for pixel.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

rom=build/wulfquest.rom
map=build/msxgl/wulfquest.map
[[ -f "$rom" && -f "$map" ]] || { echo "shell.sh: no $rom (run ./build.sh)" >&2; exit 1; }
command -v openmsx >/dev/null || { echo "shell.sh: openMSX is needed" >&2; exit 1; }
mkdir -p build/test build/shots

syms=""
for n in g_Shell g_ShellTick g_DemoTicks g_ValeRoom g_ValeX g_ValeY g_Score g_ValeLives g_TestMapOpen \
         g_QuestSlots g_QuestPieces g_ValeMode g_ContinuesUsed g_BestScore; do
    a=$(awk -v s="_$n" '$2 == s { print "0x" substr($1, length($1) - 3) }' "$map")
    [[ -n "$a" ]] || { echo "shell.sh: $n is not in $map" >&2; exit 1; }
    syms+="$n $a "
done
# Just after data.c's Data_Joystick reads the PSG's port A.
joy=$(awk '$2 == "_Data_JoyPortA" { print "0x" substr($1, length($1) - 3) }' "$map")
[[ -n "$joy" ]] || { echo "shell.sh: Data_JoyPortA is not in $map" >&2; exit 1; }

# The keys (row, bit of the keyboard matrix) and the joystick's bits.
UP="key 8 0x20"; DOWN="key 8 0x40"; SPACE="key 8 0x01"; ESC="key 7 0x04"; F1="key 6 0x20"; M="key 4 0x04"
J_UP="joy 1"; J_DOWN="joy 2"; J_FIRE="joy 16"; J_B="joy 32"
# Shell states (shell.h): title 0, attract 1, hi-scores 2, options 3, playing 4, paused 5,
# reveal 8, map 9, game over 10, won 11, entry 12.
plan=(
    # The title, its options page, the hi-scores, and back.
    "wait 0" "dump title"
    "$DOWN" "$SPACE" "wait 3" "dump options"
    "$DOWN" "$DOWN" "$DOWN" "$DOWN" "pause 0.6" "$SPACE" "wait 2" "dump hiscores"
    "pause 0.6" "$SPACE" "wait 3" "$ESC" "wait 0"
    # Left alone: the demo, to its end; then the map over its game.
    "until {[peek g_Shell] == 1 && [peek g_DemoTicks 2] >= 100}" "dump attract"
    "until {[peek g_Shell] == 0 && [peek g_ShellTick 2] >= 2}"
    "log demo {[peek g_DemoTicks 2] [peek g_ValeRoom] [peek g_ValeX 4] [peek g_ValeY 4] [peek g_Score 4] [peek g_ValeLives]}"
    "poke g_TestMapOpen 1" "until {[peek g_Shell] == 9 && [peek g_ShellTick 2] >= 1}" "dump map"
    # Escape: the demo's run is offered to the table; three letters (C, A, A).
    "$ESC" "wait 12" "dump entry"
    "$DOWN" "$DOWN" "$SPACE" "$SPACE" "$SPACE" "wait 2" "dump hiscores2"
    "pause 0.6" "$SPACE" "wait 0"
    # A game: its pause menu, the options over it, its map.
    "$SPACE" "wait 4" "pause 0.5" "dump game"
    "$F1" "wait 5" "dump pause"
    "$DOWN" "$DOWN" "$DOWN" "$SPACE" "wait 3" "dump ingameopt"
    "$ESC" "wait 5" "$ESC" "wait 4"
    "$M" "wait 9" "dump map2" "$M" "wait 4"
    # The screens a game's events bring, brought on: a quarter, game over and a continue, the win.
    "poke g_QuestSlots 1" "poke g_QuestPieces 1" "wait 8" "dump reveal"
    "until {[peek g_ShellTick 2] >= 52}" "dump reveal2" "$SPACE" "wait 4"
    "poke g_ValeMode 3" "wait 10" "pause 0.6" "dump gameover" "$SPACE" "wait 4"
    "log continued {[peek g_ContinuesUsed]}"
    "poke g_BestScore 0 4" "poke g_ValeMode 5" "wait 11" "until {[peek g_ShellTick 2] >= 101}" "dump won"
    "$SPACE" "wait 0" "log keys done"
    # The same ways with the joystick: options, hi-scores, back, a game, its map.
    "$J_DOWN" "$J_FIRE" "wait 3" "pause 0.6"
    "$J_DOWN" "$J_DOWN" "$J_DOWN" "$J_DOWN" "$J_FIRE" "wait 2" "dump joyhiscores"
    "pause 0.6" "$J_FIRE" "wait 3" "$J_DOWN" "pause 0.6" "$J_FIRE" "wait 0"
    "$J_UP" "$J_FIRE" "wait 4" "pause 0.5" "$J_B" "wait 9" "dump joymap" "$J_B" "wait 4"
    "log joystick done"
)
plan_tcl=""
for s in "${plan[@]}"; do plan_tcl+="{$s} "; done

fail=0
for m in C-BIOS_MSX2_EU:50 C-BIOS_MSX2_JP:60; do
    machine=${m%%:*}
    out=build/test/shell-$machine.txt
    SHELL_OUT="$PWD/$out" SHELL_SYMS="$syms" SHELL_JOY="$joy" SHELL_PLAN="$plan_tcl" timeout 900 \
        env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
        openmsx -machine "$machine" -carta "$rom" -script test/shell.tcl >"$out.log" 2>&1 \
        || { echo "shell.sh: openMSX failed on $machine (see $out.log)" >&2; fail=1; continue; }
    python3 -I test/check_shell.py "$out" build/gen "build/shots/shell-$machine" || fail=1
done
exit $fail
