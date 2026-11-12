// Wulf Quest for MSX2: the converter's data, read from the ROM (AGENTS.md §4).
#pragma once

#include "msxgl.h"
#include "rooms.h"
#include "constants.h"

// One room as the game needs it, copied to RAM when the room is entered.
typedef struct
{
	u8 Count;                         // scenery placements, in drawing order
	u8 Object[ROOM_MAX_PLACEMENTS];   // SCENERY_xxx
	u8 X[ROOM_MAX_PLACEMENTS];        // cells
	u8 Y[ROOM_MAX_PLACEMENTS];        // cells
} RoomData;

// The cells around a room: the room's own, and CELLS_MARGIN more on every
// side from its neighbours, outside the map solid. A byte a cell, 1 solid;
// CELLS_W a row. Enough for Vale's box to look into the next room before he
// walks into it (vale.c checks the margin is).
#define CELLS_MARGIN	2
#define CELLS_W			(CELL_COLS + 2 * CELLS_MARGIN)
#define CELLS_H			(CELL_ROWS + 2 * CELLS_MARGIN)

// Changes made in the blank (the H.TIMI hook): set the registers' values,
// then g_FlipPending; the next interrupt writes them. Install once.
#define FLIP_PAGE		1   // R#2: the page shown
#define FLIP_SPRITES	2   // R#6, R#11: the sprite tables shown
extern volatile u8 g_FlipPending;
extern u8 g_FlipR2;
extern u8 g_FlipR6;
extern u8 g_FlipR11;
extern volatile u8 g_SpritesShown;
// Each sprite draw's number (sprites.c counts them in g_FlipDraw), and the
// last one put on screen: for the tests, which wait for the draw they asked for.
extern u16 g_FlipDraw;
extern volatile u16 g_ShownDraw;
void Data_InstallFlip();

// Copy the tables (tables.h) into RAM. Once, first of all.
void Data_LoadTables();

// Copy the scenery atlas into video RAM pages 2-3. Once, at start.
void Data_LoadScenery();

// Copy room 0..255 (row * GRID_W + col) into RAM.
void Data_LoadRoom(u8 room, RoomData* out);

// Unpack the collision cells around a room into cells[CELLS_H * CELLS_W].
void Data_LoadCells(u8 room, u8* cells);

// One of Vale's frames (player.h): its four 16x16 sprite patterns to video
// RAM, its four colour tables (64 bytes) to colours.
void Data_LoadValeFrame(u8 frame, u8 vramHigh, u16 vramLow, u8* colours);
extern u8 g_ValeInk;   // the inked set Data_LoadValeFrame reads (M8: player_inked_N.bin), 0xFF his own

// A frame of the sprite bank (sprite_bank.h): its blocks' base and overlay
// patterns to video RAM, their colour tables (32 bytes a block) to colours.
// Returns its blocks: columns << 4 | rows.
u8 Data_LoadSpriteFrame(u8 frame, u8 vramHigh, u16 vramLow, u8* colours);

// Whether a box (world pixels) overlaps a solid cell of the map, from the
// ROM's masks: past the map's ends it wraps round (wrap, the big beasts'
// world, §12.9) or is solid (the Java game's WorldGrid).
bool Data_BoxBlocked(i16 left, i16 top, u8 w, u8 h, bool wrap);

// The same, its box in these: for assembly to call (no arguments, the answer in a).
extern i16 g_BoxLeft;
extern i16 g_BoxTop;
extern u8 g_BoxW;
extern u8 g_BoxH;
extern u8 g_BoxWrap;
u8 Data_BoxTest();

// A shape of the blade (blade.h): its two patterns to video RAM.
void Data_LoadBlade(u8 shape, u8 vramHigh, u16 vramLow);

// Banked code (data.c): call fn, which lives in ROM segment seg, mapped into
// bank 1 for the call. BANKED(seg, fn) does both.
extern u8 g_CodeSeg;
extern void (*g_BankFn)();
void Bank_Call(u8 seg);
#define BANKED(seg, fn)	do { g_BankFn = (fn); Bank_Call(seg); } while (0)

