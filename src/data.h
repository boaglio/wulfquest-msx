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

// Copy the scenery atlas into video RAM pages 2-3. Once, at start.
void Data_LoadScenery();

// Copy room 0..255 (row * GRID_W + col) into RAM.
void Data_LoadRoom(u8 room, RoomData* out);

// Unpack the collision cells around a room into cells[CELLS_H * CELLS_W].
void Data_LoadCells(u8 room, u8* cells);

// One of Vale's frames (player.h): its four 16x16 sprite patterns to video
// RAM, its four colour tables (64 bytes) to colours.
void Data_LoadValeFrame(u8 frame, u8 vramHigh, u16 vramLow, u8* colours);

// A frame of the sprite bank (sprite_bank.h): its base and overlay patterns to
// video RAM, their colour tables (32 bytes) to colours.
void Data_LoadSpriteFrame(u8 frame, u8 vramHigh, u16 vramLow, u8* colours);

// A shape of the blade (blade.h): its two patterns to video RAM.
void Data_LoadBlade(u8 shape, u8 vramHigh, u16 vramLow);
