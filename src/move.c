// Wulf Quest for MSX2: one axis of a creature's or a beast's move (AGENTS.md
// §10, M6): the Java game's Collision.resolve, in assembly.
//
// Measured first (rule 6): in C a beast's step cost 7 000 to 14 000 cycles,
// SDCC's 32-bit arithmetic and its locals on the stack, and the jungle has
// eight beasts and up to six creatures a tick. Here a beast's step whose box
// covers the cells it covered before costs a few hundred, and one into a new
// strip of cells one test of that strip (Beast_Step).
//
// Collision.resolve: the target, pos + delta, if its box is free; otherwise
// the nearest free whole pixel from the target back towards pos (keeping the
// target's side: c from the target's pixel down while above pos's pixel, or
// up from one past it while below, or level with a fraction); none free, pos.

#include "move.h"
#include "vale.h"
#include "data.h"
#include "constants.h"

i32 g_MovePos = 0;
i16 g_MoveDelta = 0;
i16 g_MoveOther = 0;
i8  g_MoveOff = 0;
u8  g_MoveSize = 0;
i8  g_MoveOffO = 0;
u8  g_MoveSizeO = 0;
u8  g_MoveAlongX = 0;
u8  g_MoveWorld = 0;
u8  g_MoveScenery = 0;

static i32 s_T = 0;       // the target
static i16 s_Base = 0;    // MOVE_ROOM: + a candidate = the box's near edge from the room's
static u16 s_Lim = 0;     //            the most that edge may be

#if CELL != 8
#error "Move_Axis divides by CELL with shifts"
#endif

//-----------------------------------------------------------------------------
// The cells around Vale's room (vale.c's s_Cells), for him and for the
// creatures in it. Here, below 8000h, as banked code calls it.

// Cells_Blocked's arguments: a box in the window's pixels, wholly inside it.
u16 g_CbLeft = 0;
u8  g_CbTop = 0;
u8  g_CbW = 0;
u8  g_CbH = 0;

#if CELLS_W != 36
#error "Cells_Blocked multiplies by CELLS_W as 32 + 4"
#endif

// Whether any of the window's cells under the box is solid. Assembly: the
// C was about 2 000 cycles a call, and a tick against a wall makes 8 (M6).
u8 Cells_Blocked() __naked
{
	__asm
		; the first row: s_Cells + (top >> 3) * 36
		ld	a, (_g_CbTop)
		ld	c, a                    ; c = top
		rrca
		rrca
		rrca
		and	a, #0x1F
		ld	l, a
		ld	h, #0
		add	hl, hl
		add	hl, hl
		ld	e, l
		ld	d, h                    ; de = row * 4
		add	hl, hl
		add	hl, hl
		add	hl, hl                  ; hl = row * 32
		add	hl, de
		ld	de, #_s_Cells
		add	hl, de
		push	hl
		; columns: cx0 = left >> 3, n = ((left + w - 1) >> 3) - cx0 + 1
		ld	hl, (_g_CbLeft)
		ld	a, (_g_CbW)
		dec	a
		ld	e, a
		ld	d, #0
		push	hl
		add	hl, de
		srl	h
		rr	l
		srl	h
		rr	l
		srl	h
		rr	l
		ld	b, l                    ; b = cx1
		pop	hl
		srl	h
		rr	l
		srl	h
		rr	l
		srl	h
		rr	l                       ; l = cx0
		ld	a, b
		sub	a, l
		inc	a
		ld	b, a                    ; b = columns
		ld	e, l                    ; de = cx0
		; rows: n = ((top + h - 1) >> 3) - (top >> 3) + 1
		ld	a, (_g_CbH)
		dec	a
		add	a, c
		rrca
		rrca
		rrca
		and	a, #0x1F
		ld	d, a
		ld	a, c
		rrca
		rrca
		rrca
		and	a, #0x1F
		ld	c, a
		ld	a, d
		sub	a, c
		inc	a
		ld	c, a                    ; c = rows
		ld	d, #0
		pop	hl
		add	hl, de                  ; hl = the first cell under the box
	00010$:
		push	hl
		push	bc
	00020$:
		ld	a, (hl)
		or	a
		jr	nz, 00090$
		inc	hl
		djnz	00020$
		pop	bc
		pop	hl
		ld	de, #CELLS_W
		add	hl, de
		dec	c
		jr	nz, 00010$
		xor	a
		ret
	00090$:
		pop	bc
		pop	hl
		ld	a, #1
		ret
	__endasm;
}


