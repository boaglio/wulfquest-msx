#!/usr/bin/env bash
# AGENTS.md §10, M11: the saves on a disk, through MSX-DOS, across a power
# cycle. Needs an MSX2 with a disk drive: the Philips NMS 8245 (its system ROM
# is not free: put it in roms/, which git ignores; this test is skipped
# without it). On a blank 720 KB disk, a first run of openMSX starts a game
# (saved to slot 1 at once: the file is made), walks, saves to slot 2 from the
# pause menu and leaves for the title (kept there); a second run, a fresh
# machine with the same disk, must offer that game first (CONTINUE) and load
# it back to the same room, place, score, lives, quarters, rooms walked and
# treasures. check_disk.py reads the screens' words and compares the games.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

rom=build/wulfquest.rom
map=build/msxgl/wulfquest.map
[[ -f "$rom" && -f "$map" ]] || { echo "disk.sh: no $rom (run ./build.sh)" >&2; exit 1; }
command -v openmsx >/dev/null || { echo "disk.sh: openMSX is needed" >&2; exit 1; }
machine=Philips_NMS_8245
# openMSX finds a machine's ROMs by their checksum in its systemroms folder.
roms="${OPENMSX_HOME:-$HOME/.openMSX}/share/systemroms"
if ! ls roms/*.rom >/dev/null 2>&1; then
    echo "skip disk: no MSX2 disk ROM in roms/ (see DEVELOPER.md)"
    exit 0
fi
mkdir -p "$roms" build/test build/shots
for r in roms/*.rom; do ln -sf "$PWD/$r" "$roms/"; done

syms=""
for n in g_Booted g_Shell g_ShellTick g_DiskReady g_ValeRoom g_ValeX g_ValeY g_Score g_ValeLives g_SimTick \
         g_QuestPieces g_Visited g_TreasuresTaken; do
    a=$(awk -v s="_$n" '$2 == s { print "0x" substr($1, length($1) - 3) }' "$map")
    [[ -n "$a" ]] || { echo "disk.sh: $n is not in $map" >&2; exit 1; }
    syms+="$n $a "
done
joy=$(awk '$2 == "_Data_JoyPortA" { print "0x" substr($1, length($1) - 3) }' "$map")

# A blank disk, made by openMSX's own disk tool.
dsk="$PWD/build/test/disk.dsk"
rm -f "$dsk"
echo "diskmanipulator create {$dsk} 720k; exit" > build/test/disk-make.tcl
timeout 120 env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
    openmsx -machine C-BIOS_MSX2_EU -script build/test/disk-make.tcl > build/test/disk-make.log 2>&1 || true
[[ -s "$dsk" ]] || { echo "disk.sh: openMSX made no disk image (build/test/disk-make.log)" >&2; exit 1; }

DOWN="key 8 0x40"; SPACE="key 8 0x01"; ESC="key 7 0x04"; F1="key 6 0x20"
STATE='{[peek g_ValeRoom] [peek g_ValeX 4] [peek g_ValeY 4] [peek g_Score 4] [peek g_ValeLives] [peek g_SimTick 4] [peek g_QuestPieces] [peek g_Visited 4] [peek g_TreasuresTaken 2]}'
run() {   # output, plan...
    local out=$1; shift
    local plan_tcl=""
    for s in "$@"; do plan_tcl+="{$s} "; done
    SHELL_OUT="$PWD/$out" SHELL_SYMS="$syms" SHELL_JOY="$joy" SHELL_PLAN="$plan_tcl" timeout 900 \
        env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
        openmsx -machine "$machine" -carta "$rom" -diska "$dsk" -script test/shell.tcl >"$out.log" 2>&1 \
        || { echo "disk.sh: openMSX failed (see $out.log)" >&2; exit 1; }
}
# Shell states (shell.h): title 0, playing 4, paused 5, save 6, new game 13.
run build/test/disk-1.txt \
    "wait 0" "log ready {[peek g_DiskReady]}" "pause 0.3" "dump title0" \
    "$SPACE" "wait 13" "dump newgame" "pause 0.6" "$SPACE" "wait 4" "pause 0.5" \
    "hold 8 0x80 1.2" "pause 0.5" \
    "$F1" "wait 5" "$DOWN" "$SPACE" "wait 6" "$DOWN" "pause 0.3" "$SPACE" "wait 4" "pause 1.6" "dump saved" \
    "$ESC" "wait 0" "log quit $STATE" "pause 0.3" "dump title1" "log run1 done"
run build/test/disk-2.txt \
    "wait 0" "log ready {[peek g_DiskReady]}" "pause 0.3" "dump title2" \
    "$SPACE" "wait 4" "log loaded $STATE" "pause 1.6" "dump loaded" "log run2 done"
cat build/test/disk-1.txt build/test/disk-2.txt | grep -v "^end ok" > build/test/disk.txt
for f in build/test/disk-1.txt build/test/disk-2.txt; do
    grep -q "^end ok" "$f" || { echo "FAIL disk: $(basename "$f"): $(grep '^end' "$f")"; exit 1; }
done
echo "end ok" >> build/test/disk.txt
python3 -I test/check_disk.py build/test/disk.txt build/gen build/shots/disk
