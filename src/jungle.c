// Wulf Quest for MSX2: the original's jungle, alive (AGENTS.md §10, M6; the
// Java game's §12.8 and §12.9).
//
// The Java game's Simulation.tickCreatures, tickSwarm, tickBeasts and
// resolveCombat, with the original's small creatures' DART and HOME_IN
// (wulf.sim.ai), line by line and in the same order: every random draw is the
// Java game's own (rng.c), so test/jungle.sh holds the two equal tick for tick.
// Left out until their milestones: the score (M8), the quest's amulet that a
// tribesman calls at (M7), and everything a flower does (M8).
//
// Creatures never leave their room: their world is its scenery, from the
// cells around it vale.c keeps, with its edges as walls. The beasts walk the
// whole map, wrapping round its ends, so theirs is the ROM's masks.

#include "jungle.h"
#include "vale.h"
#include "rng.h"
#include "data.h"
#include "constants.h"
#include "player.h"
#include "jungle_data.h"
#include "tables.h"
#include "move.h"
#include "quest.h"
#include "quest_data.h"
#include "game.h"
#include "game_data.h"
#include "sound.h"

#define ONE			(1 << FIXED_SHIFT)
#define FIXED(px)	((i32)(px) << FIXED_SHIFT)
#define MAP_W_FP	((i32)GRID_W * PLAYFIELD_W * ONE)
#define MAP_H_FP	((i32)GRID_H * PLAYFIELD_H * ONE)
#if SWARM_SLOTS + 2 > CREATURE_POOL   // the swarm's SWARM_SLOTS, a flame burning and one going out
#error "the creature pool is smaller than the swarm and the flame can grow"
#endif

u8 g_JungleOn = 0;

u8 g_HereN = 0;
u8 g_Here[CREATURE_POOL];
u8 g_AwayN = 0;
u8 g_Away[CREATURE_POOL];
u8  g_CSpecies[CREATURE_POOL];
u8  g_CMode[CREATURE_POOL];
u8  g_CModeTick[CREATURE_POOL];
i32 g_CX[CREATURE_POOL];
i32 g_CY[CREATURE_POOL];
i16 g_CTimer[CREATURE_POOL];
i16 g_CPhase[CREATURE_POOL];
i16 g_CAux[CREATURE_POOL];
i8  g_CFace[CREATURE_POOL];
u8  g_CHp[CREATURE_POOL];
u8  g_CHurt[CREATURE_POOL];
u8  g_CTint[CREATURE_POOL];
u8  g_CHome[CREATURE_POOL];
u8  g_CAwayTicks[CREATURE_POOL];
u16 g_CMoveTicks[CREATURE_POOL];
u16 g_CId[CREATURE_POOL];
static u16 s_LastHit[CREATURE_POOL];
static u8 s_Used[CREATURE_POOL];
static u16 s_NextId = 0;

Beast g_Beast[BEASTS];
u8  g_BKind[BEASTS];
i8  g_BFace[BEASTS];
u8  g_BAsleep[BEASTS];
u8  g_BPause[BEASTS];

u8 g_SwarmQuiet = 0;

static void SetKind(u8 b, u8 k);
static u8 s_Role[BEASTS];   // each beast's behaviour: BEAST_RHINO, BEAST_TRIBESMAN, BEAST_SLEEPER

u8  g_BHint[BEASTS];        // a tribesman level with a quarter, lit up (M7)

//-----------------------------------------------------------------------------
// Simulation.kill (vale.c's state; here, as only banked code kills him): ignored
// while he is invulnerable or already down.

bool Vale_Kill()
{
	if (g_ValeMode != MODE_ALIVE || g_ValeInvuln > 0)
		return FALSE;
	g_ValeMode = MODE_DYING;
	g_ValeModeTick = 0;
	g_ValeSwing = -1;
	g_ValeCooldown = 0;
	g_ValeWalk = 0;
	g_ValeGaitTick = 0;
	g_ValeGait = 0;
	g_ValeFreeze = 0;
	g_SabreW = 0;
	g_SabreH = 0;
	Sound_Play(SFX_PLAYER_DIE);
	Sound_Loop(SFX_WULF_GROWL, 0);
	if (g_GameOn)
	{
		// Whatever was in his blood dies with him; the flame goes when he falls.
		g_EffectKind = EFFECT_NONE;
		g_EffectOrchid = -1;
		g_EffectTicks = 0;
		g_EffectTotal = 0;
		g_EffectScale = 256;
		g_EffectUntouchable = 0;
		for (u8 k = 0; k < g_HereN; k++)
		{
			u8 i = g_Here[k];
			if (g_CSpecies[i] == FLAME_SPECIES && g_CMode[i] == CREATURE_ALIVE)
			{
				g_CMode[i] = CREATURE_DYING;
				g_CModeTick[i] = 0;
			}
		}
	}
	return TRUE;
}

//-----------------------------------------------------------------------------
// Small helpers

static i8 Sign(i16 v)
{
	return v > 0 ? 1 : v < 0 ? -1 : 0;
}

// Whether box A overlaps box B, both in these (as arguments, SDCC passes
// eight on the stack, slowly).
static i16 s_AX, s_AY, s_BX, s_BY;
static u8 s_AW, s_AH, s_BW, s_BH;

// Assembly (M13: 236 cycles in C, several a tick). The differences are
// taken by their sign: world pixels, far from overflowing 16 bits.
#if GRID_W * PLAYFIELD_W > 16000 || GRID_H * PLAYFIELD_H > 16000
#error "Overlaps compares world pixels by the sign of their difference"
#endif
static bool Overlaps() __naked
{
	__asm
		ld	hl, (_s_BX)
		ld	a, (_s_BW)
		ld	de, (_s_AX)
		call	00050$                  ; s_AX < s_BX + s_BW
		or	a
		ret	z
		ld	hl, (_s_AX)
		ld	a, (_s_AW)
		ld	de, (_s_BX)
		call	00050$                  ; s_BX < s_AX + s_AW
		or	a
		ret	z
		ld	hl, (_s_BY)
		ld	a, (_s_BH)
		ld	de, (_s_AY)
		call	00050$                  ; s_AY < s_BY + s_BH
		or	a
		ret	z
		ld	hl, (_s_AY)
		ld	a, (_s_AH)
		ld	de, (_s_BY)
	; ---- a = 1 if de < hl + a, else 0
	00050$:
		add	a, l
		ld	l, a
		adc	a, h
		sub	a, l
		ld	h, a
		ex	de, hl
		or	a
		sbc	hl, de
		ld	a, #0
		ret	p
		inc	a
		ret
	__endasm;
}

// Box A: Vale's.
static void ValeBox()
{
	s_AX = FP_PX(g_ValeX) + PLAYER_BOX_X;
	s_AY = FP_PX(g_ValeY) + PLAYER_BOX_Y;
	s_AW = PLAYER_BOX_W;
	s_AH = PLAYER_BOX_H;
}

// Box B: small creature i's. Assembly (M13: 735 cycles a call in C, and
// combat makes one a creature a tick, twice fencing).
static void CreatureBox(u8 i) __naked
{
	i;   // in a
	__asm
		ld	e, a
		ld	d, #0
		ld	hl, #_g_CSpecies
		add	hl, de
		ld	c, (hl)
		ld	b, #0                   ; bc = its species
		ex	de, hl
		add	hl, hl
		add	hl, hl                  ; 4i
		push	hl
		ld	de, #_g_CX + 1
		add	hl, de
		ld	e, (hl)
		inc	hl
		ld	d, (hl)                 ; FP_PX(x)
		ld	hl, #g_CreatureBoxX_AT
		add	hl, bc
		ld	a, (hl)
		ld	l, a
		rla
		sbc	a, a
		ld	h, a
		add	hl, de
		ld	(_s_BX), hl
		pop	hl
		ld	de, #_g_CY + 1
		add	hl, de
		ld	e, (hl)
		inc	hl
		ld	d, (hl)
		ld	hl, #g_CreatureBoxY_AT
		add	hl, bc
		ld	a, (hl)
		ld	l, a
		rla
		sbc	a, a
		ld	h, a
		add	hl, de
		ld	(_s_BY), hl
		ld	hl, #g_CreatureBoxW_AT
		add	hl, bc
		ld	a, (hl)
		ld	(_s_BW), a
		ld	hl, #g_CreatureBoxH_AT
		add	hl, bc
		ld	a, (hl)
		ld	(_s_BH), a
		ret
	__endasm;
}

static i16 ValeCentreX()
{
	return FP_PX(g_ValeX) + (PLAYER_BOX_X + PLAYER_BOX_W / 2);
}

static i16 ValeCentreY()
{
	return FP_PX(g_ValeY) + (PLAYER_BOX_Y + PLAYER_BOX_H / 2);
}

// Simulation.inFront: fencing, the side he faces; straight up or down, by y.
static bool InFront(i16 x, i16 y)
{
	i8 dx = g_ValeFacing == FACE_N || g_ValeFacing == FACE_S ? 0
		: g_ValeFacing >= FACE_SW ? -1 : 1;
	if (dx != 0)
		return Sign(x - ValeCentreX()) != -dx;
	i8 dy = g_ValeFacing == FACE_N ? -1 : 1;
	return Sign(y - ValeCentreY()) != -dy;
}