void Move_Axis() __naked
{
	__asm
		push	ix
		; s_T = pos + delta: the delta's sign first, then the low word's carry into the high
		ld	a, (_g_MoveDelta + 1)
		rla
		sbc	a, a
		ld	c, a                    ; the delta's sign, a byte
		ld	hl, (_g_MovePos)
		ld	de, (_g_MoveDelta)
		add	hl, de
		ld	(_s_T), hl
		ld	e, c
		ld	d, c
		ld	hl, (_g_MovePos + 2)
		adc	hl, de
		ld	(_s_T + 2), hl
		call	00500$                  ; the other axis: carry if every candidate is blocked
		jp	c, 00090$
		ld	hl, (_s_T + 1)          ; t, the target's pixel
		call	00600$
		or	a
		jp	nz, 00020$
		ld	hl, (_s_T)              ; free: the target
		ld	(_g_MovePos), hl
		ld	hl, (_s_T + 2)
		ld	(_g_MovePos + 2), hl
		jp	00090$
	00020$:
		ld	a, (_g_MoveDelta + 1)
		bit	7, a
		jp	nz, 00040$
		; forwards: c from t down, while c > p
		ld	hl, (_s_T + 1)
	00030$:
		ld	de, (_g_MovePos + 1)
		push	hl
		or	a
		sbc	hl, de
		pop	hl
		jp	z, 00090$
		jp	m, 00090$
		push	hl
		call	00600$
		pop	hl
		or	a
		jp	z, 00080$
		dec	hl
		jr	00030$
	00040$:
		; backwards: c from t + 1 up, while c < p, or c == p with a fraction
		ld	hl, (_s_T + 1)
		inc	hl
	00045$:
		ld	de, (_g_MovePos + 1)
		push	hl
		or	a
		sbc	hl, de
		pop	hl
		jp	z, 00047$
		jp	p, 00090$
		jp	00048$
	00047$:
		ld	a, (_g_MovePos)
		or	a
		jp	z, 00090$
	00048$:
		push	hl
		call	00600$
		pop	hl
		or	a
		jp	z, 00080$
		inc	hl
		jp	00045$
	00080$:
		; pos = c, a whole pixel
		xor	a
		ld	(_g_MovePos), a
		ld	(_g_MovePos + 1), hl
		ld	a, h
		rla
		sbc	a, a
		ld	(_g_MovePos + 3), a
	00090$:
		pop	ix
		ret

	; ---- the other axis, once (carry: the box is off the room every way)
	00500$:
		ld	hl, (_g_MoveOther)
		ld	a, (_g_MoveOffO)
		ld	e, a
		rla
		sbc	a, a
		ld	d, a
		add	hl, de                  ; hl = the other near edge, world pixels
		ld	a, (_g_MoveWorld)
		or	a
		jr	z, 00520$
		ret                             ; MOVE_MAP: nothing to set (a != 0: no carry)
	00520$:
		; MOVE_ROOM: from the room's edge, and inside it; the window's place
		ld	a, (_g_MoveAlongX)
		or	a
		jr	z, 00530$
		ld	de, (_g_RoomY)          ; moving along x: the other is y
		or	a
		sbc	hl, de
		ld	a, (_g_MoveSizeO)
		ld	e, a
		ld	d, #0
		push	hl
		ld	hl, #PLAYFIELD_H
		or	a
		sbc	hl, de
		ex	de, hl                  ; de = the room's height - size
		pop	hl
		push	hl
		or	a
		sbc	hl, de                  ; unsigned: hl > de, or hl negative, is off the room
		pop	hl
		jr	z, 00525$
		jr	c, 00525$
		scf
		ret
	00525$:
		ld	a, l
		add	a, #(CELLS_MARGIN * CELL)
		ld	(_g_CbTop), a
		; along x: s_Base = off - roomX, s_Lim = width - size
		ld	a, (_g_MoveOff)
		ld	e, a
		rla
		sbc	a, a
		ld	d, a
		ld	hl, (_g_RoomX)
		ex	de, hl
		or	a
		sbc	hl, de
		ld	(_s_Base), hl
		ld	a, (_g_MoveSize)
		ld	e, a
		ld	d, #0
		ld	hl, #PLAYFIELD_W
		jr	00540$
	00530$:
		ld	de, (_g_RoomX)          ; moving along y: the other is x
		or	a
		sbc	hl, de
		ld	a, (_g_MoveSizeO)
		ld	e, a
		ld	d, #0
		push	hl
		ld	hl, #PLAYFIELD_W
		or	a
		sbc	hl, de
		ex	de, hl
		pop	hl
		push	hl
		or	a
		sbc	hl, de
		pop	hl
		jr	z, 00535$
		jr	c, 00535$
		scf
		ret
	00535$:
		ld	de, #(CELLS_MARGIN * CELL)
		add	hl, de
		ld	(_g_CbLeft), hl
		ld	a, (_g_MoveOff)
		ld	e, a
		rla
		sbc	a, a
		ld	d, a
		ld	hl, (_g_RoomY)
		ex	de, hl
		or	a
		sbc	hl, de
		ld	(_s_Base), hl
		ld	a, (_g_MoveSize)
		ld	e, a
		ld	d, #0
		ld	hl, #PLAYFIELD_H
	00540$:
		or	a
		sbc	hl, de
		ld	(_s_Lim), hl
		ld	a, (_g_MoveAlongX)
		or	a
		ld	a, (_g_MoveSize)
		ld	b, a
		ld	a, (_g_MoveSizeO)
		jr	z, 00545$
		ld	(_g_CbH), a
		ld	a, b
		ld	(_g_CbW), a
		or	a
		ret
	00545$:
		ld	(_g_CbW), a
		ld	a, b
		ld	(_g_CbH), a
		or	a
		ret

	; ---- blocked at candidate hl (whole pixels along the axis)? a = 0 free, 1 blocked
	00600$:
		ld	a, (_g_MoveWorld)
		or	a
		jp	z, 00650$
		ld	a, (_g_MoveOff)
		ld	e, a
		rla
		sbc	a, a
		ld	d, a
		add	hl, de                  ; hl = the near edge, world pixels
		; MOVE_MAP: the box there (Data_BoxTest)
		ld	a, (_g_MoveAlongX)
		or	a
		jr	z, 00630$
		ld	(_g_BoxLeft), hl
		ld	hl, (_g_MoveOther)
		ld	a, (_g_MoveOffO)
		ld	e, a
		rla
		sbc	a, a
		ld	d, a
		add	hl, de
		ld	(_g_BoxTop), hl
		ld	a, (_g_MoveSize)
		ld	(_g_BoxW), a
		ld	a, (_g_MoveSizeO)
		ld	(_g_BoxH), a
		jr	00635$
	00630$:
		ld	(_g_BoxTop), hl
		ld	hl, (_g_MoveOther)
		ld	a, (_g_MoveOffO)
		ld	e, a
		rla
		sbc	a, a
		ld	d, a
		add	hl, de
		ld	(_g_BoxLeft), hl
		ld	a, (_g_MoveSize)
		ld	(_g_BoxH), a
		ld	a, (_g_MoveSizeO)
		ld	(_g_BoxW), a
	00635$:
		ld	a, #1
		ld	(_g_BoxWrap), a
		jp	_Data_BoxTest
	00650$:
		; MOVE_ROOM: the room's edges, then the cells around it
		ld	de, (_s_Base)
		add	hl, de                  ; hl = the near edge from the room's
		ld	de, (_s_Lim)
		push	hl
		or	a
		sbc	hl, de
		pop	hl
		jr	z, 00655$
		jr	nc, 00660$              ; past the far edge, or before the near one
	00655$:
		ld	a, (_g_MoveScenery)
		or	a
		ret	z                       ; a flier: only the edges
		ld	de, #(CELLS_MARGIN * CELL)
		add	hl, de
		ld	a, (_g_MoveAlongX)
		or	a
		jr	z, 00657$
		ld	(_g_CbLeft), hl
		jp	_Cells_Blocked
	00657$:
		ld	a, l
		ld	(_g_CbTop), a
		jp	_Cells_Blocked
	00660$:
		ld	a, #1
		ret

	; ---- hl = hl >> 3, signed (floorDiv by a cell)
	00700$:
		sra	h
		rr	l
		sra	h
		rr	l
		sra	h
		rr	l
		ret

	__endasm;
}

