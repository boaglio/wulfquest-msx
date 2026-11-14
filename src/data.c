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
#include "tables.h"   // and ram.h
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
				// The row's bits from cell cx0 on, shifted out of a byte at a time.
				const u8* m = mask + (u16)y * MASK_ROW + (cx0 >> 3);
				u8 bits = (u8)(*m++ << (cx0 & 7));
				u8 left = 8 - (cx0 & 7);
				for (u8 wx = wx0; wx < wx1; wx++)
				{
					*out++ = bits >> 7;
					bits <<= 1;
					if (--left == 0)
					{
						bits = *m++;
						left = 8;
					}
				}
			}
		}
	}

	SET_BANK_SEGMENT(ROM_DATA_BANK, g_CodeSeg);
}

__at(RAM_TABLES) unsigned char g_Tables[TABLES_BYTES];   // at its place in RAM (ram.h)
__at(RAM_LOW) u8 g_RamLow;   // the big arrays' lowest byte: build.sh's check

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
u8  g_DiskReady = 0;     // a disk ROM, and its area set aside: Data_DiskCall works (M11)

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
	// A disk ROM's hook (its motor's timer) works in its area, which is the
	// game's now (Data_DiskAside): it runs only in a disk call (M11).
	for (u8 i = 0; i < 5; i++)
		s_OldHook[i] = g_DiskReady ? 0xC9 : hook[i];
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

// A strip one cell across, round the map's ends (M13: Beast_Step's step
// into a new strip of cells; BoxTest's columns and rows were 2 000 cycles a
// strip). Along x: the column of g_BoxLeft, the rows g_BoxTop..+g_BoxH;
// along y: the row of g_BoxTop, the columns g_BoxLeft..+g_BoxW. 1 if any of
// its cells is solid.
static u8 s_SOff;   // a column's byte in a row

