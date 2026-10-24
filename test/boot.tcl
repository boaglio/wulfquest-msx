# openMSX script for test/boot.sh (AGENTS.md §10, M0). It does not judge:
# it writes what the machine shows to $env(BOOT_OUT), and boot.sh checks it.
#
#   before   video registers, palette and screen memory once the ROM has started
#   after    the screen memory again, after Space has been pressed
#
# C-BIOS shows its own logo for a few seconds before it starts a cartridge,
# hence the wait. Emulated time, with the throttle off: it takes a moment.

set throttle off

proc dump {f tag} {
    puts $f "$tag regs [binary encode hex [debug read_block {VDP regs} 0 24]]"
    puts $f "$tag palette [binary encode hex [debug read_block {VDP palette} 0 32]]"
    # SCREEN 5 page 0: 212 lines of 128 bytes. On an MSX1 this is the text
    # screen instead, which is what the MSX1 check reads.
    set size [debug size VRAM]
    if {$size > 27136} { set size 27136 }
    puts $f "$tag vram [binary encode hex [debug read_block VRAM 0 $size]]"
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

after time 10 { step {
    set f [open $env(BOOT_OUT) w]
    dump $f before
    close $f
    keymatrixdown 8 0x01
    after time 0.2 { step {
        keymatrixup 8 0x01
        after time 0.5 { step {
            set f [open $env(BOOT_OUT) a]
            dump $f after
            close $f
            exit
        } }
    } }
} }
