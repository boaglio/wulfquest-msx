# openMSX script for test/panel.sh (AGENTS.md §10, M9). It does not judge: in
# an empty room (nothing there changes the panel's state), it sets what the
# panel shows (the cases in $env(PANEL_CASES), each "name var=value ..."), waits
# for the panel to catch up, and writes the page on show's panel lines to
# $env(PANEL_OUT).bin, a case after another, and their names to $env(PANEL_OUT).
set throttle off
foreach pair $env(PANEL_SYMS) {
    lassign [split $pair =] name addr
    set ::a($name) $addr
}
set out [open $env(PANEL_OUT) w]
set bin [open $env(PANEL_OUT).bin w]
fconfigure $bin -translation binary
set ::cases [split $env(PANEL_CASES) ";"]
set ::n -1
proc poke {name value {n 1}} {
    for {set i 0} {$i < $n} {incr i} {
        debug write memory [expr {$::a($name) + $i}] [expr {($value >> (8 * $i)) & 0xFF}]
    }
}
proc finish {msg} { puts $::out "end $msg"; close $::out; close $::bin; exit }
proc next_case {} {
    if {[catch {
        if {$::n >= 0} {
            set page [expr {[debug read {VDP regs} 2] >> 5 & 3}]
            puts -nonewline $::bin [debug read_block VRAM [expr {$page * 32768 + $::top * 128}] [expr {(212 - $::top) * 128}]]
        }
        incr ::n
        if {$::n >= [llength $::cases]} { finish ok }
        set c [lindex $::cases $::n]
        puts $::out "case [lindex $c 0]"
        foreach set [lrange $c 1 end] {
            lassign [split $set =] var value
            lassign [split $var :] name size
            poke $name $value [expr {$size eq "" ? 1 : $size}]
        }
        after time 3 next_case
    } err]} { finish "error $err" }
}
set ::top $env(PANEL_TOP)
after time 25 {
    poke g_TestJungle 0
    poke g_TestOpen 0
    poke g_TestX $env(PANEL_X) 4
    poke g_TestY $env(PANEL_Y) 4
    poke g_TestStart 1
    after time 3 next_case
}
after time 600 { finish "timeout at case $::n" }
