#!/usr/bin/env bash
# AGENTS.md §10, M0, M2 and M10: the ROM boots on a 50 Hz and a 60 Hz MSX2, in
# SCREEN 5 with the game's palette, to the title; Space starts a game, which
# draws a room, and F5 shows another
# (test/rooms.sh checks the rooms themselves); on an MSX1 it says
# an MSX2 is needed. Uses C-BIOS, the free MSX BIOS that ships with openMSX.
# Pictures of what each MSX2 showed go to build/shots/.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

rom=build/wulfquest.rom
[[ -f "$rom" ]] || { echo "boot.sh: no $rom (run ./build.sh)" >&2; exit 1; }
command -v openmsx >/dev/null || { echo "boot.sh: openMSX is needed" >&2; exit 1; }
mkdir -p build/test build/shots

map=build/msxgl/wulfquest.map
addr() { awk -v s="_$1" '$2 == s { print "0x" substr($1, length($1) - 3) }' "$map"; }
shown=$(addr Test_Shown); count=$(addr g_DrawCount); shell=$(addr g_Shell); tick=$(addr g_ShellTick)
[[ -n "$shown" && -n "$count" ]] || { echo "boot.sh: Test_Shown or g_DrawCount is not in $map" >&2; exit 1; }
run() {   # machine, output file
    rm -f "$PWD/$2.started"
    BOOT_OUT="$PWD/$2" BOOT_SHOWN="$shown" BOOT_COUNT="$count" BOOT_SHELL="$shell" BOOT_TICK="$tick" timeout 120 env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
        openmsx -machine "$1" -carta "$rom" -script test/boot.tcl >"$2.log" 2>&1 \
        || { echo "boot.sh: openMSX failed on $1 (see $2.log)" >&2; return 1; }
}

fail=0
for m in C-BIOS_MSX2_EU:50 C-BIOS_MSX2_JP:60 C-BIOS_MSX1_EU:msx1; do
    machine=${m%%:*} want=${m##*:}
    out=build/test/boot-$machine.txt
    run "$machine" "$out"
    python3 -I test/check_boot.py "$out" "$want" build/gen/palette.h "build/shots/boot-$machine" \
        || fail=1
done
exit $fail
