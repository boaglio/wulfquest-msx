# Wulf Quest for MSX2 — for developers

How to build and test the ROM. [AGENTS.md](AGENTS.md) is the contract: the
decisions, the rules and the milestones. Read it first.

## Requirements

| Tool | Why | Tested with |
|---|---|---|
| git | the two submodules | — |
| Node.js | runs MSXgl's build tool | 22 |
| Python 3 | the test checks | 3.14 |
| openMSX | the tests; its C-BIOS machines are enough | 21.0 |
| a JDK 21+ and Maven | the converter | JDK 25, Maven 3.9 |

SDCC is **not** needed on the system: MSXgl brings its own (4.6.0 in MSXgl
1.5.0), and the build uses that one, so the compiler is pinned with the
submodule.

## Getting it

```bash
git clone --recurse-submodules <this repository>
# or, in a clone made without it:
git submodule update --init
```

| Submodule | Pinned to | What we take from it |
|---|---|---|
| `wulfquest/` | 3172aee (main; the next tag when there is one) | `data/`, and `core` to read it with |
| `msxgl/` | v1.5.0 | the library, its build tool, SDCC and MSXtk |

Moving a pin is its own commit: `git -C wulfquest checkout vX.Y.Z`, then
`git add wulfquest`. The converter needs the `core` module, which came after
v0.7.2; no tag has it yet, so the pin is a commit on wulfquest's `main` until
the next release.

## Build and test

```bash
./build.sh          # build/wulfquest.rom, a 512 KB ASCII16 MegaROM
./build.sh test     # the build, then every test/*.sh
./build.sh clean    # remove build/
```

To play the build:

```bash
./run.sh                 # build, then open it in openMSX on a 50 Hz MSX2
./run.sh jp              # ... on a 60 Hz MSX2
./run.sh Philips_NMS_8245  # ... on any openMSX machine you have the ROMs for
./run.sh -n              # the last build, without building
./run.sh -- -ext debugdevice   # anything after -- goes to openMSX
```

In the ROM, for now, Ranger Vale walks the whole map in his colours,
flipping from room to room: the cursor keys or `Q A O P`, or joystick 1, and
Space or the fire button for the sabre, its blade fencing out of his hand. The panel shows a debug line: his room (col,row) and
his feet in world pixels. F5 (a debug key) shows the next room by number,
with Vale standing at the same place in it.

The default machines are C-BIOS ones, which ship with openMSX. C-BIOS shows
its logo for a few seconds before the cartridge starts.

`build.sh` runs the binaries gate, then the converter, then MSXgl's build
tool from `src/`. The logs are `build/convert.log` and `build/msxgl.log`.
Options for Maven (an offline flag, a local repository) go in `MAVEN_ARGS`.

**The converter** (`convert/`, AGENTS.md §4) is a Maven module whose parent
is the Java game's own parent POM, so Java, Jackson and JUnit follow the
submodule. The top `pom.xml` builds it together with `wulfquest/core`,
straight from the submodule, with no install step. It loads the data with
the Java game's `Content.load` and writes `build/gen/`:

