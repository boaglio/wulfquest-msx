// Wulf Quest for MSX2: the converter's data, read from the ROM (AGENTS.md §4).
#pragma once

#include "msxgl.h"
#include "rooms.h"

// One room as the game needs it, copied to RAM when the room is entered.
typedef struct
{
	u8 Count;                         // scenery placements, in drawing order
	u8 Object[ROOM_MAX_PLACEMENTS];   // SCENERY_xxx
	u8 X[ROOM_MAX_PLACEMENTS];        // cells
	u8 Y[ROOM_MAX_PLACEMENTS];        // cells
	u8 Mask[ROOM_MASK_BYTES];         // collision: a row at a time, the leftmost cell in bit 7
} RoomData;

// Copy the scenery atlas into video RAM pages 2-3. Once, at start.
void Data_LoadScenery();

// Copy room 0..255 (row * GRID_W + col) into RAM.
void Data_LoadRoom(u8 room, RoomData* out);
