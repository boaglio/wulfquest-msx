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
#include "palette.h"

__sfr __at(0x98) s_VdpData;
__sfr __at(0x99) s_VdpCtrl;

//-----------------------------------------------------------------------------
// Banked code (AGENTS.md §3): a ROM segment of code mapped into bank 1 while
// it runs, in place of the program's second segment. It may call only code
// below 8000h (build.sh checks the listings). A blob read puts back whatever
// segment of code bank 1 held.

u8 g_CodeSeg = ROM_CODE_SEGMENT_1;
void (*g_BankFn)();

// Calls g_BankFn in segment seg, then puts back the segment bank 1 held. In
// assembly: SDCC calls through a pointer with a library helper, which may lie
// above 8000h. The function may itself bank-call (saved on the stack).
void Bank_Call(u8 seg) __naked
{
	seg;   // in a
	__asm
		ld	hl, #_g_CodeSeg
		ld	c, (hl)
		push	bc                      ; the segment to go back to
		ld	(hl), a
		ld	(_g_Bank1Segment), a
		ld	(ADDR_BANK_1), a
		ld	hl, (_g_BankFn)
		call	00001$
		pop	bc
		ld	a, c
		ld	(_g_CodeSeg), a
		ld	(_g_Bank1Segment), a
		ld	(ADDR_BANK_1), a
		ret
	00001$:
		jp	(hl)
	__endasm;
}

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
	SET_BANK_SEGMENT(ROM_DATA_BANK, g_CodeSeg);
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
	SET_BANK_SEGMENT(ROM_DATA_BANK, g_CodeSeg);
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

	SET_BANK_SEGMENT(ROM_DATA_BANK, g_CodeSeg);
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
		push	bc                      ; the BIOS's interrupt saves less than the C may use
		push	de
		push	hl
		push	ix
		push	iy
		call	_Sound_Frame            ; the sound's frame (sound.c, segment 0 too)
		pop	iy
		pop	ix
		pop	hl
		pop	de
		pop	bc
	_Test_Sound::                       ; test/sound.sh reads the PSG here
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
	SET_BANK_SEGMENT(ROM_DATA_BANK, g_CodeSeg);
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
		ld	(_g_Bank1Segment), a    ; recorded first: the interrupt's sound puts back what this says
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
	SET_BANK_SEGMENT(ROM_DATA_BANK, g_CodeSeg);   // BoxTest left a map segment in bank 1
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
	SET_BANK_SEGMENT(ROM_DATA_BANK, g_CodeSeg);
}

u8 g_ValeInk = 0xFF;

void Data_LoadValeFrame(u8 frame, u8 vramHigh, u16 vramLow, u8* colours)
{
	u8 seg = PLAYER_FRAMES_BIN_SEG;
	u16 rel = PLAYER_FRAMES_BIN_REL;
	if (g_ValeInk != 0xFF)
	{
		seg = g_InkedSeg[g_ValeInk];   // inked in a flower's colour (M8)
		rel = g_InkedRel[g_ValeInk];
	}
	LoadSprite(seg, (const u8*)(ROM_DATA_ADDR + rel) + (u16)frame * PLAYER_FRAME_BYTES,
		PLAYER_PATTERN_BYTES, vramHigh, vramLow, colours, PLAYER_FRAME_BYTES - PLAYER_PATTERN_BYTES);
}

// A bank frame's numbers, from frames.bin (sprite_bank.h): its offset in its
// chunk, its chunk, its blocks, its origin.
u8 g_FrameOX = 0;
u8 g_FrameOY = 0;
static u8 s_Info[FRAME_INFO_BYTES];

void Data_FrameOrigin(u8 frame)
{
	SET_BANK_SEGMENT(ROM_DATA_BANK, FRAMES_BIN_SEG);
	const u8* src = (const u8*)(ROM_DATA_ADDR + FRAMES_BIN_REL) + ((u16)frame << 3);
	for (u8 i = 0; i < FRAME_INFO_BYTES; i++)
		s_Info[i] = src[i];
	SET_BANK_SEGMENT(ROM_DATA_BANK, g_CodeSeg);
	g_FrameOX = s_Info[4];
	g_FrameOY = s_Info[5];
}

