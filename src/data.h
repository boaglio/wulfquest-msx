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
