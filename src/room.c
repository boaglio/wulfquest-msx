// Wulf Quest for MSX2: drawing a room (AGENTS.md §5).
//
// The playfield is filled with the ground, then each scenery object is copied
// from the atlas in pages 2-3 by the VDP, in the order the Java game draws
// them, skipping the atlas's transparent colour 0.

#include "room.h"
#include "constants.h"
#include "scenery.h"

void Room_Draw(const RoomData* room, u8 page)
{
	u16 top = (u16)page * 256;
	VDP_CommandHMMV(0, top, PLAYFIELD_W, PLAYFIELD_H, (ROOM_GROUND_COLOUR << 4) | ROOM_GROUND_COLOUR);
	for (u8 i = 0; i < room->Count; i++)
	{
		u8 o = room->Object[i];
		VDP_CommandLMMM(g_SceneryX[o], SCENERY_VRAM_LINE + g_SceneryY[o],
			room->X[i] * CELL, top + room->Y[i] * CELL,
			g_SceneryW[o], g_SceneryH[o], VDP_OP_TIMP);
	}
	VDP_CommandWait();
}