//-----------------------------------------------------------------------------
// Collision: a creature in its room, a beast on the wrapping map (move.c).

static i8 s_BoxX;
static i8 s_BoxY;
static u8 s_BoxW;
static u8 s_BoxH;
static bool s_Scenery;    // a creature that does not ignore scenery

// A creature's box in its room (the swarm's test of a spot): the edges walls.
static bool BoxBlocked(i16 x, i16 y)
{
	i16 left = x + s_BoxX;
	i16 top = y + s_BoxY;
	left -= g_RoomX;
	top -= g_RoomY;
	if ((u16)left > PLAYFIELD_W - s_BoxW || (u16)top > PLAYFIELD_H - s_BoxH)
		return TRUE;   // the room's edges
	if (!s_Scenery)
		return FALSE;
	g_CbLeft = (u16)left + CELLS_MARGIN * CELL;
	g_CbTop = (u8)top + CELLS_MARGIN * CELL;
	g_CbW = s_BoxW;
	g_CbH = s_BoxH;
	return Cells_Blocked();
}


// The creature being ticked, in these while it is (SDCC indexes arrays of
// 32-bit values slowly): Load, its ways, Store. Its box is s_BoxX..s_BoxH.
static u8 s_I;
static u8 s_S;
static i32 s_CX;
static i32 s_CY;
static i16 s_Ph;
static i16 s_Aux;
static i16 s_Timer;
static i8 s_Face;
static bool s_Moved;

// Assembly (M13: in C, 1 250 and 850 cycles, and a tick makes a pair a creature).
static void Load(u8 i) __naked
{
	i;   // in a
	__asm
		ld	(_s_I), a
		ld	e, a
		ld	d, #0
		ld	hl, #_g_CSpecies
		add	hl, de
		ld	c, (hl)
		ld	a, c
		ld	(_s_S), a
		ld	b, #0                   ; bc = its species
		ld	hl, #_g_CFace
		add	hl, de
		ld	a, (hl)
		ld	(_s_Face), a
		ex	de, hl
		add	hl, hl                  ; 2i
		push	hl
		ld	de, #_g_CPhase
		add	hl, de
		ld	de, #_s_Ph
		ldi
		ldi
		pop	hl
		push	hl
		ld	de, #_g_CAux
		add	hl, de
		ld	de, #_s_Aux
		ldi
		ldi
		pop	hl
		push	hl
		ld	de, #_g_CTimer
		add	hl, de
		ld	de, #_s_Timer
		ldi
		ldi
		pop	hl
		add	hl, hl                  ; 4i
		push	hl
		ld	de, #_g_CX
		add	hl, de
		ld	de, #_s_CX
		ldi
		ldi
		ldi
		ldi
		pop	hl
		ld	de, #_g_CY
		add	hl, de
		ld	de, #_s_CY
		ldi
		ldi
		ldi
		ldi
		ld	hl, #g_CreatureBoxX_AT  ; (ldi counted bc down 16: the species again)
		ld	a, (_s_S)
		ld	c, a
		ld	b, #0
		add	hl, bc
		ld	a, (hl)
		ld	(_s_BoxX), a
		ld	hl, #g_CreatureBoxY_AT
		add	hl, bc
		ld	a, (hl)
		ld	(_s_BoxY), a
		ld	hl, #g_CreatureBoxW_AT
		add	hl, bc
		ld	a, (hl)
		ld	(_s_BoxW), a
		ld	hl, #g_CreatureBoxH_AT
		add	hl, bc
		ld	a, (hl)
		ld	(_s_BoxH), a
		xor	a
		ld	(_s_Moved), a
		ret
	__endasm;
}

static void Store() __naked
{
	__asm
		ld	a, (_s_I)
		ld	e, a
		ld	d, #0
		ld	hl, #_g_CFace
		add	hl, de
		ld	a, (_s_Face)
		ld	(hl), a
		ex	de, hl
		add	hl, hl                  ; 2i
		push	hl
		ld	de, #_g_CPhase
		add	hl, de
		ex	de, hl
		ld	hl, #_s_Ph
		ldi
		ldi
		pop	hl
		push	hl
		ld	de, #_g_CAux
		add	hl, de
		ex	de, hl
		ld	hl, #_s_Aux
		ldi
		ldi
		pop	hl
		push	hl
		ld	de, #_g_CTimer
		add	hl, de
		ex	de, hl
		ld	hl, #_s_Timer
		ldi
		ldi
		pop	hl
		add	hl, hl                  ; 4i
		push	hl
		ld	de, #_g_CX
		add	hl, de
		ex	de, hl
		ld	hl, #_s_CX
		ldi
		ldi
		ldi
		ldi
		pop	hl
		ld	de, #_g_CY
		add	hl, de
		ex	de, hl
		ld	hl, #_s_CY
		ldi
		ldi
		ldi
		ldi
		ret
	__endasm;
}

// SimContext.move: x then y, each resolved on its own (move.c). A small
// creature is always free where it stands: it comes where its box is free of
// the scenery and in the room (the swarm's and the flame's BoxBlocked), and a
// move ends where it is free or stays. Its steps are under a cell (the
// converter checks), so Move_Axis may take that (g_MoveFree).
#if FLAME_SPEED_FP >= CELL * 256 || FLAME_DIAG_FP >= CELL * 256
#error "the flame steps a cell or more a tick; Move_Axis takes a creature's step as under one"
#endif
static void MoveCreature() __naked
{
	__asm
		ld	a, #MOVE_ROOM
		ld	(_g_MoveWorld), a
		ld	a, (_s_S)
		ld	e, a
		ld	d, #0
		ld	hl, #g_CreatureFlier_AT
		add	hl, de
		ld	a, (hl)
		or	a
		ld	a, #0
		jr	nz, 00001$
		inc	a
	00001$:
		ld	(_g_MoveScenery), a     ; !flier
		; x, if it moves along x
		ld	hl, (_s_Ph)
		ld	a, h
		or	a, l
		jr	z, 00010$
		ld	(_g_MoveDelta), hl
		ld	hl, (_s_CX)
		ld	(_g_MovePos), hl
		ld	hl, (_s_CX + 2)
		ld	(_g_MovePos + 2), hl
		ld	hl, (_s_CY + 1)
		ld	(_g_MoveOther), hl      ; FP_PX(s_CY)
		ld	a, (_s_BoxX)
		ld	(_g_MoveOff), a
		ld	a, (_s_BoxW)
		ld	(_g_MoveSize), a
		ld	a, (_s_BoxY)
		ld	(_g_MoveOffO), a
		ld	a, (_s_BoxH)
		ld	(_g_MoveSizeO), a
		ld	a, #1
		ld	(_g_MoveAlongX), a
		ld	(_g_MoveFree), a
		call	_Move_Axis
		ld	de, #_s_CX
		call	00050$
	00010$:
		; y, if it moves along y
		ld	hl, (_s_Aux)
		ld	a, h
		or	a, l
		ret	z
		ld	(_g_MoveDelta), hl
		ld	hl, (_s_CY)
		ld	(_g_MovePos), hl
		ld	hl, (_s_CY + 2)
		ld	(_g_MovePos + 2), hl
		ld	hl, (_s_CX + 1)
		ld	(_g_MoveOther), hl      ; FP_PX(s_CX)
		ld	a, (_s_BoxY)
		ld	(_g_MoveOff), a
		ld	a, (_s_BoxH)
		ld	(_g_MoveSize), a
		ld	a, (_s_BoxX)
		ld	(_g_MoveOffO), a
		ld	a, (_s_BoxW)
		ld	(_g_MoveSizeO), a
		xor	a
		ld	(_g_MoveAlongX), a
		inc	a
		ld	(_g_MoveFree), a
		call	_Move_Axis
		ld	de, #_s_CY
	; ---- g_MovePos into the position at de if it is another: moved
	00050$:
		ld	hl, #_g_MovePos
		ld	b, #4
		push	de
	00051$:
		ld	a, (de)
		cp	a, (hl)
		jr	nz, 00052$
		inc	hl
		inc	de
		djnz	00051$
		pop	de
		ret
	00052$:
		pop	de
		ld	hl, #_g_MovePos
		ldi
		ldi
		ldi
		ldi
		ld	a, #1
		ld	(_s_Moved), a
		ret
	__endasm;
}

//-----------------------------------------------------------------------------
// The original's small creatures' ways (wulf.sim.ai.Dart, HomeIn), on the
// creature in s_I..s_Face

#define P_SPEED_MIN		g_CreatureP0
#define P_SPEED_STEP	g_CreatureP1
#define P_SPEED_STEPS	g_CreatureP2
#define P_RUN			g_CreatureP3
#define P_PAUSE_MIN		g_CreatureP4
#define P_PAUSE_SPAN	g_CreatureP5
#define P_ACCEL			g_CreatureP0
#define P_MAX			g_CreatureP1

static i16 DartSpeed()
{
	u8 s = s_S;
	i16 v = P_SPEED_MIN[s];
	for (u16 n = Rng_Next((u16)P_SPEED_STEPS[s]); n; n--)
		v += P_SPEED_STEP[s];   // no multiplication: banked code has no SDCC library
	return Rng_Next(2) == 0 ? -v : v;
}

