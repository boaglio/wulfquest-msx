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
#include "sprite_bank.h"
#include "tables.h"
#include "blade.h"

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

u8 g_Tables[TABLES_BYTES];

//-----------------------------------------------------------------------------
// Flips in the blank (H.TIMI). The BIOS's interrupt routine calls the hook
// early, before its own work (C-BIOS's can take 13 000 cycles, longer than a
// 60 Hz blank); the main loop wakes only after it. So what must change on
// screen at once, the page shown and the sprite tables, is changed here:
// the main loop leaves the registers' values in g_Flip* and sets
// g_FlipPending; this writes them and clears it. Then the hook that was
// there before (a disk ROM's, say), copied to s_OldHook. In segment 0, which
// is always mapped: an interrupt may come with any bank switched.
#define H_TIMI_ADDR	0xFD9F
volatile u8 g_FlipPending = 0;   // FLIP_PAGE | FLIP_SPRITES
u8 g_FlipR2 = 0;
u8 g_FlipR6 = 0;
u8 g_FlipR11 = 0;
volatile u8 g_SpritesShown = 0;  // the sprite set the VDP shows: written here
u16 g_FlipDraw = 0;
volatile u16 g_ShownDraw = 0;
static u8 s_OldHook[5];

void Data_Flip() __naked
{
	__asm
		push	af                      ; the hook after may want a, the VDP status
		ld	a, (_g_FlipPending)
		or	a
		jr	z, _Test_Flipped
		bit	0, a                    ; FLIP_PAGE
		jr	z, 00010$
		ld	a, (_g_FlipR2)
		out	(#0x99), a
		ld	a, #0x82
		out	(#0x99), a
	00010$:
		ld	a, (_g_FlipPending)
		bit	1, a                    ; FLIP_SPRITES
		jr	z, 00020$
		ld	a, (_g_FlipR11)
		out	(#0x99), a
		ld	a, #0x8B
		out	(#0x99), a
		ld	a, (_g_FlipR6)
		out	(#0x99), a
		ld	a, #0x86
		out	(#0x99), a
		ld	a, (_g_FlipR11)         ; set 1's attribute table has bit 15 set
		ld	(_g_SpritesShown), a
		ld	a, (_g_FlipDraw)
		ld	(_g_ShownDraw), a
		ld	a, (_g_FlipDraw + 1)
		ld	(_g_ShownDraw + 1), a
	00020$:
		xor	a
		ld	(_g_FlipPending), a
	_Test_Flipped::
		pop	af
		jp	_s_OldHook
	__endasm;
}

void Data_InstallFlip()
{
	u8* hook = (u8*)H_TIMI_ADDR;
	__asm__("di");
	for (u8 i = 0; i < 5; i++)
		s_OldHook[i] = hook[i];
	hook[0] = 0xC3;   // jp Data_Flip
	*(u16*)(hook + 1) = (u16)Data_Flip;
	hook[3] = 0xC9;
	hook[4] = 0xC9;
	__asm__("ei");
}

void Data_LoadTables()
{
	const u8* src = (const u8*)(ROM_DATA_ADDR + TABLES_BIN_REL);
	SET_BANK_SEGMENT(ROM_DATA_BANK, TABLES_BIN_SEG);
	for (u16 i = 0; i < TABLES_BYTES; i++)
		g_Tables[i] = src[i];
	SET_BANK_SEGMENT(ROM_DATA_BANK, ROM_CODE_SEGMENT_1);
}

// The whole map's cells, from the ROM's masks (Data_BoxBlocked).
#define MAP_COLS	(GRID_W * CELL_COLS)
#define MAP_ROWS	(GRID_H * CELL_ROWS)

// Data_BoxBlocked against the map's bitmap (map_0.bin, map_1.bin: a row of
// cells 64 bytes, the leftmost in bit 7), in assembly: first the box's
// columns, as bytes and bits, round the map's ends or off it; then each row,
// from its segment. In C, from the rooms' masks, a test was 9 000 cycles, and
// the beasts make several a tick.
#define BOX_CELLS	6
#define MAP_ROWS_0	(MAP_0_BIN_SIZE / 64)
#if CELL != 8 || MAP_COLS != 512 || MAP_0_BIN_SIZE != 16384
#error "Data_BoxTest takes 8 px cells, a map 512 cells wide, its first 256 rows in map_0.bin"
#endif

i16 g_BoxLeft;
i16 g_BoxTop;
u8 g_BoxW;
u8 g_BoxH;
u8 g_BoxWrap;
static u8 s_NCols;
static u8 s_ColByte[BOX_CELLS];
static u8 s_ColBit[BOX_CELLS];
static const u8 c_Bit[8] = { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 };

static u8 BoxTest() __naked
{
	__asm
		push	ix
		; columns: cx0 = left >> 3 and how many
		ld	hl, (_g_BoxLeft)
		ld	a, (_g_BoxW)
		call	00080$
		ld	a, b
		ld	(_s_NCols), a
		ld	ix, #_s_ColByte
	00010$:
		bit	7, h
		jr	z, 00011$
		ld	a, (_g_BoxWrap)
		or	a
		jp	z, 00090$
		push	hl
		inc	h
		inc	h                       ; + 512
		jr	00013$
	00011$:
		ld	a, h
		cp	a, #2                   ; >= 512
		push	hl
		jr	c, 00013$
		ld	a, (_g_BoxWrap)
		or	a
		jp	z, 00091$
		dec	h
		dec	h
	00013$:
		ld	a, l
		and	a, #7
		ld	e, a
		ld	d, #0
		push	hl
		ld	hl, #_c_Bit
		add	hl, de
		ld	a, (hl)
		ld	BOX_CELLS (ix), a       ; s_ColBit
		pop	hl
		srl	h
		rr	l
		srl	h
		rr	l
		srl	h
		rr	l
		ld	0 (ix), l               ; s_ColByte: gx >> 3, 0..63
		inc	ix
		pop	hl
		inc	hl
		djnz	00010$
		; rows
		ld	hl, (_g_BoxTop)
		ld	a, (_g_BoxH)
		call	00080$                  ; hl = cy0, b = rows
	00020$:
		push	hl
		push	bc
		bit	7, h
		jr	z, 00021$
		ld	a, (_g_BoxWrap)
		or	a
		jp	z, 00092$
		ld	de, #MAP_ROWS
		add	hl, de
		jr	00023$
	00021$:
		ld	de, #MAP_ROWS
		or	a
		sbc	hl, de
		jr	nc, 00022$
		add	hl, de                  ; on the map: as it was
		jr	00023$
	00022$:
		ld	a, (_g_BoxWrap)         ; hl is gy - MAP_ROWS already
		or	a
		jp	z, 00092$
	00023$:
		; the row's segment and address: gy < MAP_ROWS_0 in map_0, the rest in map_1
		ld	a, h
		or	a
		jr	nz, 00024$
		ld	a, #MAP_0_BIN_SEG
		ld	de, #(ROM_DATA_ADDR + MAP_0_BIN_REL)
		jr	00025$
	00024$:
		ld	h, #0                   ; gy - 256
		ld	a, #MAP_1_BIN_SEG
		ld	de, #(ROM_DATA_ADDR + MAP_1_BIN_REL)
	00025$:
		ld	(ADDR_BANK_1), a
		add	hl, hl
		add	hl, hl
		add	hl, hl
		add	hl, hl
		add	hl, hl
		add	hl, hl                  ; * 64
		add	hl, de                  ; the row
		ld	a, (_s_NCols)
		ld	b, a
		ld	ix, #_s_ColByte
	00026$:
		ld	e, 0 (ix)
		ld	d, #0
		push	hl
		add	hl, de
		ld	a, (hl)
		pop	hl
		and	a, BOX_CELLS (ix)
		jr	nz, 00030$
		inc	ix
		djnz	00026$
		pop	bc
		pop	hl
		inc	hl
		djnz	00020$
		xor	a
		pop	ix
		ret
	00030$:
		pop	bc
		pop	hl
		ld	a, #1
		pop	ix
		ret
	00092$:
		pop	bc
		pop	hl
		ld	a, #1                   ; off the map, which does not wrap: solid
		pop	ix
		ret
	00091$:
		pop	hl
	00090$:
		ld	a, #1
		pop	ix
		ret
	; hl = start (pixels), a = length: hl = start >> 3, b = cells covered
	00080$:
		dec	a
		ld	e, a
		ld	d, #0
		push	hl
		add	hl, de
		sra	h
		rr	l
		sra	h
		rr	l
		sra	h
		rr	l
		ld	b, l
		pop	hl
		sra	h
		rr	l
		sra	h
		rr	l
		sra	h
		rr	l
		ld	a, b
		sub	a, l
		inc	a
		ld	b, a
		ret
	__endasm;
}

u8 Data_BoxTest()
{
	u8 solid = BoxTest();
	SET_BANK_SEGMENT(ROM_DATA_BANK, ROM_CODE_SEGMENT_1);   // BoxTest left a map segment in bank 1
	return solid;
}

bool Data_BoxBlocked(i16 left, i16 top, u8 w, u8 h, bool wrap)
{
	g_BoxLeft = left;
	g_BoxTop = top;
	g_BoxW = w;
	g_BoxH = h;
	g_BoxWrap = wrap;
	return Data_BoxTest();
}


#if CELL_COLS != 32
#error "Data_BoxBlocked takes a room 32 cells wide"
#endif

// CopyOut's arguments: s_OutN bytes from s_OutSrc to the VDP's data port,
// then s_RamN more to s_RamDst. An outi loop: SDCC's C loop was 68 cycles a
// byte (AGENTS.md §10, M5).
static const u8* s_OutSrc = 0;
static u8 s_OutN = 0;
static u8* s_RamDst = 0;
static u8 s_RamN = 0;

static void CopyOut() __naked
{
	__asm
		ld	hl, (_s_OutSrc)
		ld	a, (_s_OutN)
		ld	b, a
		ld	c, #0x98
	00001$:
		outi
		jr	nz, 00001$
		ld	a, (_s_RamN)
		or	a
		ret	z
		ld	c, a
		ld	b, #0
		ld	de, (_s_RamDst)
		ldir
		ret
	__endasm;
}

// A sprite frame from the ROM: its patterns to video RAM, its colours (if any) to RAM.
static void LoadSprite(u8 segment, const u8* src, u8 patternBytes, u8 vramHigh, u16 vramLow, u8* colours,
	u8 colourBytes)
{
	SET_BANK_SEGMENT(ROM_DATA_BANK, segment);
	VramWriteAt(vramHigh, vramLow);
	s_OutSrc = src;
	s_OutN = patternBytes;
	s_RamDst = colours;
	s_RamN = colourBytes;
	CopyOut();
	SET_BANK_SEGMENT(ROM_DATA_BANK, ROM_CODE_SEGMENT_1);
}

void Data_LoadValeFrame(u8 frame, u8 vramHigh, u16 vramLow, u8* colours)
{
	LoadSprite(PLAYER_FRAMES_BIN_SEG,
		(const u8*)(ROM_DATA_ADDR + PLAYER_FRAMES_BIN_REL) + (u16)frame * PLAYER_FRAME_BYTES,
		PLAYER_PATTERN_BYTES, vramHigh, vramLow, colours, PLAYER_FRAME_BYTES - PLAYER_PATTERN_BYTES);
}

u8 Data_LoadSpriteFrame(u8 frame, u8 vramHigh, u16 vramLow, u8* colours)
{
	u8 b = g_SpriteBlocks[frame];
	u8 blocks = (b >> 4) * (b & 15);
	LoadSprite(SPRITES_BIN_SEG, (const u8*)(ROM_DATA_ADDR + SPRITES_BIN_REL) + g_SpriteOffset[frame],
		blocks * (SPRITE_BLOCK_BYTES / 3 * 2), vramHigh, vramLow, colours, blocks * (SPRITE_BLOCK_BYTES / 3));
	return b;
}

void Data_LoadBlade(u8 shape, u8 vramHigh, u16 vramLow)
{
	LoadSprite(BLADE_BIN_SEG, (const u8*)(ROM_DATA_ADDR + BLADE_BIN_REL) + (u16)shape * BLADE_SHAPE_BYTES,
		BLADE_SHAPE_BYTES, vramHigh, vramLow, 0, 0);
}
