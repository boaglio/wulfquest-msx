# openMSX script for test/sprites.sh (AGENTS.md §10, M5). It does not judge:
# it shows $env(SPRITES_PLAN) (check_sprites.py's plan) through the ROM's test
# gallery and writes what the VDP holds to $env(SPRITES_OUT).
#
# The ROM calls Test_Shown at the start of each frame. A breakpoint there
# asks for a page and waits until a draw made after it is on screen (the
# ROM numbers its draws, g_FlipDraw, and the blank's hook records the one it
# shows, g_ShownDraw: a page that loads many frames takes more than a frame
# to draw), saves the sprite tables, then asks for the next. Output: a line "shown" a page or a frame of the turns,
# its tables in $env(SPRITES_OUT).spr (attributes 128 bytes, colours 512,
# patterns 2048); "crowded N", the frames the ROM found a line with more than
# 8 sprites in during the turns; then "bench OP W H US", the blitter's time
# for a big creature, and "end ok" or "end error ...".
# SPRITES_SYMS holds the RAM addresses from the symbol map, as name=address.

set throttle off

foreach pair $env(SPRITES_SYMS) {
    lassign [split $pair =] name addr
    set ::a($name) $addr
}
set out [open $env(SPRITES_OUT) w]
set spr [open $env(SPRITES_OUT).spr w]
fconfigure $spr -translation binary

# What to show, frame by frame: a page once, a turns layout for its frames.
set items {}
set f [open $env(SPRITES_PLAN)]
while {[gets $f line] >= 0} {
    if {[lindex $line 0] eq "page"} {
        lappend items [list show [lrange $line 1 end]]
    } elseif {[lindex $line 0] eq "turns"} {
        lappend items [list turns [lrange $line 2 end]]
        for {set i 1} {$i < [lindex $line 1]} {incr i} { lappend items [list hold {}] }
        lappend items [list done {}]
    }
}
close $f

proc peek {name {n 1}} {
    set v 0
    for {set i [expr {$n - 1}]} {$i >= 0} {incr i -1} {
        set v [expr {($v << 8) | [debug read memory [expr {$::a($name) + $i}]]}]
    }
    return $v
}
proc poke {name value {n 1} {at 0}} {
    for {set i 0} {$i < $n} {incr i} {
        debug write memory [expr {$::a($name) + $at + $i}] [expr {($value >> (8 * $i)) & 0xFF}]
    }
}
proc finish {msg} {
    puts $::out "end $msg"
    close $::out
    close $::spr
    exit
}

proc layout {fields} {
    lassign $fields vf blade vx vy n
    poke g_TestValeFrame $vf
    poke g_TestValeBlade $blade
    poke g_TestValeX $vx 2
    poke g_TestValeY $vy 2
    poke g_TestCount $n
    for {set i 0} {$i < $n} {incr i} {
        lassign [lrange $fields [expr {5 + 3 * $i}] [expr {7 + 3 * $i}]] fr x y
        poke g_TestFrame $fr 1 $i
        poke g_TestSpriteX $x 2 [expr {2 * $i}]
        poke g_TestSpriteY $y 2 [expr {2 * $i}]
    }
    poke g_TestGallery 1
}

proc tables {} {
    set at [expr {([debug read {VDP regs} 5] & 0xFC) << 7 | [debug read {VDP regs} 11] << 15}]
    puts -nonewline $::spr [debug read_block VRAM $at 128]
    puts -nonewline $::spr [debug read_block VRAM [expr {$at - 512}] 512]
    puts -nonewline $::spr [debug read_block VRAM [expr {[debug read {VDP regs} 6] << 11}] 2048]
}

set step -1
set dump 0
proc on_shown {} {
    if {[catch {
        if {[peek g_DrawCount 2] == 0 || $::step == -2} { return }
        # The draw asked for, or a later one, on screen yet? A draw takes
        # longer than a frame when it loads many frames.
        if {$::dump && (([peek g_ShownDraw 2] - $::want) & 0x8000)} { return }
        if {$::dump} {
            tables
            puts $::out "shown"
            set ::dump 0
        }
        incr ::step
        if {$::step >= [llength $::items]} {
            set ::step -2
            poke g_TestGallery 0
            after time 0.2 bench
            return
        }
        lassign [lindex $::items $::step] kind fields
        set ::want [expr {([peek g_FlipDraw 2] + 1) & 0xFFFF}]
        switch $kind {
            show { layout $fields; set ::dump 1 }
            turns { layout $fields; set ::crowded0 [peek g_SpritesCrowded 2]; set ::dump 1 }
            hold { set ::dump 1 }
            done {
                puts $::out "crowded [expr {([peek g_SpritesCrowded 2] - $::crowded0) & 0xFFFF}]"
                # The next item is shown from the next frame on.
                on_shown
            }
        }
    } err] == 1} { finish "error $err" }
}
debug breakpoint create -address $::a(Test_Shown) -command on_shown

# The blitter's time for a big creature's background (op 0, HMMM) and for
# the creature over it (op 1, LMMM transparent), 16 times each (wulfquest.c's Bench).
set jobs {{0 28 20} {1 28 20} {0 32 22} {1 32 22} {0 32 28} {1 32 28} {0 16 24} {1 16 24}}
set job -1
proc bench {} {
    incr ::job
    if {$::job >= [llength $::jobs]} { finish ok }
    lassign [lindex $::jobs $::job] op w h
    poke g_BenchOp $op
    poke g_BenchW $w
    poke g_BenchH $h
    poke g_BenchGo 1
}
debug breakpoint create -address $::a(Test_BenchStart) -command { set ::t0 [machine_info time] }
debug breakpoint create -address $::a(Test_BenchEnd) -command {
    lassign [lindex $::jobs $::job] op w h
    puts $::out "bench $op $w $h [expr {round(([machine_info time] - $::t0) / 16.0 * 1e6)}]"
    after time 0.1 bench
}

after time 120 { finish "timeout at step $::step" }
