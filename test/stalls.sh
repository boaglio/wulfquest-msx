#!/usr/bin/env bash
# The stalls (AGENTS.md §10, M13 part 2): a key pressed during one is not
# lost. On a 50 Hz and a 60 Hz MSX2 (C-BIOS), a game is started and F1 tapped
# (pressed and let go within two frames) while its first room is drawn: the
# game must be paused after it (the frame's hook keeps keys held while the
# game does not read them, data.c's Data_LatchKeys).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
command -v openmsx >/dev/null || { echo "stalls.sh: openMSX is needed" >&2; exit 1; }
rom=build/wulfquest.rom
map=build/msxgl/wulfquest.map
[[ -f "$rom" && -f "$map" ]] || { echo "stalls.sh: build first (./build.sh)" >&2; exit 1; }
addr() { awk -v s="_$1" '$2 == s { print "0x" substr($1, length($1) - 3) }' "$map"; }
draw=$(python3 tools/profile.py --addr Room_Draw | sed 's/Room_Draw=\(0x[0-9A-F]*\)@1/\1/')
shell=$(addr g_Shell)
bank=$(addr g_Bank1Segment)
mkdir -p build/test
fail=0
for machine in C-BIOS_MSX2_EU C-BIOS_MSX2_JP; do
    out=build/test/stalls-$machine.txt
    rm -f "$out"
    STALLS_OUT="$PWD/$out" STALLS_DRAW="$draw" STALLS_SHELL="$shell" STALLS_BANK="$bank" timeout 300 \
        env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
        openmsx -machine "$machine" -carta "$rom" -script test/stalls.tcl > "$out.log" 2>&1 || true
    # Shell states (shell.h): playing 4, paused 5.
    if grep -q "^tapped 4$" "$out" 2>/dev/null && grep -q "^after 5$" "$out"; then
        echo "ok   $machine: F1 tapped during the first room's drawing, the game paused"
    else
        echo "FAIL $machine: F1 tapped during the first room's drawing was lost ($(tr '\n' ' ' < "$out" 2>/dev/null))"
        fail=1
    fi
done
exit $fail