static void SetOff()
{
	s_Ph = DartSpeed();
	s_Aux = DartSpeed();
	i8 f = Sign(s_Ph);
	if (f != 0)
		s_Face = f;
	s_Timer = P_RUN[s_S];
}

static void Stand()
{
	u8 s = s_S;
	s_Ph = 0;
	s_Aux = 0;
	i16 span = P_PAUSE_SPAN[s] / 2;
	s_Timer = P_PAUSE_MIN[s] + 2 * (i16)Rng_Next((u16)(span < 1 ? 1 : span));
}

// Dart.glance: a step that would leave the room turns that axis about.
// Assembly (M13: 1 100 cycles in C, a creature a tick).
static void Glance() __naked
{
	__asm
		; along x: the box's left at s_CX + s_Ph, from the room's
		ld	de, (_s_Ph)
		ld	hl, (_s_CX + 1)
		ld	a, (_s_CX)
		call	00050$
		ld	a, (_s_BoxX)
		ld	e, a
		rla
		sbc	a, a
		ld	d, a
		add	hl, de
		ld	de, (_g_RoomX)
		or	a
		sbc	hl, de
		ex	de, hl                  ; de = left
		ld	a, (_s_BoxW)
		ld	c, a
		ld	b, #0
		ld	hl, #PLAYFIELD_W
		or	a
		sbc	hl, bc                  ; the most the left may be
		or	a
		sbc	hl, de                  ; unsigned: a left past it, or below 0, is off the room
		jr	nc, 00010$
		ld	hl, (_s_Ph)             ; turned about
		xor	a
		sub	a, l
		ld	l, a
		sbc	a, a
		sub	a, h
		ld	h, a
		ld	(_s_Ph), hl
		or	a, l
		jr	z, 00010$               ; Sign 0: the face as it was
		ld	a, h
		rla
		sbc	a, a
		or	a, #1                   ; -1 or 1
		ld	(_s_Face), a
	00010$:
		; along y: the box's top at s_CY + s_Aux
		ld	de, (_s_Aux)
		ld	hl, (_s_CY + 1)
		ld	a, (_s_CY)
		call	00050$
		ld	a, (_s_BoxY)
		ld	e, a
		rla
		sbc	a, a
		ld	d, a
		add	hl, de
		ld	de, (_g_RoomY)
		or	a
		sbc	hl, de
		ex	de, hl
		ld	a, (_s_BoxH)
		ld	c, a
		ld	b, #0
		ld	hl, #PLAYFIELD_H
		or	a
		sbc	hl, bc
		or	a
		sbc	hl, de
		ret	nc
		ld	hl, (_s_Aux)
		xor	a
		sub	a, l
		ld	l, a
		sbc	a, a
		sub	a, h
		ld	h, a
		ld	(_s_Aux), hl
		ret

	; ---- hl = FP_PX(the position + de): a = its fraction byte, hl its pixels
	00050$:
		add	a, e                    ; the fraction's carry
		ld	a, d
		adc	a, l
		ld	l, a
		bit	7, d
		ld	a, h
		jr	nz, 00051$
		adc	a, #0
		ld	h, a
		ret
	00051$:
		adc	a, #0xFF
		ld	h, a
		ret
	__endasm;
}

static void Dart()
{
	if (--s_Timer <= 0)
	{
		if (s_Ph != 0 || s_Aux != 0)
			Stand();
		else
			SetOff();
	}
	if (s_Ph == 0 && s_Aux == 0)
		return;
	Glance();
	MoveCreature();
	if (!s_Moved)
		Stand();   // the scenery has it fast
}

static i16 Lean(i16 v, i16 gap, i16 accel, i16 max)
{
	if (gap == 0)
		return 0;
	v += gap > 0 ? accel : -accel;
	return v > max ? max : v < -max ? -max : v;
}

static void HomeIn()
{
	u8 s = s_S;
	s_Ph = Lean(s_Ph, ValeCentreX() - (FP_PX(s_CX) + s_BoxX + s_BoxW / 2), P_ACCEL[s], P_MAX[s]);
	s_Aux = Lean(s_Aux, ValeCentreY() - (FP_PX(s_CY) + s_BoxY + s_BoxH / 2), P_ACCEL[s], P_MAX[s]);
	i8 f = Sign(s_Ph);
	if (f != 0)
		s_Face = f;
	Glance();
	MoveCreature();
}

//-----------------------------------------------------------------------------
// The swarm (§12.8)

// burst: a small creature bursts, and scores, however it went (swarmScore):
// a forming puff by its frame, else its kind's, and more facing right.
static void Burst(u8 i)
{
	if (g_CMode[i] == CREATURE_APPEARING)
	{
		u8 f = g_CModeTick[i] / SWARM_EVERY_TICKS;
		Score_Add(g_SwarmAppearScore[f < SWARM_APPEAR_SCORES ? f : SWARM_APPEAR_SCORES - 1]);
	}
	else
		Score_Add(g_CreatureScore[g_CSpecies[i]] + (g_CFace[i] > 0 ? SWARM_FACING_RIGHT_BONUS : 0));
	g_CMode[i] = CREATURE_DYING;
	g_CModeTick[i] = 0;
	Sound_Play(SFX_CREATURE_DIE);
}

#if (SWARM_EVERY_TICKS & (SWARM_EVERY_TICKS - 1)) != 0
#error "a forming puff's score frame divides by SWARM_EVERY_TICKS with a shift"
#endif

static u8 NewCreature()
{
	for (u8 i = 0; i < CREATURE_POOL; i++)
		if (!s_Used[i])
		{
			s_Used[i] = 1;
			return i;
		}
	return 0xFF;   // the pool's size is checked against the swarm's at compile time
}

// The flame's way (wulf.sim.ai.Flame): its course, every steerTicks; it
// glances off whatever stops it. Its heading (Creature.dirX, dirY): one
// flame burns at a time.
static i8 s_FlameDX = 1;
static i8 s_FlameDY = 0;
#define P_STEER		g_CreatureP0
#define P_HOMING	g_CreatureP1

static void FlameDirection(i8 dx, i8 dy)
{
	s_FlameDX = dx;
	s_FlameDY = dy;
	if (dx != 0)
		s_Face = dx;
}

static void Flame()
{
	if (s_Timer <= 0)
	{
		s_Timer = P_STEER[s_S];
		if ((i16)Rng_Next(10000) < P_HOMING[s_S])
		{
			// A diagonal at him, each axis by which side he is on.
			FlameDirection(ValeCentreX() < FP_PX(s_CX) + s_BoxX + s_BoxW / 2 ? -1 : 1,
				ValeCentreY() < FP_PX(s_CY) + s_BoxY + s_BoxH / 2 ? -1 : 1);
		}
		else
		{
			// Direction8.N.rotate(n): N NE E SE S SW W NW.
			u8 d = (u8)Rng_Next(8);
			FlameDirection(d == 0 || d == 4 ? 0 : d < 4 ? 1 : -1, d == 2 || d == 6 ? 0 : d > 2 && d < 6 ? 1 : -1);
		}
	}
	s_Timer--;
	s_Aux--;
	// Moves.step: its speed, scaled on a diagonal; blocked, it glances off.
	i16 v = s_FlameDX && s_FlameDY ? FLAME_DIAG_FP : FLAME_SPEED_FP;
	s_Ph = s_FlameDX < 0 ? -v : s_FlameDX > 0 ? v : 0;
	i16 aux = s_Aux;
	s_Aux = s_FlameDY < 0 ? -v : s_FlameDY > 0 ? v : 0;
	i32 tx = s_CX + s_Ph;
	i32 ty = s_CY + s_Aux;
	MoveCreature();
	bool bx = s_Ph != 0 && s_CX != tx;
	bool by = s_Aux != 0 && s_CY != ty;
	s_Ph = 0;   // its phase stays 0, its aux the time it has left
	s_Aux = aux;
	if (bx)
		FlameDirection(-s_FlameDX, s_FlameDY);
	if (by)
		FlameDirection(s_FlameDX, -s_FlameDY);
}

// Each creature in the room a tick on, in assembly (M13: SDCC indexed the
// arrays a creature at a time, 4 000 cycles a tick in a busy room).
#define CM_ALIVE		0
#define CM_DYING		1
#define CM_APPEARING	2
typedef char c_CModes[(CREATURE_ALIVE == CM_ALIVE && CREATURE_DYING == CM_DYING
	&& CREATURE_APPEARING == CM_APPEARING && BEHAVIOUR_DART == 0 && BEHAVIOUR_HOME_IN == 1) ? 1 : -1];
static u8 s_TcK;      // the k of g_Here
static u8 s_TcI;      // its creature
static u8 s_TcDown;   // he is down: the small creatures burst

