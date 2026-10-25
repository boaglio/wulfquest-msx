#!/usr/bin/env bash
# Builds the ROM and plays it in openMSX, in a window.
#
#   ./run.sh                 build, then run on a 50 Hz MSX2 (C-BIOS_MSX2_EU)
#   ./run.sh jp              ... on a 60 Hz MSX2 (C-BIOS_MSX2_JP)
#   ./run.sh <machine>       ... on any openMSX machine, e.g. Philips_NMS_8245
#                            (real machines need their system ROMs installed)
#   ./run.sh -n [machine]    run the last build without building
#   ./run.sh [...] -- <args> pass the rest to openMSX
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

die() { echo "run.sh: $*" >&2; exit 1; }

build=1
machine=C-BIOS_MSX2_EU
extra=()
while (($#)); do
    case "$1" in
        -n) build=0 ;;
        eu) machine=C-BIOS_MSX2_EU ;;
        jp) machine=C-BIOS_MSX2_JP ;;
        --) shift; extra=("$@"); break ;;
        -*) die "unknown option '$1'" ;;
        *) machine=$1 ;;
    esac
    shift
done

command -v openmsx >/dev/null || die "openMSX is needed"
((build)) && ./build.sh
[[ -f build/wulfquest.rom ]] || die "no build/wulfquest.rom (run ./build.sh)"

exec openmsx -machine "$machine" -carta build/wulfquest.rom "${extra[@]}"