#if FRAME_INFO_BYTES != 8
#error "frames.bin is read at frame * 8"
#endif

u8 Data_LoadSpriteFrame(u8 frame, u8 vramHigh, u16 vramLow, u8* colours)
{
	Data_FrameOrigin(frame);
	u8 b = s_Info[3];
	u8 blocks = (b >> 4) * (b & 15);
	u8 c = s_Info[2];
	LoadSprite(g_SpriteChunkSeg[c], (const u8*)(ROM_DATA_ADDR + g_SpriteChunkRel[c]) + (s_Info[0] | ((u16)s_Info[1] << 8)),
		blocks * (SPRITE_BLOCK_BYTES / 3 * 2), vramHigh, vramLow, colours, blocks * (SPRITE_BLOCK_BYTES / 3));
	return b;
}

void Data_LoadBlade(u8 shape, u8 vramHigh, u16 vramLow)
{
	LoadSprite(BLADE_BIN_SEG, (const u8*)(ROM_DATA_ADDR + BLADE_BIN_REL) + (u16)shape * BLADE_SHAPE_BYTES,
		BLADE_SHAPE_BYTES, vramHigh, vramLow, 0, 0);
}

//-----------------------------------------------------------------------------
// M8: a room's extra bytes (the converter's rooms_more.bin, Game.java), and
// the game's state kept in video RAM, in page 0's lines below the screen.

#include "game_data.h"

void Data_LoadRoomMore(u8 room, u8* out)
{
	SET_BANK_SEGMENT(ROM_DATA_BANK, ROOMS_MORE_BIN_SEG);
	const u8* src = (const u8*)(ROM_DATA_ADDR + ROOMS_MORE_BIN_REL) + ((u16)room << 4);
	for (u8 i = 0; i < GAME_ROOM_BYTES; i++)
		out[i] = src[i];
	SET_BANK_SEGMENT(ROM_DATA_BANK, g_CodeSeg);
}

u8 g_SpotX = 0;
u8 g_SpotY = 0;

void Data_RoomSpot(u8 room)
{
	SET_BANK_SEGMENT(ROM_DATA_BANK, ROOMS_MORE_BIN_SEG);
	const u8* src = (const u8*)(ROM_DATA_ADDR + ROOMS_MORE_BIN_REL) + ((u16)room << 4) + 8;
	g_SpotX = src[0];
	g_SpotY = src[1];
	SET_BANK_SEGMENT(ROM_DATA_BANK, g_CodeSeg);
}

#if GAME_ROOM_BYTES != 16
#error "rooms_more.bin is read at room * 16"
#endif

void Data_VramRead(u16 addr, u8* dst, u8 n)
{
	__asm__("di");
	s_VdpCtrl = (u8)(addr >> 14);   // R#14: bits 16-14, bit 16 clear (page 0)
	s_VdpCtrl = 0x80 | 14;
	s_VdpCtrl = (u8)addr;
	s_VdpCtrl = (u8)((addr >> 8) & 0x3F);   // and "read"
	while (n--)
		*dst++ = s_VdpData;
	__asm__("ei");
}

void Data_VramWrite(u16 addr, const u8* src, u8 n)
{
	__asm__("di");
	s_VdpCtrl = (u8)(addr >> 14);
	s_VdpCtrl = 0x80 | 14;
	s_VdpCtrl = (u8)addr;
	s_VdpCtrl = (u8)(((addr >> 8) & 0x3F) | 0x40);
	while (n--)
		s_VdpData = *src++;
	__asm__("ei");
}

// The glass's way out of a room (Game.java's ways_N.bin): for amulet set
// k, the quarters taken (a bit a lair), the room: its edge (n e s w) << 6
// and the cell along it, or 0xFF where the way ends in the room.
u8 Data_Way(u8 set, u8 taken, u8 room)
{
	u16 at = (((u16)(set & 3) << 4) + taken) << 8 | room;
	u8 seg = set < 4 ? WAYS_0_BIN_SEG : WAYS_1_BIN_SEG;
	const u8* src = (const u8*)(ROM_DATA_ADDR + (set < 4 ? WAYS_0_BIN_REL : WAYS_1_BIN_REL)) + at;
	SET_BANK_SEGMENT(ROM_DATA_BANK, seg);
	u8 v = *src;
	SET_BANK_SEGMENT(ROM_DATA_BANK, g_CodeSeg);
	return v;
}