| File | What |
|---|---|
| `palette.h` | the 16 colours as V9938 registers, numbered for the sprites' OR (`SpriteCut.order`), and each palette colour's number, `COLOUR_<NAME>` |
| `constants.h` | the game's numbers; the list, with where each comes from, is `convert/src/main/resources/wulfmsx/convert/constants.json` |
| `scenery.h`, `scenery_N.bin` | the scenery atlas for video RAM pages 2–3, in 16 KB ROM segments, and each object's place in it |
| `rooms.h`, `rooms.bin` | the 256 rooms: template of each, placements, collision masks |
| `player.h`, `player_hand.h`, `player_frames.bin` | Ranger Vale: every frame he can be shown in, as four layered 16x16 sprites (two blocks of a base and an overlay with the CC bit) with their colour tables, the frame table's layout, the hand each frame holds the blade in, and where a new game starts him |
| `blade.h`, `blade.bin` | his blade: a shape for each facing and fence pose, as two one-colour sprites, where they go from the hand, and how the ROM picks the pose as the Java game does |
| `sprite_bank.h`, `sprites_N.bin` | the other sprites the ROM shows (the list is `convert/src/main/resources/wulfmsx/convert/sprites.json`): every frame, cut as Vale's, and its number; the bank in chunks of a ROM segment, each frame's chunk in `g_SpriteChunk` |
| `quest_data.h` | the quest and the Wulf (M7, `Quest`): the amulet's sets, the lairs, the guardians, the Keeper, the way out, the original's Wulf; their tables are in `tables.h` |
| `sprites.txt`, `sprites.png` | every pixel a sprite shows in another colour than the art's, and pictures of those frames, the art beside the MSX2's; `sprites.json` accepts so many a sprite, and the converter fails past that |
| `jungle.h`, `jungle_data.h` | the swarm's roster and the big beasts' kinds, placings and sleepers (M6), from `entities/jungle.json` and `creatures.json` |
| `tables.h`, `tables.bin` | tables the jungle and the quest read, copied into RAM at start (`RamTables`): banked code must not read tables from the program's segments |
| `map_0.bin`, `map_1.bin` | the whole map's solid cells as one bitmap, a bit a cell (`MapBits`), for the beasts and for where Vale gets up |
| `rom.h`, `rawfiles.json` | which ROM segment each blob goes in: `rawfiles.json` for MSXgl (`src/project_config.js` reads it), `rom.h` for the C |
| `report.txt` | how far each colour moved, the atlas size, and the ROM budget |

To run it alone: `mvn -q -pl convert -am package -DskipTests exec:java`.

**Banked code** (AGENTS.md §3). `src/wulfquest_sN_b1.c` is compiled into ROM
segment N, for bank 1 (8000h): each holds one module (`#include "jungle.c"`),
whose header names the segment (`JUNGLE_SEG`) and a macro to call into it
(`JUNGLE(Jungle_TickBeasts)`, through `data.c`'s `Bank_Call`). A banked
function takes no arguments and returns nothing: they go in globals. While
it runs, segment 1 is not mapped: it may call code below 8000h (`data.c`,
`rng.c`, `move.c`, which `project_config.js` links first), other banked code
through its macro, and nothing else; so no SDCC library routine either (a
16-bit multiplication or division, a 32-bit shift: write it with shifts and
adds), and no `const` tables (they land in segment 0 or 1: use `tables.h`).
`build.sh` checks the calls in the listings and each file's segment; a new
banked file goes in segment 24 or up, and `build/gen/report.txt` shows what
the data uses below.

**MSXgl's start-up does not clear RAM.** A C global with no initial value
starts as whatever the RAM held, which on openMSX is not zero. Give every
global an initial value (`u8 g_Room = 0;`), or set it before it is read.

**How MSXgl is used without touching it.** MSXgl's build tool takes the
folder it is run from as the project, so `src/` holds `project_config.js` and
`msxgl_config.h` and the game's sources, and nothing in `msxgl/` is edited.
The project config sends all output to `build/msxgl/` and turns off MSXgl's
deploy step (which would write `src/emul/`). On its first run the tool copies
its `default_config.js` into `msxgl/projects/`; MSXgl's own `.gitignore`
ignores it, so the submodule stays clean.

## The tests

- **The converter's tests** (JUnit, `convert/src/test/`): every sprite
  frame, cut into layers, reads back as its art but for the changed pixels
  it reports; the palette order is the same every run and beats palette
  order; every blade the Java game's `PlayerPainter` draws, in every facing,
  is the converter's shape at the hand, with the pose the ROM's 32-bit hash
  picks; every room's
  collision mask equals `RoomBaker`'s; every room's scenery is the Java
  game's, in order; the atlas reads back to the same pixels; every constant
  has a source; changing a speed in a scratch copy of the data changes the
  constant; a data error stops the converter with the Java game's message.
  `./build.sh test` runs them as `mvn -q -pl convert -am verify
  -Dtest='wulfmsx.**'` (the filter keeps the Java game's own tests out).
- **`test/boot.sh`** runs the ROM in openMSX on three C-BIOS machines: an
  MSX2 at 50 Hz (`C-BIOS_MSX2_EU`), an MSX2 at 60 Hz (`C-BIOS_MSX2_JP`) and
  an MSX1 (`C-BIOS_MSX1_EU`). `test/boot.tcl` drives openMSX: it waits for
  C-BIOS's logo to pass (about 10 emulated seconds; with the throttle off,
  a fraction of a second), saves the video registers, the palette and the
  screen memory, presses F5, and saves the screen again.
  `test/check_boot.py` checks them: SCREEN 5, the right refresh rate, the
  game's palette, a room in the playfield with the debug line on the panel,
  and another room after F5; on the MSX1, the "MSX2 is
  required" message.
- **Pictures.** openMSX can't take a screenshot without a display, so
  `check_boot.py` draws its own from the video memory and the palette:
  `build/shots/boot-<machine>-before.png` and `-after.png`, 256×212, exact to
  the pixel.
- **`test/rooms.sh`** checks every room against the Java game. The
  converter's `RenderRooms` (`mvn ... exec:java@render-rooms`) draws the 256
  playfields with the Java game's `RoomPainter` into
  `build/test/rooms_expected.bin`; `test/rooms.tcl` walks the ROM through
  every room with F5 and saves the page on show; `test/check_rooms.py`
  compares them pixel for pixel and checks the slowest room draws in under
  half a second. The ROM's counters (`g_Room`,
  `g_DrawCount`, `g_DrawFrames`) are found through the symbol map. The start
  room, both ways, goes to `build/shots/room-8-10-msx.png` and `-java.png`;
  a room that differs gets the same pair.