// M8: room's extra bytes (game_data.h's GAME_ROOM_BYTES: its orchid's spot, its
// treasures' places, flags), and n bytes of video RAM at addr (page 0, so
// below 10000h), in or out. The game's state below the screen is at
// VRAM_STATE (game.h).
void Data_LoadRoomMore(u8 room, u8* out);
// The beasts' spot in a room (§12.9): into g_SpotX (the sprite's left), g_SpotY (its feet).
extern u8 g_SpotX;
extern u8 g_SpotY;
void Data_RoomSpot(u8 room);
// A bank frame's origin, into g_FrameOX, g_FrameOY (Data_LoadSpriteFrame sets them too).
extern u8 g_FrameOX;
extern u8 g_FrameOY;
void Data_FrameOrigin(u8 frame);
// The looking glass's way out of a room (M8): its edge (0 n, 1 e, 2 s, 3 w) << 6
// and the cell along it, or 0xFF where it ends in the room.
u8 Data_Way(u8 set, u8 taken, u8 room);
void Data_VramRead(u16 addr, u8* dst, u8 n);
void Data_VramWrite(u16 addr, const u8* src, u8 n);

// The panel (M9): a glyph (the big font's, 8x8, or the small's, 4 wide) in an
// ink on a ground, an 8x8 image of panel.bin, a string of panel.bin into out
// (its length back). addr: the top-left byte in video RAM, below 10000h.
void Data_Glyph(u16 addr, u8 big, u8 ch, u8 ink, u8 ground);
void Data_Image(u16 addr, u8 index);
u8 Data_String(u16 at, u8* out);

// The shell (M10): a VDP command from these registers (R#32-R#46: source,
// destination, size, colour, the command and its logical operation), sent
// once the last is done; Data_VdpWait waits for it.
extern u16 g_VSX;
extern u16 g_VSY;
extern u16 g_VDX;
extern u16 g_VDY;
extern u16 g_VNX;
extern u16 g_VNY;
extern u8  g_VClr;
extern u8  g_VCmd;
#define VCMD_HMMV	0xC0   // fill, bytes
#define VCMD_HMMM	0xD0   // copy, bytes
#define VCMD_LMMV	0x80   // fill, pixels (| an operation)
#define VCMD_LMMM	0x90   // copy, pixels (| an operation)
#define VOP_OR		0x02
#define VOP_TIMP	0x08   // copy all but colour 0
void Data_Vdp();
void Data_VdpWait();
// Show page 0 or 1 from the next blank (and wait for it).
void Data_ShowPage(u8 page);
// shell.bin's bytes; shell_img.bin's images into video RAM (once, at start).
void Data_ShellBytes(u16 at, u8* out, u8 n);
void Data_LoadShellImages();
// A line of text put together in g_Text: a string of shell.bin, a number's
// digits; then drawn in the big font or the small (x even), ink on black.
extern u8 g_Text[64];
extern u8 g_TextN;
void Data_TextStr(u16 at);
void Data_TextNum(u32 v);
void Data_TextDraw(u8 big, u8 page, u8 x, u8 y, u8 ink);
void Data_GlyphBits(u8 ch, u8* out);
// The map screen's room (16x11, MapScreen) in ink on a page; a page's
// playfield lines in their plain colours onto the other (the pause menu).
void Data_MapRoom(u8 room, u8 page, u8 ink);
void Data_Dim(u8 from, u8 to, u8 lines);
// A buffer shared by what never runs at once: Data_Dim's line, a save's record.
extern u8 g_Scratch[256];
// The game's palette, or (dim) each colour as its plain twin, the shell's
// heading and bright inks kept (the attract demo).
void Data_Palette(u8 dim);
// The input (M11: from wulfquest.c): the game's keys (VALE_*, shell.h's
// profile: 0 the cursors, 1 Q A O P, or joystick 1), the shell's (SK_*).
u8 Data_ReadKeys(u8 profile);
u8 Data_ShellKeys();
u8 Data_Joystick();
// The disk (M11): main's first call sets a disk ROM's work area aside and the
// stack to the top of RAM (ram.h); then g_DiskReady says MSX-DOS can be
// called, through Data_DiskCall (c g_BdosC, de g_BdosDE, hl g_BdosHL; a back
// in g_BdosA, hl in g_BdosHL).
extern u8  g_DiskReady;
extern u8  g_BdosC;
extern u16 g_BdosDE;
extern u16 g_BdosHL;
extern u8  g_BdosA;
void Data_DiskAside();
void Data_DiskCall();
// v as n digits (up to 10), zero-padded, all nines if it has more.
void Data_Pad(u32 v, u8 n, u8* out);
// The scenes (M12): scenes.bin's bytes; the figures into the atlas's video
// RAM; a VDP register; pixel row k of g_Text (x even) on line y of a page.
void Data_ScenesBytes(u16 at, u8* out, u8 n);
void Data_LoadSceneImages();
void Data_VdpReg(u8 reg, u8 value);
void Data_TextLine(u8 big, u8 page, u8 x, u8 y, u8 k, u8 ink);