//-----------------------------------------------------------------------------
// The panel (M9; panel.c): a glyph of font.bin, an image or a string of
// panel.bin, to video RAM at addr (page 0 or 1, below 10000h), x even.

#include "panel_data.h"

#if FONT_SMALL_H != 6
#error "Data_Glyph takes the small font's glyphs at 6 bytes each"
#endif

static u8 s_Glyph[32];   // a glyph's lines, or an image's bytes
static u8 s_Pair[4];     // two pixels of a glyph line's bits, as a byte: ground, ink
static u16 s_GAddr;
static u8 s_GLines;
static u8 s_GBytes;      // bytes a line: 4 (8 pixels) or 2 (4)

// s_GLines lines to video RAM from s_GAddr, 128 bytes apart: a glyph's
// (its bits two by two through s_Pair, s_GBytes a line) or, with s_GBytes
// 0, an image's (4 bytes a line, as they are). Assembly: the C was 18 000
// cycles a glyph (M9).
static void Lines() __naked
{
	__asm
		ld	hl, #_s_Glyph
		ld	de, (_s_GAddr)
		ld	a, (_s_GLines)
		ld	b, a
		di
	00001$:
		push	bc
		ld	a, d
		rlca
		rlca
		and	a, #0x03
		out	(#0x99), a              ; R#14: address bits 16-14 (below 10000h)
		ld	a, #0x8E
		out	(#0x99), a
		ld	a, e
		out	(#0x99), a
		ld	a, d
		and	a, #0x3F
		or	a, #0x40
		out	(#0x99), a              ; bits 13-8, and "write"
		ld	a, (_s_GBytes)
		or	a
		jr	nz, 00010$
		; an image's line: 4 bytes as they are
		ld	b, #4
	00005$:
		ld	a, (hl)
		out	(#0x98), a
		inc	hl
		djnz	00005$
		jr	00020$
	00010$:
		; a glyph's line: its bits two at a time, through s_Pair
		ld	b, a
		ld	c, (hl)
		inc	hl
		push	hl
	00012$:
		ld	a, c
		rlca
		rlca
		ld	c, a
		and	a, #0x03
		ld	hl, #_s_Pair
		add	a, l
		ld	l, a
		adc	a, h
		sub	a, l
		ld	h, a
		ld	a, (hl)
		out	(#0x98), a
		djnz	00012$
		pop	hl
	00020$:
		ld	a, e
		add	a, #0x80
		ld	e, a
		jr	nc, 00021$
		inc	d
	00021$:
		pop	bc
		djnz	00001$
		ei
		ret
	__endasm;
}

// A glyph's lines, the big font's (8 wide, 4 bytes a line) or the small's
// (4 wide, 2 bytes a line), in ink on ground.
void Data_Glyph(u16 addr, u8 big, u8 ch, u8 ink, u8 ground)
{
	u8 lines = big ? 8 : FONT_SMALL_H;
	u8 g = (u8)(ch - (big ? FONT_BIG_FIRST : FONT_SMALL_FIRST));
	if (g >= (big ? FONT_BIG_GLYPHS : FONT_SMALL_GLYPHS))
		g = 0;   // none: a space
	u16 at = big ? (u16)g << 3 : FONT_SMALL_AT + ((u16)g << 2) + ((u16)g << 1);   // g * 6, no library
	SET_BANK_SEGMENT(ROM_DATA_BANK, FONT_BIN_SEG);
	const u8* src = (const u8*)(ROM_DATA_ADDR + FONT_BIN_REL) + at;
	for (u8 i = 0; i < lines; i++)
		s_Glyph[i] = src[i];
	SET_BANK_SEGMENT(ROM_DATA_BANK, g_CodeSeg);
	s_Pair[0] = (u8)((ground << 4) | ground);
	s_Pair[1] = (u8)((ground << 4) | ink);
	s_Pair[2] = (u8)((ink << 4) | ground);
	s_Pair[3] = (u8)((ink << 4) | ink);
	s_GAddr = addr;
	s_GLines = lines;
	s_GBytes = big ? 4 : 2;
	Lines();
}

// An 8x8 image of panel.bin (4 bytes a line).
void Data_Image(u16 addr, u8 index)
{
	SET_BANK_SEGMENT(ROM_DATA_BANK, PANEL_BIN_SEG);
	const u8* src = (const u8*)(ROM_DATA_ADDR + PANEL_BIN_REL) + ((u16)index << 5);
	for (u8 i = 0; i < 32; i++)
		s_Glyph[i] = src[i];
	SET_BANK_SEGMENT(ROM_DATA_BANK, g_CodeSeg);
	s_GAddr = addr;
	s_GLines = 8;
	s_GBytes = 0;
	Lines();
}

#if IMAGE_BYTES != 32
#error "Data_Image takes an image at index * 32"
#endif

// A string of panel.bin at its offset: its characters into out, its length back.
u8 Data_String(u16 at, u8* out)
{
	SET_BANK_SEGMENT(ROM_DATA_BANK, PANEL_BIN_SEG);
	const u8* src = (const u8*)(ROM_DATA_ADDR + PANEL_BIN_REL) + at;
	u8 n = *src++;
	for (u8 i = 0; i < n; i++)
		out[i] = src[i];
	SET_BANK_SEGMENT(ROM_DATA_BANK, g_CodeSeg);
	return n;
}

//-----------------------------------------------------------------------------
// The shell (M10; shell.c): the VDP's commands, shell.bin's strings and
// bytes, shell_img.bin's images into video RAM, the map screen's rooms, the
// pause menu's dimmed jungle, numbers as digits.

#include "shell_data.h"

__sfr __at(0x9B) s_VdpIndirect;

u16 g_VSX = 0;
u16 g_VSY = 0;
u16 g_VDX = 0;
u16 g_VDY = 0;
u16 g_VNX = 0;
u16 g_VNY = 0;
u8  g_VClr = 0;
u8  g_VCmd = 0;

// Whether the VDP is still busy with a command (S#2's CE).
static u8 VdpBusy()
{
	__asm__("di");
	s_VdpCtrl = 2;
	s_VdpCtrl = 0x80 | 15;
	u8 s = s_VdpCtrl;
	s_VdpCtrl = 0;
	s_VdpCtrl = 0x80 | 15;
	__asm__("ei");
	return s & 1;
}

void Data_VdpWait()
{
	while (VdpBusy())
		;
}

void Data_Vdp()
{
	Data_VdpWait();
	__asm__("di");
	s_VdpCtrl = 32;
	s_VdpCtrl = 0x80 | 17;   // R#17: from R#32, counting up
	s_VdpIndirect = (u8)g_VSX;
	s_VdpIndirect = (u8)(g_VSX >> 8);
	s_VdpIndirect = (u8)g_VSY;
	s_VdpIndirect = (u8)(g_VSY >> 8);
	s_VdpIndirect = (u8)g_VDX;
	s_VdpIndirect = (u8)(g_VDX >> 8);
	s_VdpIndirect = (u8)g_VDY;
	s_VdpIndirect = (u8)(g_VDY >> 8);
	s_VdpIndirect = (u8)g_VNX;
	s_VdpIndirect = (u8)(g_VNX >> 8);
	s_VdpIndirect = (u8)g_VNY;
	s_VdpIndirect = (u8)(g_VNY >> 8);
	s_VdpIndirect = g_VClr;
	s_VdpIndirect = 0;          // R#45: down and right, video RAM
	s_VdpIndirect = g_VCmd;
	__asm__("ei");
}

void Data_ShowPage(u8 page)
{
	g_FlipR2 = (u8)((page << 5) | 0x1F);
	__asm__("di");
	g_FlipPending |= FLIP_PAGE;
	__asm__("ei");
	while (g_FlipPending)
		__asm__("halt");
}

void Data_ShellBytes(u16 at, u8* out, u8 n)
{
	SET_BANK_SEGMENT(ROM_DATA_BANK, SHELL_BIN_SEG);
	const u8* src = (const u8*)(ROM_DATA_ADDR + SHELL_BIN_REL) + at;
	while (n--)
		*out++ = *src++;
	SET_BANK_SEGMENT(ROM_DATA_BANK, g_CodeSeg);
}

void Data_LoadShellImages()
{
	SET_BANK_SEGMENT(ROM_DATA_BANK, SHELL_IMG_BIN_SEG);
	const u8* src = (const u8*)(ROM_DATA_ADDR + SHELL_IMG_BIN_REL);
	for (u8 i = 0; i < SHELL_IMAGES; i++)
	{
		u8 x = src[0];
		u16 y = SHELL_VRAM_LINE + (src[1] | (u16)src[2] << 8);
		u8 w = src[3] >> 1;
		u8 h = src[4];
		src += 5;
		for (u8 l = 0; l < h; l++)
		{
			u32 a = ((u32)(y + l) << 7) + (x >> 1);
			VramWriteAt((u8)(a >> 16), (u16)a);
			for (u8 b = 0; b < w; b++)
				s_VdpData = *src++;
		}
	}
	SET_BANK_SEGMENT(ROM_DATA_BANK, g_CodeSeg);
}

// The text being put together: shell.bin's strings, digits, characters.
u8 g_Text[64];
u8 g_TextN = 0;

void Data_TextStr(u16 at)
{
	SET_BANK_SEGMENT(ROM_DATA_BANK, SHELL_BIN_SEG);
	const u8* src = (const u8*)(ROM_DATA_ADDR + SHELL_BIN_REL) + at;
	u8 n = *src++;
	while (n-- && g_TextN < sizeof(g_Text))
		g_Text[g_TextN++] = *src++;
	SET_BANK_SEGMENT(ROM_DATA_BANK, g_CodeSeg);
}

static const u32 c_Tens[] = { 1000000000, 100000000, 10000000, 1000000, 100000, 10000, 1000, 100, 10, 1 };

void Data_TextNum(u32 v)
{
	bool lead = TRUE;
	for (u8 i = 0; i < 10; i++)
	{
		u8 d = 0;
		while (v >= c_Tens[i])
		{
			v -= c_Tens[i];
			d++;
		}
		if (d || !lead || i == 9)
		{
			lead = FALSE;
			if (g_TextN < sizeof(g_Text))
				g_Text[g_TextN++] = (u8)('0' + d);
		}
	}
}

// g_Text's characters, x even, in a font (big or small), ink on black, on a
// page (0 or 1).
void Data_TextDraw(u8 big, u8 page, u8 x, u8 y, u8 ink)
{
	u16 addr = ((u16)page << 15) + ((u16)y << 7) + (x >> 1);
	u8 step = big ? BIG_ADVANCE / 2 : SMALL_ADVANCE / 2;
	for (u8 i = 0; i < g_TextN; i++)
	{
		Data_Glyph(addr, big, g_Text[i], ink, INK_GROUND);
		addr += step;
	}
}

// The big font's glyph bits of a character (8 bytes, bit 7 the left), for
// drawing it larger (shell.c).
void Data_GlyphBits(u8 ch, u8* out)
{
	u8 g = (u8)(ch - FONT_BIG_FIRST);
	if (g >= FONT_BIG_GLYPHS)
		g = 0;
	SET_BANK_SEGMENT(ROM_DATA_BANK, FONT_BIN_SEG);
	const u8* src = (const u8*)(ROM_DATA_ADDR + FONT_BIN_REL) + ((u16)g << 3);
	for (u8 i = 0; i < 8; i++)
		out[i] = src[i];
	SET_BANK_SEGMENT(ROM_DATA_BANK, g_CodeSeg);
}

// A room's miniature on the map screen (MapScreen): its open ground in ink,
// the rest black, at its place in the playfield of a page.
void Data_MapRoom(u8 room, u8 page, u8 ink)
{
	u8 bits[MINI_BYTES];
	u16 at = MAP_MINIS + ((u16)room << 4) + ((u16)room << 2) + ((u16)room << 1);   // room * 22
	Data_ShellBytes(at, bits, MINI_BYTES);
	u8 row = room >> 4;
	u16 addr = ((u16)page << 15) + ((u16)((row << 3) + (row << 1) + row) << 7) + ((room & 15) << 3);
	u8 on = (u8)((ink << 4) | ink);
	u8 lo = (u8)((ink << 4) | INK_GROUND);
	u8 hi = (u8)((INK_GROUND << 4) | ink);
	u8 off = (u8)((INK_GROUND << 4) | INK_GROUND);
	u8 line[8];
	for (u8 v = 0; v < MINI_H; v++)
	{
		u16 b = (u16)bits[2 * v] << 8 | bits[2 * v + 1];
		for (u8 i = 0; i < 8; i++)
		{
			u8 two = (u8)(b >> 14);
			line[i] = two == 3 ? on : two == 2 ? lo : two == 1 ? hi : off;
			b <<= 2;
		}
		Data_VramWrite(addr, line, 8);
		addr += 128;
	}
}

// Framebuffer.dim: lines of a page's playfield in their plain colours, onto
// the other page (the pause menu's jungle). In assembly: a line read, each
// byte through the table, written (C took seconds).
static const u8 c_Dim[16] = { DIM_COLOURS };
static const u8 c_DimByte[256] = { DIM_BYTES };
static u8 s_DimBuf[128];
static u16 s_DimSrc;
static u16 s_DimDst;

static void DimLine() __naked
{
	__asm
		di
		ld	de, (_s_DimSrc)
		ld	a, d
		rlca
		rlca
		and	a, #0x03
		out	(#0x99), a
		ld	a, #0x8E
		out	(#0x99), a              ; R#14: bits 16-14
		ld	a, e
		out	(#0x99), a
		ld	a, d
		and	a, #0x3F
		out	(#0x99), a              ; bits 13-8, and "read"
		ld	hl, #_s_DimBuf
		ld	bc, #0x8098             ; 128 bytes from port 98h
		inir
		; each byte through the table
		ld	de, #_s_DimBuf
		ld	b, #128
	00001$:
		ld	a, (de)
		ld	hl, #_c_DimByte
		add	a, l
		ld	l, a
		adc	a, h
		sub	a, l
		ld	h, a
		ld	a, (hl)
		ld	(de), a
		inc	de
		djnz	00001$
		ld	de, (_s_DimDst)
		ld	a, d
		rlca
		rlca
		and	a, #0x03
		out	(#0x99), a
		ld	a, #0x8E
		out	(#0x99), a
		ld	a, e
		out	(#0x99), a
		ld	a, d
		and	a, #0x3F
		or	a, #0x40
		out	(#0x99), a              ; and "write"
		ld	hl, #_s_DimBuf
		ld	bc, #0x8098
		otir
		ei
		ret
	__endasm;
}

void Data_Dim(u8 from, u8 to, u8 lines)
{
	s_DimSrc = (u16)from << 15;
	s_DimDst = (u16)to << 15;
	while (lines--)
	{
		DimLine();
		s_DimSrc += 128;
		s_DimDst += 128;
	}
}

// The palette: the game's, or dimmed (the attract demo's jungle, ShellPainter:
// each colour its plain twin's), the headings' inks kept for its bands.
__sfr __at(0x9A) s_VdpPalette;

void Data_Palette(u8 dim)
{
	__asm__("di");
	s_VdpCtrl = 0;
	s_VdpCtrl = 0x80 | 16;   // R#16: from colour 0
	for (u8 n = 0; n < 16; n++)
	{
		u8 from = dim && n != INK_HEADING && n != INK_BRIGHT ? c_Dim[n] : n;
		s_VdpPalette = g_Palette[2 * from];
		s_VdpPalette = g_Palette[2 * from + 1];
	}
	__asm__("ei");
}
