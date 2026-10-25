// Wulf Quest for MSX2: drawing a room (AGENTS.md §5).
#pragma once

#include "data.h"

// Draw a room's playfield into video RAM page 0 or 1, and wait until the VDP is done.
void Room_Draw(const RoomData* room, u8 page);
