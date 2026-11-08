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
  wulfquest/         git submodule: the Java game, pinned to a release tag (§10, M1)
  msxgl/             git submodule: MSXgl, pinned to a release
  build.sh           build the ROM; `./build.sh test` runs every test
  pom.xml            builds wulfquest/core and convert/ together
  convert/           the converter (Java, on wulfquest's core module)
  src/               the game in C, and MSXgl's project_config.js and msxgl_config.h
  tools/             the binaries gate
  test/              openMSX scripts and their checks
  build/             build output: never committed
```

**The ROM's layout** (since M7): a 512 KB ASCII16 MegaROM of 32 segments of
16 KB. Segments 0 and 1 are the program, always mapped (4000h-BFFFh);
segments 2-23 hold the converter's blobs, mapped into bank 1 (8000h) only
while `data.c` reads them; segments 24-31 hold **banked code**, one
`src/wulfquest_sN_b1.c` each, mapped into bank 1 while it runs (`data.c`'s
`Bank_Call`, wrapped as `JUNGLE(fn)`, `SPAWN(fn)`, `QUEST(fn)`, `GAME(fn)`,
`SHELL(fn)`). Banked code
replaces segment 1 while it runs, so it may call only code below 8000h
(`data.c`, `rng.c`, `move.c`, `sound.c`, linked first), keeps its tables in RAM
(`tables.h`), and uses no SDCC library routine (they are linked into segment
1): `build.sh` checks every call in the listings, and that each file's
segment is the one its header names.

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
- **Palette**: our 16 colours, each as the nearest of the V9938's 512. Colour 0
  is transparent to the blitter and to sprites, so art is written with
  palette colour 0 as its twin (another colour that is the same on the
  V9938), and transparent pixels as 0.
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
- **Big creatures** (rhino, hippo, the Wulf, the guardians): **hardware
  sprites**, 16x16 blocks side by side, each two layers, taking turns when a
  line is crowded. Decided in M5 (§10): the blitter would take 7.7 ms of a
  20 ms frame for one 32x22 creature, its background and itself.

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
- **Slipped to M1** (and done there): the palette is written by `tools/gen_palette.py`, a
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

**Landed (2026-10-24).** All of the above, with these specifics:

- **The pin** is wulfquest commit 3172aee on `main`, the first with `core`:
  no tag has it yet. It moves to the next wulfquest tag.
- **The build:** a top `pom.xml` builds `wulfquest/core` from the submodule
  and `convert/` in one reactor; `convert/`'s parent is the Java game's
  parent POM, so versions follow the submodule. `build.sh` runs it with
  `exec:java`; `tools/gen_palette.py` is gone.
- **Output** (`build/gen/`): `palette.h`, `constants.h` (35 constants for
  M2–M3; more are added to `constants.json` as the C needs them), `scenery.h`
  with `scenery_0..2.bin`, `rooms.h` with `rooms.bin`, `report.txt`.
- **Rooms** are stored by template, as the original does: 48 templates,
  a byte a room for its template, at most 26 placements in one, and one
  88-byte mask a template. `rooms.bin` is 7 381 bytes.
- **Colour 0** is the V9938's transparent colour, so art never uses it:
  palette colour 0 (black) is written as colour 8 (bright black), which is
  the same on the V9938. The converter fails if colour 0 ever loses its twin.
- **The atlas** takes 328 of the 512 lines of pages 2–3: 41 objects, packed
  in shelves, every one starting on a byte.
- **The budget:** 49 365 bytes of data in 4 of the 14 data segments; 10
  segments (160 KB) free. Segments 0–1 are left to the program.
- **Palette drift:** at most 16 of 255 on a channel (green, cyan, yellow).
- 15 converter tests, among them the scratch-copy speed change.

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

**Landed (2026-10-24).** All of the above, with these specifics:

- **The ROM's data:** the converter places its blobs in ROM segments and
  writes `rawfiles.json`, which `src/project_config.js` hands to MSXgl, and
  `rom.h`, where the C finds them. `src/data.c` maps a blob into bank 1
  (8000h) only while it copies from it, talking to the VDP through its ports;
  so it must run from segment 0, and `build.sh` fails if it does not.
- **Drawing:** a room is copied from the ROM to RAM (`RoomData`: placements
  and mask), the hidden page is filled with the ground (HMMV), each object
  is copied from the atlas with LMMM and TIMP, and the pages swap at the
  next frame.
- **Debug keys:** the cursor keys go to the next room that way, Space to the
  next room by number. `./run.sh` shows it.
- **`test/rooms.sh`** walks the ROM through all 256 rooms on a 50 Hz MSX2 and
  compares each with the Java game's drawing of it (`RenderRooms`, from
  `RoomPainter`): **all 256 equal, pixel for pixel**, colours mapped. The
  start room's two pictures go to `build/shots/room-8-10-msx.png` and
  `-java.png`. The M0 boot test now expects a room instead of text.
- **Timing**, measured by the ROM in frames on a 50 Hz MSX2, the copy from
  ROM included: **slowest room 0,13 at 320 ms (16 frames)**, median 280 ms.

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

**Landed (2026-11-04).** All of the above, with these specifics:

- **The loop** (`src/wulfquest.c`) waits for the BIOS's frame counter,
  `JIFFY`, runs a tick a frame (none in every sixth frame at 60 Hz, from the
  VDP's 50/60 Hz bit), at most `maxCatchupTicks` (`game.json`) at once, then
  draws Vale. A frame that passes while the loop is busy is counted as late.
- **Input:** the cursor keys and `Q A O P` at once (the choice between them
  is M10), Space, joystick 1 and its first button; one state a tick.
  **Space is now the sabre, so the M2 debug keys changed:** F5 shows the
  next room by number, Vale standing at the same place in it; the cursor
  keys walk.
- **Vale** (`src/vale.c`) follows the Java game's `Simulation.updateSwing`,
  `move` and `computeSabre` line by line: world positions in 8.8 fixed point
  (`i32`), the momentum of §11.3, the diagonal scale, the axis-separated
  resolve and its wall slide, facing, the walk counter, the sabre's stroke,
  cooldown and hit box. The whole room's cells are unpacked to a byte each
  when he enters it. **Until M4, outside the room on screen is solid.**
- **Drawn** as two 16×16 sprites, one above the other, in **bright white with
  the black outline left out**: the Java game's own one-colour look. The
  converter writes all 76 frames he can show while walking and fighting
  (`player_frames.bin`, 64 bytes each, frames mirrored for the left), and the
  ROM copies a frame's patterns to video RAM when it changes. The frame is
  picked as `PlayerPainter.frameFor` picks it. The blade itself is not drawn
  yet; the pose and the hit box are. **Slipped to M5:** the blade line.
- **`test/moves.sh`:** the Java game's `Simulation` (convert's `MoveTraces`)
  and the ROM play the same keys; **all 1 946 ticks of the 7 scenarios are
  equal, at 50 and at 60 Hz**: position to the 1/256 px, facing, stroke, walk
  counter and the sabre's hit box. The movement numbers are at the top of
  the run-up (§11.3's inertia came after this milestone was written), as
  the Java game's `MovementFeelTest` measures them: after 24 ticks of run-up,
  **85 ticks right move 255 px, 59 ticks down move 177 px**; down-right
  along the map's edge slides down at full speed on all 56 ticks after the
  run-up. **Walls stop him where the Java game stops him** in rooms 8,10,
  5,6 and 11,3: 166, 200 and 200 ticks pressed against a wall, every one
  equal. The loop is never late, and runs 50 ticks a second on both
  machines.
- **Timing:** the busiest frame, Vale pushing into a corner, takes **30 356
  Z80 cycles, 42% of a 50 Hz frame** (50% of a 60 Hz one). A profile of the
  frames: the box test 30%, `Move` 20%, the wall search 15%, drawing about
  10%. That is a lot for one walker. M6 must measure again before it adds
  creatures, and may need the box test in assembly (§9, rule 6).
- **`test/boot.sh`** now also checks Vale is on screen where a new game
  starts him, in his first frame, and its pictures include the sprites.
- **Found:** MSXgl's start-up does not clear RAM, so every global has an
  initial value (DEVELOPER.md).

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

**Landed (2026-11-06).** All of the above, with these specifics:

- **The flip** (`src/vale.c`) is the Java game's `updateRoom`: Vale walks in
  world pixels, and the room is the one his feet box's centre is in, so the
  cross-edge coordinate carries over exactly. The tick he changes rooms has
  no blade, and the next `transition.freezeTicks` (6) are frozen.
  **Collision** looks past the room's edge: entering a room unpacks its
  cells and a margin of 2 cells on every side from its neighbours (936
  bytes), the map's outside solid; a compile-time check holds the margin to
  the box and the speed.
- **Drawing** (`src/wulfquest.c`): the new room is drawn on the hidden page
  while the old one stays on screen; then, in one blank, the pages swap and
  Vale's sprites move to his place in it. Every frame now starts by putting
  the last tick's Vale on screen, then runs its ticks, so sprites never
  change mid-picture. The drawing takes **320 ms median, 360 ms worst**,
  longer than the 6 frozen ticks (120 ms), so the frozen ticks are run
  straight after it for the frames it took: the hitch is the drawing alone.
  It is longer than the Java game's; M5–M6 may want the room drawn in a
  background of frames.
- **The debug overlay** (`src/overlay.c`): one line on the panel,
  `ROOM cc,rr  X xxxx  Y yyyy`, in MSXgl's 6×8 sample font (CC BY-SA,
  credited with MSXgl), bright white. It writes at most 2 characters a frame
  from a queue of those that changed, so it lags a walking Vale by a frame
  or two. Measured on the way, as a warning for the real panel (M9): SDCC
  keeps a loop's variables on the stack at 19 cycles an access, so the first
  version, MSXgl's 17-bit VRAM writer and the line redone each frame, cost
  30 000 cycles a frame; now at most 9 000, 4 400 median, with the glyph
  writer in assembly.
- **`test/moves.sh`** plays a second plan, `test/flips.json`: a **route of
  1 902 ticks from the start across 22 room edges** to 4,14 (the converter's
  `Route` finds the path and steers the Java game along it, braking for its
  momentum), and **all 314 edges `MapAudit` finds can be crossed** (stranded
  ones included), each walked across and back. **Every tick equal to the
  Java game's, at 50 and at 60 Hz**: position, facing, stroke, walk, blade,
  room and frozen ticks. **All 650 flips** put the whole new room on screen,
  as `RenderRooms` draws it, with Vale's sprites where he stands in it; and
  every frame of every scenario is put on screen while the screen is blank
  (the VDP's VR bit at `Test_Shown`). The M3 open-world scenarios have no
  frozen ticks (the Java game's with `transitionFreezeTicks` 0), so their
  movement numbers still hold across a flip.
- **Corrected in M6:** `check_moves.py` read each scenario's end line with
  the wrong number of fields, so the "never late" and "put on screen while
  blank" checks above **never ran** in M4 or M5. Fixed in M6; since then the
  moves and flips plans pass both, at 50 and 60 Hz.
- **Timing:** the busiest frame is **38 421 Z80 cycles, 53% of a 50 Hz
  frame (64% at 60 Hz)**, up from M3's 30 356: about 6 700 for the interrupt
  and drawing Vale, 27 100 for the tick (pushing into a corner), 4 500 for
  the overlay. M6 must measure before it adds creatures.

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

**Landed (2026-11-06).** All of the above, with these specifics:

- **The palette's order** (`convert/.../SpriteCut.java`, `VdpPalette.forGame`):
  the 15 colours the V9938 shows (black and bright black are one) get
  numbers 1-15 so that the colour triples the sprites' lines need are made
  by the OR: a hill climb over swaps from seeded random starts, the same
  every run, two seconds. **Black, the outline in nearly every line, came
  out as 15, every bit**, so the outline is where the two layers overlap.
  Number 0 is transparent and holds black too. The scenery is written with
  the same numbers, so every room still equals the Java game's (test/rooms.sh).
- **The cut:** each 16x16 block of a frame is a base and an overlay with
  CC; a line's two colours are the pair of its own colours that changes
  fewest pixels, then the least far; a pixel no pair makes shows the nearest
  colour made. **Every changed pixel is listed** in `build/gen/sprites.txt`,
  with pictures in `sprites.png`, and `convert/src/main/resources/wulfmsx/
  convert/sprites.json` accepts a count a sprite: the converter fails past
  it. Accepted after looking at them: **Vale 100 pixels over his 76 frames**
  (lines with four or more colours: no order can help, weighting him did
  not); the 14 small creatures 276 together (the parrot 52, the bird 48, the
  monkey and the earwig 42), the puff and the arrow none, the flame 84 (its
  four-colour heart loses some white), **the amulet piece 302** (its quarters'
  faces lose detail lines; it lies still in a room, so M7 may draw it with
  the blitter instead). The big ones, cut for the report only until they come
  in: 114 together (the warthog 37, the Keeper 31, the Wulf 22).
- **Vale** is four sprites, two blocks of two layers (player_frames.bin, 192
  bytes a frame). **The blade**, which slipped from M3, is two one-colour
  sprites, the blade in bright white and the crossguard in bright yellow,
  each placed from the frame's `hand` (blade.bin, 64 shapes: 8 facings × the
  4 guards and 4 thrusts). Its pose is `PlayerPainter.pose`'s hash of the game
  tick: its low bits come from the multiplier's low 32 bits, so the ROM adds
  that once a window (`BladeTest` holds it to `pose` for 50 000 ticks, and
  every blade `PlayerPainter` draws in all eight facings to the shapes).
- **The sprite manager** (`src/sprites.c`): slots own sprites, patterns and
  colours (the blade, Vale, 13 small); a frame puts the slots front to back,
  and the attribute table is written anew each frame, ended by Y 216. Vale and
  his blade keep their places (an absent blade is held below the screen), so
  his colours are rewritten only with his frame. The early clock is set per
  sprite when it stands partly off the left edge, and the lines below the
  playfield are given colour 0, the Java game's clip at the playfield. **When
  a line has more than 8 sprites** (counted in bands of 4 lines, so sometimes
  a band early), Vale and his blade stay first and the others take turns,
  one place a frame. The placing is in assembly: in C it cost 10 000 cycles a
  sprite (SDCC's locals on the stack again); now about 700, and a colour
  table written about 500.
- **The big creatures are hardware sprites.** Measured on openMSX's V9938,
  sprites and the screen on (test/sprites.sh, `Bench`): restoring a 28x20
  background takes 2.1 ms and drawing a 28x20 creature over it, transparent,
  4.2 ms; 32x22, 2.3 and 5.2 ms; 32x28 (a guardian), 2.9 and 6.5 ms; 16x24,
  1.5 and 3.0 ms. A frame is 20 ms, and the CPU is idle while it waits only
  if it has nothing else to do: two rhinos and the Wulf would not fit. The
  blitter is that slow everywhere: a room's 320 ms is the same blitter. As
  sprites a big creature costs no VDP time, keeps its colours (the report:
  114 pixels over all ten), and needs 2 blocks a line, 4 sprites: with Vale's
  2 and his blade's 2 that is a full line, so a small creature beside them
  takes turns. A 32x28 one is 8 sprites; room 8,9's two rhinos, Vale, his
  blade, three small creatures and the flame are 30 of the 32. M6 must give
  the manager slots for them and count; if a room needs more than 32, the
  manager's turns take care of it as they do of a line.
- **Tests.** `test/moves.sh`: each tick now has Vale's frame and blade
  shape, and **all 13 434 ticks equal the Java game's**, at 50 and 60 Hz
  (`PlayerPainter.frameFor` and `pose`, reached by reflection, the
  converter's `Painter`); at all 650 flips the sprites, drawn from video RAM
  as the V9938 draws them (`test/vdp_sprites.py`: the 8 a line, the early
  clock, the CC rule), are Vale in that frame and blade. New
  **`test/sprites.sh`**: the ROM's test gallery shows every frame of Vale,
  every blade shape and every bank frame, at the left edge and over the
  panel too, 143 pages, each exactly as cut; 14 sprites on a line, Vale and
  the blade whole every frame, the five creatures whole in 4 of 10 each.
  `test/boot.sh` checks Vale in colour. (As in M4, the lateness and blank
  checks did not run; see M4's correction.) openMSX cannot render headless (a
  screenshot comes out black), so the V9938 model is the oracle.
- **Timing:** the busiest frame is **49 672 Z80 cycles, 69% of a 50 Hz frame
  (84% at 60 Hz)**, up from M4's 38 421: Vale pushing into a corner, fencing,
  on a frame his picture changes. Loading a frame of Vale is about 6 800
  cycles (192 bytes), its colours about 2 500, a blade shape 3 500; the
  debug overlay now waits for a frame that loads nothing. **M6 must measure
  first**: the creatures' AI has about 10 000 cycles left at 60 Hz in the
  worst frame as it is, and Vale's tick (about 24 000 pushing into a wall) is
  the place to look.

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

**Landed (2026-11-07), timing slipped.** The behaviour, all of it; the
"50 ticks a second" does not hold yet. The user chose to finish the whole game
first and tune after (see M13). Specifics:

- **The jungle** (`src/jungle.c`) is the Java game's `tickCreatures`,
  `tickSwarm`, `tickBeasts` and `resolveCombat`, in the same order and with
  the same random draws: the swarm and its puffs, bursting on a facing swing
  and killing on a touch; the eight beasts (four rhinos, three tribesmen, the
  sleeper: hippo or warthog by the seed) walking the whole map, wrapping round
  its ends, tribesmen following Vale across rooms. Death, respawn
  (`src/spawn.c`, the Java `SpawnFinder` as a flood of box classes rather
  than of pixels), lives, and game over. (Corrected in M7: the record said
  game over showed "as a plain message"; the ROM only stopped. M7's debug
  line now shows `GAME OVER`, and the lives.) Left for their
  milestones: the score (M8), the amulet a tribesman calls at (M7), the
  flowers (M8).
- **Random numbers** (`src/rng.c`): the Java `Rng` bit for bit, xorshift128+
  seeded by splitmix64; a draw in assembly.
- **The converter** writes the creatures' and beasts' tables
  (`Jungle.java`, `RamTables.java`, `MapBits.java`) and refuses a beast
  speed of a cell or more, or a box too big for the assembly step.
- **Tests:** `test/moves.sh`'s third plan, `test/jungle.json`: five scenes
  (fencing, standing, rhinos, follow, game over), **11 000 ticks equal to the
  Java game's at 50 and 60 Hz**: Vale, every creature and every beast, each
  field. Each scene's point is checked in the Java game first
  (`JungleChecks`): a facing swing bursts, a touch kills, a rhino turns, a
  tribesman follows through a flip, the game ends. The sleeper wakes and
  charges in all five. The sprite gallery and boot tests now wait for their
  own draw (numbered draws, `g_FlipDraw`/`g_ShownDraw`), and a late frame
  never starts over the sprite tables still waiting to be shown.
- **Timing, slipped.** Busiest room, 8,10 with Vale fencing and three
  creatures, Z80 cycles a frame, measured with a sampling profiler in
  openMSX: **the tick about 64 000** (beasts 28 000, small creatures 21 000,
  combat 7 500, swarm 1 400), **the sprites about 57 000** (the jungle's
  placing 23 000, PlaceAll 17 000, the line count 8 000), the interrupt about
  3 000: **about 123 000, against 71 600 at 50 Hz and 59 700 at 60 Hz.**
  Already in assembly: the beasts' step (only the new strip of cells tested,
  a blocked strip remembered per beast), their loop, the line count, the
  walk phase. In the rhinos scene: tick 36 000, sprites 38 500. Until the
  tuning, `test/moves.sh` prints the jungle's lateness as `SLIP` lines
  instead of failing (`MOVES_TIMING_SLIPPED`); every other check still
  fails. Ideas measured but not taken: the rest in assembly gives perhaps
  65-75 000; running the tick from the interrupt and drawing from a copy,
  dropping pictures but never ticks, would change §6.

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

**Landed (2026-11-07), timing slipped as M6's; the reveal moved to M10.**
Specifics:

- **The quest** (`src/quest.c`): Simulation's `questEntersRoom`, `tickQuest`
  and `takePiece`, the original's eight sets (`Simulation.hideAmulet`: the set
  from its own stream of the game's seed, `Rng.hash(seed, 0xA29D)`, in
  `rng.c`'s `Rng_Stream`), each lair's guardian (`GuardOrbit`: its circle on
  the Java game's sine table, its lunge, a swing's push and stun; never
  hurt), a quarter taken by touch, the Keeper of the Arch lethal until the
  amulet is whole and then sliding away from Vale's half of the room, and the
  escape: control taken, the walk up into the arch, won. A tribesman level
  with a quarter still lying is lit (`g_BHint`; its drawing comes with the
  tinted sprites of M8, its call with M9's sound).
- **The original's Wulf** (`src/wulf.c`): Simulation's `prowl`, one Wulf on
  room 6,15's row, round the map's ends, steered once as Vale comes onto its
  row, sitting while a sabre is held towards it, then rising faster. **A
  finding in the Java game:** `getUp` sends any Wulf that is not ABSENT away
  (`gone()`), the original's prowler too, so after Vale's first death it
  walks its row unseen and harmless for the rest of the game. The ROM does
  the same, as §1 asks; the fix belongs in wulfquest, and this repository
  will take it with the pin.
- **The converter** (`Quest.java`) writes `quest_data.h` and the RAM tables:
  every set's lairs (room, pedestal, the guardian's circle), the guardians,
  the Keeper, the way out, the Wulf, and a quarter of the sine table (the
  rest folded, checked). The Wulf, the four guardians and the Keeper are in
  the sprite bank, which no longer fits a segment: `sprites_0.bin`,
  `sprites_1.bin`.
- **The ROM grew to 512 KB, with banked code** (§3): the program had 2 KB
  left before M7, so the simulation (`jungle.c`, `spawn.c`, `quest.c` with
  `wulf.c`) moved into segments 24-26. Main code went from 30.5 to 23 KB.
- **Tests.** `test/moves.sh`'s fourth plan, `test/quest.json`: **21 579
  ticks equal to the Java game's at 50 and 60 Hz**, Vale, the jungle, the
  quest and the Wulf, field by field (a `Q` line a tick beside M6's `J`). The
  eight sets, each from the first seed that picks it; the Wulf sitting on a
  held sabre and rising; a guardian lunging and parried; and **a whole game
  won**: convert's `QuestRoute` plays the Java game with infiniteLives (the
  ROM's `g_TestEndless`), nearest lair first and then the arch, finding its
  way on the map's cell blocks again after each death. It takes the four
  quarters of set 0 and leaves through the arch in 20 562 ticks, with 76
  deaths and 83 flips. `QuestTest` holds the converter's set tables to the
  Java game's lairs and guardian circles for all eight sets.
- **Found and fixed on the way:** the sprite manager's colour buffer is one
  for both sets of tables, so a set rewritten when a sprite's look changed
  (the early clock at a screen edge) could take the other set's frame's
  colours: one wrong line, after a flip at an edge (M5's bug). Vale blinked
  out after a death no longer shows his blade. `g_SwingSerial` is 16 bits:
  a guardian struck 256 swings before was taken for struck by this one.
- **Slipped:** the reveal of the amulet and its verse (a shell screen in the
  big font) to M10, with the font and the strings; the pickup's border flash
  and the quarter's flight to the panel to M9, with the panel; the guardian's
  hurt flash to M8, with tinted sprites. **Timing** as M6's: the quest plan's
  lateness is a SLIP (`MOVES_TIMING_SLIPPED`), and a respawn in some rooms
  stops the game for up to 2.2 s (`Spawn_NearestFree`'s flood, 8 million
  cycles in room 14,14): both are for M13's tuning.

### M8 — Orchids, flames, treasures and the score

**For:** everything that makes a run a run.

- The orchids' growth and their four effects (red slow, cyan fast, magenta
  reversed, yellow knocked flat), untouchable while they last; white cures.
- The flame that comes for a player who stays too long in one room.
- The treasures (about 250 a game, 150 each), the spare lives among them.
- The score and the hi-score, Easy and Hard (Easy's numbers from the data).
- The map scroll and the two eyes; the two looking glasses and their arrow,
  and the four more once the amulet is whole; the shrines and their hints.
- Tinted sprites (from M7): Vale in a flower's colours, a hurt guardian's
  flash, a tribesman lit up level with a quarter.
- Continues: three a game, back where he fell, score from zero.

**Accept when:** each orchid's effect lasts and behaves as the Java game's in
a scripted scene; a flame arrives after the data's time in a room; Easy and
Hard differ exactly as the data says; a scripted run's score matches the
score the same route gives in the Java game, to the point.

**Landed (2026-11-07), timing slipped as M6's.** Specifics:

- **The rules** (`src/game.c`, banked in segment 27, with the items, treasures
  and shrines in `quest.c` and the flame in `jungle.c`): the score and its
  events (a first visit, a burst creature by its frame, kind and facing, a
  quarter, a treasure, the escape's bonuses for time and lives), the run's best
  across continues; the orchids (one a room at the original's spot, its cycle
  staggered from the seed, its colour drawn per cycle as `OrchidField.orchidAt`
  draws it, with the Java game's hashes in `rng.c`) and the effects they give:
  slowed, hastened, reversed, knocked down (sitting, the room's creatures
  burst, the swarm stops), all untouchable while they last, white cures; the
  flame (`tickFlame`, `lightFlame`, the `FLAME` way) and its wait; the
  treasures laid from their own stream of the seed, taken by touch, lives
  among them; the map scroll, the eyes and the looking glasses (the four more
  once the amulet is whole); the shrines' hints; continues (fire at game over,
  three a game); Easy (its lives, swarm, no flame, slower beasts and Wulf,
  longer arrows, hints every time), all from the data (`Game.java`). Their
  state too big for RAM is in video RAM below the screen (512 treasure
  places, 256 orchid cycle starts).
- **The drawing:** the orchid by stage (sprout, bud, swaying bloom, wilt); the
  treasures as silhouettes in their kind's ink; the items, an eye and a glass
  glancing; a hurt creature's and a stunned guardian's white flash; a
  tribesman level with a quarter inked yellow; Vale sitting when knocked down
  and inked in his flower's colours (`player_inked_N.bin`, one segment a
  flower, `Ink` as `Sprite.blitInked`, `InkTest` holding it to the Java
  game's), flashing back near its end. **The glass's arrow** is the Java
  game's `Wayfinder` path out of the room, found by the converter for every
  set and state of the quarters taken (`ways_N.bin`, 32 KB) from the room's
  middle: exact when Vale stands near it, an approximation elsewhere (the
  Java game finds it from his feet, a whole-map search the Z80 cannot do).
- **Tests.** `test/moves.sh`'s fifth plan, `test/game.json`, the game as it
  ships (`Content.ecosystem()`, and Easy's): **72 322 ticks equal to the Java
  game's at 50 and 60 Hz, the score among them, to the point** (an `M` line
  a tick). An orchid scene where Vale picks the room's orchid each bloom and
  walks with it (`OrchidScene`: torpor, haste, reversal and knockdown all
  shown), a flame arriving after 256 ticks, the map scroll, a shrine's hint,
  a game over and a continue, and **a whole game won in Hard (score 134 361)
  and in Easy (125 871)**, each the Java game's to the point, 72 and 70
  treasures taken. At every flip Vale's sprites are his drawn frame, sitting
  and inked included.
- **RAM and the bank:** the sprite bank's frame tables (1.5 KB) and the beasts'
  spots (512 bytes) moved out of RAM into ROM blobs read on demand
  (`frames.bin`, `rooms_more.bin`); RAM ended past the BIOS's work area for a
  while, so `build.sh` now fails when less than 512 bytes are left under
  F380h. The bank keeps 238 frames, of the 255 a byte numbers: mirrors the ROM
  never draws, the invented orchids' colours and treasures' own colours are
  left out (`sprites.json`'s `unmirrored`, `silhouettesOnly`). Vale is weighted
  four times in the palette's order, which the new sprites had pulled away
  from him (120 changed pixels to 181); the lizard and the frog took the cost.
  Small sprite slots went from 6 to 9: the pattern table's 64 sprites.
- **Found on the way:** `Rng_Seed`'s 64-bit multiplication was C, 340 000
  cycles each: in assembly now, ten times faster.
- **Timing, slipped further:** a new game spends 3.3 s laying the treasures
  (1 024 draws, about 11 000 cycles each), so the first room comes 11-12 s
  after power-on (`test/boot.tcl`, `rooms.tcl` wait longer); and the plain
  movement plan is now one frame late at 60 Hz in room 5,6 (46 000 cycles
  of work and the BIOS's interrupt), so `moves` joined `MOVES_TIMING_SLIPPED`.
  All for M13.

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
- From M7: the border's flashes (a pickup's white, a death's alarm) and a
  taken quarter's flight to its place on the panel.

**Accept when:** the panel shows every item the Java panel shows, readable on
a real television's worth of blur (checked in openMSX's TV mode); every
effect and tune plays at the right pitch and length (compared with the Java
game's); sound on costs no ticks in the busiest room.

**Landed (2026-11-07).** Specifics:

- **The panel** (`src/panel.c`), the Java panel's items in 36 lines: SCORE
  and the score in bright white, HI and the run's best in white (the big
  font, for a television's blur), LIVES and a head a life (five, then X and
  the count), EASY in its colour, the amulet's four quarters at half size in
  the middle, dim until held, and a line at the bottom: a shrine's hint (the
  small font; the words are the Java game's own `GameSession.hint`, which the
  converter calls), the flower's bar in its colour with what is left of it,
  and for now `GAME OVER` and the tally's heading (`shell.json`'s, until
  M10's screens). As the debug overlay before it, a cell at a time on the page
  on show (two a frame), the whole on a room's hidden page. The overlay is
  gone: F4 puts the room and Vale's feet on the bottom line, as the Java
  game's dev mode does. The fonts, strings and images come from the converter
  (`Panel`: `font.bin`, `panel.bin`).
- **The border** flashes as `Simulation.borderFlash` says: a death's alarm,
  the white of a quarter or an item taken. **The taken quarter flies** to its
  place on the panel, as a sprite: sprites stop at the playfield's foot, so it
  goes out of sight where the Java game's goes on over the panel.
- **The sound** (`src/sound.c`, in segment 0, run from the frame's interrupt):
  the 17 effects and 5 tunes rendered by the converter (`Sound`) into the
  PSG's periods, noise and volumes a frame at a time, for 50 and 60 Hz
  (`sound50.bin`, `sound60.bin`, 2 KB each); the Java Mixer's voices: two for
  the effects, taken by priority, the tune on the third, silent while voice A
  plays. The simulation asks for every effect where the Java game does (the
  footstep's cadence too, faster in haste; the Wulf's growl while it is about);
  the game over's and the win's tunes play, the rest are the title's (M10).
- **Tests.** Convert's `SoundTest`: every effect's frames against the Java
  game's own `BeeperSynth`, each frame's pitch from its samples (within 12%:
  the PSG's periods and the vibrato read once a frame) and its length (to a
  frame); every tune's notes and length. `test/sound.sh`: every sound played
  alone in openMSX, the PSG's registers after each interrupt equal to the
  tables, 50 and 60 Hz. `test/panel.sh`: five states of the panel (a score,
  lives past five, Easy, a hint, the arch's hint, the flower's bar, game over)
  in video RAM, pixel for pixel the panel drawn from the converter's font and
  images.
- **Found on the way, with the sound in the interrupt:** `data.c`'s map test
  (M6) mapped its segments without recording them in `g_Bank1Segment`, so an
  interrupt that maps its own segment and puts back the recorded one left
  the test reading code as map: beasts went astray by a pixel. Every mapping
  is recorded now; and the hook saves the registers the C may use, which the
  BIOS's interrupt does not. A room's panel is copied from the page on show by
  the VDP (drawn whole it was 140 ms a room).
- **TV mode**: openMSX draws nothing headless, so the panel was looked at as
  a picture from video RAM, blurred as a television would (it reads); not in
  openMSX's TV mode itself.
- **Timing, slipped as M6's:** the player costs about 2 900 cycles a frame in
  the interrupt, 11 000 at worst (C, through SDCC: for M13). Sound on is not
  yet free in the busiest room.

### M10 — Title, menus, map screen and hi-scores

**For:** a game you can sit down to.

- The font (8×8) and every string from the data, in the converter.
- From M7: the reveal of the amulet so far when a quarter is taken, with its
  verse (`shell.json`'s `amuletReveal`), and the real game over and win
  messages in place of the debug line's.
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

**Landed (2026-11-07), timing slipped as M6's.** Specifics:

- **The shell** (`src/shell.c`, banked in segment 28, `SHELL(fn)`) is the
  Java game's `Shell` state machine with `GameSession`'s reveal and map: the
  title, the attract demo, the options, the pause menu and its save and load
  pages, the reveal, the map, game over and its continue, the hi-scores and
  their three letters, and the win's sum. The main loop reads the input once
  a tick, calls it, and runs a tick of the game when it says (`g_ShellDo`);
  after the tick, it sees a quarter taken, game over or the win. The screens
  are `ShellPainter`'s, fitted to 256x212 with the playfield at the top: a
  screen of its own is drawn on the page not shown, then shown; a screen over
  the game (the menus, the reveal, the map, game over) on the other page over
  a copy of the game's, which is left as it was, and the sprites are hidden
  under it. The tests' hooks (`g_TestStart`) skip the shell, as the Java
  game's checks drive the simulation without a `GameSession`.
- **The converter** (`Shell`): `shell_data.h`, `shell.bin` (every string the
  screens show, from `shell.json`, `input.json`'s profiles and the few words
  in the Java screens' code; the hi-score table a new machine has; each
  room's 16x11 miniature for the map, as `MapScreen` samples its mask; the
  demo's input) and `shell_img.bin` (the menu's wolf, the map's diamonds and
  arch, the quarters at the reveal's scale, the wordmark's words as stencils),
  copied to video RAM under the scenery (page 3, from line 840).
- **The title**: the wordmark's colour cycle is `TitleScreen`'s, band by band,
  each word coloured in spare video RAM and copied on (black is colour 15, so
  a band filled and the stencil ORed over it does it). `START` (with Hard or
  Easy under it; `D` switches it too) and `OPTIONS`; `CONTINUE` comes with the
  saves (M11), `EXIT` is a PC's. Left alone for `attract.idleTicks`, the demo.
- **The demo, for now**: the Java game's `replays/attract.json`, Hard, its
  seed and room, its input played tick for tick. **Still to do (the user,
  2026-11-07): the demo must be recorded on the MSX itself (§7)**; this one
  stands in until then, and its test checks the attract mode around it. It
  is dimmed by the palette (each colour its
  plain twin's, the bands' inks kept), every other row black, the wordmark and
  `PRESS ANY KEY` on their bands, no panel. Any key, or its end, goes back.
- **Options**: from the title, difficulty, keys (the two profiles' walking
  keys: the cursors, or `Q A O P`), sound, load, hi-scores, back; over a game,
  sound and back (`ShellConfig.Options.TITLE_ONLY`). **The pause menu**:
  `STOP` or `F1`, over the jungle dimmed as `Framebuffer.dim` (the bright
  colours their plain twins, by a table, in assembly). Its save and load pages
  list the slots empty and do nothing on fire, as the Java shell with no slots
  does, until M11.
- **The reveal** (`AmuletRevealScreen`), its tune, its verse, its prompt after
  `skipAfterTicks`; **game over** (`GameOverScreen`) with the percentage, the
  continue offer, its count and the seconds left; the continue is a tick of the
  game with fire pressed (`GameSession.carryOn`: the ROM's Vale is told the
  fire was let go, `Vale_FireReleased`). **The win** shows the tally's heading
  and total until the endings (M12). **The panel's** `HI` is the table's best;
  its game over and win lines (M9) are gone.
- **The map** (`M` or the joystick's second button): `MapScreen`'s rooms,
  quarters (shiny once an eye showed them, dull once taken), the arch and its
  glow, Vale's room blinking, the panel's line saying how to go back. **Not
  the looking glass's trail** (it needs the whole map's distances: M12).
- **Hi-scores**: `HighScores`' ranking, the table in RAM (its saving is M11's);
  `HiScoreEntry`'s three letters, its repeat and its blink.
- **Tests.** Convert's `ShellTest`: the strings are the data's, the table is
  `default_highscores.json`'s, each miniature is its room's mask as `MapScreen`
  samples it. `test/shell.sh`, at 50 and 60 Hz: every screen from the title and
  back with the keys, then with the joystick (a breakpoint in MSXgl's
  `Joystick_Read` puts its bits in); each screen's words read off the screen
  with the ROM's font and found to be `shell.bin`'s; the demo played to its
  end, where the Java game's ends (`demo_check.txt`: 3 000 ticks, room, place,
  score and lives); the map over it the Java game's `MapScreen` drawn by the
  converter, pixel for pixel (`demo_map.bin`); a reveal, game over, a continue
  and the win brought on by setting the game's state. `test/boot.sh` boots to
  the title first; `rooms.sh` and `sprites.sh` start a game from it.
- **The joystick** has two buttons: fire and the map. The pause menu is the
  keyboard's (`STOP`, `F1`), as the user agreed; everything else is reachable
  with the joystick.
- **Timing, slipped as M6's:** the pause menu takes 1.2 s to come up (the
  jungle dimmed, 2.5 million cycles) and as long again for each menu over the
  game; a key pressed and let go while the game is stalled (a respawn, M7; a
  new game, M8) is not seen; the demo starts after a new game's 3.3 s.

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

**From M10:** the title's `CONTINUE` line and its slot; the save and load
pages' slots, and fire on one choosing it (the user, 2026-11-07: fire selects
the slot); the new game's slot (`NEW_GAME`); the hi-score
table and the options kept.

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

**From M10:** the closing scene, the tally's lines and the roll in place of
the win's sum the shell shows for now; the plane intro before the demo (the
title goes straight to the demo until then); the looking glass's trail on the
map screen; the jobs' line under `START`.

**Accept when:** each of the three endings plays through in openMSX from a
scripted win; the credits carry the §2.5 text exactly (checked by a test
against the data); the plane intro plays and any key returns to the title.

### M13 — 60 Hz machines and real hardware

**For:** the game as players will run it.

- **The tuning, slipped from M6:** the jungle's busiest room must hold 50
  ticks a second at 50 and 60 Hz (M6's record has the budget), with the rest
  of the game in, and the plain movement plan never late again (M8); then
  `MOVES_TIMING_SLIPPED` goes from `test/moves.sh`. A respawn must not stop the
  game (M7: up to 2.2 s in `Spawn_NearestFree`), a new game must not take
  seconds (M8: 3.3 s of random draws; the generator is 11 000 cycles a draw),
  and the sound's interrupt must cost the busiest room no tick (M9: 2 900
  cycles a frame, 11 000 at worst). The pause menu must come up at once (M10:
  1.2 s to dim the jungle, again for each menu over it), and no key pressed
  during a stall may be lost (M10).
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
