// Wulf Quest for MSX2: the hardware sprites (AGENTS.md §5, §10 M5).
//
// Sprite mode 2 (SCREEN 5): 32 sprites of 16x16, a colour a line in each
// sprite's colour table, the early clock (EC, colour bit 7) drawing a sprite
// 32 px left so it can stand partly off the left edge, and at most 8 sprites
// on a line: the V9938 draws the first 8 by number and drops the rest. Each
// frame the attribute table is written anew, in the order the slots were put,
// ended by Y 216; a sprite's colour table follows its place in the table, so
// a slot's colours are written again when it moves to another place, or its
// frame, its early clock or its clip at the panel changed.
//
// The playfield is the screen's top 176 lines, the panel below: a sprite's
// lines below the playfield get colour 0, transparent, as the Java game clips
// at the playfield. Its top, left and right edges are the screen's.
//
// The slots before SLOT_SMALL (Vale and his blade) keep their places in the
// table: one off the screen is held below it (Y 212) rather than left out, so
// the places after it, and their colours, stay put.
//
// Taking turns: the lines are counted in bands of 4, so a crowded line is
// sometimes seen a band early; the cost is only that creatures that overlap
// swap which is in front while others are crowded.

#include "sprites.h"
#include "constants.h"
#include "data.h"
#include "tables.h"   // and ram.h

#define NONE		0xFF
#define BANDS		((PLAYFIELD_H + 3) / 4)
#define PER_LINE	8
#define TERMINATOR	216   // Y that ends the attribute table in sprite mode 2
#define HIDDEN_Y	212   // below the screen's last line

u16 g_SpritesCrowded = 0;

// Each slot's first sprite; slot sprites are numbered in slot order. And
// the sprites it shows now: its first plus two a block.
static const u8 c_First[SPR_SLOTS + 1] = {
	0, 2, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 32, 40, 48, 56, 64
};
#if SPR_SLOTS != 16 || SPR_SPRITES != 64
#error "c_First is laid out for 16 slots: the blade, Vale, 9 small, 5 big"
#endif
static u8 s_SlotEnd[SPR_SLOTS] = { 2, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 32, 40, 48, 56, 64 };

static __at(RAM_COLOUR) u8 s_Colour[RAM_COLOUR_BYTES];   // as loaded, by sprite (at its place in RAM, ram.h)
static i8 s_DX[SPR_SPRITES];
static i8 s_DY[SPR_SPRITES];
static u8 s_Serial[SPR_SLOTS];          // goes up when a slot's colours change

// Two sets of sprite tables, the one shown and the one drawn (the back):
// colours, attributes and patterns at 7400h, 7600h, 7800h (page 0's lines
// past the screen), or at F400h, F600h, F800h (page 1's). A frame's
// sprites are drawn into the back set whenever, and shown by pointing the
// VDP at it in the blank: no sprite ever changes on screen mid-picture.
static u8 s_Set = 1;          // the back set
static u8 s_SetOff = SPR_PLACES;   // its place records' offset: s_Set * SPR_PLACES
static u16 s_ColourBase = 0xF400;
static u16 s_SatBase = 0xF600;
#define COLOURS_0	0x7400
#define SAT_0		0x7600
#define PATTERNS_0	0x7800
#define SET_1		SPR_SET_1   // set 1 is set 0 plus this (sprites.h)

// What each set's colour table holds at each place in its attribute table.
// Three bytes a place: its sprite, its look (EC bit, and the lines shown),
// its slot's serial (PlaceAll walks them with iy).
#define PLACE_BYTES	3
static u8 s_Place[2 * SPR_PLACES * PLACE_BYTES];

// This frame's slots, in the order put.
static u8 s_Puts = 0;
static u8 s_Pinned = 0;                         // of them, before SLOT_SMALL
static u8 s_PutSlot[SPR_SLOTS];
static i16 s_PutX[SPR_SLOTS];
static i16 s_PutY[SPR_SLOTS];
static u8 s_Turn = 0;

static u8 s_Sat[SPR_PLACES * 4 + 1];
static u8 s_Band[BANDS];

