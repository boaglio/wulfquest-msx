#!/usr/bin/env bash
# AGENTS.md §10, M9: the panel shows every item the Java panel shows, laid
# out for the MSX2 (panel.c): test/panel.tcl sets each case's state in an empty
# room, check_panel.py draws the panel it should be from the converter's font
# and images and compares the video RAM, pixel for pixel.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

rom=build/wulfquest.rom
map=build/msxgl/wulfquest.map
[[ -f "$rom" && -f "$map" ]] || { echo "panel.sh: no $rom (run ./build.sh)" >&2; exit 1; }
command -v openmsx >/dev/null || { echo "panel.sh: openMSX is needed" >&2; exit 1; }
mkdir -p build/test

syms=""
for n in g_TestStart g_TestJungle g_TestOpen g_TestX g_TestY g_Score g_BestScore g_ValeLives g_QuestSlots g_Easy \
         g_HintTicks g_HintDirection g_HintToExit g_EffectKind g_EffectOrchid g_EffectTicks g_EffectTotal g_ValeMode; do
    a=$(awk -v s="_$n" '$2 == s { print "0x" substr($1, length($1) - 3) }' "$map")
    [[ -n "$a" ]] || { echo "panel.sh: $n is not in $map" >&2; exit 1; }
    syms+="$n=$a "
done
x=$(awk '$2 == "PLAYER_START_X_FP" { sub(/L$/, "", $3); print $3 }' build/gen/player.h)
y=$(awk '$2 == "PLAYER_START_Y_FP" { sub(/L$/, "", $3); print $3 }' build/gen/player.h)
top=$(awk '$2 == "PLAYFIELD_H" { print $3 }' build/gen/constants.h)
torpor=$(awk '$2 == "EFFECT_TORPOR" { print $3 }' build/gen/game_data.h)
over=$(awk '$1 == "enum" && /MODE_GAME_OVER/ { print 3 }' src/vale.h)

# Each case: what to set (name[:bytes]=value), and what check_panel.py draws.
base="g_EffectKind=0 g_EffectOrchid=255 g_HintTicks=0 g_ValeMode=0"
cases="plain g_Score:4=1234567 g_BestScore:4=7654321 g_ValeLives=3 g_QuestSlots=5 g_Easy=0 $base"
cases+=";hint g_ValeLives=8 g_QuestSlots=15 g_Easy=1 $base g_HintTicks=50 g_HintDirection=1 g_HintToExit=0"
cases+=";bar g_Easy=0 $base g_EffectKind=$torpor g_EffectOrchid=0 g_EffectTicks:2=192 g_EffectTotal:2=384"
cases+=";arch $base g_HintTicks=50 g_HintDirection=5 g_HintToExit=1"
cases+=";over $base g_ValeMode=$over"
want='{"plain": {"score": 1234567, "best": 7654321, "lives": 3, "slots": 5, "easy": 0},
       "hint": {"score": 1234567, "best": 7654321, "lives": 8, "slots": 15, "easy": 1, "hint": 1},
       "bar": {"score": 1234567, "best": 7654321, "lives": 8, "slots": 15, "easy": 0, "orchid": 0, "left": 192, "total": 384},
       "arch": {"score": 1234567, "best": 7654321, "lives": 8, "slots": 15, "easy": 0, "hint": 13},
       "over": {"score": 1234567, "best": 7654321, "lives": 8, "slots": 15, "easy": 0, "over": 1}}'

out=build/test/panel.txt
PANEL_OUT="$PWD/$out" PANEL_SYMS="$syms" PANEL_CASES="$cases" PANEL_X="$x" PANEL_Y="$y" PANEL_TOP="$top" timeout 900 \
    env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
    openmsx -machine C-BIOS_MSX2_EU -carta "$rom" -script test/panel.tcl >"$out.log" 2>&1 \
    || { echo "panel.sh: openMSX failed (see $out.log)" >&2; exit 1; }
python3 -I test/check_panel.py "$out" build/gen "$want"
