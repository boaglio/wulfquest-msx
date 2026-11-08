# openMSX script for tools/record-demo.sh (AGENTS.md §7, M10: the attract demo
# is recorded on the MSX). It starts a game from the title in the ROM's
# recording mode (g_TestRecord) and plays $env(REC_ROUTE)'s keys into the
# MSX's keyboard, a tick's keys set just before the ROM reads them (a
# breakpoint on Test_BeforeTick); the ROM keeps what it read, as runs, in
# video RAM. Then it writes those runs to $env(REC_OUT), "COUNT KEYS" a line.

set throttle off
foreach {n v} $env(REC_SYMS) { set ::a($n) $v }
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
proc finish {msg} {
    set f [open $::env(REC_OUT) w]
    puts $f "end $msg"
    close $f
    exit
}

set f [open $env(REC_ROUTE)]
set ::seed [lindex [gets $f] 1]
lassign [lrange [gets $f] 1 2] ::x ::y
set ::keys {}
while {[gets $f line] >= 0} { lappend ::keys $line }
close $f

# The keys (row, bit of the keyboard matrix) for up, down, left, right, Space.
set ::matrix {{8 0x20} {8 0x40} {8 0x10} {8 0x80} {8 0x01}}
proc press {bits} {
    for {set i 0} {$i < 5} {incr i} {
        lassign [lindex $::matrix $i] row mask
        if {$bits & (1 << $i)} { keymatrixdown $row $mask } else { keymatrixup $row $mask }
    }
}
debug breakpoint create -address $::a(Test_BeforeTick) -command {
    if {[peek g_TestRecord] == 1} {
        set i [peek g_RecTicks 2]
        press [expr {$i < [llength $::keys] ? [lindex $::keys $i] : 0}]
    }
}

proc title {} {
    if {[peek g_Shell] == 0 && [peek g_ShellTick 2] >= 2} {
        for {set i 0} {$i < 8} {incr i} { debug write memory [expr {$::a(g_TestSeed) + $i}] [expr {($::seed >> (8 * $i)) & 0xFF}] }
        poke g_TestX $::x 4
        poke g_TestY $::y 4
        poke g_TestRecordTicks [llength $::keys] 2
        poke g_TestRecord 1
        keymatrixdown 8 0x01
        after time 0.2 { keymatrixup 8 0x01; done }
        return
    }
    after time 0.05 title
}
proc done {} {
    if {[peek g_TestRecord] == 2} {
        set runs [peek g_RecRuns 2]
        set data [debug read_block VRAM 0xEA00 [expr {2 * $runs}]]
        set f [open $::env(REC_OUT) w]
        binary scan $data cu* bytes
        foreach {n k} $bytes { puts $f "$n $k" }
        puts $f "end ok [peek g_RecTicks 2] $runs"
        close $f
        exit
    }
    after time 0.1 done
}
after time 4 title
after time 600 { finish "timeout: [peek g_RecTicks 2] ticks recorded" }
