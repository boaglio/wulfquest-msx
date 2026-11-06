# openMSX script for test/moves.sh (AGENTS.md §10, M3 and M4). It does not
# judge: it plays $env(MOVES_PLAN) (MoveTraces's plan: a start and a key byte
# a tick per scenario) through the ROM and writes what Vale did to
# $env(MOVES_OUT).
#
# The ROM calls Test_BeforeTick before every tick. A breakpoint there records
# Vale's state after the last tick and writes the keys for the next one, so
# the input lands on exactly the tick it is meant for, at any speed.
# MOVES_SYMS holds the RAM addresses from the symbol map, as name=address.
#
# Output: per scenario "scenario NAME", a line after each tick
# ("X_FP Y_FP FACING SWING WALK SX SY SW SH ROOM FREEZE FRAME BLADE"), and
# "end NAME LATE TICKS FRAMES FLIPS FLIP_FRAMES UNBLANKED": frames the loop
# came late to, ticks run and frames seen from the first tick to the last,
# flips and the frames they took, and frames put on screen (Test_Shown) while
# the screen was not blank. Then "end ok", or "end error ...".
#
# At each flip, when the ROM has put the new room on screen, a line
# "flip TICKS PAGE DRAW_FRAMES" (ticks played so far, the page shown, the
# frames the drawing took), the shown page's playfield in $env(MOVES_OUT).bin,
# and the sprite tables in $env(MOVES_OUT).spr: the attribute table (128
# bytes), the colour table (512) and the patterns (2048).
# After each scenario, "busiest NAME CYCLES TICK": its busiest frame without
# a flip, and the ticks played when it came.

set throttle off

foreach pair $env(MOVES_SYMS) {
    lassign [split $pair =] name addr
    set ::a($name) $addr
}
# A playfield's bytes.
set ::field $env(MOVES_FIELD)
set out [open $env(MOVES_OUT) w]
set bin [open $env(MOVES_OUT).bin w]
fconfigure $bin -translation binary
set spr [open $env(MOVES_OUT).spr w]
fconfigure $spr -translation binary
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
    close $::bin
    close $::spr
    exit
}

proc state {} {
    set sw [peek g_SabreW]
    set box [expr {$sw == 0 ? "0 0 0 0" : "[peek g_SabreX 2 1] [peek g_SabreY 2 1] $sw [peek g_SabreH]"}]
    return "[peek g_ValeX 4 1] [peek g_ValeY 4 1] [peek g_ValeFacing] [peek g_ValeSwing 1 1] [peek g_ValeWalk 2] $box [peek g_ValeRoom] [peek g_ValeFreeze] [peek g_ValeFrame] [peek g_ValeBlade 1 1]"
}

set scenario -1
set running 0

proc next_scenario {} {
    incr ::scenario
    if {$::scenario >= [llength $::plan]} { finish ok }
    lassign [lindex $::plan $::scenario] head ::keys
    lassign $head ::name room open x y
    puts $::out "scenario $::name"
    poke g_TestOpen $open
    poke g_TestX $x 4
    poke g_TestY $y 4
    poke g_TestStart 1
    set ::tick 0
    set ::running 1
    # Twice its ticks' time, and a flip's drawing in every room it might see.
    set ::deadline [expr {[machine_info time] + 30 + [llength $::keys] / 25.0}]
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
            set ::flips0 [peek g_Flips 2]
            set ::flipframes0 [peek g_FlipFrames 2]
            set ::flips [peek g_Flips 2]
            set ::unblanked 0
        } else {
            puts $::out [state]
        }
        if {$::tick == [llength $::keys]} {
            set ticks [expr {([peek g_Ticks 2] - $::ticks0) & 0xFFFF}]
            set frames [expr {([peek g_Frames 2] - $::frames0) & 0xFFFF}]
            set flips [expr {([peek g_Flips 2] - $::flips0) & 0xFFFF}]
            set flipframes [expr {([peek g_FlipFrames 2] - $::flipframes0) & 0xFFFF}]
            puts $::out "end $::name [peek g_LateFrames 2] $ticks $frames $flips $flipframes $::unblanked"
            puts $::out "busiest $::name [expr {round($::busiest * 3579545)}] $::busiest_tick"
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

# A frame put on screen: it must be while the screen is blank (the VDP's
# status register 2, bit 6). At a flip, what is on screen.
set unblanked 0
proc on_shown {} {
    if {[catch {
        if {!$::running || [peek g_TestStart] != 0 || $::tick == 0} { return }
        if {!([debug read {VDP status regs} 2] & 0x40)} { incr ::unblanked }
        set flips [peek g_Flips 2]
        if {$flips == $::flips} { return }
        set ::flips $flips
        set page [expr {[debug read {VDP regs} 2] >> 5 & 3}]
        puts $::out "flip $::tick $page [peek g_DrawFrames]"
        puts -nonewline $::bin [debug read_block VRAM [expr {$page * 32768}] $::field]
        # The sprites: the attribute table where R5 and R11 say, the colours
        # 512 bytes before it, the patterns where R6 says.
        set at [expr {([debug read {VDP regs} 5] & 0xFC) << 7 | [debug read {VDP regs} 11] << 15}]
        puts -nonewline $::spr [debug read_block VRAM $at 128]
        puts -nonewline $::spr [debug read_block VRAM [expr {$at - 512}] 512]
        puts -nonewline $::spr [debug read_block VRAM [expr {[debug read {VDP regs} 6] << 11}] 2048]
    } err] == 1} { finish "error $err" }
}
debug breakpoint create -address $::a(Test_Shown) -command on_shown

# The busiest frame of each scenario: from the frame's start to the end of its
# work, frames with a flip left out (a flip is a hitch, timed apart).
set busiest 0
set busiest_tick 0
set frame_at 0
set frame_flips 0
proc on_frame_start {} {
    set ::frame_at [machine_info time]
    set ::frame_flips [peek g_Flips 2]
}
proc on_frame_end {} {
    if {!$::running || [peek g_Flips 2] != $::frame_flips} { return }
    set busy [expr {[machine_info time] - $::frame_at}]
    if {$busy > $::busiest} { set ::busiest $busy; set ::busiest_tick $::tick }
}
debug breakpoint create -address $::a(Test_FrameStart) -command on_frame_start
debug breakpoint create -address $::a(Test_FrameEnd) -command on_frame_end

# C-BIOS shows its logo for a few seconds before it starts the cartridge.
after time 10 {
    if {[catch next_scenario err]} { finish "error $err" }
    watchdog
}
