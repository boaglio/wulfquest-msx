# openMSX script for test/shell.sh (AGENTS.md §10, M10). It does not judge:
# it walks the ROM through the shell's screens, with the keys and then with
# a joystick, and writes to $env(SHELL_OUT) what each screen showed ("TAG
# regs|palette|vram HEX") and the states it saw ("log ..."); check_shell.py
# judges. The joystick is joystick 1 as the ROM reads it: a breakpoint just
# after data.c's Data_Joystick reads the PSG's port A puts our bits in A.
#
# $env(SHELL_SYMS): "name addr" pairs from the symbol map.

set throttle off
foreach {n v} $env(SHELL_SYMS) { set ::a($n) $v }
set out [open $env(SHELL_OUT) w]

proc finish {msg} {
    puts $::out "end $msg"
    close $::out
    exit
}
proc peek {name {n 1}} {
    set v 0
    for {set i [expr {$n - 1}]} {$i >= 0} {incr i -1} {
        set v [expr {($v << 8) | [debug read memory [expr {$::a($name) + $i}]]}]
    }
    return $v
}
proc poke {name v {n 1}} {
    for {set i 0} {$i < $n} {incr i} { debug write memory [expr {$::a($name) + $i}] [expr {($v >> (8 * $i)) & 0xFF}] }
}
# The shell is up (g_Booted, when the plan's symbols have it): before, RAM holds anything.
proc booted {} { expr {![info exists ::a(g_Booted)] || [peek g_Booted] == 0xA5} }
proc dump {tag} {
    puts $::out "$tag regs [binary encode hex [debug read_block {VDP regs} 0 24]]"
    puts $::out "$tag palette [binary encode hex [debug read_block {VDP palette} 0 32]]"
    set start [expr {([debug read {VDP regs} 2] >> 5 & 3) * 32768}]
    # The whole page: the credits roll scrolls it (R#23) through all of its 256 lines.
    puts $::out "$tag vram [binary encode hex [debug read_block VRAM $start 32768]]"
    flush $::out
}

# The joystick: active-low bits 0-5 (up, down, left, right, A, B) of port A.
set joy 0
debug breakpoint create -address $env(SHELL_JOY) -command {
    if {$::joy} { reg a [expr {([reg a] & 0xC0) | (~$::joy & 0x3F)}] }
}

# The plan: steps run one after the other, each when the last is done.
#   wait STATE        until g_Shell is STATE and its screen is up
#   until EXPR        until the Tcl expression is true
#   key ROW MASK      a key held for a moment, then let go
#   joy BITS          the joystick held so for a moment, then let go
#   hold ROW MASK SECONDS   a key held so long
#   vpoke ADDR BYTES  video RAM written (a list of bytes)
#   dump TAG, log TEXT, poke NAME VALUE [BYTES], pause SECONDS
set plan $env(SHELL_PLAN)
set at 0
set deadline 0

proc next {} {
    if {$::at >= [llength $::plan]} { finish ok }
    set step [lindex $::plan $::at]
    incr ::at
    set ::deadline [expr {[machine_info time] + 120}]
    lassign $step what x y z
    switch $what {
        wait { poll [list expr "\[booted\] && \[peek g_Shell\] == $x && \[peek g_ShellTick 2\] >= 2 && \[peek g_ShellTick 2\] < 1000"] "state $x" }
        until { poll [list expr $x] $x }
        key {
            keymatrixdown $x $y
            after time 0.15 [list keymatrixup $x $y]
            after time 0.35 { step next }
        }
        hold {
            keymatrixdown $x $y
            after time $z [list keymatrixup $x $y]
            after time [expr {$z + 0.2}] { step next }
        }
        vpoke {
            set i 0
            foreach b $y { debug write VRAM [expr {$x + $i}] $b; incr i }
            next
        }
        joy {
            set ::joy $x
            after time 0.15 { set ::joy 0 }
            after time 0.35 { step next }
        }
        dump { dump $x; next }
        log { puts $::out "log $x [peek g_Shell] [subst $y]"; next }
        poke { poke $x $y [expr {$z eq "" ? 1 : $z}]; next }
        pause { after time $x { step next } }
    }
}
proc poll {cond what} {
    if {[uplevel #0 $cond]} {
        next
    } elseif {[machine_info time] > $::deadline} {
        finish "timeout at step $::at waiting for $what (state [peek g_Shell], tick [peek g_ShellTick 2])"
    } else {
        after time 0.02 [list step [list poll $cond $what]]
    }
}
proc step {body} {
    if {[catch {uplevel #0 $body} err]} { finish "error $err" }
}

# C-BIOS shows its logo for a few seconds before it starts the cartridge.
after time 5 { step next }
after time 900 { finish "timeout" }
