# openMSX script for test/moves.sh (AGENTS.md §10, M3). It does not judge: it
# plays $env(MOVES_PLAN) (MoveTraces's plan: a start and a key byte a tick per
# scenario) through the ROM and writes what Vale did to $env(MOVES_OUT).
#
# The ROM calls Test_BeforeTick before every tick. A breakpoint there records
# Vale's state after the last tick and writes the keys for the next one, so
# the input lands on exactly the tick it is meant for, at any speed.
# MOVES_SYMS holds the RAM addresses from the symbol map, as name=address.
#
# Output: per scenario "scenario NAME", a line after each tick
# ("X_FP Y_FP FACING SWING WALK SX SY SW SH"), and "end NAME LATE TICKS FRAMES":
# frames the loop came late to, and ticks run and frames seen from the first
# tick to the last. Then "end ok", or "end error ...".

set throttle off

foreach pair $env(MOVES_SYMS) {
    lassign [split $pair =] name addr
    set ::a($name) $addr
}
set out [open $env(MOVES_OUT) w]
set plan {}
set f [open $env(MOVES_PLAN)]
while {[gets $f line] >= 0} {
    if {[lindex $line 0] eq "scenario"} {
        set current [lrange $line 1 end]
    } elseif {[lindex $line 0] eq "keys"} {
        lappend plan [list $current [lrange $line 1 end]]
    }
}
close $f

proc peek {name {n 1} {signed 0}} {
    set v 0
    for {set i [expr {$n - 1}]} {$i >= 0} {incr i -1} {
        set v [expr {($v << 8) | [debug read memory [expr {$::a($name) + $i}]]}]
    }
    if {$signed && $v >= (1 << (8 * $n - 1))} { set v [expr {$v - (1 << (8 * $n))}] }
    return $v
}
proc poke {name value {n 1}} {
    for {set i 0} {$i < $n} {incr i} {
        debug write memory [expr {$::a($name) + $i}] [expr {($value >> (8 * $i)) & 0xFF}]
    }
}

proc finish {msg} {
    puts $::out "end $msg"
    close $::out
    exit
}

proc state {} {
    set sw [peek g_SabreW]
    set box [expr {$sw == 0 ? "0 0 0 0" : "[peek g_SabreX 2 1] [peek g_SabreY 2 1] $sw [peek g_SabreH]"}]
    return "[peek g_ValeX 4 1] [peek g_ValeY 4 1] [peek g_ValeFacing] [peek g_ValeSwing 1 1] [peek g_ValeWalk 2] $box"
}

set scenario -1
set running 0

proc next_scenario {} {
    incr ::scenario
    if {$::scenario >= [llength $::plan]} { finish ok }
    lassign [lindex $::plan $::scenario] head ::keys
    lassign $head ::name room open x y
    puts $::out "scenario $::name"
    poke g_TestRoom $room
    poke g_TestOpen $open
    poke g_TestX $x 4
    poke g_TestY $y 4
    poke g_TestStart 1
    set ::tick 0
    set ::running 1
    set ::deadline [expr {[machine_info time] + 30}]
}

# Before every tick.
proc on_tick {} {
    # catch gives 2 for the returns inside: only 1 is an error.
    if {[catch {
        # Ticks before the ROM has taken the start belong to the last scenario.
        if {!$::running || [peek g_TestStart] != 0} { return }
        if {$::tick == 0} {
            set ::ticks0 [peek g_Ticks 2]
            set ::frames0 [peek g_Frames 2]
        } else {
            puts $::out [state]
        }
        if {$::tick == [llength $::keys]} {
            set ticks [expr {([peek g_Ticks 2] - $::ticks0) & 0xFFFF}]
            set frames [expr {([peek g_Frames 2] - $::frames0) & 0xFFFF}]
            puts $::out "end $::name [peek g_LateFrames 2] $ticks $frames"
            puts $::out "busiest $::name [expr {round($::busiest * 3579545)}]"
            set ::busiest 0
            set ::running 0
            next_scenario
            return
        }
        poke g_TestKeys [lindex $::keys $::tick]
        incr ::tick
    } err] == 1} { finish "error $err" }
}

proc watchdog {} {
    if {$::running && [machine_info time] > $::deadline} { finish "timeout in scenario $::name at tick $::tick" }
    after time 1 watchdog
}

debug breakpoint create -address $::a(Test_BeforeTick) -command on_tick

# The busiest frame of each scenario: from the frame's start to the end of its work.
set busiest 0
set frame_at 0
proc on_frame_start {} { set ::frame_at [machine_info time] }
proc on_frame_end {} {
    if {!$::running} { return }
    set busy [expr {[machine_info time] - $::frame_at}]
    if {$busy > $::busiest} { set ::busiest $busy }
}
debug breakpoint create -address $::a(Test_FrameStart) -command on_frame_start
debug breakpoint create -address $::a(Test_FrameEnd) -command on_frame_end

# C-BIOS shows its logo for a few seconds before it starts the cartridge.
after time 10 {
    if {[catch next_scenario err]} { finish "error $err" }
    watchdog
}