//-----------------------------------------------------------------------------
// A big beast's step (Simulation.stepBeast): x then y on the wrapping map,
// round its ends, and the room it is in now. On the Beast at g_BeastPtr
// (jungle.h; its offsets are fixed here). 1 if it moved.
//
// Its box free where it stands, a step of less than a cell (the converter
// checks the beasts' speeds) is Collision.resolve with one test at most.
// Whether the leading edge changed cells shows in its pixel's low byte: the
// step is under 256 px, so the low bytes' cell bits differ when the cells do.
// Forwards, the box's far edge stays in its cell or goes one on: the same
// cell, the target is free; the next, the target is free if that strip of
// cells is. If not, every candidate whose far edge is in the new cell is
// blocked (its box holds the strip) and every one whose far edge is still in
// the old cell is free (its box lies in the free one's cells), so the first
// free one, from the target down, is the last pixel before the strip, if that
// is past where it stands. Backwards the same with the near edge, from the
// target up: the first pixel after the strip, if that is short of where it
// stands, or level with it but for a fraction. A box not free where it stands
// (set down in scenery) takes Move_Axis's search, a test a candidate.

Beast* g_BeastPtr = 0;
static u8 s_Free = 0;     // its Free, during the step
static u8 s_Moved = 0;    // the axis being stepped moved
static u8 s_MovedX = 0;   // x moved
static i8 s_EdgeOff = 0;  // the edge that leads: forwards the far one, backwards the near
static i16 s_Strip = 0;   // the strip it steps into, its first pixel along the axis
static u8* s_Other = 0;   // the other axis's block
static u8* s_Cache = 0;   // its Blocked
static u8 s_Key[7];       // a strip: the axis, its first pixel along it, its cells across

