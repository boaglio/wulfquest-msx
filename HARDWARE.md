# Wulf Quest for MSX2 — the real-hardware checklist

AGENTS.md §10, M13 part 5: the game played on real MSX2s, and what was found
written down. The emulator (openMSX, C-BIOS and the Philips NMS 8245's
ROMs) plays every check in `test/`; these are the things only a real
machine can show. Tick each box, and note anything odd under **Found**
below, with the machine, the medium and what was on screen.

## What to have

- At least one **European MSX2** (50 Hz) and one **Japanese MSX2** (60 Hz),
  or as close as can be found; an MSX2+ or a turbo R as well, if there is one.
- A **flash cartridge** that can be a 512 KB **ASCII16** MegaROM
  (MegaFlashROM SCC+ SD, Carnivore2 and the like), with `wulfquest.rom`
  written to it as ASCII16.
- A **disk drive** and a blank 720 KB disk, for the saves.
- The common **joysticks** (one with two buttons), and the keyboards to hand
  (Japanese, European; an AZERTY one if possible).

## The checks

### Starting
- [ ] It boots from the cartridge to the title, on each machine.
- [ ] On an MSX1 (if there is one) it says it needs an MSX2, and stops there.
- [ ] Left alone on the title for 20 s: the plane lands, the line types out,
      then the demo plays; any key brings the title back.
- [ ] Two new games after two power-ons are different (the seed comes from
      the MSX2's clock, among others).

### Speed and pictures
- [ ] A game's speed is the same on the 50 Hz and the 60 Hz machine: walk
      one room's width and time it (about the same seconds on both).
- [ ] Room flips: the new room comes whole, no half-drawn picture, no flash.
- [ ] Sprites: Vale and the creatures in their colours, no garbled sprites;
      in a crowded room they take turns (some flicker), none vanish for long.
- [ ] The busiest rooms (several creatures, a beast, an orchid): note how
      they feel; the emulator shows them a little slow (AGENTS.md M13).

### The interrupt (the game takes the frame's interrupt itself)
- [ ] The keys answer at once in the game and in every menu.
- [ ] `STOP` and `F1` pause; `ESC` goes back; `M` opens the map.
- [ ] A key tapped while a room is drawn is not lost (tap `F1` just as a
      room flips: the pause comes after).
- [ ] The music and the effects keep their pitch and tempo on both machines.

### Screens
- [ ] The pause menu comes up at once, the jungle darkening behind it.
- [ ] The map comes up at once, its rooms filling in; with a looking glass,
      the trail appears from Vale's room out (it can take some seconds).
- [ ] The win's scenes, the tally, the credits roll and the hi-score letters.

### Saves (a disk drive)
- [ ] Save from the pause menu to a slot; turn the machine off and on; the
      title offers the game first (`CONTINUE`) and it loads as it was.
- [ ] The autosave every minute, the three slots, a bad slot refused.
- [ ] After a save the game goes on normally (sound, keys, speed).
- [ ] Without a disk drive (or no disk) the game plays, with no saves.

### Controls
- [ ] Each joystick: the four ways, fire swings, the second button opens
      the map.
- [ ] The cursors, and `Q A O P` when chosen in the options.
- [ ] On an AZERTY keyboard, `Q A O P` are where MSX's own matrix puts them
      (note what the keys are labelled on it).

### Other machines
- [ ] An MSX2+ and a turbo R (in Z80 mode and in R800 mode, if it is one)
      play it as an MSX2.

## Found

(Machine, medium, what happened. Each one becomes a fix, or a line in
AGENTS.md M13's record.)

-
