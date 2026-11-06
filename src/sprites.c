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

#define NONE		0xFF
#define BANDS		((PLAYFIELD_H + 3) / 4)
#define PER_LINE	8
#define TERMINATOR	216   // Y that ends the attribute table in sprite mode 2
#define HIDDEN_Y	212   // below the screen's last line

u16 g_SpritesCrowded = 0;

// Each slot's first sprite and its count; slot sprites are numbered 0..31 in slot order.
static const u8 c_First[SPR_SLOTS + 1] = {
	0, 2, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32
};
#if SPR_SLOTS != 15
#error "c_First is laid out for 15 slots"
#endif

static u8 s_Colour[SPR_SPRITES * 16] = { 0 };   // as loaded, by sprite
static i8 s_DX[SPR_SPRITES] = { 0 };
static i8 s_DY[SPR_SPRITES] = { 0 };
static u8 s_Serial[SPR_SLOTS] = { 0 };          // goes up when a slot's colours change

// What the colour table holds at each place in the attribute table.
static u8 s_PlaceSprite[SPR_SPRITES] = { 0 };
static u8 s_PlaceSerial[SPR_SPRITES] = { 0 };
static u8 s_PlaceLook[SPR_SPRITES] = { 0 };     // EC bit, and the first clipped line

// This frame's slots, in the order put.
static u8 s_Puts = 0;
static u8 s_Pinned = 0;                         // of them, before SLOT_SMALL
static u8 s_PutSlot[SPR_SLOTS] = { 0 };
static i16 s_PutX[SPR_SLOTS] = { 0 };
static i16 s_PutY[SPR_SLOTS] = { 0 };
static u8 s_Turn = 0;

static u8 s_Sat[SPR_SPRITES * 4 + 1] = { 0 };
static u8 s_Band[BANDS] = { 0 };

void Sprites_Init()
{
	for (u8 i = 0; i < SPR_SPRITES; i++)
		s_PlaceSprite[i] = NONE;
	s_Sat[0] = TERMINATOR;
	VDP_WriteVRAM(s_Sat, g_SpriteAttributeLow, g_SpriteAttributeHigh, 1);
}