static void TickHere() __naked
{
	__asm
		xor	a
		ld	(_s_TcK), a
	00010$:
		ld	a, (_s_TcK)
		ld	hl, #_g_HereN
		cp	a, (hl)
		ret	nc
		ld	e, a
		ld	d, #0
		ld	hl, #_g_Here
		add	hl, de
		ld	e, (hl)                 ; de = i
		ld	a, e
		ld	(_s_TcI), a
		ld	hl, #_g_CMode
		add	hl, de
		ld	a, (hl)
		cp	a, #CM_DYING
		jr	nz, 00020$
		ld	hl, #_g_CModeTick       ; a puff: its frames on
		add	hl, de
		inc	(hl)
		jp	00080$
	00020$:
		cp	a, #CM_APPEARING
		jr	nz, 00030$
		ld	hl, #_g_CModeTick       ; forming: alive when it has formed
		add	hl, de
		inc	(hl)
		ld	a, (hl)
		cp	a, #SWARM_APPEAR_TICKS
		jp	c, 00080$
		ld	(hl), #0
		ld	hl, #_g_CMode
		add	hl, de
		ld	(hl), #CM_ALIVE
		ld	hl, #_g_CSpecies
		add	hl, de
		ld	c, (hl)
		ld	b, #0
		ld	hl, #g_CreatureBehaviour_AT
		add	hl, bc
		ld	a, (hl)
		or	a
		jp	nz, 00080$              ; a dart is on its way the moment it forms
		ld	a, (_s_TcI)
		call	_Load
		call	_SetOff
		call	_Store
		jp	00080$
	00030$:
		ld	a, (_s_TcDown)
		or	a
		jr	z, 00040$
		ld	hl, #_g_CSpecies
		add	hl, de
		ld	a, (hl)
		cp	a, #FLAME_SPECIES
		jr	z, 00040$
		ld	a, e                    ; with him down, every small creature bursts
		call	_Burst
		jp	00080$
	00040$:
		ld	hl, #_g_CHurt
		add	hl, de
		ld	a, (hl)
		or	a
		jr	z, 00041$
		dec	(hl)
	00041$:
		ld	a, e
		call	_Load
		ld	a, (_s_S)
		ld	e, a
		ld	d, #0
		ld	hl, #g_CreatureBehaviour_AT
		add	hl, de
		ld	a, (hl)
		or	a
		jr	nz, 00042$
		call	_Dart
		jr	00045$
	00042$:
		dec	a
		jr	nz, 00043$
		call	_HomeIn
		jr	00045$
	00043$:
		call	_Flame
	00045$:
		call	_Store
		ld	a, (_s_TcI)             ; its walk: on if it moved, else again
		ld	l, a
		ld	h, #0
		add	hl, hl
		ld	de, #_g_CMoveTicks
		add	hl, de
		ld	a, (_s_Moved)
		or	a
		jr	z, 00046$
		inc	(hl)
		jr	nz, 00080$
		inc	hl
		inc	(hl)
		jp	00080$
	00046$:
		ld	(hl), a
		inc	hl
		ld	(hl), a
	00080$:
		ld	hl, #_s_TcK
		inc	(hl)
		jp	00010$
	__endasm;
}

void Jungle_TickCreatures()
{
	s_TcDown = g_ValeMode != MODE_ALIVE || (g_GameOn && g_EffectKind == EFFECT_KNOCKDOWN);
	TickHere();
	// removeFinished: the puffs that are done.
	u8 keep = 0;
	for (u8 k = 0; k < g_HereN; k++)
	{
		u8 i = g_Here[k];
		if (g_CMode[i] == CREATURE_DYING && g_CModeTick[i] >= CREATURE_PUFF_TICKS)
			s_Used[i] = 0;
		else
			g_Here[keep++] = i;
	}
	g_HereN = keep;
	// ageAway: one left behind waits lingerTicks in all, then is gone.
	keep = 0;
	for (u8 k = 0; k < g_AwayN; k++)
	{
		u8 i = g_Away[k];
		if (++g_CAwayTicks[i] < SWARM_LINGER_TICKS)
			g_Away[keep++] = i;
		else
			s_Used[i] = 0;
	}
	g_AwayN = keep;
}

void Jungle_TickSwarm()
{
	if (g_SwarmQuiet > 0)
	{
		g_SwarmQuiet--;
		return;
	}
	if ((g_GameOn && g_EffectKind == EFFECT_KNOCKDOWN) || (u16)g_SimTick % SWARM_EVERY_TICKS != 0)
		return;
#if (65536 % SWARM_EVERY_TICKS) != 0
#error "the swarm's tick test takes the game tick's low 16 bits"
#endif
	u8 here = 0;
	for (u8 k = 0; k < g_HereN; k++)
		if (g_CSpecies[g_Here[k]] != FLAME_SPECIES)
			here++;   // the swarm's, not the flame
	if (here >= (g_Easy ? EASY_SWARM_ROOM_MAX : SWARM_ROOM_MAX) || here + g_AwayN >= SWARM_SLOTS)
		return;
	i16 lx = SWARM_AREA_X + (i16)Rng_Next(SWARM_AREA_W);
	i16 ly = SWARM_AREA_Y + (i16)Rng_Next(SWARM_AREA_H);
	u8 roll = (u8)Rng_Next(SWARM_PICKS);
	u8 index = g_SwarmPick[roll < SWARM_PICK_OPEN + (g_Easy ? EASY_SWARM_OPEN_PER_QUARTER : SWARM_PICK_OPEN_PER_QUARTER) * Quest_Ramp() ? roll
		: roll % SWARM_PICK_FOLD];
	u8 tint = g_SwarmTint[Rng_Next(SWARM_TINTS)];
	i16 x = g_RoomX + lx;
	i16 y = g_RoomY + ly;
	s_BoxX = g_CreatureBoxX[index];
	s_BoxY = g_CreatureBoxY[index];
	s_BoxW = g_CreatureBoxW[index];
	s_BoxH = g_CreatureBoxH[index];
	s_Scenery = TRUE;   // the Java game's roomSolid, whatever the kind
	if (BoxBlocked(x, y))
		return;
	s_AX = FP_PX(g_ValeX) + PLAYER_BOX_X - SWARM_CLEARANCE_PX;
	s_AY = FP_PX(g_ValeY) + PLAYER_BOX_Y - SWARM_CLEARANCE_PX;
	s_AW = PLAYER_BOX_W + 2 * SWARM_CLEARANCE_PX;
	s_AH = PLAYER_BOX_H + 2 * SWARM_CLEARANCE_PX;
	s_BX = x + s_BoxX;
	s_BY = y + s_BoxY;
	s_BW = s_BoxW;
	s_BH = s_BoxH;
	if (Overlaps())
		return;
	u8 i = NewCreature();
	g_CSpecies[i] = index;
	g_CMode[i] = CREATURE_APPEARING;
	g_CModeTick[i] = 0;
	g_CX[i] = FIXED(x);
	g_CY[i] = FIXED(y);
	g_CTimer[i] = 0;
	g_CPhase[i] = 0;
	g_CAux[i] = 0;
	g_CFace[i] = 1;
	g_CHp[i] = g_CreatureHp[index];
	g_CHurt[i] = 0;
	g_CTint[i] = tint;
	g_CHome[i] = g_ValeRoom;
	g_CAwayTicks[i] = 0;
	g_CMoveTicks[i] = 0;
	g_CId[i] = s_NextId++;
	s_LastHit[i] = 0xFFFF;
	g_Here[g_HereN++] = i;
}

//-----------------------------------------------------------------------------
// The flame (§12.7): Simulation.tickFlame and lightFlame

void Jungle_TickFlame()
{
	if (!FLAME_COMES || (g_Easy && !EASY_FLAME))
		return;
	for (u8 k = 0; k < g_HereN; k++)
	{
		u8 i = g_Here[k];
		if (g_CSpecies[i] == FLAME_SPECIES && g_CMode[i] == CREATURE_ALIVE)
		{
			if (g_CAux[i] <= 0)
			{
				g_CMode[i] = CREATURE_DYING;   // burnt out
				g_CModeTick[i] = 0;
			}
			return;
		}
	}
	if (g_FlameWait > 0)
	{
		g_FlameWait--;
		return;
	}
	// Straight above or below him, or level with him; never on him, never in
	// a wall: else next tick.
	i16 px = FP_PX(g_ValeX) - g_RoomX;
	i16 py = FP_PX(g_ValeY) - g_RoomY;
	bool column = Rng_Next(2) == 0;
	i16 x = column ? px : FLAME_INSET_PX + (i16)Rng_Next(PLAYFIELD_W - 2 * FLAME_INSET_PX);
	i16 y = column ? FLAME_INSET_PX + (i16)Rng_Next(PLAYFIELD_H - 2 * FLAME_INSET_PX) : py;
	x += g_RoomX;
	y += g_RoomY;
	ValeBox();
	s_BX = x + FLAME_BOX_X;
	s_BY = y + FLAME_BOX_Y;
	s_BW = FLAME_BOX_W;
	s_BH = FLAME_BOX_H;
	if (Overlaps())
		return;
	s_BoxX = FLAME_BOX_X;
	s_BoxY = FLAME_BOX_Y;
	s_BoxW = FLAME_BOX_W;
	s_BoxH = FLAME_BOX_H;
	s_Scenery = TRUE;   // roomSolid
	if (BoxBlocked(x, y))
		return;
	u8 i = NewCreature();
	g_CSpecies[i] = FLAME_SPECIES;
	g_CMode[i] = CREATURE_ALIVE;
	g_CModeTick[i] = 0;
	g_CX[i] = FIXED(x);
	g_CY[i] = FIXED(y);
	g_CTimer[i] = 0;   // Flame.spawn: a course at once
	g_CPhase[i] = 0;
	g_CAux[i] = FLAME_LIFE_TICKS;
	g_CFace[i] = 1;
	g_CHp[i] = 0;
	g_CHurt[i] = 0;
	g_CTint[i] = 0;
	g_CHome[i] = 0xFF;   // no room's: it is not the swarm's
	g_CAwayTicks[i] = 0;
	g_CMoveTicks[i] = 0;
	g_CId[i] = s_NextId++;
	s_LastHit[i] = 0xFFFF;
	s_FlameDX = 1;
	s_FlameDY = 0;
	g_Here[g_HereN++] = i;
}