#if ((ROM_DATA_ADDR + MAP_0_BIN_REL) | (ROM_DATA_ADDR + MAP_1_BIN_REL)) & 63
#error "Data_StripTest takes the map's rows on 64 bytes"
#endif
u8 Data_StripTest(u8 alongX) __naked
{
	alongX;   // a
	__asm
		or	a
		jp	z, 00100$
		; ---- a column: its byte in a row, and its bit
		ld	hl, (_g_BoxLeft)
		call	00090$                  ; the column (signed: its low bits are the map's)
		ld	a, l
		and	a, #7
		ld	e, a
		ld	d, #0
		push	hl
		ld	hl, #_c_Bit
		add	hl, de
		ld	c, (hl)                 ; c = its bit
		pop	hl
		ld	a, l
		srl	h
		rra
		srl	h
		rra
		srl	h
		rra
		and	a, #63
		ld	(_s_SOff), a
		; its rows
		ld	hl, (_g_BoxTop)
		ld	a, (_g_BoxH)
		call	00080$                  ; hl = the first, b = how many
		call	00085$
		ex	de, hl                  ; de = the row, 0..MAP_ROWS - 1
		; its byte's address once, then 64 on a row (M13: worked out a row,
		; the segment mapped each time); again at row 256 (map_1) and round
		; the map's foot
	00050$:
		ld	h, d
		ld	l, e
		call	00070$
		ld	a, (_s_SOff)
		add	a, l
		ld	l, a
		adc	a, h
		sub	a, l
		ld	h, a
	00051$:
		ld	a, (hl)
		and	a, c
		jr	nz, 00098$
		dec	b
		jr	z, 00053$
		inc	de
		ld	a, e
		or	a
		jr	z, 00050$               ; row 256: the next segment's
		cp	a, #(MAP_ROWS & 0xFF)
		jr	nz, 00052$
		ld	a, d
		cp	a, #(MAP_ROWS >> 8)
		jr	nz, 00052$
		ld	de, #0                  ; round the map's foot
		jr	00050$
	00052$:
		ld	a, l
		add	a, #64
		ld	l, a
		jr	nc, 00051$
		inc	h
		jr	00051$
	00053$:
		xor	a
		jr	00099$
	00100$:
		; ---- a row, and its columns
		ld	hl, (_g_BoxTop)
		call	00090$
		call	00085$
		call	00070$
		push	hl                      ; the row's bytes
		ld	hl, (_g_BoxLeft)
		ld	a, (_g_BoxW)
		call	00080$                  ; hl = the first column, b = how many
		pop	de
		; the first column's byte and bit, then a bit on a column: past bit 0
		; the next byte, round the row's end (rows lie on 64 bytes: M13, the
		; column worked out anew each time)
		ld	a, l
		and	a, #7
		push	de
		ld	e, a
		ld	d, #0
		push	hl
		ld	hl, #_c_Bit
		add	hl, de
		ld	c, (hl)                 ; c = its bit
		pop	hl
		pop	de
		ld	a, l
		srl	h
		rra
		srl	h
		rra
		srl	h
		rra
		and	a, #63                  ; (column mod 512) >> 3, round the map's ends
		add	a, e
		ld	l, a
		adc	a, d
		sub	a, l
		ld	h, a                    ; hl = its byte
	00110$:
		ld	a, (hl)
		and	a, c
		jr	nz, 00098$
		dec	b
		jr	z, 00112$
		rrc	c
		jr	nc, 00110$              ; the next column in the same byte
		inc	l
		ld	a, l
		and	a, #63
		jr	nz, 00110$
		ld	a, l
		sub	a, #64                  ; round the row's end
		ld	l, a
		jr	00110$
	00112$:
		xor	a
		jr	00099$
	00098$:
		ld	a, #1
	00099$:
		ld	c, a
		ld	a, (_g_CodeSeg)
		ld	(_g_Bank1Segment), a
		ld	(ADDR_BANK_1), a
		ld	a, c
		ret

	; ---- hl = row 0..MAP_ROWS - 1: hl = its bytes, its segment mapped (de kept)
	00070$:
		push	de
		ld	a, h
		or	a
		jr	nz, 00072$
		ld	a, #MAP_0_BIN_SEG
		ld	de, #(ROM_DATA_ADDR + MAP_0_BIN_REL)
		jr	00073$
	00072$:
		ld	h, #0                   ; row - 256
		ld	a, #MAP_1_BIN_SEG
		ld	de, #(ROM_DATA_ADDR + MAP_1_BIN_REL)
	00073$:
		ld	(_g_Bank1Segment), a    ; recorded first: the interrupt's sound puts back what this says
		ld	(ADDR_BANK_1), a
		add	hl, hl
		add	hl, hl
		add	hl, hl
		add	hl, hl
		add	hl, hl
		add	hl, hl                  ; * 64
		add	hl, de
		pop	de
		ret

	; ---- hl = start (pixels), a = length: hl = start >> 3, b = cells covered
	00080$:
		dec	a
		ld	e, a
		ld	d, #0
		push	hl
		add	hl, de
		call	00090$
		ld	b, l
		pop	hl
		call	00090$
		ld	a, b
		sub	a, l
		inc	a
		ld	b, a
		ret

	; ---- hl = hl >> 3, signed
	00090$:
		sra	h
		rr	l
		sra	h
		rr	l
		sra	h
		rr	l
		ret

	; ---- a row, hl, round the map's ends once: 0..MAP_ROWS - 1
	00085$:
		bit	7, h
		jr	z, 00086$
		ld	de, #MAP_ROWS
		add	hl, de
		ret
	00086$:
		ld	de, #-MAP_ROWS
		add	hl, de
		ret	c                       ; MAP_ROWS or more: less MAP_ROWS
		ld	de, #MAP_ROWS
		add	hl, de
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

