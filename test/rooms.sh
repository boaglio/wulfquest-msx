#!/usr/bin/env bash
# AGENTS.md §10, M2: every room the MSX2 draws is the Java game's drawing of
# it, pixel for pixel, and the slowest takes under half a second on a 50 Hz
# MSX2. The Java game draws the expected rooms (convert's RenderRooms);
# openMSX walks the ROM through all 256 (test/rooms.tcl); check_rooms.py
# compares. Pictures of the start room, both ways, go to build/shots/.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

rom=build/wulfquest.rom
map=build/msxgl/wulfquest.map
[[ -f "$rom" && -f "$map" ]] || { echo "rooms.sh: no $rom (run ./build.sh)" >&2; exit 1; }
command -v openmsx >/dev/null || { echo "rooms.sh: openMSX is needed" >&2; exit 1; }
mkdir -p build/test build/shots

mvn -q -pl convert -am package -DskipTests exec:java@render-rooms > build/test/render-rooms.log 2>&1 \
    || { tail -30 build/test/render-rooms.log >&2; echo "rooms.sh: RenderRooms failed" >&2; exit 1; }

sym() { awk -v s="_$1" '$2 == s { print "0x" substr($1, length($1) - 3) }' "$map"; }
syms="$(sym g_Room) $(sym g_DrawCount) $(sym g_DrawFrames)"
[[ $(wc -w <<<"$syms") -eq 3 ]] || { echo "rooms.sh: the ROM's counters are not in $map" >&2; exit 1; }

out=build/test/rooms-C-BIOS_MSX2_EU
ROOMS_OUT="$PWD/$out" ROOMS_SYMS="$syms" timeout 300 \
    env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
    openmsx -machine C-BIOS_MSX2_EU -carta "$rom" -script test/rooms.tcl >"$out.log" 2>&1 \
    || { echo "rooms.sh: openMSX failed (see $out.log)" >&2; exit 1; }

python3 -I test/check_rooms.py "$out" build/test/rooms_expected.bin build/gen/constants.h \
    build/gen/palette.h build/shots/room