#define MAP_W_HI	(GRID_W)                      // MAP_W_FP >> 16: a room is 256 px
#define MAP_H_HI	((GRID_H * PLAYFIELD_H) >> 8)  // MAP_H_FP >> 16
#if PLAYFIELD_W != 256 || ((GRID_H * PLAYFIELD_H) & 255) != 0
#error "Beast_Step takes rooms 256 px wide, and the map's height a whole number of 256 px"
#endif
// Each row of rooms' top, world pixels, and one past the last.
static const i16 c_RowTop[GRID_H + 1] = {
	0 * PLAYFIELD_H, 1 * PLAYFIELD_H, 2 * PLAYFIELD_H, 3 * PLAYFIELD_H,
	4 * PLAYFIELD_H, 5 * PLAYFIELD_H, 6 * PLAYFIELD_H, 7 * PLAYFIELD_H,
	8 * PLAYFIELD_H, 9 * PLAYFIELD_H, 10 * PLAYFIELD_H, 11 * PLAYFIELD_H,
	12 * PLAYFIELD_H, 13 * PLAYFIELD_H, 14 * PLAYFIELD_H, 15 * PLAYFIELD_H,
	16 * PLAYFIELD_H
};
#if GRID_H != 16
#error "c_RowTop has 16 rows"
#endif

