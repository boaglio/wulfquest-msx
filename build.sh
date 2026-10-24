#!/usr/bin/env bash
# Builds build/wulfquest.rom (AGENTS.md §10, M0). Needs Node.js; MSXgl's own
# SDCC and tools come from the msxgl submodule. Everything it writes is under
# build/, which is never committed.
#
#   ./build.sh          build the ROM
#   ./build.sh test     build, then run every test in test/
#   ./build.sh clean    remove build/
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

die() { echo "build.sh: $*" >&2; exit 1; }

case "${1:-}" in
    clean) rm -rf build; exit 0 ;;
    ""|test) ;;
    *) die "unknown command '$1' (try: test, clean)" ;;
esac

[[ -f msxgl/engine/script/js/build.js && -d wulfquest/data ]] \
    || die "the submodules are missing: git submodule update --init"
command -v node >/dev/null || die "Node.js is needed to run MSXgl's build tool"

tools/check-no-binaries.sh

mkdir -p build/gen
python3 tools/gen_palette.py wulfquest/data/art/palette.json build/gen/palette.h

# MSXgl takes the folder it is run from as the project.
(cd src && node ../msxgl/engine/script/js/build.js) > build/msxgl.log 2>&1 \
    || { tail -30 build/msxgl.log >&2; die "the MSXgl build failed (build/msxgl.log)"; }

rom=$(find build/msxgl -name 'wulfquest.rom' -newer build/gen/palette.h 2>/dev/null | head -1)
[[ -n "$rom" ]] || { tail -30 build/msxgl.log >&2; die "MSXgl wrote no wulfquest.rom"; }
cp "$rom" build/wulfquest.rom
echo "build.sh: build/wulfquest.rom ($(stat -c %s build/wulfquest.rom) bytes)"

if [[ "${1:-}" == test ]]; then
    for t in test/*.sh; do
        echo "build.sh: $t"
        "$t" || die "$t failed"
    done
    echo "build.sh: every test passed"
fi