static void Repopulate()
{
	// Those here wait where they are; the puffs are done with.
	for (u8 k = 0; k < g_HereN; k++)
	{
		u8 i = g_Here[k];
		if (g_CMode[i] != CREATURE_DYING && g_CSpecies[i] != FLAME_SPECIES)
			g_Away[g_AwayN++] = i;
		else
			s_Used[i] = 0;   // a puff, or the flame: gone
	}
	g_HereN = 0;
	// Any waiting in this room are back.
	u8 keep = 0;
	for (u8 k = 0; k < g_AwayN; k++)
	{
		u8 i = g_Away[k];
		if (g_CHome[i] == g_ValeRoom)
			g_Here[g_HereN++] = i;
		else
			g_Away[keep++] = i;
	}
	g_AwayN = keep;
}

void Jungle_EnterRoom()
{
	Repopulate();
}

void Jungle_GetUp()
{
	g_SwarmQuiet = SWARM_QUIET_TICKS;
	Repopulate();
}

//-----------------------------------------------------------------------------
// The big beasts (§12.9)

#define P_PACE		g_BeastP0
#define P_SHOVED	g_BeastP1
#define P_WALK_SLOW	g_BeastP2
#define P_WALK_FAST	g_BeastP3
#define P_CHARGE	g_BeastP4
#define P_WAKE		g_BeastP5
#define P_HINT		g_BeastP6
#define P_CALL		g_BeastP7

enum { TOUCH_NONE, TOUCH_STRUCK, TOUCH_HIT };

static void Easy(i16* hard, const i16* easy)
{
	for (u8 k = 0; k < BEAST_KINDS; k++)
		hard[k] = easy[k];
}

#if BEAST_PARAMS_N != 8
#error "Jungle_NewGame copies Easy's eight beast parameters"
#endif

void Jungle_NewGame()
{
	// The tables as the ROM has them; in Easy, the beasts at Easy's pace (BeastRules.slower).
	Data_LoadTables();
	if (g_GameOn && g_Easy)
	{
		Easy((i16*)g_BeastP0, g_EasyBeastP0);
		Easy((i16*)g_BeastP1, g_EasyBeastP1);
		Easy((i16*)g_BeastP2, g_EasyBeastP2);
		Easy((i16*)g_BeastP3, g_EasyBeastP3);
		Easy((i16*)g_BeastP4, g_EasyBeastP4);
		Easy((i16*)g_BeastP5, g_EasyBeastP5);
		Easy((i16*)g_BeastP6, g_EasyBeastP6);
		Easy((i16*)g_BeastP7, g_EasyBeastP7);
	}
	for (u8 b = 0; b < BEASTS; b++)
	{
		u8 k = g_PlaceKind[b];
		SetKind(b, k);
		g_Beast[b].At = g_PlaceRoom[b];
		g_Beast[b].X = FIXED(g_PlaceX[b]);
		g_Beast[b].Y = FIXED(g_PlaceY[b]);
		i16 speed = g_BeastKind[k] == BEAST_RHINO ? P_PACE[k] : g_BeastKind[k] == BEAST_SLEEPER ? P_CHARGE[k] : 0;
		g_Beast[b].VX = g_PlaceHeading[b] < 0 ? -speed : g_PlaceHeading[b] > 0 ? speed : 0;
		g_Beast[b].VY = 0;
		g_BFace[b] = g_PlaceHeading[b] < 0 ? -1 : 1;
		g_BAsleep[b] = g_PlaceAsleep[b];
		g_BPause[b] = 0;
		g_BHint[b] = 0;
		g_Beast[b].MoveTicks = 0;
		g_Beast[b].Blocked[0] = 0xFF;   // no strip found blocked yet
		s_Role[b] = g_BeastKind[k];
	}
	for (u8 i = 0; i < CREATURE_POOL; i++)
		s_Used[i] = 0;
	g_HereN = 0;
	g_AwayN = 0;
	g_SwarmQuiet = 0;
	s_NextId = 0;
}

static i16 BeastCentreX(u8 b)
{
	u8 k = g_BKind[b];
	return FP_PX(g_Beast[b].X) + g_BeastBoxX[k] + g_BeastBoxW[k] / 2;
}

//-----------------------------------------------------------------------------
// A big beast's step (Simulation.stepBeast; here since M13, out of the main
// code, which had no room): x then y on the wrapping map,
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
static u8 s_BsFree = 0;     // its Free, during the step
static u8 s_BsMoved = 0;    // the axis being stepped moved
static i8 s_BsEdge = 0;  // the edge that leads: forwards the far one, backwards the near
static i16 s_BsStrip = 0;   // the strip it steps into, its first pixel along the axis
static u8 s_BsCross = 0;    // an axis's step took its place (not its edge) into another cell:
                          // the map's ends and the rooms' edges are cells' edges, so only
                          // then may it go round an end or into another room (M13)
static u8 s_BsOldH = 0;
static u16 s_BsT = 0;     // the axis's target's low word
static u8 s_BsAdj = 0;    // its high word's change: 0, 1 or -1     // the axis's pixel's low byte where it stood

#define MAP_W_HI	(GRID_W)                      // MAP_W_FP >> 16: a room is 256 px
#define MAP_H_HI	((GRID_H * PLAYFIELD_H) >> 8)  // MAP_H_FP >> 16
#if PLAYFIELD_W != 256 || ((GRID_H * PLAYFIELD_H) & 255) != 0
#error "Beast_Step takes rooms 256 px wide, and the map's height a whole number of 256 px"
#endif
#if PLAYFIELD_H != 176
#error "Beast_Step takes a row of rooms as 176 px (128 + 32 + 16)"
#endif

// The fast step (M13): free where it stands, each axis's leading edge in its
// cell, its pixel's low byte not round (so neither an end of the map nor
// another column of rooms), and its place, if into another cell, still in its
// row of rooms: the target, no test, as the full step would have it.
static u16 s_BsFX = 0;   // x's target's low word, till y is through
static u8 s_BsFOn = 0;   // bit 0: x to put; bit 1: y put
static u8 s_BsOCell = 0; // y's place into another cell

