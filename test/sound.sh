#!/usr/bin/env bash
# AGENTS.md §10, M9: every effect and tune comes out of the PSG, on a 50 Hz and
# a 60 Hz MSX2, frame by frame as the converter's tables say (test/sound.tcl
# plays them, check_sound.py reads the registers); the tables are held to the
# Java game's synth by convert's SoundTest.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

rom=build/wulfquest.rom
map=build/msxgl/wulfquest.map
[[ -f "$rom" && -f "$map" ]] || { echo "sound.sh: no $rom (run ./build.sh)" >&2; exit 1; }
command -v openmsx >/dev/null || { echo "sound.sh: openMSX is needed" >&2; exit 1; }
mkdir -p build/test

syms=""
for n in g_TestStart g_TestJungle g_TestOpen g_TestX g_TestY g_TestSound Test_Sound; do
    a=$(awk -v s="_$n" '$2 == s { print "0x" substr($1, length($1) - 3) }' "$map")
    [[ -n "$a" ]] || { echo "sound.sh: $n is not in $map" >&2; exit 1; }
    syms+="$n=$a "
done
x=$(awk '$2 == "PLAYER_START_X_FP" { sub(/L$/, "", $3); print $3 }' build/gen/player.h)
y=$(awk '$2 == "PLAYER_START_Y_FP" { sub(/L$/, "", $3); print $3 }' build/gen/player.h)

fail=0
for m in C-BIOS_MSX2_EU:50 C-BIOS_MSX2_JP:60; do
    machine=${m%%:*} hz=${m##*:}
    # Each sound's frames, and a few more for the start.
    frames=$(python3 -I -c "
import sys
b = open('build/gen/sound$hz.bin', 'rb').read()
n = (b[0] | b[1] << 8) // 4
out = []
for i in range(n):
    at, k = b[4*i] | b[4*i+1] << 8, b[4*i+2] | b[4*i+3] << 8
    out.append(sum(b[at + 4*j + 3] for j in range(k)) + 4)
print(' '.join(map(str, out)))")
    out=build/test/sound-$machine.txt
    SOUND_OUT="$PWD/$out" SOUND_SYMS="$syms" SOUND_FRAMES="$frames" SOUND_X="$x" SOUND_Y="$y" timeout 900 \
        env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
        openmsx -machine "$machine" -carta "$rom" -script test/sound.tcl >"$out.log" 2>&1 \
        || { echo "sound.sh: openMSX failed on $machine (see $out.log)" >&2; fail=1; continue; }
    python3 -I test/check_sound.py "$out" "build/gen/sound$hz.bin" build/gen/sound_data.h "$hz" || fail=1
done
exit $fail