u8 Beast_Step() __naked
{
	__asm
		push	ix
		ld	ix, (_g_BeastPtr)
		xor	a
		ld	(_s_Moved), a
		ld	(_s_MovedX), a
		inc	a
		ld	(_g_BoxWrap), a
		ld	a, 21 (ix)
		ld	(_s_Free), a
		or	a
		jr	nz, 00002$
		; not known free where it stands: test it there, once
		ld	l, 1 (ix)
		ld	h, 2 (ix)
		ld	a, 6 (ix)
		ld	e, a
		rla
		sbc	a, a
		ld	d, a
		add	hl, de
		ld	(_g_BoxLeft), hl
		ld	l, 10 (ix)
		ld	h, 11 (ix)
		ld	a, 15 (ix)
		ld	e, a
		rla
		sbc	a, a
		ld	d, a
		add	hl, de
		ld	(_g_BoxTop), hl
		ld	a, 7 (ix)
		ld	(_g_BoxW), a
		ld	a, 16 (ix)
		ld	(_g_BoxH), a
		call	_Data_BoxTest
		xor	a, #1
		ld	(_s_Free), a
	00002$:
		; x, if it moves along x; the other axis is y's block
		ld	a, 4 (ix)
		or	a, 5 (ix)
		jr	z, 00010$
		push	ix
		pop	hl
		ld	de, #9
		add	hl, de
		ld	(_s_Other), hl
		ld	a, #1
		ld	(_g_MoveAlongX), a
		call	00300$
		ld	a, (_s_Moved)           ; x moved; then y's own
		ld	(_s_MovedX), a
		xor	a
		ld	(_s_Moved), a
	00010$:
		; y, if it moves along y: ix on its block
		ld	de, #9
		add	ix, de
		ld	a, 4 (ix)
		or	a, 5 (ix)
		jr	z, 00015$
		push	ix
		pop	hl
		ld	de, #-9
		add	hl, de
		ld	(_s_Other), hl
		xor	a
		ld	(_g_MoveAlongX), a
		call	00300$
	00015$:
		ld	de, #-9
		add	ix, de
		ld	a, (_s_Free)
		ld	21 (ix), a
		ld	a, (_s_MovedX)
		ld	b, a
		ld	a, (_s_Moved)           ; y's
		ld	c, a
		or	a, b
		jr	nz, 00030$
		ld	19 (ix), a              ; still: its walk starts again
		ld	20 (ix), a
		pop	ix
		ret
	00030$:
		; round the map's ends, x in [0, MAP_W) and y in [0, MAP_H), each if it moved
		ld	a, b
		or	a
		jr	z, 00033$
		ld	a, 3 (ix)
		bit	7, a
		jr	z, 00031$
		ld	a, 2 (ix)
		add	a, #MAP_W_HI
		ld	2 (ix), a
		ld	a, 3 (ix)
		adc	a, #0
		ld	3 (ix), a
		jr	00033$
	00031$:
		or	a                       ; byte 3 not 0: past the end
		jr	nz, 00032$
		ld	a, 2 (ix)
		cp	a, #MAP_W_HI
		jr	c, 00033$
	00032$:
		ld	a, 2 (ix)
		sub	a, #MAP_W_HI
		ld	2 (ix), a
		ld	a, 3 (ix)
		sbc	a, #0
		ld	3 (ix), a
	00033$:
		ld	a, c
		or	a
		jr	nz, 00037$
		; y did not move: the same row, the column from x
		ld	a, 18 (ix)
		and	a, #0xF0
		or	a, 2 (ix)
		ld	18 (ix), a
		jr	00046$
	00037$:
		ld	a, 12 (ix)
		bit	7, a
		jr	z, 00034$
		ld	a, 11 (ix)
		add	a, #MAP_H_HI
		ld	11 (ix), a
		ld	a, 12 (ix)
		adc	a, #0
		ld	12 (ix), a
		jr	00036$
	00034$:
		or	a
		jr	nz, 00035$
		ld	a, 11 (ix)
		cp	a, #MAP_H_HI
		jr	c, 00036$
	00035$:
		ld	a, 11 (ix)
		sub	a, #MAP_H_HI
		ld	11 (ix), a
		ld	a, 12 (ix)
		sbc	a, #0
		ld	12 (ix), a
	00036$:
		; the row: from its room's, up or down by the tops
		ld	a, 18 (ix)
		rrca
		rrca
		rrca
		rrca
		and	a, #15
		ld	c, a                    ; row
		ld	e, 10 (ix)
		ld	d, 11 (ix)              ; de = y's whole pixels
	00040$:
		ld	hl, #_c_RowTop
		ld	a, c
		add	a, a
		add	a, l
		ld	l, a
		adc	a, h
		sub	a, l
		ld	h, a
		ld	a, e
		sub	a, (hl)
		inc	hl
		ld	a, d
		sbc	a, (hl)
		inc	hl
		jr	nc, 00042$
		dec	c                       ; above its top: up a row
		jr	00040$
	00042$:
		ld	a, e
		sub	a, (hl)
		inc	hl
		ld	a, d
		sbc	a, (hl)
		jr	c, 00044$               ; above the next row's top: this row
		inc	c
		jr	00040$
	00044$:
		ld	a, c
		add	a, a
		add	a, a
		add	a, a
		add	a, a
		or	a, 2 (ix)               ; the column: x's whole pixels >> 8
		ld	18 (ix), a
	00046$:
		inc	19 (ix)
		jr	nz, 00045$
		inc	20 (ix)
	00045$:
		ld	a, #1
		pop	ix
		ret

	; ---- one axis, ix on its block: position 0-3, delta 4-5, near edge 6,
	; size 7, far edge 8; s_Other the other's
	00300$:
		ld	a, (_s_Free)
		or	a
		jp	z, 00380$               ; not free where it stands: the search
		ld	e, 4 (ix)
		ld	d, 5 (ix)
		ld	a, d
		rla
		sbc	a, a
		ld	b, a                    ; bde: the delta, signed
		; the leading edge, and its cell's bits in the pixel's low byte
		or	a
		ld	a, 8 (ix)
		jr	z, 00305$
		ld	a, 6 (ix)
	00305$:
		ld	(_s_EdgeOff), a
		add	a, 1 (ix)
		and	a, #0xF8
		ld	c, a
		; to the target
		ld	a, 0 (ix)
		add	a, e
		ld	0 (ix), a
		ld	a, 1 (ix)
		adc	a, d
		ld	1 (ix), a
		ld	a, 2 (ix)
		adc	a, b
		ld	2 (ix), a
		ld	a, 3 (ix)
		adc	a, b
		ld	3 (ix), a
		ld	a, (_s_EdgeOff)
		add	a, 1 (ix)
		and	a, #0xF8
		cp	a, c
		jp	z, 00360$               ; the same cell: there
		; a new strip: its first pixel, (target + edge) rounded down to a cell
		ld	a, (_s_EdgeOff)
		ld	l, a
		rla
		sbc	a, a
		ld	h, a
		ld	a, 1 (ix)
		add	a, l
		ld	l, a
		ld	a, 2 (ix)
		adc	a, h
		ld	h, a
		ld	a, l
		and	a, #0xF8
		ld	l, a
		ld	(_s_Strip), hl
		call	00400$
		or	a
		jp	z, 00360$               ; free: there
		; blocked: back where it stood, then the candidate next to the strip
		ld	e, 4 (ix)
		ld	d, 5 (ix)
		ld	a, d
		rla
		sbc	a, a
		ld	b, a
		ld	a, 0 (ix)
		sub	a, e
		ld	0 (ix), a
		ld	a, 1 (ix)
		sbc	a, d
		ld	1 (ix), a
		ld	a, 2 (ix)
		sbc	a, b
		ld	2 (ix), a
		ld	a, 3 (ix)
		sbc	a, b
		ld	3 (ix), a
		ld	a, (_s_EdgeOff)
		ld	e, a
		rla
		sbc	a, a
		ld	d, a
		ld	hl, (_s_Strip)
		ld	a, b
		or	a
		jr	nz, 00340$
		; forwards: the strip's first pixel - 1 - the far edge; where it stands, it stays
		dec	hl
		or	a
		sbc	hl, de
		ld	a, l
		cp	a, 1 (ix)
		jr	nz, 00370$
		ld	a, h
		cp	a, 2 (ix)
		ret	z
		jr	00370$
	00340$:
		; backwards: the strip's first pixel + 8 - the near edge; level, only with a fraction
		ld	bc, #8
		add	hl, bc
		or	a
		sbc	hl, de
		ld	a, l
		cp	a, 1 (ix)
		jr	nz, 00370$
		ld	a, h
		cp	a, 2 (ix)
		jr	nz, 00370$
		ld	a, 0 (ix)
		or	a
		ret	z
	00370$:
		; hl, a whole pixel
		ld	0 (ix), #0
		ld	1 (ix), l
		ld	2 (ix), h
		ld	a, h
		rla
		sbc	a, a
		ld	3 (ix), a
	00360$:
		ld	a, #1
		ld	(_s_Moved), a
		ret
	00380$:
		; the search, a test a candidate (Move_Axis); where it ends is free if it moved
		ld	l, 0 (ix)
		ld	h, 1 (ix)
		ld	(_g_MovePos), hl
		ld	l, 2 (ix)
		ld	h, 3 (ix)
		ld	(_g_MovePos + 2), hl
		ld	l, 4 (ix)
		ld	h, 5 (ix)
		ld	(_g_MoveDelta), hl
		ld	a, 6 (ix)
		ld	(_g_MoveOff), a
		ld	a, 7 (ix)
		ld	(_g_MoveSize), a
		ld	hl, (_s_Other)
		inc	hl
		ld	e, (hl)
		inc	hl
		ld	d, (hl)
		ld	(_g_MoveOther), de      ; the other's whole pixels
		inc	hl
		inc	hl
		inc	hl
		inc	hl
		ld	a, (hl)
		ld	(_g_MoveOffO), a
		inc	hl
		ld	a, (hl)
		ld	(_g_MoveSizeO), a
		ld	a, #MOVE_MAP
		ld	(_g_MoveWorld), a
		call	_Move_Axis
		ld	hl, (_g_MovePos)
		ld	e, 0 (ix)
		ld	d, 1 (ix)
		or	a
		sbc	hl, de
		jr	nz, 00382$
		ld	hl, (_g_MovePos + 2)
		ld	e, 2 (ix)
		ld	d, 3 (ix)
		or	a
		sbc	hl, de
		ret	z
	00382$:
		ld	hl, (_g_MovePos)
		ld	0 (ix), l
		ld	1 (ix), h
		ld	hl, (_g_MovePos + 2)
		ld	2 (ix), l
		ld	3 (ix), h
		ld	a, #1
		ld	(_s_Free), a
		ld	(_s_Moved), a
		ret

	; ---- the strip at s_Strip along the axis, the box's breadth across: a = 1
	; blocked. The map never changes: a strip once found blocked is blocked, and
	; the last one is kept (a beast pushing along a wall meets it every tick).
	00400$:
		ld	hl, (_s_Other)
		inc	hl
		ld	e, (hl)
		inc	hl
		ld	d, (hl)                 ; the other's whole pixels
		inc	hl
		inc	hl
		inc	hl
		inc	hl
		ld	a, (hl)                 ; its near edge
		inc	hl
		ld	c, (hl)                 ; its size
		ld	l, a
		rla
		sbc	a, a
		ld	h, a
		add	hl, de                  ; the box's near edge across
		; the key: the axis, the strip, the cells across
		ld	a, (_g_MoveAlongX)
		ld	(_s_Key), a
		ld	de, (_s_Strip)
		ld	(_s_Key + 1), de
		push	hl
		sra	h
		rr	l
		sra	h
		rr	l
		sra	h
		rr	l
		ld	(_s_Key + 3), hl
		pop	hl
		push	hl
		ld	e, c
		dec	e
		ld	d, #0
		add	hl, de
		sra	h
		rr	l
		sra	h
		rr	l
		sra	h
		rr	l
		ld	(_s_Key + 5), hl
		ld	hl, (_g_BeastPtr)
		ld	de, #22
		add	hl, de
		ld	(_s_Cache), hl
		ld	de, #_s_Key
		ld	b, #7
	00401$:
		ld	a, (de)
		cp	a, (hl)
		jr	nz, 00402$
		inc	hl
		inc	de
		djnz	00401$
		pop	hl
		ld	a, #1                   ; the strip it found blocked
		ret
	00402$:
		pop	hl
		ld	a, (_g_MoveAlongX)
		or	a
		jr	z, 00410$
		ld	(_g_BoxTop), hl
		ld	a, c
		ld	(_g_BoxH), a
		ld	hl, (_s_Strip)
		ld	(_g_BoxLeft), hl
		ld	a, #1
		ld	(_g_BoxW), a
		jr	00420$
	00410$:
		ld	(_g_BoxLeft), hl
		ld	a, c
		ld	(_g_BoxW), a
		ld	hl, (_s_Strip)
		ld	(_g_BoxTop), hl
		ld	a, #1
		ld	(_g_BoxH), a
	00420$:
		call	_Data_BoxTest
		or	a
		ret	z
		ld	hl, #_s_Key             ; blocked: kept
		ld	de, (_s_Cache)
		ld	bc, #7
		ldir
		ld	a, #1
		ret
	__endasm;
}