u16 Sprites_PatternAddress(u8 slot)
{
	return g_SpritePatternLow + ((u16)c_First[slot] << 5);
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
static u8 s_Order[SPR_SLOTS] = { 0 };
static u8 s_Placed = 0;     // sprites written to the attribute table
static u8 s_K = 0;
static u8 s_Slot = 0;
static u8 s_End = 0;
static i16 s_BX = 0;
static i16 s_BY = 0;
static i16 s_TX = 0;
static i16 s_TY = 0;
static u8 s_LookNow = 0;

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
// sprite (AGENTS.md §10, M5).
static void PlaceAll() __naked
{
	__asm
		push	ix
		xor	a
		ld	(_s_Placed), a
		ld	(_s_K), a
		ld	ix, #_s_Sat
	00010$:                         ; the next slot put
		ld	a, (_s_K)
		ld	hl, #_s_Puts
		cp	(hl)
		jp	nc, 00090$
		inc	a
		ld	(_s_K), a
		dec	a
		ld	e, a
		ld	d, #0
		ld	hl, #_s_Order
		add	hl, de
		ld	e, (hl)                 ; e = n
		ld	hl, #_s_PutSlot
		add	hl, de
		ld	a, (hl)
		ld	(_s_Slot), a
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
		ld	a, (_s_Slot)
		ld	e, a
		ld	hl, #_c_First
		add	hl, de
		ld	b, (hl)                 ; b = its first sprite
		inc	hl
		ld	a, (hl)
		ld	(_s_End), a
	00020$:                         ; sprite b
		ld	a, (_s_End)
		cp	b
		jr	z, 00010$
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
		; on the screen: (x + 15) < 271 and (y + 15) < PLAYFIELD_H + 15, unsigned
		ld	hl, (_s_TX)
		ld	de, #15
		add	hl, de
		ld	de, #271
		or	a
		sbc	hl, de
		jr	nc, 00060$
		ld	hl, (_s_TY)
		ld	de, #15
		add	hl, de
		ld	de, #(PLAYFIELD_H + 15)
		or	a
		sbc	hl, de
		jr	nc, 00060$
		; c = look: the lines above the panel, and EC
		ld	c, #16
		ld	a, (_s_TY)
		add	a, #15                  ; y + 15, 0..PLAYFIELD_H + 14
		cp	a, #PLAYFIELD_H
		jr	c, 00030$
		ld	d, a
		ld	a, #(PLAYFIELD_H + 15)
		sub	a, d
		ld	c, a
	00030$:
		ld	a, (_s_TX)
		ld	e, a
		ld	a, (_s_TX + 1)
		bit	7, a
		jr	z, 00035$
		set	7, c
		ld	a, e
		add	a, #32
		ld	e, a
		jr	00035$
	00060$:                         ; not on the screen: left out, or held below it
		ld	a, (_s_Slot)
		cp	a, #SLOT_SMALL
		jp	nc, 00080$
		ld	c, #0                   ; no line shown
		ld	e, #0
		ld	a, #(HIDDEN_Y + 1)
		jr	00036$
	00035$:
		ld	a, (_s_TY)
	00036$:
		; the attribute: Y (a line above), X, pattern
		dec	a
		ld	0 (ix), a
		ld	1 (ix), e
		ld	a, b
		add	a, a
		add	a, a
		ld	2 (ix), a
		ld	3 (ix), #0
		inc	ix
		inc	ix
		inc	ix
		inc	ix
		; the colours at this place: the same sprite, frame and look?
		ld	a, (_s_Placed)
		ld	e, a
		ld	d, #0
		ld	hl, #_s_PlaceSprite
		add	hl, de
		ld	a, (hl)
		cp	b
		jr	nz, 00040$
		ld	hl, #_s_PlaceLook
		add	hl, de
		ld	a, (hl)
		cp	c
		jr	nz, 00040$
		push	de
		ld	a, (_s_Slot)
		ld	e, a
		ld	hl, #_s_Serial
		add	hl, de
		ld	a, (hl)
		pop	de
		ld	hl, #_s_PlaceSerial
		add	hl, de
		cp	(hl)
		jr	z, 00070$
	00040$:                         ; no: write them, e = the place
		ld	hl, #_s_PlaceSprite
		add	hl, de
		ld	(hl), b
		ld	hl, #_s_PlaceLook
		add	hl, de
		ld	(hl), c
		push	de
		ld	a, (_s_Slot)
		ld	e, a
		ld	hl, #_s_Serial
		add	hl, de
		ld	a, (hl)
		pop	de
		ld	hl, #_s_PlaceSerial
		add	hl, de
		ld	(hl), a
		push	bc
		ld	a, c
		ld	(_s_LookNow), a
		; video RAM address: the colour table + place * 16
		ex	de, hl
		add	hl, hl
		add	hl, hl
		add	hl, hl
		add	hl, hl
		ld	de, (_g_SpriteColorLow)
		add	hl, de
		ld	a, (_g_SpriteColorHigh)
		add	a, a
		add	a, a
		ld	e, a
		ld	a, h
		rlca
		rlca
		and	a, #0x03
		or	a, e
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
		ld	b, #16
		ld	c, #0x98
		cp	a, #16                  ; all lines, no EC: as they are
		jr	nz, 00045$
	00042$:
		outi
		jr	nz, 00042$
		jr	00058$
	00045$:
		and	a, #0x80
		ld	e, a                    ; e = EC
		ld	a, (_s_LookNow)
		and	a, #0x1F
		ld	d, a                    ; d = lines shown
	00050$:
		ld	a, d
		or	a
		jr	z, 00055$
		dec	d
		ld	a, (hl)
		or	a, e
	00055$:
		out	(c), a
		inc	hl
		djnz	00050$
	00058$:
		ei
		pop	bc
	00070$:
		ld	hl, #_s_Placed
		inc	(hl)
	00080$:
		inc	b
		jp	00020$
	00090$:
		ld	0 (ix), #TERMINATOR
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
	VDP_WriteVRAM(s_Sat, g_SpriteAttributeLow, g_SpriteAttributeHigh, (s_Placed << 2) + 1);

	// More than 8 on a line? Only possible with more than 8 shown. Counted in bands of 4 lines.
	if (s_Placed <= PER_LINE)
		return;
	for (u8 b = 0; b < BANDS; b++)
		s_Band[b] = 0;
	const u8* a = s_Sat - 4;
	for (u8 i = 0; i < s_Placed; i++)
	{
		a += 4;
		i16 top = (i16)a[0] + 1;
		if (top >= 256 - 16)
			top -= 256;
		if (top >= PLAYFIELD_H)
			continue;   // held below the screen
		u8 b0 = top < 0 ? 0 : (u8)top >> 2;
		u8 b1 = (u8)((top + 15 >= PLAYFIELD_H ? PLAYFIELD_H - 1 : top + 15) >> 2);
		for (u8 b = b0; b <= b1; b++)
			s_Band[b]++;
	}
	bool crowded = FALSE;
	for (u8 b = 0; b < BANDS; b++)
		if (s_Band[b] > PER_LINE)
			crowded = TRUE;
	if (crowded)
	{
		g_SpritesCrowded++;
		if (others && ++s_Turn >= others)
			s_Turn = 0;
	}
}
