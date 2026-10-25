// Wulf Quest for MSX2. M2: one room on screen (AGENTS.md §10, M2).
//
// Starts in the start room and draws rooms the way every room will be drawn:
// into the hidden page, then the pages swap. Debug keys walk the map:
// the cursor keys go to the next room that way, Space to the next room by
// number. Drawing times are kept for test/rooms.sh.

#include "msxgl.h"
#include "constants.h"
#include "palette.h"
#include "data.h"
#include "room.h"

// Read by test/rooms.sh (through the symbol map): which room is on screen,
// how many rooms have been drawn, and how many frames the last and the
// slowest drawing took.
u8  g_Room;
u16 g_DrawCount;
u8  g_DrawFrames;
u8  g_DrawWorst;

static RoomData s_Room;
static u8 s_Shown;   // the page on screen: 0 or 1

static void ShowRoom(u8 room)
{
	u16 start = g_JIFFY;
	u8 hidden = 1 - s_Shown;
	Data_LoadRoom(room, &s_Room);
	Room_Draw(&s_Room, hidden);
	g_DrawFrames = (u8)(g_JIFFY - start);
	if (g_DrawFrames > g_DrawWorst)
		g_DrawWorst = g_DrawFrames;
	Halt();
	VDP_SetPage(hidden);
	s_Shown = hidden;
	g_Room = room;
	g_DrawCount++;
}

// A key that has just gone down.
static bool Pressed(u8 key, bool* was)
{
	bool now = Keyboard_IsKeyPressed(key);
	bool edge = now && !*was;
	*was = now;
	return edge;
}

void main()
{
	VDP_SetMode(VDP_MODE_SCREEN5);
	VDP_EnableVBlank(TRUE);
	VDP_ClearVRAM();
	VDP_SetPalette(g_Palette);
	VDP_SetColor(0);
	Data_LoadScenery();

	ShowRoom(START_ROW * GRID_W + START_COL);

	bool space = FALSE, left = FALSE, right = FALSE, up = FALSE, down = FALSE;
	while (TRUE)
	{
		Halt();
		u8 col = g_Room % GRID_W;
		u8 row = g_Room / GRID_W;
		if (Pressed(KEY_SPACE, &space))
			ShowRoom(g_Room + 1);
		else if (Pressed(KEY_LEFT, &left))
			ShowRoom(row * GRID_W + (col + GRID_W - 1) % GRID_W);
		else if (Pressed(KEY_RIGHT, &right))
			ShowRoom(row * GRID_W + (col + 1) % GRID_W);
		else if (Pressed(KEY_UP, &up))
			ShowRoom((row + GRID_H - 1) % GRID_H * GRID_W + col);
		else if (Pressed(KEY_DOWN, &down))
			ShowRoom((row + 1) % GRID_H * GRID_W + col);
	}
}