u8 Beast_Step() __naked
{
	__asm
		; the fast step: through hl, the beast's bytes in order (M13: through
		; ix it was 550 cycles); the full one takes ix
		ld	hl, (_g_BeastPtr)
		ld	de, #21
		add	hl, de
		ld	a, (hl)
		or	a
		jp	z, 00089$               ; not known free where it stands
		xor	a
		ld	(_s_BsFOn), a
		; x
		ld	hl, (_g_BeastPtr)
		ld	c, (hl)
		inc	hl
		ld	b, (hl)                 ; bc = its low word
		inc	hl
		inc	hl
		inc	hl
		ld	e, (hl)
		inc	hl
		ld	d, (hl)                 ; de = VX
		ld	a, d
		or	a, e
		jr	z, 00060$
		inc	hl
		ld	a, (hl)                 ; backwards, the near edge
		bit	7, d
		jr	nz, 00053$
		inc	hl
		inc	hl
		ld	a, (hl)                 ; forwards, the far one
	00053$:
		ld	l, c
		ld	h, b
		ld	c, a
		add	hl, de
		jr	c, 00051$
		bit	7, d
		jp	nz, 00089$              ; backwards, no carry: round below its low byte
		jr	00052$
	00051$:
		bit	7, d
		jp	z, 00089$               ; forwards, a carry: round past it
	00052$:
		ld	a, b
		add	a, c
		and	a, #0xF8
		ld	b, a
		ld	a, h
		add	a, c
		and	a, #0xF8
		cp	a, b
		jp	nz, 00089$              ; its leading edge into another cell
		ld	(_s_BsFX), hl
		ld	a, #1
		ld	(_s_BsFOn), a
	00060$:
		; y
		ld	hl, (_g_BeastPtr)
		ld	de, #9
		add	hl, de
		ld	c, (hl)
		inc	hl
		ld	b, (hl)
		inc	hl
		inc	hl
		inc	hl
		ld	e, (hl)
		inc	hl
		ld	d, (hl)                 ; de = VY
		ld	a, d
		or	a, e
		jp	z, 00070$
		inc	hl
		ld	a, (hl)
		bit	7, d
		jr	nz, 00063$
		inc	hl
		inc	hl
		ld	a, (hl)
	00063$:
		ld	l, c
		ld	h, b
		ld	c, a
		add	hl, de
		jr	c, 00061$
		bit	7, d
		jp	nz, 00094$
		jr	00062$
	00061$:
		bit	7, d
		jp	z, 00094$
	00062$:
		ld	a, b
		xor	a, h
		and	a, #0xF8
		ld	(_s_BsOCell), a
		ld	a, b
		add	a, c
		and	a, #0xF8
		ld	b, a
		ld	a, h
		add	a, c
		and	a, #0xF8
		cp	a, b
		jp	nz, 00094$
		ld	a, (_s_BsOCell)
		or	a
		jr	z, 00065$
		; its place into another cell: still in its row, top <= y < top + PLAYFIELD_H
		push	hl
		ld	e, h                    ; y's whole pixels, the low byte
		ld	hl, (_g_BeastPtr)
		ld	bc, #11
		add	hl, bc
		ld	d, (hl)                 ; and the high, as it was
		ld	bc, #7
		add	hl, bc
		ld	a, (hl)                 ; At
		rrca
		rrca
		rrca
		rrca
		and	a, #15
		ld	l, a
		ld	h, #0
		add	hl, hl
		add	hl, hl
		add	hl, hl
		add	hl, hl
		ld	b, h
		ld	c, l                    ; 16 rows
		add	hl, hl
		add	hl, hl
		add	hl, hl
		add	hl, bc
		sla	c
		rl	b
		add	hl, bc                  ; the row's top, row * 176
		ld	a, e
		sub	a, l
		ld	a, d
		sbc	a, h
		jr	c, 00066$               ; above it
		ld	bc, #PLAYFIELD_H
		add	hl, bc
		ld	a, e
		sub	a, l
		ld	a, d
		sbc	a, h
		jr	nc, 00066$              ; at or past the next row's top
		pop	hl
	00065$:
		ex	de, hl
		ld	hl, (_g_BeastPtr)
		ld	bc, #9
		add	hl, bc
		ld	(hl), e
		inc	hl
		ld	(hl), d
		ld	a, (_s_BsFOn)
		or	a, #2
		ld	(_s_BsFOn), a
	00070$:
		ld	a, (_s_BsFOn)
		or	a
		jr	z, 00089$               ; still: the full step's
		ld	hl, (_g_BeastPtr)
		rrca
		jr	nc, 00071$
		ld	de, (_s_BsFX)
		ld	(hl), e
		inc	hl
		ld	(hl), d
		dec	hl
	00071$:
		ld	de, #19
		add	hl, de
		inc	(hl)                    ; its walk on
		jr	nz, 00072$
		inc	hl
		inc	(hl)
	00072$:
		ld	a, #1
		ret
	00066$:
		pop	hl
	00094$:
		; y takes the full step: x, if it took the fast one, put first
		ld	a, (_s_BsFOn)
		rrca
		jr	nc, 00089$
		push	ix
		ld	ix, (_g_BeastPtr)
		ld	hl, (_s_BsFX)
		ld	0 (ix), l
		ld	1 (ix), h
		ld	a, #1
		ld	(_s_BsFree), a
		xor	a
		ld	(_s_BsCross), a
		ld	bc, #0x0100             ; x moved
		jp	00010$
	00089$:
		push	ix
		ld	ix, (_g_BeastPtr)
	00090$:
		; the full step
		ld	a, 21 (ix)
		ld	(_s_BsFree), a
		or	a
		call	z, 00200$               ; not known free where it stands: test it there, once
		xor	a
		ld	(_s_BsCross), a
		ld	bc, #0                  ; b: x moved, c: y moved
		; x, if it moves along x
		ld	a, 4 (ix)
		or	a, 5 (ix)
		jr	z, 00010$
		ld	a, #1
		ld	(_g_MoveAlongX), a
		dec	a
		ld	(_s_BsMoved), a
		call	00300$
		ld	a, (_s_BsMoved)
		ld	b, a
		ld	c, #0
	00010$:
		; y, if it moves along y: ix on its block
		ld	a, 13 (ix)
		or	a, 14 (ix)
		jr	z, 00015$
		ld	de, #9
		add	ix, de
		xor	a
		ld	(_g_MoveAlongX), a
		ld	(_s_BsMoved), a
		push	bc
		call	00300$
		pop	bc
		ld	de, #-9
		add	ix, de
		ld	a, (_s_BsMoved)
		ld	c, a
	00015$:
		ld	a, (_s_BsFree)
		ld	21 (ix), a
		ld	a, b
		or	a, c
		jp	nz, 00030$
		ld	19 (ix), a              ; still: its walk starts again
		ld	20 (ix), a
		pop	ix
		ret

	; ---- where it stands, tested once (it was set down in scenery, or not known)
	00200$:
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
		ld	a, #1
		ld	(_g_BoxWrap), a
		call	_Data_BoxTest
		xor	a, #1
		ld	(_s_BsFree), a
		ret

	; ---- hl = the other axis's block: ix + 9 along x, ix - 9 along y
	00210$:
		ld	de, #9
		ld	a, (_g_MoveAlongX)
		or	a
		jr	nz, 00211$
		ld	de, #-9
	00211$:
		push	ix
		pop	hl
		add	hl, de
		ret

	00030$:
		; its place still in its cells: no end of the map, no other room (s_BsCross)
		ld	a, (_s_BsCross)
		or	a
		jp	z, 00046$
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
		; hl = the row's top, row * PLAYFIELD_H (176: 128 + 32 + 16)
		ld	a, c
		ld	l, c
		ld	h, #0
		add	hl, hl
		add	hl, hl
		add	hl, hl
		add	hl, hl
		ld	b, h
		ld	c, l                    ; bc = 16 rows
		add	hl, hl
		add	hl, hl
		add	hl, hl
		add	hl, bc
		sla	c
		rl	b
		add	hl, bc
		ld	c, a
		ld	a, e
		sub	a, l
		ld	a, d
		sbc	a, h
		jr	nc, 00042$
		dec	c                       ; above its top: up a row
		jr	00040$
	00042$:
		ld	a, l
		add	a, #PLAYFIELD_H
		ld	l, a
		jr	nc, 00043$
		inc	h                       ; the next row's top
	00043$:
		ld	a, e
		sub	a, l
		ld	a, d
		sbc	a, h
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
	; size 7, far edge 8; the other's at ix + 9 or - 9 (00210$)
	00300$:
		ld	a, (_s_BsFree)
		or	a
		jp	z, 00380$               ; not free where it stands: the search
		ld	e, 4 (ix)
		ld	d, 5 (ix)               ; de: the delta
		ld	l, 0 (ix)
		ld	h, 1 (ix)               ; hl: the position's low word
		; the leading edge (forwards the far one), and its cell where it stands
		ld	a, d
		or	a
		ld	a, 8 (ix)
		jp	p, 00305$
		ld	a, 6 (ix)
	00305$:
		ld	(_s_BsEdge), a
		ld	c, a
		add	a, h
		and	a, #0xF8
		ld	b, a                    ; its cell's bits in the pixel's low byte
		ld	a, h
		ld	(_s_BsOldH), a
		; the target, kept till it is known free (M13: written, then taken
		; back when blocked): the low word, and the high one's change, a
		; carry out not the delta's sign's (forwards a carry, backwards none)
		add	hl, de
		ld	(_s_BsT), hl
		jr	c, 00306$
		bit	7, d
		ld	a, #0
		jr	z, 00307$
		dec	a                       ; backwards, no carry: the high word less one
		jr	00307$
	00306$:
		bit	7, d
		ld	a, #0
		jr	nz, 00307$
		inc	a                       ; forwards, a carry: the high word one more
	00307$:
		ld	(_s_BsAdj), a
	00308$:
		ld	a, h
		add	a, c
		and	a, #0xF8
		cp	a, b
		jr	nz, 00310$
		; the same cell: there; its place into another cell or not
		call	00365$
		ld	a, (_s_BsOldH)
		xor	a, h
		and	a, #0xF8
		jp	z, 00360$
		ld	a, #1
		ld	(_s_BsCross), a
		jp	00360$
	00310$:
		ld	a, #1
		ld	(_s_BsCross), a
		; a new strip: its first pixel, (target + edge) rounded down to a cell
		ld	a, (_s_BsAdj)
		add	a, 2 (ix)
		ld	d, a
		ld	e, h                    ; de = the target's whole pixels
		ld	a, (_s_BsEdge)
		ld	l, a
		rla
		sbc	a, a
		ld	h, a
		add	hl, de
		ld	a, l
		and	a, #0xF8
		ld	l, a
		ld	(_s_BsStrip), hl
		call	00400$
		or	a
		jr	nz, 00320$
		call	00365$                  ; free: there
		jp	00360$
	00320$:
		; blocked: from where it stands, the candidate next to the strip
		ld	a, (_s_BsEdge)
		ld	e, a
		rla
		sbc	a, a
		ld	d, a
		ld	hl, (_s_BsStrip)
		bit	7, 5 (ix)
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
		jr	00370$
	; ---- the target into the position (hl kept: the target's low word)
	00365$:
		ld	hl, (_s_BsT)
		ld	0 (ix), l
		ld	1 (ix), h
		ld	a, (_s_BsAdj)
		or	a
		ret	z
		inc	a
		jr	z, 00366$
		inc	2 (ix)                  ; one more
		ret	nz
		inc	3 (ix)
		ret
	00366$:
		ld	a, 2 (ix)               ; one less
		sub	a, #1
		ld	2 (ix), a
		ret	nc
		dec	3 (ix)
		ret
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
		ld	(_s_BsMoved), a
		ret
	00380$:
		; the search, a test a candidate (Move_Axis); where it ends is free if it moved
		ld	a, #1
		ld	(_s_BsCross), a
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
		call	00210$
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
		ld	(_s_BsFree), a
		ld	(_s_BsMoved), a
		ret

	; ---- the strip at s_BsStrip along the axis, the box's breadth across: a = 1
	; blocked. Read off the map's bitmap (M13: a strip remembered, and its
	; key, cost more than the map's bits cost now, Data_StripTest)
	00400$:
		call	00210$
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
		ld	a, (_g_MoveAlongX)
		or	a
		jr	z, 00410$
		ld	(_g_BoxTop), hl
		ld	a, c
		ld	(_g_BoxH), a
		ld	hl, (_s_BsStrip)
		ld	(_g_BoxLeft), hl
		ld	a, #1
		jp	_Data_StripTest
	00410$:
		ld	(_g_BoxLeft), hl
		ld	a, c
		ld	(_g_BoxW), a
		ld	hl, (_s_BsStrip)
		ld	(_g_BoxTop), hl
		xor	a
		jp	_Data_StripTest
	__endasm;
}

