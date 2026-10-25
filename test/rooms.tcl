# openMSX script for test/rooms.sh (AGENTS.md §10, M2). It does not judge: it
# walks the ROM through every room with its Space key and writes what the
# MSX2 shows to $env(ROOMS_OUT): for each room a line in ROOMS_OUT.txt
# ("room drawFrames page") and the shown page's 212 lines in ROOMS_OUT.bin.
# The RAM addresses of the ROM's counters come from the symbol map, in
# ROOMS_SYMS: "room count frames".

set throttle off

lassign $env(ROOMS_SYMS) a_room a_count a_frames
set txt [open $env(ROOMS_OUT).txt w]
set bin [open $env(ROOMS_OUT).bin w]
fconfigure $bin -translation binary
set want 1
set deadline 0

proc mem {addr} { debug read memory $addr }
proc count {} { expr {[mem $::a_count] | [mem [expr {$::a_count + 1}]] << 8} }

proc finish {msg} {
    puts $::txt "end $msg"
    close $::txt
    close $::bin
    exit
}

# Any error ends the run at once, so rooms.sh never waits for its timeout.
proc step {body} {
    if {[catch {uplevel #0 $body} err]} { finish "error $err" }
}

proc record {} {
    set page [expr {[debug read {VDP regs} 2] >> 5 & 3}]
    puts $::txt "[mem $::a_room] [mem $::a_frames] $page"
    puts -nonewline $::bin [debug read_block VRAM [expr {$page * 32768}] 27136]
}

# Wait for drawing number $::want, record it, and ask for the next room.
proc poll {} {
    if {[count] >= $::want} {
        record
        if {$::want == 257} { finish ok }
        incr ::want
        set ::deadline [expr {[machine_info time] + 5}]
        # The key went up while the ROM was drawing; it reads keys once a
        # frame, so let it see the key up before the next press.
        after time 0.06 { step {
            keymatrixdown 8 0x01
            after time 0.06 { step { keymatrixup 8 0x01; after time 0.02 { step poll } } }
        } }
    } elseif {[machine_info time] > $::deadline} {
        finish "timeout waiting for drawing $::want"
    } else {
        after time 0.02 { step poll }
    }
}

# C-BIOS shows its logo for a few seconds before it starts the cartridge.
after time 10 { step { set ::deadline [expr {[machine_info time] + 5}]; poll } }
