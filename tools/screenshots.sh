#!/usr/bin/env bash
# Screenshots for a release (AGENTS.md §10, M14), never committed: the
# title, the intro, a game, its pause menu and its map, from build/wulfquest.rom
# on openMSX's C-BIOS MSX2, into build/shots/*.png (512x424).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
command -v openmsx >/dev/null || { echo "screenshots.sh: openMSX is needed" >&2; exit 1; }
map=build/msxgl/wulfquest.map
[[ -f build/wulfquest.rom && -f "$map" ]] || { echo "screenshots.sh: build first (./build.sh)" >&2; exit 1; }
addr() { awk -v s="_$1" '$2 == s { print "0x" substr($1, length($1) - 3) }' "$map"; }
rm -rf build/shots
mkdir -p build/shots
SHOTS_DIR="$PWD/build/shots" SHOTS_SHELL="$(addr g_Shell)" SHOTS_TICK="$(addr g_ShellTick)" SHOTS_VISIBLE="$(addr g_ValeVisible)" SHOTS_INVULN="$(addr g_ValeInvuln)" timeout 300 \
    env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
    openmsx -machine C-BIOS_MSX2_EU -carta build/wulfquest.rom -script tools/screenshots.tcl > build/shots/openmsx.log 2>&1
python3 tools/screenshots.py build/shots
[[ -n "${SHOTS_KEEP:-}" ]] || rm -f build/shots/*.regs build/shots/*.pal build/shots/*.vram
