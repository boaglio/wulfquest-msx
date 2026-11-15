# openMSX script for tools/screenshots.sh (AGENTS.md §10, M14): walks the
# ROM through a few screens and dumps, for each, the VDP's registers, its
# palette and the whole video RAM to $env(SHOTS_DIR)/NAME.{regs,pal,vram};
# tools/screenshots.py draws them. Each screen is waited for by the shell's
# state (shell.h) and its ticks on it.
set throttle off
proc peek {a {n 1}} {
    set v 0
    for {set i [expr {$n - 1}]} {$i >= 0} {incr i -1} { set v [expr {($v << 8) | [debug read memory [expr {$a + $i}]]}] }
    return $v
}
proc shell {} { peek $::env(SHOTS_SHELL) }
proc ticks {} { peek $::env(SHOTS_TICK) 2 }
# Vale on show and past his blinking (not invulnerable any more).
proc visible {} { expr {[peek $::env(SHOTS_VISIBLE)] && [peek $::env(SHOTS_INVULN)] == 0} }
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
proc press {row mask {secs 0.3}} { keymatrixdown $row $mask; after time $secs [list keymatrixup $row $mask] }
# The plan: {state ticks condition action}: when the shell is in that state
# with at least those ticks on it and the condition holds, the action runs
# and the next step waits.
set ::plan {
    {0 50 1 {dump title}}
    {17 700 1 {dump intro; press 8 1}}
    {0 30 1 {press 8 1}}
    {4 40 1 {press 8 0x80 1.6}}
    {4 160 {[visible]} {after time 0.2 {dump game; press 6 0x20}}}
    {5 60 1 {dump pause; press 7 0x04}}
    {4 20 1 {press 4 0x04}}
    {9 100 1 {dump map; exit}}
}
proc step {} {
    if {![llength $::plan]} { exit }
    lassign [lindex $::plan 0] state min cond action
    if {[shell] == $state && [ticks] >= $min && [expr $cond]} {
        set ::plan [lrange $::plan 1 end]
        eval $action
    }
    after time 0.05 step
}
after time 3 step
after time 120 { puts stderr "screenshots: timed out"; exit 1 }
