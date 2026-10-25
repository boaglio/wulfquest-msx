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
	const u8* m = blob + ROOMS_MASKS + (u16)t * ROOM_MASK_BYTES;
	for (u8 i = 0; i < ROOM_MASK_BYTES; i++)
		out->Mask[i] = m[i];

	SET_BANK_SEGMENT(ROM_DATA_BANK, ROM_CODE_SEGMENT_1);
}