// x then y against the scenery, room to room, the map's ends joined. Its
// room follows it by comparison: it moves a few pixels a tick at most.
// Each beast's struct, without a multiplication.
static Beast* const c_BeastPtr[] = {
	&g_Beast[0], &g_Beast[1], &g_Beast[2], &g_Beast[3], &g_Beast[4], &g_Beast[5], &g_Beast[6], &g_Beast[7]
};
#if BEASTS != 8
#error "c_BeastPtr has 8 beasts"
#endif

static bool StepBeast(u8 b)
{
	g_BeastPtr = c_BeastPtr[b];
	return Beast_Step();
}

// Its kind's box, into the struct Beast_Step reads.
static void SetKind(u8 b, u8 k)
{
	g_BKind[b] = k;
	g_Beast[b].BoxX = g_BeastBoxX[k];
	g_Beast[b].BoxY = g_BeastBoxY[k];
	g_Beast[b].BoxW = g_BeastBoxW[k];
	g_Beast[b].BoxH = g_BeastBoxH[k];
	g_Beast[b].FarX = g_BeastBoxX[k] + g_BeastBoxW[k] - 1;
	g_Beast[b].FarY = g_BeastBoxY[k] + g_BeastBoxH[k] - 1;
	g_Beast[b].Free = 0;   // another box: not known free
}

#if PLAYFIELD_W != 256 || GRID_W != 16
#error "StepBeast takes a room 256 px wide and the map 16 rooms wide"
#endif

static void TurnAbout(u8 b)
{
	g_Beast[b].VX = -g_Beast[b].VX;
	g_BFace[b] = -g_BFace[b];
}

static void Shove(u8 b, i16 speed)
{
	i8 dx = g_ValeFacing == FACE_N || g_ValeFacing == FACE_S ? 0 : g_ValeFacing >= FACE_SW ? -1 : 1;
	i8 dir = dx != 0 ? dx : BeastCentreX(b) < ValeCentreX() ? -1 : 1;
	g_Beast[b].VX = dir < 0 ? -speed : speed;
	g_BFace[b] = dir;
	Sound_Play(SFX_BEAST_SHOVE);
}

static bool OverlapsVale(u8 b)
{
	if (g_Beast[b].At != g_ValeRoom)
		return FALSE;
	Beast* p = &g_Beast[b];
	ValeBox();
	s_BX = FP_PX(p->X) + p->BoxX;
	s_BY = FP_PX(p->Y) + p->BoxY;
	s_BW = p->BoxW;
	s_BH = p->BoxH;
	return Overlaps();
}

static u8 TouchBeast(u8 b)
{
	if (g_Beast[b].At != g_ValeRoom || g_ValeMode != MODE_ALIVE || g_ValeInvuln > 0
		|| (g_GameOn && (g_EffectKind == EFFECT_KNOCKDOWN || EFFECT_IMMUNE())) || !OverlapsVale(b))
		return TOUCH_NONE;
	u8 k = g_BKind[b];
	i16 cy = FP_PX(g_Beast[b].Y) + g_BeastBoxY[k] + g_BeastBoxH[k] / 2;
	return g_ValeSwing >= 0 && InFront(BeastCentreX(b), cy) ? TOUCH_STRUCK : TOUCH_HIT;
}

// Two rows or more from him, set down a row from him, at the room's spot.
static bool Follow(u8 b)
{
	u8 at = g_Beast[b].At;
	u8 room = g_ValeRoom;
	i8 dr = (i8)(at / GRID_W) - (i8)(room / GRID_W);
	if (dr < BEAST_FOLLOW_ROWS && dr > -BEAST_FOLLOW_ROWS)
		return FALSE;
	i8 row = (i8)(room / GRID_W) + (Rng_Next(2) == 0 ? -1 : 1);
	if (row < 0 || row >= GRID_H)
		row = room / GRID_W;
	u8 col = room % GRID_W;
	col = col - col % BEAST_FOLLOW_COLUMNS + (at % GRID_W) % BEAST_FOLLOW_COLUMNS;
	u8 to = (u8)row * GRID_W + col;
	if (to == room)
		return FALSE;
	if (g_BeastKind[g_BKind[b]] == BEAST_SLEEPER)
		SetKind(b, g_Sleepers[Rng_Next(SLEEPER_KINDS)]);
	g_Beast[b].At = to;
	g_Beast[b].Free = 0;   // set down somewhere else
	Data_RoomSpot(to);
	g_Beast[b].X = FIXED((i16)col * PLAYFIELD_W + g_SpotX + g_BeastHalfW[g_BKind[b]]);
	g_Beast[b].Y = FIXED((i16)row * PLAYFIELD_H + g_SpotY);
	g_Beast[b].MoveTicks = 0;
	return TRUE;
}

static i16 Walk(u8 b)
{
	u8 k = g_BKind[b];
	i16 speed = Rng_Next(2) == 0 ? P_WALK_SLOW[k] : P_WALK_FAST[k];
	return Rng_Next(2) == 0 ? -speed : speed;
}

// The beast being ticked, and whether its step moved it. Jungle_TickBeasts's
// loop, in assembly, takes the usual tick itself, a step and nothing more,
// and calls these for the rest; in C the loop cost 700 to 1 500 cycles a beast.
static u8 s_Bi;
static u8 s_BMoved;

// A rhino after its step: blocked, it turns about; and his touch.
void Jungle_Rhino()
{
	u8 b = s_Bi;
	u8 k = g_BKind[b];
	if (!s_BMoved)
	{
		g_Beast[b].VX = g_Beast[b].VX < 0 ? -P_PACE[k] : P_PACE[k];
		TurnAbout(b);
	}
	switch (TouchBeast(b))
	{
	case TOUCH_STRUCK:
		Shove(b, P_SHOVED[k]);
		break;
	case TOUCH_HIT:
		Vale_Kill();
		TurnAbout(b);
		break;
	}
}

// levelWithAQuarter: on the map column of a quarter still lying and level
// with it across the room, or on its map row and level with it up and down.
static bool LevelWithAQuarter(u8 b)
{
	u8 at = g_Beast[b].At;
	i16 bx = FP_PX(g_Beast[b].X) - (i16)(at % GRID_W) * PLAYFIELD_W;
	i16 by = FP_PX(g_Beast[b].Y) - (i16)(at / GRID_W) * PLAYFIELD_H;
	i16 hint = P_HINT[g_BKind[b]];
	u8 k = g_QuestSet * LAIRS;
	u8 done = Quest_Done();
	for (u8 i = 0; i < LAIRS; i++, k++, done >>= 1)
	{
		if (done & 1)
			continue;
		u8 room = g_LairRoom[k];
		i16 dx = g_LairPedX[k] - bx;
		i16 dy = g_LairPedY[k] - by;
		if ((room % GRID_W == at % GRID_W && (dx < 0 ? -dx : dx) < hint)
			|| (room / GRID_W == at / GRID_W && (dy < 0 ? -dy : dy) < hint))
			return TRUE;
	}
	return FALSE;
}

// A tribesman after his step: still, he sets off another way; and his touch.
void Jungle_TribesmanAfter()
{
	u8 b = s_Bi;
	u8 k = g_BKind[b];
	if (!s_BMoved)
	{
		g_Beast[b].VX = Walk(b);
		g_Beast[b].VY = Walk(b);
		g_BFace[b] = g_Beast[b].VX < 0 ? -1 : 1;
	}
	u8 t = TouchBeast(b);
	if (t != TOUCH_NONE)
	{
		Shove(b, P_SHOVED[k]);
		g_Beast[b].VY = Walk(b);
		if (t == TOUCH_HIT)
			Vale_Kill();
	}
	// Level with a quarter still lying, he lights up and calls (the call is M9's).
	bool hinting = g_QuestOn && g_Beast[b].At == g_ValeRoom && LevelWithAQuarter(b);
	if (hinting && (!g_BHint[b] || ((u16)g_SimTick & (P_CALL[k] - 1)) == 0))
		Sound_Play(SFX_TRIBESMAN_CALL);
	g_BHint[b] = hinting;
}

