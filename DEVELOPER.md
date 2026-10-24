# Wulf Quest for MSX2 — for developers

How to build and test the ROM. [AGENTS.md](AGENTS.md) is the contract: the
decisions, the rules and the milestones. Read it first.

## Requirements

| Tool | Why | Tested with |
|---|---|---|
| git | the two submodules | — |
| Node.js | runs MSXgl's build tool | 22 |
| Python 3 | the palette generator and the test checks | 3.14 |
| openMSX | the tests; its C-BIOS machines are enough | 21.0 |
| a JDK 21+ and Maven | the converter, from M1 | — |

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
| `wulfquest/` | v0.7.2 | `data/`: every number, word, map and picture |
| `msxgl/` | v1.5.0 | the library, its build tool, SDCC and MSXtk |

Moving a pin is its own commit: `git -C wulfquest checkout vX.Y.Z`, then
`git add wulfquest`. M1's converter needs a wulfquest tag that has the
`core` module (after v0.7.2).

## Build and test

```bash
./build.sh          # build/wulfquest.rom, a 256 KB ASCII16 MegaROM
./build.sh test     # the build, then every test/*.sh
./build.sh clean    # remove build/
```

`build.sh` runs the binaries gate, generates `build/gen/` (today only
`palette.h`, from `wulfquest/data/art/palette.json`), then runs MSXgl's build
tool from `src/`. MSXgl's log is `build/msxgl.log`.

**How MSXgl is used without touching it.** MSXgl's build tool takes the
folder it is run from as the project, so `src/` holds `project_config.js` and
`msxgl_config.h` and the game's sources, and nothing in `msxgl/` is edited.
The project config sends all output to `build/msxgl/` and turns off MSXgl's
deploy step (which would write `src/emul/`). On its first run the tool copies
its `default_config.js` into `msxgl/projects/`; MSXgl's own `.gitignore`
ignores it, so the submodule stays clean.

## The tests

- **`test/boot.sh`** runs the ROM in openMSX on three C-BIOS machines: an
  MSX2 at 50 Hz (`C-BIOS_MSX2_EU`), an MSX2 at 60 Hz (`C-BIOS_MSX2_JP`) and
  an MSX1 (`C-BIOS_MSX1_EU`). `test/boot.tcl` drives openMSX: it waits for
  C-BIOS's logo to pass (about 10 emulated seconds; with the throttle off,
  a fraction of a second), saves the video registers, the palette and the
  screen memory, presses Space, and saves the screen again.
  `test/check_boot.py` checks them: SCREEN 5, the right refresh rate, the
  game's palette, the text, the answer to Space; on the MSX1, the "MSX2 is
  required" message.
- **Pictures.** openMSX can't take a screenshot without a display, so
  `check_boot.py` draws its own from the screen memory and the palette:
  `build/shots/boot-<machine>-before.png` and `-after.png`, 256×212, exact to
  the pixel.
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
src/             the game: wulfquest.c, MSXgl's project_config.js and msxgl_config.h
tools/           check-no-binaries.sh; gen_palette.py (until the converter, M1)
test/            boot.sh, boot.tcl, check_boot.py, gate.sh
wulfquest/       submodule: the Java game, for its data/
msxgl/           submodule: MSXgl
build/           everything the build writes; never committed
```
