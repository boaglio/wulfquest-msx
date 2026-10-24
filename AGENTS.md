# AGENTS.md — Wulf Quest for MSX2

**Draft 2, 2026-10-23.** Decisions marked *to prototype* are not settled yet.

**Wulf Quest for MSX2** is the MSX2 version of
[Wulf Quest](https://github.com/boaglio/wulfquest): the same 256-room jungle,
the same quest, the same rules, written for a Z80 and a V9938 as a ROM
cartridge. It is a separate program in C, not a port of the Java code. It
shares one thing with the Java game: the content in its `data/` folder.

This file is the contract for every agent working in this repository. Read it
before your first edit in a session. When you change a rule here, change the
code and the tests in the same commit.

The Java game's `AGENTS.md` (in the `wulfquest/` submodule) is the
specification of **how the game plays**: the movement, the creatures, the
Wulf, the orchids, the scoring, all with their fidelity tags. This file does
not repeat it. It says what the MSX2 keeps, changes and leaves out, and how.

---

## 1. Decisions

| Question | Decision |
|---|---|
| Machine | **MSX2 only.** MSX1 is out; MSX2+ and turbo R run it as MSX2s, with no extras. |
| CPU budget | Z80 at 3.58 MHz: about 71 000 cycles per game tick at 50 ticks a second. |
| Screen | **SCREEN 5**: 256×212, 16 colours chosen from 512, 4 bits a pixel. |
| Medium | **MegaROM cartridge** (ASCII8 or ASCII16 mapper), 256–512 KB. |
| Saves | **Both**: battery-backed **SRAM** on the cartridge, and a **disk** file. The game uses whichever the machine has; with both, the player picks. |
| Look | **Better than one colour per creature.** Hardware sprites with a colour per line, layered for more (§5). |
| Sound | The **PSG** (AY-3-8910): 3 square-wave channels and noise. No FM. |
| Language | **C** (SDCC) with **MSXgl**; Z80 assembly only where a measured hot loop needs it. |
| Rules | **The Java game as it ships**, with `game.json`'s flags at their defaults: the original's creatures, Wulf and orchids, plus what Wulf Quest has on by default (the guardians, the shrines, the map scroll and eyes, the looking glasses). The options that are off by default (`inventedCreatures`, `roamingWulf`, `inventedOrchids`, `inkedCreatures`) are left out. |

---

## 2. IP rules (hard constraints)

Everything in the Java game's §2 applies here unchanged: no original pixel
art, no original audio, no ROM, tape, snapshot or disassembly source, no
original trade dress. The protagonist is **Ranger Vale**, the beast is **the
Wulf**. The §2.5 attribution ships verbatim in the credits that roll after a
win and in `README.md`.

- **No binary assets in git.** Art and sound come from `wulfquest/data/` through
  the converter. The ROM, disk images and converter output are build output,
  under `build/` only, and are attached to releases, never committed. A gate
  fails the build if a `.rom`, `.dsk`, image or sound file is committed.
- **Licence:** GPL-3.0-or-later, like the Java game. MSXgl is CC BY-SA 4.0,
  which is one-way compatible with GPLv3; credit MSXgl and its author,
  Guillaume "Aoineko" Blanchard, in the credits and the README. Check SDCC's
  runtime library licence text before the first release.

---

## 3. Repository layout

```
wulfquest-msx/
  AGENTS.md          this file
  README.md          for players: what it is, how to get it and play it
  DEVELOPER.md       for developers: toolchain, build, test, release
  wulfquest/         git submodule: the Java game, pinned to a release tag
  msxgl/             git submodule: MSXgl, pinned to a release
  build.sh           build the ROM; `./build.sh test` runs every test
  convert/           the converter (Java, on wulfquest's core module), from M1
  src/               the game in C, and MSXgl's project_config.js and msxgl_config.h
  tools/             the binaries gate; helpers until the converter replaces them
  test/              openMSX scripts and their checks
  build/             build output: never committed
```

---

## 4. The converter

The MSX never reads JSON. `convert/` turns `wulfquest/data/` into C tables and
binary blobs for the ROM, and does everything heavy on the PC, once:

- **It loads the data with `Content.load` from wulfquest's `core`**, so every
  schema and semantic check of the Java game applies, and a bad file fails
  the MSX build too.
- **Collision** comes from the same `RoomBaker` the Java game uses: one mask a
  room, 32×22 cells, about 88 bytes, 22 KB for all 256. The two games collide
  with the same walls.
- **Rooms**: the scenery placements of each room, in the order they are drawn.
- **Ways**: the looking glass's "which way next" from every room to every
  target, precomputed. No path search runs on the Z80.
- **Art**: scenery as 4-bit bitmaps for video RAM; sprites cut into hardware
  sprite layers with per-line colours (§5). It reports every sprite it had to
  simplify, and how.
- **Palette**: our 16 colours, each as the nearest of the V9938's 512.
- **Sound**: every note and effect as PSG register values.
- **Numbers and text**: every speed, timing, score and string the game uses,
  as constants generated from the data. **No game number is typed into the C.**

The converter's output is the only input to the C build besides `src/`.

---

## 5. Screen and look

- **Layout.** SCREEN 5 has 212 lines: the 256×176 playfield on top, a
  **256×36 panel** below it (the Java panel is 40 px tall and must be re-laid
  out).
- **Rooms are pictures.** The 41 scenery objects live in spare video RAM
  (pages 2–3). Entering a room, the blitter (VDP commands) copies them into a
  hidden page, orchids and treasures included, and the pages swap. Rooms flip;
  nothing scrolls.
- **What moves is a sprite.** The V9938's sprite mode 2: 32 sprites, 16×16,
  **a colour per line**, and two sprites laid over each other with the OR bit
  give a third colour on that line. So a 16×16 creature can have up to three
  colours on every line and different colours on different lines.
- **The palette is arranged for the OR bit:** where two layer colours combine,
  their OR must be the colour wanted. The converter orders the 16 colours so
  the common pairs work, and reports those that do not.
- **8 sprites on a line**, a layered sprite counting twice. When a line is
  crowded the sprites take turns, as the Java game's sprite flicker does.
- **Big creatures** (rhino, hippo, the Wulf, the guardians): several sprites
  side by side, or drawn into the picture by the blitter with the background
  restored behind them. *To prototype.*

Left out: SCALE, FULLSCREEN, GLOW, scanlines (the television does that),
8-BIT MODE and the colour clash, the one-colour sprites switch.

---

## 6. Timing

- **The game runs at 50 ticks a second**, as the Java game does, so every
  speed and timing in the data means the same thing.
- **On 50 Hz machines**, one tick per frame.
- **On 60 Hz machines** (Japanese MSX2s), the game skips a tick in every six
  frames, so it runs at the same speed as everywhere else.
- Integer and fixed-point arithmetic only, the same as the Java simulation.

---

## 7. Features

**Kept**: all 256 rooms; the 8 amulet sets and the four guardians; the
swarm of small creatures; rhinos, tribesmen, hippo and warthog; the Wulf; the
orchids and their effects; the flame; the treasures; lives, continues, Easy
and Hard, scoring, the verse on each quarter, the game-over percentage; the
map screen, the scroll, the eyes, the looking glasses; New Game+ and its jobs;
the endings, Mr Jones and the credits; the title, the plane intro, the
hi-score table and its three letters.

**Changed**:
- **Saves**: three slots, autosave every minute, the last game offered first
  on the title, in SRAM or on disk (§1).
- **Hi-scores** are kept with the saves.
- **Controls**: cursor keys or a joystick to walk; Space or fire to swing;
  `M` or the second button for the map; `STOP` or `F1` to pause; `ESC` goes
  back a step. The keys page becomes one choice: cursors or `Q A O P`.
- **Sound**: on or off; no volume.
- **The demo** on the title is recorded on the MSX itself.

**Left out**: the options that belong to a PC window (§5); the invented
options (§1); the stats ledger, unless SRAM has room for a few counters;
practice and developer options, except in a debug build.

---

## 8. Testing

- **The converter** has its own tests, in Java, and fails on any data it
  cannot place: a sprite with too many colours on a line, a palette pair the
  OR bit cannot make, a room that does not fit in its budget.
- **openMSX** runs the ROM headless from scripts in `test/`: it plays a
  recorded input and checks the room, score and lives at given ticks. The
  MSX version is not promised bit-for-bit equal to the Java simulation; it is
  promised the same rules, checked at those points.
- **Every build** checks the ROM boots to the title on an MSX2 at 50 Hz and at
  60 Hz.

---

## 9. Working agreement

1. **Read this file before your first edit** in a session.
2. **Never type a game number into the C.** It comes from the converter.
3. **Never edit anything inside `wulfquest/`.** A change to the game goes to
   that repository; this one moves its pin to a new tag.
4. **Never commit a binary.** Art and sound are made by the converter.
5. **Build and run the tests before declaring anything done**, and report the
   actual result.
6. **Measure before writing assembly.** C first; assembly only for a loop the
   profiler shows is too slow.
7. **Never tag v1.0** until the user says it is ready. Releases are `v0.x.y`.
8. Commits end with
   `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. One concern per
   commit. The user pushes code and tags.

---

## 10. Milestones

Each milestone says what it is for, what it builds, and when it is accepted.
A milestone is done only when every "accept when" holds, the build is green,
and this section records what landed and what slipped. Numbers below that
come from the Java game's data (sprite sizes, counts) are as of wulfquest
v0.7.2; the converter's reports are the truth if they change.

### M0 — The repository, the toolchain and an empty ROM that boots in openMSX

**For:** a build anyone can run from a clean clone, before any game code.

- `wulfquest/` as a git submodule, pinned to the latest release tag; `msxgl/`
  as a git submodule, pinned to a release. Work out how MSXgl builds a
  project that lives outside its own tree, without editing the submodule.
- One build command (`make` or MSXgl's build script, wrapped in a
  `build.sh`) that writes `build/wulfquest.rom`.
- The ROM: an ASCII16 MegaROM header, SCREEN 5 set, the palette loaded, a
  line of text, and a wait for a key. It checks it is on an MSX2 and says so
  on an MSX1 instead of crashing.
- `test/boot.tcl`: openMSX runs the ROM headless on a stock MSX2 (a European
  50 Hz machine and a Japanese 60 Hz one), takes a screenshot into `build/`,
  and checks the text is on screen.
- The binaries gate: the build fails if a `.rom`, `.dsk`, image or sound file
  is tracked by git.
- `.gitignore`, `.gitattributes` (LF), `README.md` (one paragraph, for
  players), `DEVELOPER.md` (toolchain versions, how to build, how to test).

**Accept when:** a clean clone with the submodules builds the ROM with one
command; the openMSX test passes on both machines; an MSX1 shows the
"needs an MSX2" message; the gate fails on a planted `.rom` in git.

**Landed (2026-10-24).** All of the above, with these specifics:

- Pinned: wulfquest **v0.7.2**, MSXgl **v1.5.0** (its SDCC 4.6.0 builds the
  game; the system's SDCC is not used). MSXgl builds from `src/` with nothing
  in `msxgl/` edited; its deploy step is off so all output stays in `build/`.
- The machines are openMSX's **C-BIOS** ones (`C-BIOS_MSX2_EU` at 50 Hz,
  `C-BIOS_MSX2_JP` at 60 Hz, `C-BIOS_MSX1_EU`): free, so any machine and CI
  can run the tests. Real system ROMs come in M13.
- **The screenshot** is drawn by `test/check_boot.py` from the screen memory
  and the palette, not by openMSX: openMSX needs a display to take one, and
  its offscreen renderer gives a black picture.
- The test waits 10 emulated seconds for C-BIOS's logo before the ROM runs.
- On an MSX1 the message is MSXgl's: "Error: A MSX2 is required!", with its
  "press C to continue anyway" for developers.
- **Slipped to M1:** the palette is written by `tools/gen_palette.py`, a
  stand-in until the converter. The converter needs wulfquest's `core`
  module, which no released tag has yet (it came after v0.7.2).

### M1 — The converter: the data checked and turned into tables

**For:** every number, word, map and picture the MSX needs, made on the PC.

- `convert/`: a Maven project that depends on wulfquest's `core` (built from
  the submodule) and runs as one command, writing `build/gen/` (C headers and
  binary blobs).
- It loads the data with `Content.load`; any data error stops the build with
  the Java game's own message.
- **Rooms:** for each of the 256 rooms, its scenery placements in drawing
  order, and its collision mask from `RoomBaker` (32×22 cells, bit-packed).
- **Scenery art:** the 41 objects as 4-bit bitmaps, packed for video RAM
  pages 2–3, with each object's position in that atlas. It fails if they do
  not fit.
- **Palette:** our 16 colours as the nearest V9938 colours (3 bits per
  channel), and a report of how far each moved.
- **Constants:** every speed, timing, score, size and limit the C will use,
  generated from the data with names that say where they came from
  (`PLAYER_WALK_FP`, `WULF_SIT_TICKS`). Text and the font come in M10.
- **A budget report:** bytes per table and per ROM bank, so we see the ROM
  filling up from the start.
- Converter tests (JUnit): collision masks equal `RoomBaker`'s for every room;
  the atlas round-trips to the same pixels; every constant has a source.

**Accept when:** the converter runs from the build command; its tests pass;
the budget report fits in a 256 KB ROM with room to spare; changing a speed
in `wulfquest/data/` (in a scratch checkout) changes the generated constant.

### M2 — One room on screen: 8,10 in SCREEN 5

**For:** the drawing method every room will use, proven on one.

- Load the scenery atlas into video RAM pages 2–3 at start.
- Draw a room by blitter copies (VDP commands, transparent where the art is
  transparent) into the hidden page, then swap pages.
- The playfield at the top 176 lines; the 36-line panel area below, blank
  for now.
- A debug key steps through every room, to look at all 256.
- Measure how long drawing a room takes, on a 50 Hz machine, worst room
  included.

**Accept when:** room 8,10 matches the Java game's drawing of it (compare an
openMSX screenshot with the Java render of the same room, colours mapped);
all 256 rooms draw without corruption; the slowest room draws in under half a
second, and the time is written in this section.

### M3 — Ranger Vale walks: input, movement and collision

**For:** the feel of the game, which everything else is built on.

- The game loop: 50 ticks a second, driven by the vertical interrupt, with
  the 60 Hz tick-skip (§6).
- Input: cursor keys, `Q A O P`, joystick 1; one input state a tick, like
  the Java game's `InputState`.
- Vale's movement from the Java spec (its §11): 8 directions, fixed-point
  speeds from the converter, axis-separated collision against the room's
  mask, the wall slide.
- Vale drawn as a plain one-colour hardware sprite for now (colour is M5),
  walking frames only.
- The sabre swing, its frames and its reach, without anything to hit yet.

**Accept when:** the Java spec's movement numbers hold on the MSX, checked
by an openMSX script: holding right for 85 ticks moves 256 px ±1, holding
down for 59 ticks moves 176 px ±1, down-right along a wall slides down at
full speed; walls stop him exactly where the Java game stops him in three
chosen rooms; the loop never misses a tick in a room with nothing else
moving.

### M4 — Flip-screen: every room, every edge

**For:** the whole map, walkable end to end.

- Leaving a room by an edge enters the next one at the same position across
  the edge, as the Java game does (its §7.5); the outer ring of the map is a
  wall.
- The room is drawn on the hidden page while the old one stays on screen,
  then they swap: no half-drawn room is ever seen.
- A debug overlay: room number and Vale's position on the panel.

**Accept when:** an openMSX script walks a recorded route across at least 20
rooms and ends in the right room at the right position; every edge the Java
`MapAudit` says can be crossed can be crossed on the MSX (the converter writes
the list, a script tries each); no flicker or tearing on a flip.

### M5 — Sprites in colour: layered sprites, a colour per line

**For:** the look we chose: more than one colour per creature.

- The converter cuts each sprite frame into 16×16 hardware sprites: two
  layers with the OR bit, a colour per line in each, giving up to three
  colours on a line.
- It orders the palette so the colour pairs the sprites need are made by the
  OR bit, and reports every pixel it had to change, per sprite and frame.
- Vale (16×24, 62 frames, 10 colours) as 2 sprites tall × 2 layers; the 14
  small creatures (16×12) as 1 × 2 layers each; the puff, the flame, the
  amulet piece, the looking glass's arrow.
- A sprite manager: which hardware sprites are free, and **the 8-per-line
  limit**: Vale's 2 on a line plus three small creatures' 2 each is exactly
  8. When a line is over, the sprites on it take turns frame by frame.
- Decide, with a prototype, how the **big creatures** are drawn: rhino
  (28×20), hippo (30×22), warthog (22×16), the Wulf (32×22), the guardians
  (32×28), the Keeper (24×32), the tribesman (16×24). As hardware sprites
  they need 2×2 blocks of layered pairs, 4 sprites on a line each, which
  breaks the line limit with Vale beside them; drawn by the blitter into the
  picture they keep all 16 colours but cost time to draw and to restore.
  Record the decision and the measurements here.

**Accept when:** Vale walks in colour in every direction; the converter's
report lists every changed pixel and is empty or accepted sprite by sprite;
the big-creature decision is made, measured and written down.

### M6 — The jungle's creatures: the swarm, rhinos, tribesmen, hippo and warthog

**For:** the original's jungle, alive.

- The swarm (Java spec §12.8): creatures appear as puffs, up to three to a
  room, kinds coming in as the amulet fills; a sabre facing them bursts
  them; touching one kills Vale.
- Rhinos pacing their corridors, tribesmen following across rooms (and
  calling when level with a quarter), the sleeping hippo or warthog and its
  charge.
- Death and respawn, lives in reserve, game over (as a plain message for
  now).
- Random numbers from the same kind of generator as the Java game, seeded
  per game.

**Accept when:** each creature's behaviour matches the Java game's in a
scripted openMSX scene (a spider bursts on a facing swing, kills on a touch;
a rhino turns at its corridor's end; a tribesman follows through a flip); a
room with Vale and three small creatures holds 50 ticks a second; the
budget report shows CPU use per tick in the busiest room.

### M7 — The Wulf, the guardians and the amulet

**For:** the quest, start to finish.

- The Wulf prowling the bottom row, its chase, its sitting when the sabre is
  held towards it, its howl.
- The four guardians orbiting their lairs, their lunge and their stun.
- The 8 amulet sets, picked at random per game; the four quarters on their
  pedestals; picking one up; the reveal of the amulet so far, with its verse.
- The Keeper of the Arch at room 8,8: deadly while the amulet is not whole,
  stepping aside once it is; leaving through the arch wins.

**Accept when:** an openMSX script plays a full winning route through one
amulet set, takes all four quarters and leaves through the arch; the Wulf
chases across rooms and sits on a held sabre; each of the 8 sets puts the
quarters where the Java game puts them (the converter writes the table,
a test checks it).

### M8 — Orchids, flames, treasures and the score

**For:** everything that makes a run a run.

- The orchids' growth and their four effects (red slow, cyan fast, magenta
  reversed, yellow knocked flat), untouchable while they last; white cures.
- The flame that comes for a player who stays too long in one room.
- The treasures (about 250 a game, 150 each), the spare lives among them.
- The score and the hi-score, Easy and Hard (Easy's numbers from the data).
- The map scroll and the two eyes; the two looking glasses and their arrow,
  and the four more once the amulet is whole; the shrines and their hints.
- Continues: three a game, back where he fell, score from zero.

**Accept when:** each orchid's effect lasts and behaves as the Java game's in
a scripted scene; a flame arrives after the data's time in a room; Easy and
Hard differ exactly as the data says; a scripted run's score matches the
score the same route gives in the Java game, to the point.

### M9 — The panel, sound and music on the PSG

**For:** the game heard, and the panel read.

- The panel in 256×36: lives, score, hi-score, the amulet so far, the orchid
  bar, and room for the shrine and pause messages. Its layout is new; its
  contents are the Java panel's.
- The 17 sound effects (footstep, sabre swing, creature dying, the Wulf's
  howl and growl, and the rest) on the PSG, from the converter's register
  tables, with an effect taking a channel as the Java mixer's does.
- The 5 tunes (title, win, reveal, ending, game over) on the PSG; no music
  while playing in the jungle, as in the Java game.
- An interrupt-driven sound player that never costs the game a tick.

**Accept when:** the panel shows every item the Java panel shows, readable on
a real television's worth of blur (checked in openMSX's TV mode); every
effect and tune plays at the right pitch and length (compared with the Java
game's); sound on costs no ticks in the busiest room.

### M10 — Title, menus, map screen and hi-scores

**For:** a game you can sit down to.

- The font (8×8) and every string from the data, in the converter.
- The title: WULF QUEST, `START` / `CONTINUE` / `OPTIONS`, Hard or Easy and the
  job under `START`; the little wolf marking the line.
- Options: Easy or Hard, controls (cursors or `Q A O P`), sound on or off,
  load a game, the hi-scores.
- The pause menu: resume, save, load, options, quit to the title.
- The map screen: rooms walked, the arch, the quarters the eyes showed.
- Hi-score entry, three letters.
- The demo on the title after it is left alone, recorded on the MSX.

**Accept when:** every screen is reachable from the title and back with the
keys and with a joystick; every string on screen comes from the data; the
map screen matches the Java map for the same walked rooms.

### M11 — Saves: SRAM and disk

**For:** a game you can come back to, on any MSX2.

- A save format of fixed size, versioned, with a checksum: the rooms walked,
  the treasures taken, the quarters, the positions, the score, lives,
  continues, the set, the job.
- **SRAM:** detect a cartridge with battery-backed SRAM; three slots and the
  hi-score table there.
- **Disk:** detect a disk drive; three slots and the hi-scores as files on
  the disk, through the disk ROM.
- With both, the options page lets the player choose; with neither, saving
  is greyed out and says why.
- Autosave every minute and on leaving for the title; a finished game clears
  its slot; the title offers the last game first.

**Accept when:** in openMSX, a game saved to SRAM and one saved to disk both
load back to the same room, score and state after a power cycle; a damaged
save is refused with a message, not loaded; the three cases (SRAM only, disk
only, both) all work.

### M12 — Endings, New Game+, the credits and the plane intro

**For:** everything after the arch, and before the title.

- The win: the walk to Mr Jones, the sums, the credits with the §2.5 text
  verbatim and the MSXgl credit.
- New Game+: Mr Jones's two later jobs, their amulets (halves, whole), their
  endings, including the one on horseback; opened by a win and kept with the
  saves.
- The plane landing in the forest after the title is left alone, with
  *ONE MAN, / ONE MISSING ARTIFACT, / ONE MISSION*.

**Accept when:** each of the three endings plays through in openMSX from a
scripted win; the credits carry the §2.5 text exactly (checked by a test
against the data); the plane intro plays and any key returns to the title.

### M13 — 60 Hz machines and real hardware

**For:** the game as players will run it.

- Check every timing on a 60 Hz machine: the tick-skip keeps game speed equal
  to 50 Hz; music and effects keep their pitch and tempo.
- Run on real MSX2 hardware (at least one European and one Japanese machine,
  or as close as can be found) from a flash cartridge with SRAM, and from a
  disk.
- Test with the common joysticks and with keyboards of different layouts.
- Fix what real hardware shows that the emulator did not.

**Accept when:** a full game plays at the same speed on 50 Hz and 60 Hz
machines (a timed scripted route, within a tick); the game has been played
on real hardware, and what was found is written in this section.

### M14 — The first release: the ROM and a disk image

**For:** players.

- A release workflow: a `v0.x.y` tag builds the ROM and a bootable disk image
  that loads it, runs the tests, and attaches both to a GitHub pre-release.
- `README.md` for players: what it is, which machines, how to run the ROM on
  a flash cartridge or an emulator, how saves work, the controls, the §2.5
  text, the licence, the MSXgl credit.
- Screenshots from openMSX, attached to the release, never committed.

**Accept when:** pushing a tag produces a release with the ROM and the disk
image; both boot in openMSX on a 50 Hz and a 60 Hz MSX2; the README's
instructions work from a clean download. Never `v1.0` until the user says so.