// A tribesman's whole tick: following him, the step, the rest.
void Jungle_Tribesman()
{
	u8 b = s_Bi;
	Follow(b);
	s_BMoved = (g_Beast[b].VX != 0 || g_Beast[b].VY != 0) && StepBeast(b);
	Jungle_TribesmanAfter();
}

// The hippo's or warthog's whole tick.
void Jungle_Sleeper()
{
	u8 b = s_Bi;
	if (g_BAsleep[b])
	{
		Follow(b);
		if (g_Beast[b].At != g_ValeRoom)
			return;
		u8 t = TouchBeast(b);
		// With Vale not on his feet, any touch at all wakes it.
		bool bumped = t != TOUCH_NONE || (g_ValeMode != MODE_ALIVE && OverlapsVale(b));
		if (bumped)
		{
			g_BAsleep[b] = 0;
			TurnAbout(b);
			g_BPause[b] = (u8)P_WAKE[g_BKind[b]];
			Sound_Play(SFX_BEAST_SHOVE);
			if (t == TOUCH_HIT)
				Vale_Kill();
		}
		return;
	}
	if (g_BPause[b] > 0)
	{
		g_BPause[b]--;
		g_Beast[b].MoveTicks = 0;
		if (TouchBeast(b) == TOUCH_HIT)
			Vale_Kill();
		return;
	}
	if (!StepBeast(b))
	{
		g_BAsleep[b] = 1;   // where it stops, it sleeps
		g_Beast[b].MoveTicks = 0;
		return;
	}
	switch (TouchBeast(b))
	{
	case TOUCH_STRUCK:
		Shove(b, P_SHOVED[g_BKind[b]]);
		break;
	case TOUCH_HIT:
		Vale_Kill();
		TurnAbout(b);
		break;
	}
}

// Every beast in turn. The usual tick: a rhino steps and is not blocked, a
// tribesman with no one to follow steps and is not still, the sleeper sleeps
// out of his room; and none is in his room, so none touches him. Follow is a
// no-op within BEAST_FOLLOW_ROWS - 1 rows of his.
#if BEAST_RHINO != 0 || BEAST_TRIBESMAN != 1 || GRID_W != 16
#error "Jungle_TickBeasts takes the roles 0, 1 and the rest, and a room as row * 16 + column"
#endif
typedef char c_BeastSize[sizeof(Beast) == BEAST_SIZE ? 1 : -1];

void Jungle_TickBeasts() __naked
{
	__asm
		ld	hl, #_g_Beast
		ld	(_g_BeastPtr), hl
		xor	a
		ld	(_s_Bi), a
	00510$:
		ld	a, (_s_Bi)
		ld	e, a
		ld	d, #0
		ld	hl, #_s_Role
		add	hl, de
		ld	a, (hl)
		or	a
		jr	z, 00520$               ; a rhino
		dec	a
		jr	z, 00530$               ; a tribesman
		; the sleeper: asleep, near him and out of his room, it sleeps on
		ld	hl, #_g_BAsleep
		add	hl, de
		ld	a, (hl)
		or	a
		jr	z, 00515$
		call	00600$
		jr	c, 00515$
		call	00610$
		jr	nz, 00590$
	00515$:
		call	_Jungle_Sleeper
		jr	00590$
	00520$:
		; a rhino: the step; then, blocked or in his room, the rest
		call	_Beast_Step
		ld	(_s_BMoved), a
		or	a
		jr	z, 00525$
		call	00610$
		jr	nz, 00590$
	00525$:
		call	_Jungle_Rhino
		jr	00590$
	00530$:
		; a tribesman: near him and on his way, the step; then, still or in his room, the rest
		call	00600$
		jr	c, 00535$
		ld	hl, (_g_BeastPtr)
		ld	de, #4
		add	hl, de
		ld	a, (hl)                 ; VX
		inc	hl
		or	a, (hl)
		ld	de, #8
		add	hl, de
		or	a, (hl)                 ; VY
		inc	hl
		or	a, (hl)
		jr	z, 00535$
		call	_Beast_Step
		ld	(_s_BMoved), a
		or	a
		jr	z, 00537$
		call	00610$
		jr	nz, 00590$
	00537$:
		call	_Jungle_TribesmanAfter
		jr	00590$
	00535$:
		call	_Jungle_Tribesman
	00590$:
		ld	hl, (_g_BeastPtr)
		ld	de, #BEAST_SIZE
		add	hl, de
		ld	(_g_BeastPtr), hl
		ld	hl, #_s_Bi
		inc	(hl)
		ld	a, (hl)
		cp	a, #BEASTS
		jr	c, 00510$
		ret

	; ---- carry if it is BEAST_FOLLOW_ROWS rows or more from his
	00600$:
		ld	hl, (_g_BeastPtr)
		ld	de, #18
		add	hl, de
		ld	a, (hl)
		rrca
		rrca
		rrca
		rrca
		and	a, #15
		ld	b, a
		ld	a, (_g_ValeRoom)
		rrca
		rrca
		rrca
		rrca
		and	a, #15
		sub	a, b
		jp	p, 00601$
		neg
	00601$:
		cp	a, #BEAST_FOLLOW_ROWS
		ccf
		ret

	; ---- z if it is in his room
	00610$:
		ld	hl, (_g_BeastPtr)
		ld	de, #18
		add	hl, de
		ld	a, (_g_ValeRoom)
		cp	a, (hl)
		ret
	__endasm;
}

//-----------------------------------------------------------------------------
// Simulation.resolveCombat: the blade, then his touch

// The next creature of g_Here from s_TcK the box at s_AX.. touches, in
// assembly (M13: the C indexed each array a creature at a time): with a set,
// the blade's (alive or forming, not hit by this swing yet, one it can hurt);
// else his (alive). Its index, or 0xFF.
static u8 s_HitSabre;
static u8 NextHit(u8 sabre) __naked
{
	sabre;   // in a
	__asm
		ld	(_s_HitSabre), a
	00010$:
		ld	a, (_s_TcK)
		ld	hl, #_g_HereN
		cp	a, (hl)
		jp	nc, 00090$
		inc	a
		ld	(_s_TcK), a
		dec	a
		ld	e, a
		ld	d, #0
		ld	hl, #_g_Here
		add	hl, de
		ld	e, (hl)                 ; de = i
		ld	a, e
		ld	(_s_TcI), a
		ld	hl, #_g_CMode
		add	hl, de
		ld	c, (hl)
		ld	a, (_s_HitSabre)
		or	a
		jr	z, 00020$
		ld	a, c
		cp	a, #CM_DYING
		jr	z, 00010$
		ld	hl, #_s_LastHit
		add	hl, de
		add	hl, de
		ld	a, (_g_SwingSerial)
		cp	a, (hl)
		jr	nz, 00015$
		inc	hl
		ld	a, (_g_SwingSerial + 1)
		cp	a, (hl)
		jr	z, 00010$               ; this swing has hit it
	00015$:
		ld	hl, #_g_CSpecies
		add	hl, de
		ld	c, (hl)
		ld	b, #0
		ld	hl, #g_CreatureHp_AT
		add	hl, bc
		ld	a, (hl)
		or	a
		jr	z, 00010$               ; no blade hurts it
		jr	00030$
	00020$:
		ld	a, c
		or	a
		jr	nz, 00010$              ; not alive
	00030$:
		ld	a, (_s_TcI)
		call	_CreatureBox
		call	_Overlaps
		or	a
		jp	z, 00010$
		ld	a, (_s_TcI)
		ret
	00090$:
		ld	a, #0xFF
		ret
	__endasm;
}

void Jungle_Combat()
{
	u8 i;
	if (g_SabreW != 0)
	{
		s_AX = g_SabreX;
		s_AY = g_SabreY;
		s_AW = g_SabreW;
		s_AH = g_SabreH;
		s_TcK = 0;
		while ((i = NextHit(1)) != 0xFF)
		{
			s_LastHit[i] = g_SwingSerial;
			if (--g_CHp[i] == 0)
				Burst(i);
			else
				g_CHurt[i] = CREATURE_HURT_FLASH_TICKS;
		}
		if (g_QuestOn)
			QUEST(Quest_Parry);   // the original's Wulf is not pushed; the guardian is
	}
	if (g_ValeMode != MODE_ALIVE || g_ValeInvuln > 0)
		return;
	if (g_QuestOn)
	{
		QUEST(Quest_Touch);   // the guardian, the Keeper, the Wulf
		if (g_QuestHit)
		{
			Vale_Kill();
			return;
		}
	}
	if (g_GameOn && EFFECT_IMMUNE())
		return;   // the flower's: no creature can touch him
	ValeBox();
	s_TcK = 0;
	while ((i = NextHit(0)) != 0xFF)
	{
		if (g_CSpecies[i] == FLAME_SPECIES)
		{
			Vale_Kill();   // the flame: no blade can touch it
			return;
		}
		// A small creature that touches him bursts either way. Fencing and
		// facing it, that is all; otherwise it takes him with it.
		Burst(i);
		if (g_ValeSwing >= 0 && InFront(s_BX + s_BW / 2, s_BY + s_BH / 2))
			continue;
		Vale_Kill();
		return;
	}
}