- **`test/moves.sh`** checks Vale moves as the Java game moves him, room to
  room (M3, M4). Two plans: `test/moves.json` lists scenarios as a room and a
  start, and the keys held for so many ticks; `test/flips.json` asks for a
  generated route across the map (the converter's `Route` finds a path and
  steers the Java game along it, braking for its momentum) and for every
  edge between two rooms that the Java game's `MapAudit` finds can be crossed,
  walked across and back. The converter's `MoveTraces`
  (`exec:java@move-traces`, `exec:java@flip-traces`) plays them in the Java
  game's own `Simulation` and writes the plan (with each scenario's check)
  and the expected state after every tick; `test/moves.tcl` plays the same
  keys through the ROM on a 50 Hz and a 60 Hz MSX2, from a breakpoint on
  `Test_BeforeTick` that writes each tick's keys and reads Vale's state;
  `test/check_moves.py` compares them tick by tick and checks the speeds, the
  wall slide, the walls, the route and the edges, that the loop is never
  late, and 50 ticks a second on both. Each tick also has the frame Vale is
  drawn in and his blade's shape, which must be the Java game's
  `PlayerPainter`'s (the converter's `Painter` calls its package-private
  `frameFor` and `pose`). At every flip it checks, from a
  breakpoint on `Test_Shown`, that the page on screen is the new room as
  `RenderRooms` draws it, and that the sprites on it, drawn as the V9938
  draws them, are Vale in that frame and blade; and on every frame that
  it was put on screen while the screen was blank (the VDP's VR bit). It
  reports the hitch a flip makes and the busiest frame in Z80 cycles, from
  breakpoints on `Test_FrameStart` and `Test_FrameEnd`.
  `MOVES_PLANS=flips test/moves.sh` runs one plan.
  A third plan, `test/jungle.json` (M6), puts the jungle round him: the
  swarm, the beasts, deaths and game over. `exec:java@jungle-traces` plays it
  with a seed per scene, `JungleChecks` first checks in the Java game that the
  scene shows what it is there for (a facing swing bursts a creature, a touch
  kills, a rhino turns, a tribesman follows through a flip, the game ends),
  and each tick's `J` line, every creature and beast field by field, must be
  equal. A fourth, `test/quest.json` (M7), adds the quest and the Wulf
  (`exec:java@quest-traces`, a `Q` line a tick): `"quest": true`; `"jungle":
  "set:K"` for the first seed that hides the amulet in set K, `"room":
  "lair:N"` for lair N's room in that game; `"endless": true` for the Java
  game's infiniteLives (the ROM's `g_TestEndless`); and `"questRoute": true`,
  whose keys `QuestRoute` finds by playing the Java game to a win. Their
  timing slipped (AGENTS.md M6): plans named in `MOVES_TIMING_SLIPPED`
  (default `jungle quest`) print late frames as `SLIP` lines instead of
  failing; set it empty to see them fail.
