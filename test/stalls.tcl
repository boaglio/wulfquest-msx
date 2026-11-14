# openMSX script for test/stalls.sh (AGENTS.md §10, M13 part 2): a key
# pressed and let go while the game is busy (the first room's drawing, a
# stall of a dozen frames) is not lost: F1 tapped then pauses the game. It
# writes "tapped SHELL" and "after SHELL" to $env(STALLS_OUT), the shell's
# state (shell.h) as the key was let go and two seconds on.
set throttle off
set ::out [open $env(STALLS_OUT) w]
proc peek {a} { debug read memory $a }
proc log {s} { puts $::out $s; flush $::out }
proc done {} { close $::out; exit }
set ::tapped 0
proc up {} { keymatrixup 6 0x20; log "tapped [peek $::env(STALLS_SHELL)]" }
debug breakpoint create -address $env(STALLS_DRAW) \
    -condition {[debug read memory $::env(STALLS_BANK)] == 1} -command {
    if {!$::tapped} {
        set ::tapped 1
        keymatrixdown 6 0x20      ;# F1
        after time 0.04 up        ;# two frames: still in the drawing
        after time 2.0 { log "after [peek $::env(STALLS_SHELL)]"; done }
    }
}
# From the title, a new game: Space held long enough for the title's own pace.
proc press {r m} { keymatrixdown $r $m; after time 0.6 [list keymatrixup $r $m] }
after time 6 { press 8 1 }
after time 8 { press 8 1 }
after time 10 { press 8 1 }
after time 40 { log "timeout"; done }
