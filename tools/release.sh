#!/usr/bin/env bash
# The release's files (AGENTS.md §10, M14), from the last build, into
# build/release/: the ROM, a blank 720 KB disk for the saves (made by
# openMSX's own disk tool), and screenshots (tools/screenshots.sh). Never
# committed; .github/workflows/release.yml attaches them to a pre-release.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
command -v openmsx >/dev/null || { echo "release.sh: openMSX is needed" >&2; exit 1; }
[[ -f build/wulfquest.rom ]] || { echo "release.sh: build first (./build.sh)" >&2; exit 1; }
out=build/release
rm -rf "$out"
mkdir -p "$out"
cp build/wulfquest.rom "$out/wulfquest.rom"
dsk="$PWD/$out/wulfquest-saves.dsk"
echo "diskmanipulator create {$dsk} 720k; exit" > build/release-disk.tcl
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
    openmsx -machine C-BIOS_MSX2_EU -script build/release-disk.tcl > build/release-disk.log 2>&1 || true
[[ -s "$dsk" ]] || { tail -5 build/release-disk.log >&2; echo "release.sh: no blank disk made" >&2; exit 1; }
tools/screenshots.sh > /dev/null
cp build/shots/*.png "$out/"
( cd "$out" && sha256sum wulfquest.rom wulfquest-saves.dsk > SHA256SUMS )
echo "release.sh: $(ls "$out" | tr '\n' ' ')"