// Arrays without initial values (they would cost their size in ROM): cleared
// here, or set before they are read.
void Sprites_Init()
{
	for (u16 i = 0; i < sizeof(s_Colour); i++)
		s_Colour[i] = 0;
	for (u8 i = 0; i < SPR_SPRITES; i++)
	{
		s_DX[i] = 0;
		s_DY[i] = 0;
	}
	for (u8 i = 0; i < SPR_SLOTS; i++)
		s_Serial[i] = 0;
	for (u8 i = 0; i < 2 * SPR_PLACES; i++)
	{
		s_Place[i * PLACE_BYTES] = NONE;
		s_Place[i * PLACE_BYTES + 1] = 0;
		s_Place[i * PLACE_BYTES + 2] = 0;
	}
	s_Sat[0] = TERMINATOR;
	VDP_WriteVRAM(s_Sat, SAT_0, 0, 1);
	VDP_WriteVRAM(s_Sat, SAT_0 + SET_1, 0, 1);
	// Set 0 shown.
	VDP_RegWrite(11, 0);
	VDP_RegWrite(6, PATTERNS_0 >> 11);
	g_SpritesShown = 0;
	Sprites_Prepare();
}

void Sprites_Prepare()
{
	// A draw still waiting for its blank is in the back set, which this one
	// is about to write over: it is never shown (a frame dropped), rather
	// than shown half written.
	__asm__("di");
	g_FlipPending &= ~FLIP_SPRITES;
	__asm__("ei");
	s_Set = g_SpritesShown ? 0 : 1;   // the one not shown
	s_SetOff = s_Set ? SPR_PLACES : 0;
	s_ColourBase = COLOURS_0 + (s_Set ? SET_1 : 0);
	s_SatBase = SAT_0 + (s_Set ? SET_1 : 0);
}

u8 Sprites_Back()
{
	return s_Set;
}

u16 Sprites_PatternAddress(u8 slot)
{
	return PATTERNS_0 + (s_Set ? SET_1 : 0) + ((u16)c_First[slot] << 5);
}

u8* Sprites_Colours(u8 slot)
{
	return s_Colour + ((u16)c_First[slot] << 4);
}

void Sprites_Changed(u8 slot)
{
	s_Serial[slot]++;
}

void Sprites_SetOffset(u8 slot, u8 i, i8 dx, i8 dy)
{
	u8 s = c_First[slot] + i;
	s_DX[s] = dx;
	s_DY[s] = dy;
}

// Assembly (M13: 1 700 cycles in C, a frame loaded).
void Sprites_SetBlocks(u8 slot, u8 blocks) __naked
{
	slot;     // a
	blocks;   // l
	__asm
		push	ix
		ld	e, a
		ld	d, #0
		push	de                      ; the slot
		ld	ix, #_c_First
		add	ix, de
		ld	e, 0 (ix)               ; its first sprite
		ld	a, l
		and	a, #15
		ld	c, a                    ; c = rows of blocks
		ld	a, l
		rrca
		rrca
		rrca
		rrca
		and	a, #15
		ld	b, a                    ; b = columns
		ld	ix, #_s_DX
		add	ix, de
		ld	hl, #_s_DY
		add	hl, de                  ; ix on s_DX[s], hl on s_DY[s]
		ld	d, #0                   ; d = by << 4
		ld	a, c
		or	a
		jr	z, 00030$
	00010$:
		push	bc
		xor	a                       ; a = bx << 4
	00020$:
		ld	0 (ix), a
		ld	1 (ix), a
		ld	(hl), d
		inc	hl
		ld	(hl), d
		inc	hl
		inc	ix
		inc	ix
		inc	e
		inc	e
		add	a, #16
		djnz	00020$
		pop	bc
		ld	a, d
		add	a, #16
		ld	d, a
		dec	c
		jr	nz, 00010$
	00030$:
		pop	hl                      ; the slot
		ld	bc, #_s_SlotEnd
		add	hl, bc
		ld	(hl), e
		pop	ix
		ret
	__endasm;
}

void Sprites_Begin()
{
	s_Puts = 0;
	s_Pinned = 0;
}

void Sprites_Put(u8 slot, i16 x, i16 y)
{
	u8 n = s_Puts++;
	s_PutSlot[n] = slot;
	s_PutX[n] = x;
	s_PutY[n] = y;
	if (slot < SLOT_SMALL)
		s_Pinned++;
}

