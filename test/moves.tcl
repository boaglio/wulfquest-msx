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
# ("X_FP Y_FP FACING SWING WALK SX SY SW SH ROOM FREEZE FRAME BLADE"; with
# the jungle a "J" line after it, with the quest a "Q" line), and
# "end NAME LATE TICKS FRAMES FLIPS FLIP_FRAMES UNBLANKED": frames the loop
# came late to, ticks run and frames seen from the first tick to the last,
# flips and the frames they took, and changes put on screen (the page or the
# sprite tables, in data.c's Data_Flip) while the screen was not blank. Then "end ok", or "end error ...".
#
# At each flip, when the ROM has put the new room on screen, a line
# "flip TICKS PAGE DRAW_FRAMES" (ticks played so far, the page shown, the
# frames the drawing took), the shown page's playfield in $env(MOVES_OUT).bin,
# and the sprite tables in $env(MOVES_OUT).spr: the attribute table (128
# bytes), the colour table (512) and the patterns (2048).
# After each scenario, "busiest NAME CYCLES TICK": its busiest frame without
# a flip, and the ticks played when it came.

set throttle off
set ::jungle 0
set ::quest 0
set ::beasts [expr {[info exists env(MOVES_BEASTS)] ? $env(MOVES_BEASTS) : 0}]

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

# The jungle (M6): Vale's mode, his creatures in list order, the beasts.
proc jungle {} {
    set s "J [peek g_ValeMode] [peek g_ValeModeTick] [peek g_ValeLives] [peek g_ValeInvuln] [peek g_HereN]"
    for {set k 0} {$k < [peek g_HereN]} {incr k} {
        set i [debug read memory [expr {$::a(g_Here) + $k}]]
        append s " [peekat g_CSpecies $i 1 0] [peekat g_CMode $i 1 0] [peekat g_CModeTick $i 1 0]"
        append s " [peekat g_CX $i 4 1] [peekat g_CY $i 4 1] [peekat g_CTimer $i 2 1] [peekat g_CPhase $i 2 1]"
        append s " [peekat g_CAux $i 2 1] [peekat g_CFace $i 1 1] [peekat g_CHome $i 1 0]"
    }
    append s " B"
    for {set b 0} {$b < $::beasts} {incr b} {
        # Beast (jungle.h): X 0, VX 4, Y 9, VY 13, At 18, MoveTicks 19; 29 bytes.
        set at [expr {$::a(g_Beast) + 29 * $b}]
        append s " [peekat g_BKind $b 1 0] [rd [expr {$at + 18}] 1 0] [rd $at 4 1] [rd [expr {$at + 9}] 4 1]"
        append s " [rd [expr {$at + 4}] 2 1] [rd [expr {$at + 13}] 2 1] [peekat g_BFace $b 1 1]"
        append s " [peekat g_BAsleep $b 1 0] [peekat g_BPause $b 1 0] [rd [expr {$at + 19}] 2 0]"
    }
    return $s
}
# The quest and the Wulf (M7), as JungleChecks.questLine writes them.
proc quest {} {
    set here [peek g_KeeperHere]
    set s "Q [peek g_QuestSet] [peek g_QuestSlots] [peek g_QuestTaken] [peek g_QuestLair 1 1] [peek g_QuestFlash]"
    append s " [peek g_QuestFly] $here [peek g_KeeperState] [expr {$here ? [peek g_KeeperX 4 1] : "-"}]"
    append s " [peek g_KeeperMoveTicks 2]"
    if {[peek g_QuestLair 1 1] >= 0} {
        append s " G [peek g_GuardX 4 1] [peek g_GuardY 4 1] [peek g_GuardPhase 2 1] [peek g_GuardTimer 2 1]"
        append s " [peek g_GuardAux 2 1] [peek g_GuardFace 1 1] [peek g_GuardHurt] [peek g_GuardMoveTicks 2]"
    } else {
        append s " G -"
    }
    append s " W [peek g_WulfState] [peek g_WulfStateTick] [peek g_WulfAt] [peek g_WulfHeading 1 1]"
    append s " [peek g_WulfSpeed 2 1] [peek g_WulfLatch] [peek g_WulfHere] [peek g_WulfX 4 1] [peek g_WulfY 4 1]"
    append s " [peek g_WulfMoveTicks 2]"
    return $s
}
proc rd {addr n signed} {
    set v 0
    for {set i [expr {$n - 1}]} {$i >= 0} {incr i -1} {
        set v [expr {($v << 8) | [debug read memory [expr {$addr + $i}]]}]
    }
    if {$signed && $v >= (1 << (8 * $n - 1))} { set v [expr {$v - (1 << (8 * $n))}] }
    return $v
}
proc peekat {name i n signed} {
    rd [expr {$::a($name) + $n * $i}] $n $signed
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
    lassign $head ::name room open x y check seed quest endless
    puts $::out "scenario $::name"
    poke g_TestOpen $open
    set ::jungle [expr {$seed >= 0}]
    poke g_TestJungle $::jungle
    set ::quest [expr {$quest eq "1"}]
    poke g_TestQuest $::quest
    poke g_TestEndless [expr {$endless eq "1"}]
    if {$::jungle} {
        # The seed as the Java game's long, little-endian (Tcl's integers are unbounded).
        for {set i 0} {$i < 8} {incr i} { debug write memory [expr {$::a(g_TestSeed) + $i}] [expr {($seed >> (8 * $i)) & 0xFF}] }
    }
    poke g_TestX $x 4
    poke g_TestY $y 4
    poke g_TestStart 1
    set ::tick 0
    set ::running 1
    # Five times its ticks' time (the jungle's busy rooms are slow until M13
    # tunes them), and a flip's drawing in every room it might see.
    set ::deadline [expr {[machine_info time] + 30 + [llength $::keys] / 10.0}]
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
            if {$::jungle} { puts $::out [jungle] }
            if {$::quest} { puts $::out [quest] }
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

# A flip (the page, the sprite tables) is made in the blank's hook, data.c's
# Data_Flip: when one is waiting there, the screen must be blank (the VDP's
# status register 2, bit 6).
proc on_flip {} {
    if {$::running && [peek g_FlipPending] != 0 && !([debug read {VDP status regs} 2] & 0x40)} {
        incr ::unblanked
    }
}
debug breakpoint create -address $::a(Data_Flip) -command on_flip

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
