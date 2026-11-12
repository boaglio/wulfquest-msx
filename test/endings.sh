#!/usr/bin/env bash
# AGENTS.md §10, M12: each of the three endings played through, on a 50 Hz and
# a 60 Hz MSX2. From a new machine's title, test/shell.tcl wins the first job
# (the escape brought on), and goes through its ending, tally, the roll's top
# (the job's own "the end") and Mr Jones's offer of the next job; the title
# then has that job under START, and the next game is it: its reveal (its own
# amulet and verse), its win, and so on to the last job's ride home, which
# offers nothing. check_endings.py reads each screen's words with the ROM's
# font and wants them the data's, and finds the ride's horses where the Java
# EndingScreen puts them.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

rom=build/wulfquest.rom
map=build/msxgl/wulfquest.map
gen=build/gen
[[ -f "$rom" && -f "$map" ]] || { echo "endings.sh: no $rom (run ./build.sh)" >&2; exit 1; }
command -v openmsx >/dev/null || { echo "endings.sh: openMSX is needed" >&2; exit 1; }
mkdir -p build/test build/shots

syms=""
for n in g_Booted g_Shell g_ShellTick g_RollTick g_ValeMode g_BestScore g_QuestSlots g_QuestPieces g_Job g_ShellJob \
         g_Unlocked g_Offered; do
    a=$(awk -v s="_$n" '$2 == s { print "0x" substr($1, length($1) - 3) }' "$map")
    [[ -n "$a" ]] || { echo "endings.sh: $n is not in $map" >&2; exit 1; }
    syms+="$n $a "
done
joy=$(awk '$2 == "_Data_JoyPortA" { print "0x" substr($1, length($1) - 3) }' "$map")

def() {   # a plain number define of a generated header
    awk -v n="$2" '$1 == "#define" && $2 == n { print $3 }' "$gen/$1"
}
macro() {   # a generated function-like macro's value: HEADER NAME ARG...
    python3 -I test/cmacro.py "$gen/$1" "${@:2}"
}
jobs=$(def game_data.h JOBS)
ride_job=$(def scenes_data.h ENDING_RIDE_JOB)
# The roll's top in sight: its first row a few lines up the screen.
roll_top=$(( ($(def scenes_data.h ROLL_FROM_Y) - 212 + 3 * $(def scenes_data.h ROLL_LINE)) * $(def scenes_data.h ROLL_TICKS_PER_PX) ))

# The keys (row, bit of the keyboard matrix). Shell states (shell.h): title 0, playing 4,
# reveal 8, ending 14, tally 15, roll 16, offer 18.
SPACE="key 8 0x01"; ESC="key 7 0x04"
plan=("wait 0")
for (( j = 0; j < jobs; j++ )); do
    prompt=$(macro scenes_data.h ENDING_PROMPT_AT $j)
    plan+=("dump title$j" "log title {$j [peek g_ShellJob] [peek g_Unlocked]}")
    plan+=("$SPACE" "wait 4" "pause 0.5" "log playing {$j [peek g_Job]}")
    # A piece taken: the job's reveal (its own quarters and verse).
    mask=$(macro game_data.h JOB_MASK $j 0); (( mask )) || mask=1   # the game itself: a quarter
    plan+=("poke g_QuestSlots $mask")
    plan+=("poke g_QuestPieces 1" "wait 8" "until {[peek g_ShellTick 2] >= 52}" "dump reveal$j" "$SPACE" "wait 4")
    # The win: the escape's walk, the closing scene to its prompt.
    plan+=("poke g_ValeMode 4" "wait 14")
    if [[ $j == "$ride_job" ]]; then
        for t in $(( $(def scenes_data.h RIDE_COME_AT) + $(def scenes_data.h RIDE_COME_TICKS) / 2 )) \
                 $(( $(def scenes_data.h RIDE_MOUNT_AT) + 10 )) \
                 $(( $(def scenes_data.h RIDE_AT) + $(def scenes_data.h RIDE_TICKS) / 4 )); do
            plan+=("until {[peek g_ShellTick 2] >= $t}" "dump ride$t" "log ride {$t [peek g_ShellTick 2]}")
        done
    fi
    plan+=("until {[peek g_ShellTick 2] >= $prompt + 5}" "dump ending$j")
    # The tally, then the roll's top; Escape leaves the roll.
    plan+=("$SPACE" "wait 15" "until {[peek g_ShellTick 2] >= 105}" "$SPACE" "wait 16" "poke g_BestScore 0 4")
    plan+=("until {[peek g_RollTick 2] >= $roll_top}" "dump roll$j" "log roll {$j [peek g_Offered]}" "$ESC")
    if (( j + 1 < jobs )); then
        offer=$(macro scenes_data.h OFFER_PROMPT_AT $(( j + 1 )))
        plan+=("wait 18" "until {[peek g_ShellTick 2] >= $offer + 5}" "dump offer$j" "$SPACE")
    fi
    plan+=("wait 0")
done
plan+=("dump titlelast" "log title {$jobs [peek g_ShellJob] [peek g_Unlocked]}")
plan_tcl=""
for s in "${plan[@]}"; do plan_tcl+="{$s} "; done

fail=0
for m in C-BIOS_MSX2_EU:50 C-BIOS_MSX2_JP:60; do
    machine=${m%%:*}
    out=build/test/endings-$machine.txt
    SHELL_OUT="$PWD/$out" SHELL_SYMS="$syms" SHELL_JOY="$joy" SHELL_PLAN="$plan_tcl" timeout 900 \
        env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
        openmsx -machine "$machine" -carta "$rom" -script test/shell.tcl >"$out.log" 2>&1 \
        || { echo "endings.sh: openMSX failed on $machine (see $out.log)" >&2; fail=1; continue; }
    python3 -I test/check_endings.py "$out" "$gen" "build/shots/endings-$machine" || fail=1
done
exit $fail
