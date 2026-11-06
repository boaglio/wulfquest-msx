#!/usr/bin/env bash
# AGENTS.md §10, M5: the sprites on the MSX2 are what the converter cut. The
# ROM's test gallery shows every frame of Vale, every shape of his blade and
# every frame of the sprite bank, at the screen's edges too (test/sprites.tcl);
# check_sprites.py draws the sprite tables as the V9938 does and compares
# them with build/gen/, and checks that 14 sprites on one line take turns.
# It also times the blitter on a big creature, the measurement behind
# drawing them as sprites (AGENTS.md M5).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

rom=build/wulfquest.rom
map=build/msxgl/wulfquest.map
[[ -f "$rom" && -f "$map" ]] || { echo "sprites.sh: no $rom (run ./build.sh)" >&2; exit 1; }
command -v openmsx >/dev/null || { echo "sprites.sh: openMSX is needed" >&2; exit 1; }
mkdir -p build/test

names="g_DrawCount g_TestGallery g_TestValeFrame g_TestValeBlade g_TestValeX g_TestValeY g_TestCount
       g_TestFrame g_TestSpriteX g_TestSpriteY g_SpritesCrowded g_BenchGo g_BenchOp g_BenchW g_BenchH
       Test_Shown Test_BenchStart Test_BenchEnd"
syms=""
for n in $names; do
    a=$(awk -v s="_$n" '$2 == s { print "0x" substr($1, length($1) - 3) }' "$map")
    [[ -n "$a" ]] || { echo "sprites.sh: $n is not in $map" >&2; exit 1; }
    syms+="$n=$a "
done

plan=build/test/sprites_plan.txt
python3 -I test/check_sprites.py plan build/gen > "$plan"
out=build/test/sprites-C-BIOS_MSX2_EU.txt
SPRITES_OUT="$PWD/$out" SPRITES_PLAN="$PWD/$plan" SPRITES_SYMS="$syms" timeout 600 \
    env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
    openmsx -machine C-BIOS_MSX2_EU -carta "$rom" -script test/sprites.tcl >"$out.log" 2>&1 \
    || { echo "sprites.sh: openMSX failed (see $out.log)" >&2; exit 1; }
python3 -I test/check_sprites.py check build/gen "$plan" "$out"