// This frame's order: indices into the puts. And PlaceAll's working values.
static u8 s_Order[SPR_SLOTS];
static u8 s_Placed = 0;     // sprites written to the attribute table
static u8 s_K = 0;          // slots left
static u8 s_Slot = 0;
static u8 s_SlotSerial = 0;
static u8 s_End = 0;
static i16 s_BX = 0;
static i16 s_BY = 0;
static i16 s_TX = 0;
static i16 s_TY = 0;
static u16 s_ColAt = 0;     // the colour table's address for the next place
static u8* s_OrderAt = 0;
static u8 s_Full = 0;       // PlaceAll ran out of places
static u8 s_PrevA, s_PrevE, s_PrevC, s_PrevOff;   // a block's base's place: Y, X, look, off the screen

#if PLAYFIELD_H + 15 > 255
#error "PlaceAll keeps y + 15 in a byte"
#endif

// The attribute table, in s_Order's order, into s_Sat, ended by TERMINATOR;
// and the colours of every place whose sprite, frame, early clock or clip
// changed, straight to video RAM. Each sprite: its position is its slot's
// put plus its offset; it is left out unless some of it is on the screen
// above the panel (-15 <= x <= 255, -15 <= y < PLAYFIELD_H), or held below
// the screen if its slot keeps its place; its look is the
// lines above the panel (16 when all), and the early clock when x < 0.
// Assembly: in C (SDCC keeps locals on the stack) this was 10 000 cycles a
// sprite (AGENTS.md §10, M5); M13 took its bookkeeping down by half (the
// places' records walked, the colour table's address carried on).
static void PlaceAll() __naked
{
	__asm
		push	ix
		push	iy
		xor	a
		ld	(_s_Placed), a
		ld	(_s_Full), a
		ld	ix, #_s_Sat
		ld	a, (_s_SetOff)          ; this set's first place record
		ld	l, a
		ld	h, #0
		ld	e, l
		ld	d, h
		add	hl, hl
		add	hl, de
		ld	de, #_s_Place
		add	hl, de
		push	hl
		pop	iy
		ld	hl, (_s_ColourBase)
		ld	(_s_ColAt), hl
		ld	hl, #_s_Order
		ld	(_s_OrderAt), hl
		ld	a, (_s_Puts)
		ld	(_s_K), a
	00010$:                         ; the next slot put
		ld	a, (_s_K)
		or	a
		jp	z, 00090$
		dec	a
		ld	(_s_K), a
		ld	hl, (_s_OrderAt)
		ld	e, (hl)
		inc	hl
		ld	(_s_OrderAt), hl
		ld	d, #0                   ; de = n
		ld	hl, #_s_PutSlot
		add	hl, de
		ld	c, (hl)
		ld	hl, #_s_PutX
		add	hl, de
		add	hl, de
		ld	a, (hl)
		inc	hl
		ld	h, (hl)
		ld	l, a
		ld	(_s_BX), hl
		ld	hl, #_s_PutY
		add	hl, de
		add	hl, de
		ld	a, (hl)
		inc	hl
		ld	h, (hl)
		ld	l, a
		ld	(_s_BY), hl
		ld	a, c
		ld	(_s_Slot), a
		ld	b, #0
		ld	hl, #_s_Serial
		add	hl, bc
		ld	a, (hl)
		ld	(_s_SlotSerial), a
		ld	hl, #_s_SlotEnd
		add	hl, bc
		ld	a, (hl)
		ld	(_s_End), a
		ld	hl, #_c_First
		add	hl, bc
		ld	b, (hl)                 ; b = its first sprite
	00020$:                         ; sprite b
		ld	a, (_s_End)
		cp	b
		jr	z, 00010$
		ld	a, (_s_Placed)          ; the table full: the rest wait their turn
		cp	a, #SPR_PLACES
		jp	nc, 00095$
		; a block's second sprite (the overlay, at its base's offset; not the
		; blade's, whose two have their own): the base's place again (M13)
		bit	0, b
		jr	z, 00021$
		ld	a, (_s_Slot)
		or	a
		jr	z, 00021$
		ld	a, (_s_PrevOff)
		or	a
		jp	nz, 00060$
		ld	a, (_s_PrevE)
		ld	e, a
		ld	a, (_s_PrevC)
		ld	c, a
		ld	a, (_s_PrevA)
		jp	00037$
	00021$:
		; on the screen: (x + 15) < 271 and (y + 15) < PLAYFIELD_H + 15, unsigned
		ld	e, b
		ld	d, #0
		ld	hl, #_s_DX
		add	hl, de
		ld	a, (hl)
		ld	e, a
		rla
		sbc	a, a
		ld	d, a
		ld	hl, (_s_BX)
		add	hl, de
		ld	(_s_TX), hl
		ld	de, #15
		add	hl, de
		ld	de, #-271
		add	hl, de
		jp	c, 00060$
		ld	e, b
		ld	d, #0
		ld	hl, #_s_DY
		add	hl, de
		ld	a, (hl)
		ld	e, a
		rla
		sbc	a, a
		ld	d, a
		ld	hl, (_s_BY)
		add	hl, de
		ld	(_s_TY), hl
		ld	de, #15
		add	hl, de
		ld	de, #-(PLAYFIELD_H + 15)
		add	hl, de
		jp	c, 00060$
		; c = look: the lines above the panel, and EC
		ld	c, #16
		ld	a, (_s_TY)
		add	a, #15                  ; y + 15, 0..PLAYFIELD_H + 14
		cp	a, #PLAYFIELD_H
		jr	c, 00030$
		cpl
		add	a, #(PLAYFIELD_H + 15 + 1)
		ld	c, a                    ; PLAYFIELD_H + 15 - (y + 15)
	00030$:
		ld	hl, (_s_TX)
		ld	e, l
		bit	7, h
		jr	z, 00035$
		set	7, c
		ld	a, l
		add	a, #32
		ld	e, a
	00035$:
		ld	a, (_s_TY)
	00036$:
		ld	(_s_PrevA), a           ; kept for the block's overlay
		ld	a, e
		ld	(_s_PrevE), a
		ld	a, c
		ld	(_s_PrevC), a
		xor	a
		ld	(_s_PrevOff), a
		ld	a, (_s_PrevA)
	00037$:
		; the attribute: Y (a line above), X, pattern
		dec	a
		ld	0 (ix), a
		ld	1 (ix), e
		ld	a, b
		add	a, a
		add	a, a
		ld	2 (ix), a
		ld	3 (ix), #0
		ld	de, #4
		add	ix, de
		; the colours at this place: the same sprite, look and serial?
		ld	a, 0 (iy)
		cp	b
		jr	nz, 00040$
		ld	a, 1 (iy)
		cp	c
		jr	nz, 00040$
		ld	a, (_s_SlotSerial)
		cp	a, 2 (iy)
		jr	z, 00070$
	00040$:                         ; no: written
		ld	0 (iy), b
		ld	1 (iy), c
		ld	a, (_s_SlotSerial)
		ld	2 (iy), a
		push	bc
		ld	hl, (_s_ColAt)
		ld	a, h
		rlca
		rlca
		and	a, #0x03                ; the address is under 10000h
		di
		out	(#0x99), a              ; R#14: address bits 16-14
		ld	a, #0x8E
		out	(#0x99), a
		ld	a, l
		out	(#0x99), a              ; bits 7-0
		ld	a, h
		and	a, #0x3F
		or	a, #0x40
		out	(#0x99), a              ; bits 13-8, and "write"
		; the bytes: the line's colour with EC, or 0 below the panel's top
		ld	l, b
		ld	h, #0
		add	hl, hl
		add	hl, hl
		add	hl, hl
		add	hl, hl
		ld	de, #_s_Colour
		add	hl, de
		ld	a, c
		ld	bc, #0x1098             ; 16 bytes, to the VDP's data port
		cp	a, #16                  ; all lines, no EC: as they are
		jr	nz, 00045$
	00042$:
		outi
		jr	nz, 00042$
		jr	00058$
	00045$:
		ld	e, a
		and	a, #0x80
		ld	d, a                    ; d = EC
		ld	a, e
		and	a, #0x1F
		ld	e, a                    ; e = lines shown
	00050$:
		ld	a, e
		or	a
		jr	z, 00055$
		dec	e
		ld	a, (hl)
		or	a, d
	00055$:
		out	(c), a
		inc	hl
		djnz	00050$
	00058$:
		ei
		pop	bc
	00070$:
		ld	de, #PLACE_BYTES
		add	iy, de
		ld	hl, (_s_ColAt)
		ld	de, #16
		add	hl, de
		ld	(_s_ColAt), hl
		ld	hl, #_s_Placed
		inc	(hl)
	00080$:
		inc	b
		jp	00020$
	00060$:                         ; not on the screen: left out, or held below it
		ld	a, #1
		ld	(_s_PrevOff), a
		ld	a, (_s_Slot)
		cp	a, #SLOT_SMALL
		jr	nc, 00080$
		ld	c, #0                   ; no line shown
		ld	e, #0
		ld	a, #(HIDDEN_Y + 1)
		jp	00036$
	00095$:
		ld	a, #1
		ld	(_s_Full), a
	00090$:
		ld	0 (ix), #TERMINATOR
		pop	iy
		pop	ix
		ret
	__endasm;
}

// The attribute table's sprites a band of 4 lines, s_Band; 1 if a band has
// more than PER_LINE. A sprite's top is its Y + 1, from -15 (Y 240 and on:
// 256 less) to PLAYFIELD_H - 1; held below the screen (Y + 1 past the
// playfield), it counts in none. Assembly: in C, 600 cycles a sprite.
#if PLAYFIELD_H + 15 > 255 || PLAYFIELD_H > 240
#error "CountBands keeps a sprite's top + 15 in a byte, and the tops from 240 on as above the screen"
#endif
static u8 CountBands() __naked
{
	__asm
		ld	hl, #_s_Band            ; M13: cleared by ldir, and a band past
		ld	(hl), #0                ; PER_LINE ends it at once (no scan after)
		ld	de, #_s_Band + 1
		ld	bc, #(BANDS - 1)
		ldir
		ld	a, (_s_Placed)
		or	a
		ret	z
		push	ix
		ld	b, a
		ld	ix, #_s_Sat
	00010$:
		ld	a, 0 (ix)
		inc	a                       ; the top, a byte
		ld	de, #4
		add	ix, de
		cp	a, #240
		jr	nc, 00020$
		cp	a, #PLAYFIELD_H
		jr	nc, 00050$              ; held below the screen
		ld	c, a
		srl	c
		srl	c                       ; c = the first band
		add	a, #15
		cp	a, #PLAYFIELD_H
		jr	c, 00015$
		ld	a, #(PLAYFIELD_H - 1)
	00015$:
		srl	a
		srl	a                       ; a = the last band
		jr	00030$
	00020$:
		sub	a, #241                 ; above the screen: top + 15, 0..14
		srl	a
		srl	a
		ld	c, #0
	00030$:
		sub	a, c
		inc	a
		ld	e, a                    ; the bands it covers
		ld	a, c
		ld	hl, #_s_Band
		add	a, l
		ld	l, a
		adc	a, h
		sub	a, l
		ld	h, a
	00040$:
		inc	(hl)
		ld	a, (hl)
		cp	a, #(PER_LINE + 1)
		jr	nc, 00070$              ; over PER_LINE: crowded
		inc	hl
		dec	e
		jr	nz, 00040$
	00050$:
		djnz	00010$
		xor	a
		pop	ix
		ret
	00070$:
		ld	a, #1
		pop	ix
		ret
	__endasm;
}

void Sprites_End()
{
	// The pinned ones as put; then the others from this frame's turn on.
	u8 others = s_Puts - s_Pinned;
	u8 n = s_Turn;
	for (u8 k = 0; k < s_Puts; k++)
	{
		if (k < s_Pinned)
			s_Order[k] = k;
		else
		{
			if (n >= others)
				n = 0;
			s_Order[k] = s_Pinned + n++;
		}
	}
	PlaceAll();
	VDP_WriteVRAM(s_Sat, s_SatBase, 0, (s_Placed << 2) + 1);
	// Ready: the next blank shows it (data.c's H.TIMI hook).
	g_FlipR11 = s_Set;   // R#11: the attribute table's address bit 15
	g_FlipR6 = (u8)((PATTERNS_0 + (s_Set ? SET_1 : 0)) >> 11);   // R#6: the patterns' bits 16-11
	__asm__("di");
	g_FlipDraw++;
	g_FlipPending |= FLIP_SPRITES;
	__asm__("ei");

	// More than 8 on a line? Only possible with more than 8 shown. Counted in bands of 4 lines.
	bool crowded = s_Full;
	if (s_Placed <= PER_LINE && !crowded)
		return;
	if (CountBands())
		crowded = TRUE;
	if (crowded)
	{
		g_SpritesCrowded++;
		if (others && ++s_Turn >= others)
			s_Turn = 0;
	}
}