- **`test/boot.sh`** also checks Vale is on screen where a new game starts
  him, in his first frame, in his colours; its pictures draw the sprites from
  the sprite tables. It takes them as the ROM shows each room (`Test_Shown`,
  `g_DrawCount`), not at fixed times, since the jungle is live from the
  start; on an MSX1, where no room comes, it falls back to its timed path.
- **`test/sprites.sh`** (M5) shows, through the ROM's test gallery
  (`g_TestGallery` in `wulfquest.c`), every frame of Vale, every shape of his
  blade and every frame of the sprite bank, some at the screen's left edge
  (the early clock) and over the panel (the clip); `check_sprites.py` draws
  the sprite tables as the V9938 does and compares them, pixel for pixel,
  with what the converter cut. Then Vale, his blade and five creatures on the
  same lines, 14 sprites where the V9938 draws 8: Vale and the blade must be
  whole every frame, the creatures in turn. And it times the blitter on a big
  creature's size (`Bench` in `wulfquest.c`), the measurement behind
  drawing big creatures as sprites.
  Each page waits for its own draw on screen: the ROM numbers its sprite
  draws (`g_FlipDraw`, and `g_ShownDraw` once a draw is shown), as a page
  that loads many sprites takes more than a frame.
- **How the V9938 draws sprites** is in `test/vdp_sprites.py`, shared by the
  checks: sprite mode 2, the 8-a-line limit, the early clock, and the CC bit
  (an overlay with it is drawn only on a line where a sprite without it,
  numbered before it, is, and ORs its colour where they overlap). openMSX
  can't render headless, so this model is the oracle; it follows the V9938's
  data book.
- **`test/gate.sh`** checks the binaries gate in a scratch repository: it
  passes on text and fails on a ROM, an image, and a binary file with a text
  name.

openMSX prints an ALSA "could not open sequencer" error on machines with no
MIDI device. It is harmless.

**On an MSX1** MSXgl's start-up check shows "Error: A MSX2 is required!" and
waits. Its "press C to continue anyway" is MSXgl's own way past it, for
developers; the game is not meant to run there.

## Layout

```
AGENTS.md        the contract
DEVELOPER.md     this file
README.md        for players
build.sh         build and test
run.sh           build, then play it in openMSX
src/             the game: wulfquest.c (main), data.c (the ROM's data; stays in
                 segment 0), room.c (drawing), vale.c (Ranger Vale),
                 overlay.c (the debug line on the panel), sprites.c (the
                 hardware sprites and their turns), jungle.c (the swarm,
                 the beasts, combat, deaths), move.c (box movement against
                 the scenery or the map), spawn.c (where Vale gets up),
                 rng.c (the Java game's random numbers), quest.c and
                 wulf.c (the amulet, the guardians, the Keeper, the
                 Wulf); wulfquest_sN_b1.c, the banked segments; MSXgl's
                 project_config.js and msxgl_config.h
pom.xml          builds wulfquest/core and convert/ together
convert/         the converter (Java)
tools/           check-no-binaries.sh
test/            boot.*, rooms.*, moves.*, sprites.*, flips.json, jungle.json,
                 quest.json,
                 check_*.py,
                 vdp_sprites.py, gate.sh
wulfquest/       submodule: the Java game, for its data/
msxgl/           submodule: MSXgl
build/           everything the build writes; never committed
```
