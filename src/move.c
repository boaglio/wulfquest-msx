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
u8  g_MoveFree = 0;

static i32 s_T = 0;       // the target
static i16 s_MStrip = 0;  // a creature's step: the strip of cells it steps into
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
		; known free where it stands (g_MoveFree, M13): a step under a cell whose
		; leading edge stays in its cell keeps the box in the cells it had, so the
		; target is free (the room's edges are cells' edges)
		ld	a, (_g_MoveFree)
		or	a
		jp	z, 00005$
		xor	a
		ld	(_g_MoveFree), a
		ld	a, (_g_MoveOff)
		ld	b, a                    ; backwards, the near edge
		ld	a, (_g_MoveDelta + 1)
		rla
		jr	c, 00002$
		ld	a, (_g_MoveSize)        ; forwards, the far one
		dec	a
		add	a, b
		ld	b, a
	00002$:
		ld	a, (_g_MovePos + 1)
		add	a, b
		and	a, #0xF8
		ld	c, a                    ; its cell where it stands (the low byte's cell bits)
		ld	a, (_s_T + 1)
		add	a, b
		and	a, #0xF8
		cp	a, c
		jr	nz, 00100$
	00099$:
		ld	hl, (_s_T)              ; the same cell, or a free strip: the target
		ld	(_g_MovePos), hl
		ld	hl, (_s_T + 2)
		ld	(_g_MovePos + 2), hl
		pop	ix
		ret
	00100$:
		; into a new strip of cells (M13, as Beast_Step): only it is tested, and
		; blocked (a wall, or past the room's edge, a cell's edge too) every
		; candidate whose leading edge is in it is blocked and every other one
		; free, so the first free is the last pixel before it
		ld	hl, (_s_T + 1)
		ld	e, b
		ld	a, b
		rla
		sbc	a, a
		ld	d, a
		add	hl, de
		ld	a, l
		and	a, #0xF8
		ld	l, a                    ; the strip's first pixel, the world's
		ld	(_s_MStrip), hl
		ld	a, (_g_MoveWorld)
		or	a
		jp	nz, 00130$              ; the map's world: the strip off its bitmap
		ld	a, (_g_MoveAlongX)
		or	a
		jr	z, 00110$
		ld	de, (_g_RoomX)
		or	a
		sbc	hl, de
		ld	a, h
		or	a
		jp	nz, 00150$              ; past the room's left or right edge
		ld	a, (_g_MoveScenery)
		or	a
		jr	z, 00099$
		ld	de, #(CELLS_MARGIN * CELL)
		add	hl, de
		ld	(_g_CbLeft), hl
		ld	a, #1
		ld	(_g_CbW), a
		ld	hl, (_g_MoveOther)
		ld	a, (_g_MoveOffO)
		ld	e, a
		rla
		sbc	a, a
		ld	d, a
		add	hl, de
		ld	de, (_g_RoomY)
		or	a
		sbc	hl, de
		ld	a, l
		add	a, #(CELLS_MARGIN * CELL)
		ld	(_g_CbTop), a
		ld	a, (_g_MoveSizeO)
		ld	(_g_CbH), a
		jr	00120$
	00110$:
		ld	de, (_g_RoomY)
		or	a
		sbc	hl, de
		ld	de, #PLAYFIELD_H
		or	a
		sbc	hl, de
		jr	nc, 00150$              ; past the room's top or bottom (unsigned)
		add	hl, de
		ld	a, (_g_MoveScenery)
		or	a
		jr	z, 00099$
		ld	a, l
		add	a, #(CELLS_MARGIN * CELL)
		ld	(_g_CbTop), a
		ld	a, #1
		ld	(_g_CbH), a
		ld	hl, (_g_MoveOther)
		ld	a, (_g_MoveOffO)
		ld	e, a
		rla
		sbc	a, a
		ld	d, a
		add	hl, de
		ld	de, (_g_RoomX)
		or	a
		sbc	hl, de
		ld	de, #(CELLS_MARGIN * CELL)
		add	hl, de
		ld	(_g_CbLeft), hl
		ld	a, (_g_MoveSizeO)
		ld	(_g_CbW), a
		jr	00120$
	00130$:
		; the map's world (Wulf): the strip round its ends (Data_StripTest), the
		; box's breadth across
		ld	(_g_BoxLeft), hl        ; along x the strip's column; along y, set below
		ld	(_g_BoxTop), hl
		ld	hl, (_g_MoveOther)
		ld	a, (_g_MoveOffO)
		ld	e, a
		rla
		sbc	a, a
		ld	d, a
		add	hl, de                  ; the box's near edge across
		ld	a, (_g_MoveSizeO)
		ld	c, a
		ld	a, (_g_MoveAlongX)
		or	a
		jr	z, 00135$
		ld	(_g_BoxTop), hl
		ld	a, c
		ld	(_g_BoxH), a
		ld	a, #1
		jr	00136$
	00135$:
		ld	(_g_BoxLeft), hl
		ld	a, c
		ld	(_g_BoxW), a
		xor	a
	00136$:
		call	_Data_StripTest
		jr	00121$
	00120$:
		call	_Cells_Blocked
	00121$:
		or	a
		jp	z, 00099$
	00150$:
		; blocked: forwards, the strip's first pixel - 1 - the far edge; where it
		; stands, it stays. Backwards, the strip's first pixel + 8 - the near
		; edge; level with where it stands, only with a fraction
		ld	hl, (_s_MStrip)
		ld	a, (_g_MoveOff)
		ld	e, a
		ld	a, (_g_MoveDelta + 1)
		rla
		jr	c, 00160$
		ld	a, (_g_MoveSize)
		dec	a
		add	a, e
		ld	e, a                    ; the far edge
		ld	a, e
		rla
		sbc	a, a
		ld	d, a
		dec	hl
		or	a
		sbc	hl, de
		ld	de, (_g_MovePos + 1)
		or	a
		sbc	hl, de
		jr	z, 00170$               ; where it stands
		add	hl, de
		jr	00165$
	00160$:
		ld	a, e
		rla
		sbc	a, a
		ld	d, a
		ld	bc, #8
		add	hl, bc
		or	a
		sbc	hl, de
		ld	de, (_g_MovePos + 1)
		or	a
		sbc	hl, de
		jr	nz, 00163$
		ld	a, (_g_MovePos)
		or	a
		jr	z, 00170$               ; level, and no fraction: where it stands
	00163$:
		add	hl, de
	00165$:
		; hl, a whole pixel
		xor	a
		ld	(_g_MovePos), a
		ld	(_g_MovePos + 1), hl
		ld	a, h
		rla
		sbc	a, a
		ld	(_g_MovePos + 3), a
	00170$:
		pop	ix
		ret
	00005$:
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
