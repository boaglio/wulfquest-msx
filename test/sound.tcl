# openMSX script for test/sound.sh (AGENTS.md §10, M9). It does not judge:
# in an empty room (a test start without the jungle), it plays each sound
# through the ROM's test hook (g_TestSound) and writes the PSG's registers
# 0-10 after each interrupt's sound frame (a breakpoint on Test_Sound), to
# $env(SOUND_OUT): "sound ID", then a line of 11 numbers a frame.
set throttle off
foreach pair $env(SOUND_SYMS) {
    lassign [split $pair =] name addr
    set ::a($name) $addr
}
set out [open $env(SOUND_OUT) w]
set ::frames $env(SOUND_FRAMES)   ;# frames to record for each sound, in order
set ::recording -1
set ::left 0
proc poke {name value {n 1}} {
    for {set i 0} {$i < $n} {incr i} {
        debug write memory [expr {$::a($name) + $i}] [expr {($value >> (8 * $i)) & 0xFF}]
    }
}
proc finish {msg} { puts $::out "end $msg"; close $::out; exit }
proc on_sound {} {
    if {[catch {
        if {$::recording < 0} { return }
        if {$::left > 0} {
            set r {}
            for {set i 0} {$i <= 10} {incr i} { lappend r [debug read {PSG regs} $i] }
            puts $::out $r
            incr ::left -1
            if {$::left == 0} {
                poke g_TestSound 0xFF   ;# the tune stopped (0xFE + 1)
                after time 0.2 next_sound
            }
        }
    } err] == 1} { finish "error $err" }
}
proc next_sound {} {
    incr ::recording
    if {$::recording >= [llength $::frames]} { finish ok }
    puts $::out "sound $::recording"
    set ::left [lindex $::frames $::recording]
    poke g_TestSound [expr {$::recording + 1}]
}
debug breakpoint create -address $::a(Test_Sound) -command on_sound
# C-BIOS's logo, and a new game laid out: then an empty room to play in.
after time 25 {
    poke g_TestJungle 0
    poke g_TestOpen 0
    poke g_TestX $env(SOUND_X) 4
    poke g_TestY $env(SOUND_Y) 4
    poke g_TestStart 1
    after time 2 { set ::recording -1; next_sound }
}
after time 600 { finish "timeout at sound $::recording" }
