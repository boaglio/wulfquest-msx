# openMSX script for tools/screenshots.sh: two screenshots from a real game
# (no demo overlay), the attract demo's keys (demo.txt, from tools/
# record-demo.sh) played into a new game from its seed and start, as
# tools/record_demo.tcl plays them. At each tick in $env(SHOTS_AT) ("TICK
# NAME ..."), the VDP's registers, palette and video RAM go to
# $env(SHOTS_DIR)/NAME.{regs,pal,vram}, for tools/screenshots.py. At tick
# $env(SHOTS_MAP_AT) the keys stop, Vale is given the looking glass
# ($env(SHOTS_GLASS) ticks of it) and the map opened (test/trail.sh's hooks);
# once the trail to the nearest quarter is found, the map is NAME "trail".

set throttle off
foreach {n v} $env(SHOTS_SYMS) { set ::a($n) $v }
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
proc save {name what data} {
    set f [open "$::env(SHOTS_DIR)/$name.$what" w]
    fconfigure $f -translation binary
    puts -nonewline $f $data
    close $f
}
proc dump {name} {
    save $name regs [debug read_block {VDP regs} 0 24]
    save $name pal [debug read_block {VDP palette} 0 32]
    save $name vram [debug read_block VRAM 0 131072]
}

# demo.txt: comments, "seed S", "start X Y", then runs "COUNT KEYS".
set f [open $env(SHOTS_DEMO)]
set ::keys {}
while {[gets $f line] >= 0} {
    if {[string match "#*" $line] || $line eq ""} continue
    switch [lindex $line 0] {
        seed { set ::seed [lindex $line 1] }
        start { lassign [lrange $line 1 2] ::x ::y }
        default { lassign $line n k; for {set i 0} {$i < $n} {incr i} { lappend ::keys $k } }
    }
}
close $f
array set ::at $env(SHOTS_AT)
set ::mapAt $env(SHOTS_MAP_AT)
set ::stopped 0

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
        if {[info exists ::at($i)]} {
            dump $::at($i)
            unset ::at($i)
        }
        if {$i >= $::mapAt && !$::stopped} {
            set ::stopped 1
            poke g_WayTicks $::env(SHOTS_GLASS) 2
            poke g_TestMapOpen 1
            after time 0.05 trail
        }
        press [expr {!$::stopped && $i < [llength $::keys] ? [lindex $::keys $i] : 0}]
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
        after time 0.2 { keymatrixup 8 0x01 }
        return
    }
    after time 0.05 title
}
# The map up and its trail found (trail.h: stage 4), then held between two
# of its ticks for the dump.
proc trail {} {
    if {[peek g_Shell] == 9 && [peek g_TrailStage] == 4} {
        poke g_TestMapHold 1
        held
        return
    }
    after time 0.05 trail
}
proc held {} {
    if {[peek g_TestMapHold] == 2} { dump trail; exit }
    after time 0.02 held
}
after time 4 title
after time 400 { puts stderr "screenshots_demo: timed out"; exit 1 }