// Assembly (M13: 1 700 cycles in C, a frame loaded).
void Data_FrameOrigin(u8 frame) __naked
{
	frame;   // a
	__asm
		ld	l, a
		ld	h, #0
		add	hl, hl
		add	hl, hl
		add	hl, hl                  ; frame * 8
		ld	de, #(ROM_DATA_ADDR + FRAMES_BIN_REL)
		add	hl, de
		ld	a, #FRAMES_BIN_SEG
		ld	(_g_Bank1Segment), a    ; recorded first: the interrupt's sound puts back what this says
		ld	(ADDR_BANK_1), a
		ld	de, #_s_Info
		ld	bc, #FRAME_INFO_BYTES
		ldir
		ld	a, (_g_CodeSeg)
		ld	(_g_Bank1Segment), a
		ld	(ADDR_BANK_1), a
		ld	a, (_s_Info + 4)
		ld	(_g_FrameOX), a
		ld	a, (_s_Info + 5)
		ld	(_g_FrameOY), a
		ret
	__endasm;
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

// The glass's trail (M12; trail.c): a room's record of its openings, from the
// piece of trail_N.bin that holds it (its rooms' offsets halved, then theirs).
#include "trail_data.h"
static const u8 c_TrailRoom0[TRAIL_PIECES] = { TRAIL_ROOM0 };

void Data_TrailRoom(u8 room, u8* out)
{
	u8 p = TRAIL_PIECES - 1;
	while (room < c_TrailRoom0[p])
		p--;
	SET_BANK_SEGMENT(ROM_DATA_BANK, g_TrailPieceSeg[p]);
	const u8* piece = (const u8*)(ROM_DATA_ADDR + g_TrailPieceRel[p]);
	const u8* src = piece + ((u16)piece[room - c_TrailRoom0[p]] << 1);
	for (u8 n = 0; n < TRAIL_RECORD_MAX; n++)   // past its end at most into the next one's: harmless
		*out++ = *src++;
	SET_BANK_SEGMENT(ROM_DATA_BANK, g_CodeSeg);
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

// n images of a blob in segment seg, from src (its header and bytes each).
static void LoadImages(u8 seg, const u8* src, u8 n)
{
	SET_BANK_SEGMENT(ROM_DATA_BANK, seg);
	for (u8 i = 0; i < n; i++)
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

void Data_LoadShellImages()
{
	LoadImages(SHELL_IMG_BIN_SEG, (const u8*)(ROM_DATA_ADDR + SHELL_IMG_BIN_REL), SHELL_IMAGES);
}

void Data_LoadJobImages(u8 job)
{
	if (job == 0)
	{
		Data_LoadShellImages();   // the game's own quarters are among them
		return;
	}
	const u8* src = (const u8*)(ROM_DATA_ADDR + SHELL_JOBS_BIN_REL);
	while (--job)
		src += SHELL_JOB_IMG_BYTES;
	LoadImages(SHELL_JOBS_BIN_SEG, src, SHELL_JOB_IMAGES);
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

// v as n digits, zero-padded, all nines when it has more (PanelPainter.pad;
// the panel's, banked, which may not divide).
void Data_Pad(u32 v, u8 n, u8* out)
{
	u8 first = 10 - n;
	if (first && v >= c_Tens[first - 1])
		v = c_Tens[first - 1] - 1;
	for (u8 i = 0; i < n; i++)
	{
		u32 ten = c_Tens[first + i];
		u8 d = '0';
		while (v >= ten)
		{
			v -= ten;
			d++;
		}
		out[i] = d;
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
// The pause menu's line of jungle being dimmed, and a save's record
// (save.h's g_SaveBuf): one buffer, never wanted by both at once (RAM, M12).
u8 g_Scratch[256];
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
		ld	hl, #_g_Scratch
		ld	bc, #0x8098             ; 128 bytes from port 98h
		inir
		; each byte through the table
		ld	de, #_g_Scratch
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
		ld	hl, #_g_Scratch
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

//-----------------------------------------------------------------------------
// The input (§7; M11 moved it here from wulfquest.c, for room there): the
// keyboard's rows (the PPI) and joystick 1 (the PSG's port A), read from
// their ports with the interrupts off, as the BIOS's own scan and the sound
// share them.

#include "shell.h"
#include "vale.h"

__sfr __at(0xA9) s_PpiB;
__sfr __at(0xAA) s_PpiC;

static u8 KeyRow(u8 row)
{
	__asm__("di");
	s_PpiC = (u8)((s_PpiC & 0xF0) | row);
	u8 v = s_PpiB;
	__asm__("ei");
	return v;
}

static bool Key(u8 key)   // MSXgl's KEY_*: row in bits 3-0, bit in 6-4
{
	return !(KeyRow(key & 0x0F) & (1 << (key >> 4)));
}

// Joystick 1's port A: bits 0-5 (up, down, left, right, A, B), 0 when held.
// test/shell.sh's breakpoint is Data_JoyPortA, just after it is read.
u8 Data_Joystick() __naked
{
	__asm
		ld	a, #15
		di
		out	(#0xA0), a
		ld	a, #0x0F                ; joystick port 1
		out	(#0xA1), a
		ld	a, #14
		out	(#0xA0), a
		in	a, (#0xA2)
	_Data_JoyPortA::
		ei
		ret
	__endasm;
}

u8 Data_ReadKeys(u8 profile)
{
	u8 joy = ~Data_Joystick();
	u8 k = joy & (VALE_UP | VALE_DOWN | VALE_LEFT | VALE_RIGHT);
	if (joy & 0x10)
		k |= VALE_FIRE;
	if (Key(profile ? KEY_Q : KEY_UP))
		k |= VALE_UP;
	if (Key(profile ? KEY_A : KEY_DOWN))
		k |= VALE_DOWN;
	if (Key(profile ? KEY_O : KEY_LEFT))
		k |= VALE_LEFT;
	if (Key(profile ? KEY_P : KEY_RIGHT))
		k |= VALE_RIGHT;
	if (Key(KEY_SPACE))
		k |= VALE_FIRE;
	return k;
}

u8 Data_ShellKeys()
{
	u8 k = 0;
	if (Key(KEY_ESC))
		k |= SK_ESC;
	if (Key(KEY_STOP) || Key(KEY_F1))
		k |= SK_PAUSE;
	if (Key(KEY_M) || !(Data_Joystick() & 0x20))
		k |= SK_MAP;
	if (Key(KEY_D))
		k |= SK_DIFF;
	if (Key(KEY_RETURN))
		k |= SK_ENTER;
	if (Key(JOB_KEY))
		k |= SK_JOB;
	for (u8 r = 0; r < 11; r++)
		if (KeyRow(r) != 0xFF)
			k |= SK_ANY;
	return k;
}

//-----------------------------------------------------------------------------
// The disk (M11; src/ram.h). A disk ROM keeps its work area from HIMEM up to
// F37Fh, where the game's big arrays and its stack are: Data_DiskAside, the
// first thing main does, copies that area into the RAM under the BIOS (page
// 0's RAM, by RAMAD0) and takes the top of RAM for the stack; each disk call
// (Data_DiskCall) puts the area back for the call and the game's back after.
// While page 0 holds RAM the interrupts are off: there is no handler there.

#define HIMEM_ADDR	0xFC4A
#define RAMAD0_ADDR	0xF341
#define BDOS_ADDR	0xF37D
#define DISKVE_ADDR	0xF323   // the disk error handler: a pointer to its address
#define BREAKV_ADDR	0xF325   // CTRL-STOP's, the same
#define AREA_DISK	0x0000   // in page 0's RAM: the disk ROM's area
#define AREA_GAME	0x2000   //   and the game's, while a call has the disk's out
#define AREA_MAX	0x2000

static u8  s_RamSlot = 0;     // page 0's RAM (RAMAD0: expanded flag, secondary, primary)
static u16 s_Himem = 0;
static u16 s_AreaLen = 0;
static u8  s_A8 = 0;
static u8  s_FFFF = 0;
static u16 s_GameSP = 0;
static u8  s_DiskHook[5];          // H.TIMI as the disk ROM had it: for its calls only
static u8  s_DiskStack[192];       // the calls' stack, below HIMEM
// The error handlers MSX-DOS calls (through DISKVE, BREAKV): "abort" (c = 2).
static u8  s_Abort[3] = { 0x0E, 0x02, 0xC9 };   // ld c, 2; ret
static u16 s_AbortAt = 0;
u8  g_BdosC = 0;
u16 g_BdosDE = 0;
u16 g_BdosHL = 0;
u8  g_BdosA = 0;

// Page 0 to the RAM (s_RamSlot), and back; the interrupts off between.
static void Page0Ram() __naked
{
	__asm
		in	a, (#0xA8)
		ld	(_s_A8), a
		ld	a, (_s_RamSlot)
		bit	7, a
		jr	z, 00001$
		ld	a, (0xFFFF)             ; the secondary slots of page 3's slot (RAM's too)
		cpl
		ld	(_s_FFFF), a
		and	a, #0xFC
		ld	b, a
		ld	a, (_s_RamSlot)
		rrca
		rrca
		and	a, #0x03
		or	a, b
		ld	(0xFFFF), a
	00001$:
		ld	a, (_s_RamSlot)
		and	a, #0x03
		ld	b, a
		ld	a, (_s_A8)
		and	a, #0xFC
		or	a, b
		out	(#0xA8), a
		ret
	__endasm;
}

static void Page0Bios() __naked
{
	__asm
		ld	a, (_s_A8)
		out	(#0xA8), a
		ld	a, (_s_RamSlot)
		bit	7, a
		ret	z
		ld	a, (_s_FFFF)
		ld	(0xFFFF), a
		ret
	__endasm;
}

// main's first call: set the disk ROM's area aside, if there is one, and the
// stack to the top of RAM (main has no frame yet: it is to have none).
void Data_DiskAside() __naked
{
	__asm
		pop	de                      ; main's return
		di
		ld	hl, (HIMEM_ADDR)
		ld	(_s_Himem), hl
		ld	a, h                    ; HIMEM below F380h: a disk ROM took the rest
		cp	a, #0xF3
		jr	c, 00010$
		jr	nz, 00090$
		ld	a, l
		cp	a, #0x80
		jr	nc, 00090$
	00010$:
		ld	a, (BDOS_ADDR)          ; and its MSX-DOS hook
		cp	a, #0xC9
		jr	z, 00090$
		ld	hl, #0xF380
		ld	bc, (_s_Himem)
		or	a
		sbc	hl, bc
		ld	(_s_AreaLen), hl
		ld	bc, #AREA_MAX + 1
		or	a
		sbc	hl, bc
		jr	nc, 00090$              ; more than page 0 holds two of: no disk
		ld	a, (RAMAD0_ADDR)
		ld	(_s_RamSlot), a
		in	a, (#0xA8)              ; page 0's RAM must share page 3's primary slot
		rlca
		rlca
		and	a, #0x03
		ld	b, a
		ld	a, (_s_RamSlot)
		and	a, #0x03
		cp	a, b
		jr	nz, 00090$
		ld	hl, #0xFD9F             ; H.TIMI as the disk ROM left it
		ld	bc, #5
		push	de
		ld	de, #_s_DiskHook
		ldir
		call	_Page0Ram
		ld	hl, (_s_Himem)
		ld	de, #AREA_DISK
		ld	bc, (_s_AreaLen)
		ldir
		call	_Page0Bios
		ld	a, #0xC9                ; its H.TIMI quiet from now on: its area is the game's
		ld	(0xFD9F), a
		pop	de
		ld	a, #1
		ld	(_g_DiskReady), a
	00090$:
		ld	sp, #RAM_STACK
		ei
		push	de
		ret
	__endasm;
}

// An MSX-DOS call (BDOS, F37Dh): c g_BdosC, de g_BdosDE, hl g_BdosHL; back,
// a in g_BdosA, hl in g_BdosHL. Its own stack, the disk ROM's area and hook
// in place for it, the game's after; disk errors answered "abort".
void Data_DiskCall() __naked
{
	__asm
		di
		ld	(_s_GameSP), sp
		ld	sp, #_s_DiskStack + 192
		push	ix
		push	iy
		call	_Page0Ram
		ld	hl, (_s_Himem)          ; the game's area out
		ld	de, #AREA_GAME
		ld	bc, (_s_AreaLen)
		ldir
		ld	hl, #AREA_DISK          ; the disk ROM's in
		ld	de, (_s_Himem)
		ld	bc, (_s_AreaLen)
		ldir
		call	_Page0Bios
		ld	hl, #_s_DiskHook        ; its H.TIMI
		ld	de, #0xFD9F
		ld	bc, #5
		ldir
		ld	hl, #_s_Abort
		ld	(_s_AbortAt), hl
		ld	hl, #_s_AbortAt
		ld	(DISKVE_ADDR), hl
		ld	(BREAKV_ADDR), hl
		ei
		ld	a, (_g_BdosC)
		ld	c, a
		ld	de, (_g_BdosDE)
		ld	hl, (_g_BdosHL)
		call	BDOS_ADDR
		ld	(_g_BdosA), a
		ld	(_g_BdosHL), hl
		di
		ld	hl, #0xFD9F             ; its H.TIMI, kept; ours back
		ld	de, #_s_DiskHook
		ld	bc, #5
		ldir
		ld	a, #0xC3
		ld	(0xFD9F), a
		ld	hl, #_Data_Flip
		ld	(0xFDA0), hl
		ld	a, #0xC9
		ld	(0xFDA2), a
		ld	(0xFDA3), a
		call	_Page0Ram
		ld	hl, (_s_Himem)          ; the disk ROM's area kept, as the call left it
		ld	de, #AREA_DISK
		ld	bc, (_s_AreaLen)
		ldir
		ld	hl, #AREA_GAME          ; the game's back
		ld	de, (_s_Himem)
		ld	bc, (_s_AreaLen)
		ldir
		call	_Page0Bios
		pop	iy
		pop	ix
		ld	sp, (_s_GameSP)
		ei
		ret
	__endasm;
}

//-----------------------------------------------------------------------------
// The scenes (M12; scenes.c): scenes.bin's bytes, the figures into the
// atlas's video RAM (Data_LoadScenery puts the atlas back after), a VDP
// register, one pixel row of g_Text (the credits roll, a line at a time).

#include "scenes_data.h"

void Data_ScenesBytes(u16 at, u8* out, u8 n)
{
	SET_BANK_SEGMENT(ROM_DATA_BANK, SCENES_BIN_SEG);
	const u8* src = (const u8*)(ROM_DATA_ADDR + SCENES_BIN_REL) + at;
	while (n--)
		*out++ = *src++;
	SET_BANK_SEGMENT(ROM_DATA_BANK, g_CodeSeg);
}

void Data_LoadSceneImages()
{
	SET_BANK_SEGMENT(ROM_DATA_BANK, SCENES_IMG_BIN_SEG);
	const u8* src = (const u8*)(ROM_DATA_ADDR + SCENES_IMG_BIN_REL);
	for (u8 i = 0; i < SCENE_IMAGES; i++)
	{
		u8 x = src[0];
		u16 y = SCENE_BACKDROP_Y + (src[1] | (u16)src[2] << 8);
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

void Data_VdpReg(u8 reg, u8 value)
{
	__asm__("di");
	s_VdpCtrl = value;
	s_VdpCtrl = 0x80 | reg;
	__asm__("ei");
}

// Pixel row k of g_Text's glyphs (big: 8 a glyph, small: 6), ink on black,
// from x (even) on line y of a page; past the glyphs' rows, black.
void Data_TextLine(u8 big, u8 page, u8 x, u8 y, u8 k, u8 ink)
{
	u8 line[128];
	u8 n = 0;
	u8 rows = big ? 8 : FONT_SMALL_H;
	u8 pair[4];
	pair[0] = (u8)((INK_GROUND << 4) | INK_GROUND);
	pair[1] = (u8)((INK_GROUND << 4) | ink);
	pair[2] = (u8)((ink << 4) | INK_GROUND);
	pair[3] = (u8)((ink << 4) | ink);
	SET_BANK_SEGMENT(ROM_DATA_BANK, FONT_BIN_SEG);
	const u8* font = (const u8*)(ROM_DATA_ADDR + FONT_BIN_REL);
	for (u8 i = 0; i < g_TextN && n < 124; i++)
	{
		u8 bits = 0;
		if (k < rows)
		{
			u8 g = (u8)(g_Text[i] - (big ? FONT_BIG_FIRST : FONT_SMALL_FIRST));
			if (g >= (big ? FONT_BIG_GLYPHS : FONT_SMALL_GLYPHS))
				g = 0;
			bits = big ? font[((u16)g << 3) + k] : font[FONT_SMALL_AT + ((u16)g << 2) + ((u16)g << 1) + k];
		}
		for (u8 b = 0; b < (big ? 4 : 2); b++)
		{
			line[n++] = pair[bits >> 6];
			bits <<= 2;
		}
	}
	SET_BANK_SEGMENT(ROM_DATA_BANK, g_CodeSeg);
	Data_VramWrite(((u16)page << 15) + ((u16)y << 7) + (x >> 1), line, n);
}
