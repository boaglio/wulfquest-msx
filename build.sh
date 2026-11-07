#!/usr/bin/env bash
# Builds build/wulfquest.rom (AGENTS.md §10). Needs Java 21+, Maven and
# Node.js; MSXgl's own SDCC and tools come from the msxgl submodule.
# Everything it writes is under build/ (and Maven's target/ folders), which
# is never committed. Maven options can be passed in MAVEN_ARGS.
#
#   ./build.sh          convert the data, then build the ROM
#   ./build.sh test     the same, then the converter's tests and every test in test/
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
command -v mvn >/dev/null || die "Maven (and Java 21+) is needed to run the converter"

tools/check-no-binaries.sh

# The converter (AGENTS.md §4): wulfquest/data/ into build/gen/, through the
# Java game's own core, built from the submodule.
mkdir -p build
mvn -q -pl convert -am package -DskipTests exec:java > build/convert.log 2>&1 \
    || { tail -30 build/convert.log >&2; die "the converter failed (build/convert.log)"; }
echo "build.sh: build/gen/ ($(tail -1 build/gen/report.txt); see build/gen/report.txt)"

# MSXgl takes the folder it is run from as the project.
(cd src && node ../msxgl/engine/script/js/build.js) > build/msxgl.log 2>&1 \
    || { tail -30 build/msxgl.log >&2; die "the MSXgl build failed (build/msxgl.log)"; }

rom=$(find build/msxgl -name 'wulfquest.rom' -newer build/gen/palette.h 2>/dev/null | head -1)
[[ -n "$rom" ]] || { tail -30 build/msxgl.log >&2; die "MSXgl wrote no wulfquest.rom"; }
cp "$rom" build/wulfquest.rom

# src/data.c maps blobs into bank 1 (8000h), so it must run from segment 0,
# below 8000h. It is linked first and wulfquest.c's main comes after it, so
# main below 8000h means all of data.c is.
main_at=$(awk '$2 == "_main" { print $1 }' build/msxgl/wulfquest.map)
[[ -n "$main_at" ]] && (( 16#$main_at < 16#8000 )) \
    || die "src/data.c no longer fits in ROM segment 0 (main at ${main_at:-?}h)"
# And everything data.c calls must be there too: with bank 1 switched to a
# data segment, a call above 8000h (SDCC's library, for a division) lands in
# data. Every call in data.c's listing, to an address in the map, below 8000h.
bad=$(awk '$NF ~ /^_/ && $(NF-1) == "call" { print $NF }' build/msxgl/data.lst | sort -u | while read -r f; do
    a=$(awk -v s="$f" '$2 == s { print $1 }' build/msxgl/wulfquest.map)
    if [[ -n "$a" ]] && (( 16#$a >= 16#8000 )); then echo "$f"; fi
done)
[[ -z "$bad" ]] || die "src/data.c calls code above 8000h, which its bank switching hides: $bad"
# Banked code (src/wulfquest_sN_b1.c, data.c's Bank_Call) runs with its
# segment in bank 1, so it, and the segment-0 code it calls (rng.c, move.c),
# may call only code below 8000h, or its own segment's.
for f in src/wulfquest_s*_b1.c; do
    seg=${f#src/wulfquest_s}
    seg=${seg%_b1.c}
    rom_first=$(awk '$2 == "ROM_BANKED_CODE_FIRST" { print $3 }' build/gen/rom.h)
    (( seg >= rom_first )) || die "$f: segment $seg holds data; banked code goes in $rom_first and up"
    # The header that calls into it names the same segment (JUNGLE_SEG and the like).
    mod=$(sed -n 's/^#include "\(.*\)\.c"$/\1/p' "$f" | head -1)
    grep -Eq "^#define [A-Z]+_SEG[[:space:]]+$seg\b" "src/$mod.h" \
        || die "$f holds $mod.c, but src/$mod.h does not name segment $seg"
done
for lst in $(for f in src/wulfquest_s*_b1.c; do echo "build/msxgl/$(basename "$f" .c).lst"; done) \
           build/msxgl/rng.lst build/msxgl/move.lst; do
    [[ -f "$lst" ]] || die "$lst is missing: the bank check cannot run"
    bad=$(awk '$NF ~ /^_/ && ($(NF-1) == "call" || $(NF-1) == "jp") { print $NF }' "$lst" | sort -u | while read -r f; do
        a=$(awk -v s="$f" '$2 == s { print $1 }' build/msxgl/wulfquest.map)
        if [[ -n "$a" ]] && (( 16#$a >= 16#8000 && 16#$a < 16#10000 )); then echo "$f"; fi
    done)
    [[ -z "$bad" ]] || die "$(basename "$lst" .lst) calls code in segment 1, which banked code hides: $bad"
done
echo "build.sh: build/wulfquest.rom ($(stat -c %s build/wulfquest.rom) bytes)"

if [[ "${1:-}" == test ]]; then
    echo "build.sh: the converter's tests"
    mvn -q -pl convert -am verify -Dtest='wulfmsx.**' > build/convert-test.log 2>&1 \
        || { tail -40 build/convert-test.log >&2; die "the converter's tests failed (build/convert-test.log)"; }
    for t in test/*.sh; do
        echo "build.sh: $t"
        "$t" || die "$t failed"
    done
    echo "build.sh: every test passed"
fi
