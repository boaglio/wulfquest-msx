# AGENTS.md — Wulf Quest for MSX2

**Draft 1, 2026-10-23.** Decisions marked *to prototype* are not settled yet.

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
| Rules | The original's only. Wulf Quest's invented options (`inventedCreatures`, `roamingWulf`, `inventedOrchids`) are left out. |

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
  convert/           the converter (Java, on wulfquest's core module)
  src/               the game in C
  test/              openMSX scripts and their expected results
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

- **M0** — The repository, the toolchain and an empty ROM that boots in openMSX
- **M1** — The converter: the data checked and turned into tables
- **M2** — One room on screen: 8,10 in SCREEN 5
- **M3** — Ranger Vale walks: input, movement and collision
- **M4** — Flip-screen: every room, every edge
- **M5** — Sprites in colour: layered sprites, a colour per line
- **M6** — The jungle's creatures: the swarm, rhinos, tribesmen, hippo and warthog
- **M7** — The Wulf, the guardians and the amulet
- **M8** — Orchids, flames, treasures and the score
- **M9** — The panel, sound and music on the PSG
- **M10** — Title, menus, map screen and hi-scores
- **M11** — Saves: SRAM and disk
- **M12** — Endings, New Game+, the credits and the plane intro
- **M13** — 60 Hz machines and real hardware
- **M14** — The first release: the ROM and a disk image
