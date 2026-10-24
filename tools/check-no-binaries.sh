#!/usr/bin/env bash
# AGENTS.md §2 — the binaries gate. Nothing binary is ever committed: art and
# sound come from wulfquest/data/ through the converter, and the ROM, disk
# images and converter output are build output under build/. This fails if
# git tracks (or is about to commit) any image, sound, ROM, disk or tape
# file, or any other file that is not text. The submodules are other
# repositories and are not looked into. No allowlist, no exceptions.
set -euo pipefail
cd "$(dirname "$0")/.."

readonly FORBIDDEN='\.(png|jpe?g|gif|bmp|ico|webp|tiff?|wav|mp3|ogg|flac|aiff?|midi?|ttf|otf|woff2?|rom|mx[12]|dsk|di[mk]|cas|tsx|wav|z80|sna|tap|tzx|bin|rel|ihx|lib|exe|com|sys)$'

fail=0
# Tracked and staged files; a submodule is a gitlink (mode 160000), not a file.
while IFS= read -r f; do
    if [[ "$f" =~ $FORBIDDEN ]]; then
        echo "FAIL: $f is a binary file type (AGENTS.md §2)" >&2
        fail=1
    elif [[ -f "$f" && -s "$f" ]] && ! LC_ALL=C grep -qI . "$f"; then
        echo "FAIL: $f is a binary file (AGENTS.md §2)" >&2
        fail=1
    fi
done < <(git ls-files -s | awk '$1 != "160000" { sub(/^[^\t]*\t/, ""); print }')

[[ $fail -eq 0 ]] && echo "check-no-binaries: OK"
exit $fail
