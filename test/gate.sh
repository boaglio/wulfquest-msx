#!/usr/bin/env bash
# AGENTS.md §2, §10 M0: the binaries gate passes on text and fails on a ROM,
# an image, and a binary file with an innocent name. It runs the real gate
# in a scratch repository, so this repository's index is never touched.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
mkdir -p "$scratch/tools"
cp tools/check-no-binaries.sh "$scratch/tools/"
git -C "$scratch" init -q

expect() {   # pass|fail, what
    if (cd "$scratch" && tools/check-no-binaries.sh >/dev/null 2>&1); then got=pass; else got=fail; fi
    if [[ $got != "$1" ]]; then echo "FAIL gate: $2 should $1, it did not"; exit 1; fi
    echo "ok   gate: $2 → $1"
}

echo 'int main(void) { return 0; }' > "$scratch/a.c"
git -C "$scratch" add a.c tools
expect pass "text only"

printf 'AB\x00\x01' > "$scratch/game.rom"
git -C "$scratch" add game.rom
expect fail "a .rom"
git -C "$scratch" rm -q --cached game.rom

printf 'x' > "$scratch/shot.png"
git -C "$scratch" add shot.png
expect fail "an image"
git -C "$scratch" rm -q --cached shot.png

printf '\x00\x01\x02\x03' > "$scratch/notes.txt"
git -C "$scratch" add notes.txt
expect fail "a binary file named .txt"
