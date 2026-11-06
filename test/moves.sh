#!/usr/bin/env bash
# AGENTS.md §10, M3 and M4: Ranger Vale moves on the MSX2 exactly as in the
# Java game, room to room. The Java game plays test/moves.json and
# test/flips.json (convert's MoveTraces); openMSX plays the same keys through
# the ROM, on a 50 Hz and a 60 Hz MSX2 (test/moves.tcl); check_moves.py
# compares them tick by tick, and checks the speeds, the wall slide, the
# walls, the loop's timing, the route across the map, every edge between two
# rooms, and that every flip shows a whole room, swapped while the screen is
# blank (the rooms as RenderRooms draws them).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

rom=build/wulfquest.rom
map=build/msxgl/wulfquest.map
[[ -f "$rom" && -f "$map" ]] || { echo "moves.sh: no $rom (run ./build.sh)" >&2; exit 1; }
command -v openmsx >/dev/null || { echo "moves.sh: openMSX is needed" >&2; exit 1; }
mkdir -p build/test

mvn -q -pl convert -am package -DskipTests exec:java@move-traces exec:java@flip-traces exec:java@render-rooms \
    > build/test/move-traces.log 2>&1 \
    || { tail -30 build/test/move-traces.log >&2; echo "moves.sh: MoveTraces failed" >&2; exit 1; }

names="g_ValeX g_ValeY g_ValeFacing g_ValeSwing g_ValeWalk g_SabreX g_SabreY g_SabreW g_SabreH
       g_ValeRoom g_ValeFreeze g_Ticks g_Frames g_LateFrames g_Flips g_FlipFrames g_DrawFrames
       g_TestStart g_TestKeys g_TestOpen g_TestX g_TestY
       Test_BeforeTick Test_FrameStart Test_FrameEnd Test_Shown"
syms=""
for n in $names; do
    a=$(awk -v s="_$n" '$2 == s { print "0x" substr($1, length($1) - 3) }' "$map")
    [[ -n "$a" ]] || { echo "moves.sh: $n is not in $map" >&2; exit 1; }
    syms+="$n=$a "
done
field=$(( $(awk '$2 == "PLAYFIELD_H" { print $3 }' build/gen/constants.h) * 128 ))

fail=0
for plan in ${MOVES_PLANS:-moves flips}; do
    for m in C-BIOS_MSX2_EU:50 C-BIOS_MSX2_JP:60; do
        machine=${m%%:*} hz=${m##*:}
        out=build/test/$plan-$machine.txt
        MOVES_OUT="$PWD/$out" MOVES_PLAN="$PWD/build/test/${plan}_plan.txt" MOVES_SYMS="$syms" \
            MOVES_FIELD="$field" timeout 600 \
            env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
            openmsx -machine "$machine" -carta "$rom" -script test/moves.tcl >"$out.log" 2>&1 \
            || { echo "moves.sh: openMSX failed on $machine (see $out.log)" >&2; fail=1; continue; }
        python3 -I test/check_moves.py "$out" "build/test/${plan}_expected.txt" "build/test/${plan}_plan.txt" \
            build/gen/constants.h "$hz" build/test/rooms_expected.bin || fail=1
    done
done
exit $fail
