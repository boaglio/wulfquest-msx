// Wulf Quest for MSX2: the converter's data, read from the ROM (AGENTS.md §4).
//
// The blobs live in ROM segments that are mapped into bank 1 (8000h) only
// while this file copies from them; the rest of the time bank 1 holds the
// program's second segment. So this code must sit in segment 0 (build.sh
// checks the map) and call nothing outside this file while a blob is mapped:
// it talks to the VDP through its ports directly.

#include "data.h"
#include "rom.h"
#include "scenery.h"
#include "player.h"

__sfr __at(0x98) s_VdpData;
__sfr __at(0x99) s_VdpCtrl;

// Point the VDP at a 17-bit video RAM address, for writing.
static void VramWriteAt(u8 high, u16 low)
{
	__asm__("di");
	s_VdpCtrl = (u8)((high << 2) | (low >> 14));   // R#14: address bits 16-14
	s_VdpCtrl = 0x80 | 14;
	s_VdpCtrl = (u8)low;                           // address bits 7-0
	s_VdpCtrl = (u8)(((low >> 8) & 0x3F) | 0x40);  // bits 13-8, and "write"
	__asm__("ei");
}

void Data_LoadScenery()
{
	u32 vram = (u32)SCENERY_VRAM_LINE * 128;
	for (u8 c = 0; c < SCENERY_CHUNKS; c++)
	{
		const u8* src = (const u8*)(ROM_DATA_ADDR + g_SceneryChunkRel[c]);
		u16 n = g_SceneryChunkSize[c];
		SET_BANK_SEGMENT(ROM_DATA_BANK, g_SceneryChunkSeg[c]);
		VramWriteAt((u8)(vram >> 16), (u16)vram);
		while (n--)
			s_VdpData = *src++;
		vram += g_SceneryChunkSize[c];
	}
	SET_BANK_SEGMENT(ROM_DATA_BANK, ROM_CODE_SEGMENT_1);
}

void Data_LoadRoom(u8 room, RoomData* out)
{
	const u8* blob = (const u8*)(ROM_DATA_ADDR + ROOMS_BIN_REL);
	SET_BANK_SEGMENT(ROM_DATA_BANK, ROOMS_BIN_SEG);

	u8 t = blob[ROOMS_TEMPLATE_OF + room];
	const u8* p = blob + ROOMS_PLACEMENTS + 2 * t;
	p = blob + (p[0] | (p[1] << 8));
	out->Count = *p++;
	for (u8 i = 0; i < out->Count; i++)
	{
		out->Object[i] = *p++;
		out->X[i] = *p++;
		out->Y[i] = *p++;
	}
	SET_BANK_SEGMENT(ROM_DATA_BANK, ROM_CODE_SEGMENT_1);
}

// The masks are a row at a time, CELL_COLS / 8 bytes a row, the leftmost cell
// in bit 7. The window is filled a room at a time: for each of the nine rooms
// around this one, the rectangle of it that falls in the window.
#define MASK_ROW	(CELL_COLS / 8)

void Data_LoadCells(u8 room, u8* cells)
{
	const u8* blob = (const u8*)(ROM_DATA_ADDR + ROOMS_BIN_REL);
	SET_BANK_SEGMENT(ROM_DATA_BANK, ROOMS_BIN_SEG);

	u8 col = room % GRID_W;
	u8 row = room / GRID_W;
	for (i8 dr = -1; dr <= 1; dr++)
	{
		// The window's rows that are this room's, and the room's first row among them.
		u8 wy0 = dr < 0 ? 0 : dr == 0 ? CELLS_MARGIN : CELLS_MARGIN + CELL_ROWS;
		u8 wy1 = dr < 0 ? CELLS_MARGIN : dr == 0 ? CELLS_MARGIN + CELL_ROWS : CELLS_H;
		u8 cy = dr < 0 ? CELL_ROWS - CELLS_MARGIN : 0;
		for (i8 dc = -1; dc <= 1; dc++)
		{
			u8 wx0 = dc < 0 ? 0 : dc == 0 ? CELLS_MARGIN : CELLS_MARGIN + CELL_COLS;
			u8 wx1 = dc < 0 ? CELLS_MARGIN : dc == 0 ? CELLS_MARGIN + CELL_COLS : CELLS_W;
			u8 cx0 = dc < 0 ? CELL_COLS - CELLS_MARGIN : 0;
			bool outside = (dr < 0 && row == 0) || (dr > 0 && row == GRID_H - 1)
				|| (dc < 0 && col == 0) || (dc > 0 && col == GRID_W - 1);
			const u8* mask = outside ? 0
				: blob + ROOMS_MASKS + (u16)blob[ROOMS_TEMPLATE_OF + room + dr * GRID_W + dc] * ROOM_MASK_BYTES;
			u8 y = cy;
			for (u8 wy = wy0; wy < wy1; wy++, y++)
			{
				u8* out = cells + (u16)wy * CELLS_W + wx0;
				if (outside)
				{
					for (u8 wx = wx0; wx < wx1; wx++)
						*out++ = 1;
					continue;
				}
				const u8* m = mask + (u16)y * MASK_ROW;
				u8 x = cx0;
				for (u8 wx = wx0; wx < wx1; wx++, x++)
					*out++ = (m[x >> 3] & (0x80 >> (x & 7))) ? 1 : 0;
			}
		}
	}

	SET_BANK_SEGMENT(ROM_DATA_BANK, ROM_CODE_SEGMENT_1);
}

void Data_LoadValeFrame(u8 frame, u8 vramHigh, u16 vramLow)
{
	const u8* src = (const u8*)(ROM_DATA_ADDR + PLAYER_FRAMES_BIN_REL) + (u16)frame * PLAYER_FRAME_BYTES;
	SET_BANK_SEGMENT(ROM_DATA_BANK, PLAYER_FRAMES_BIN_SEG);
	VramWriteAt(vramHigh, vramLow);
	for (u8 n = PLAYER_FRAME_BYTES; n; n--)
		s_VdpData = *src++;
	SET_BANK_SEGMENT(ROM_DATA_BANK, ROM_CODE_SEGMENT_1);
}
