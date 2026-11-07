# openMSX script for test/boot.sh (AGENTS.md §10, M0, M2). It does not judge:
# it writes what the machine shows to $env(BOOT_OUT), and boot.sh checks it.
#
#   before   video registers, palette and screen memory once the ROM has started
#            (and on an MSX2 the first 64 KB of video RAM, for the sprites)
#   after    the screen memory again, after F5 (the debug key for the next room) has been pressed
#
# Each is taken as the ROM puts a room on screen: its Test_Shown
# ($env(BOOT_SHOWN)) once g_DrawCount ($env(BOOT_COUNT)) says the room is
# drawn. On an MSX1, which shows no room, after C-BIOS's logo (a few
# seconds; emulated time, with the throttle off, so it takes a moment).

set throttle off

proc dump {f tag} {
    puts $f "$tag regs [binary encode hex [debug read_block {VDP regs} 0 24]]"
    puts $f "$tag palette [binary encode hex [debug read_block {VDP palette} 0 32]]"
    # The SCREEN 5 page on show: 212 lines of 128 bytes. On an MSX1 this is
    # the text screen instead, which is what the MSX1 check reads.
    set size [debug size VRAM]
    if {$size > 27136} {
        set start [expr {([debug read {VDP regs} 2] >> 5 & 3) * 32768}]
        set size 27136
    } else {
        set start 0
    }
    puts $f "$tag vram [binary encode hex [debug read_block VRAM $start $size]]"
    # The first 64 KB on an MSX2, where the sprite tables are.
    if {[debug size VRAM] > 65536} {
        puts $f "$tag vram64 [binary encode hex [debug read_block VRAM 0 65536]]"
    }
}

# Any error ends the run at once, so boot.sh never waits for its timeout.
proc step {body} {
    if {[catch {uplevel #0 $body} err]} {
        set f [open $::env(BOOT_OUT) a]
        puts $f "error script $err"
        close $f
        exit
    }
}

proc dumpto {tag} {
    set f [open $::env(BOOT_OUT) [expr {$tag eq "before" ? "w" : "a"}]]
    dump $f $tag
    close $f
}
proc drawn {} {
    expr {[debug read memory $::env(BOOT_COUNT)] | ([debug read memory [expr {$::env(BOOT_COUNT) + 1}]] << 8)}
}
# The game (an MSX2): "before" as its first room is put on screen, Vale on
# it (the jungle is live, so later he may be anywhere, or down); F5 then, and
# "after" as the next room is put on screen.
set ::state 0
debug breakpoint create -address $env(BOOT_SHOWN) -command { step {
    if {$::state == 0 && [drawn] >= 1} {
        dumpto before
        keymatrixdown 7 0x02
        set ::state 1
        after time 0.2 { keymatrixup 7 0x02 }
    } elseif {$::state == 1 && [drawn] >= 2} {
        dumpto after
        exit
    }
} }
# No room by then (an MSX1: the ROM says it needs an MSX2, and stays): the
# screen as it is, before and after F5. A new game takes a few seconds to
# lay out (M8: the treasures' 1 024 draws), so the room comes after 10 s.
after time 20 { step {
    if {$::state != 0} { return }
    set ::state 2
    dumpto before
    keymatrixdown 7 0x02
    after time 0.2 { step {
        keymatrixup 7 0x02
        after time 0.5 { step {
            dumpto after
            exit
        } }
    } }
} }
after time 40 { step { error "no room shown, and no MSX1 message" } }
