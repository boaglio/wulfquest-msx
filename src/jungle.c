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

#define ONE			(1 << FIXED_SHIFT)
#define FIXED(px)	((i32)(px) << FIXED_SHIFT)
#define MAP_W_FP	((i32)GRID_W * PLAYFIELD_W * ONE)
#define MAP_H_FP	((i32)GRID_H * PLAYFIELD_H * ONE)
#if SWARM_SLOTS > CREATURE_POOL   // a creature comes only while here and away are fewer than SWARM_SLOTS
#error "the creature pool is smaller than the swarm can grow"
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

// The amulet's quarters held (M7); the swarm's kinds open with them.
static u8 s_Quarters = 0;

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

static bool Overlaps()
{
	return s_AX < s_BX + s_BW && s_BX < s_AX + s_AW && s_AY < s_BY + s_BH && s_BY < s_AY + s_AH;
}

// Box A: Vale's.
static void ValeBox()
{
	s_AX = FP_PX(g_ValeX) + PLAYER_BOX_X;
	s_AY = FP_PX(g_ValeY) + PLAYER_BOX_Y;
	s_AW = PLAYER_BOX_W;
	s_AH = PLAYER_BOX_H;
}

// Box B: small creature i's.
static void CreatureBox(u8 i)
{
	u8 s = g_CSpecies[i];
	s_BX = FP_PX(g_CX[i]) + g_CreatureBoxX[s];
	s_BY = FP_PX(g_CY[i]) + g_CreatureBoxY[s];
	s_BW = g_CreatureBoxW[s];
	s_BH = g_CreatureBoxH[s];
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
static i32 s_T;

static void Load(u8 i)
{
	u8 s = g_CSpecies[i];
	s_I = i;
	s_S = s;
	s_CX = g_CX[i];
	s_CY = g_CY[i];
	s_Ph = g_CPhase[i];
	s_Aux = g_CAux[i];
	s_Timer = g_CTimer[i];
	s_Face = g_CFace[i];
	s_BoxX = g_CreatureBoxX[s];
	s_BoxY = g_CreatureBoxY[s];
	s_BoxW = g_CreatureBoxW[s];
	s_BoxH = g_CreatureBoxH[s];
	s_Moved = FALSE;
}

static void Store()
{
	u8 i = s_I;
	g_CX[i] = s_CX;
	g_CY[i] = s_CY;
	g_CPhase[i] = s_Ph;
	g_CAux[i] = s_Aux;
	g_CTimer[i] = s_Timer;
	g_CFace[i] = s_Face;
}

// SimContext.move: x then y, each resolved on its own (move.c).
static void MoveCreature()
{
	g_MoveWorld = MOVE_ROOM;
	g_MoveScenery = !g_CreatureFlier[s_S];
	if (s_Ph != 0)
	{
		g_MovePos = s_CX;
		g_MoveDelta = s_Ph;
		g_MoveOther = FP_PX(s_CY);
		g_MoveOff = s_BoxX;
		g_MoveSize = s_BoxW;
		g_MoveOffO = s_BoxY;
		g_MoveSizeO = s_BoxH;
		g_MoveAlongX = 1;
		Move_Axis();
		if (g_MovePos != s_CX)
		{
			s_CX = g_MovePos;
			s_Moved = TRUE;
		}
	}
	if (s_Aux != 0)
	{
		g_MovePos = s_CY;
		g_MoveDelta = s_Aux;
		g_MoveOther = FP_PX(s_CX);
		g_MoveOff = s_BoxY;
		g_MoveSize = s_BoxH;
		g_MoveOffO = s_BoxX;
		g_MoveSizeO = s_BoxW;
		g_MoveAlongX = 0;
		Move_Axis();
		if (g_MovePos != s_CY)
		{
			s_CY = g_MovePos;
			s_Moved = TRUE;
		}
	}
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
	i16 v = P_SPEED_MIN[s] + P_SPEED_STEP[s] * (i16)Rng_Next((u16)P_SPEED_STEPS[s]);
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
static void Glance()
{
	s_T = s_CX + s_Ph;
	i16 left = FP_PX(s_T) + s_BoxX - g_RoomX;
	if (left < 0 || left + s_BoxW > PLAYFIELD_W)
	{
		s_Ph = -s_Ph;
		i8 f = Sign(s_Ph);
		if (f != 0)
			s_Face = f;
	}
	s_T = s_CY + s_Aux;
	i16 top = FP_PX(s_T) + s_BoxY - g_RoomY;
	if (top < 0 || top + s_BoxH > PLAYFIELD_H)
		s_Aux = -s_Aux;
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

static void Burst(u8 i)
{
	g_CMode[i] = CREATURE_DYING;
	g_CModeTick[i] = 0;
}

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

void Jungle_TickCreatures()
{
	bool down = g_ValeMode != MODE_ALIVE;
	for (u8 k = 0; k < g_HereN; k++)
	{
		u8 i = g_Here[k];
		if (g_CMode[i] == CREATURE_DYING)
		{
			g_CModeTick[i]++;
			continue;
		}
		if (g_CMode[i] == CREATURE_APPEARING)
		{
			if (++g_CModeTick[i] >= SWARM_APPEAR_TICKS)
			{
				g_CMode[i] = CREATURE_ALIVE;
				g_CModeTick[i] = 0;
				if (g_CreatureBehaviour[g_CSpecies[i]] == BEHAVIOUR_DART)
				{
					Load(i);
					SetOff();   // on its way the moment it forms
					Store();
				}
			}
			continue;
		}
		if (down)
		{
			Burst(i);   // with him down, every small creature in the room bursts
			continue;
		}
		if (g_CHurt[i] > 0)
			g_CHurt[i]--;
		Load(i);
		if (g_CreatureBehaviour[s_S] == BEHAVIOUR_DART)
			Dart();
		else
			HomeIn();
		Store();
		g_CMoveTicks[i] = s_Moved ? g_CMoveTicks[i] + 1 : 0;
	}
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
	if ((u16)g_SimTick % SWARM_EVERY_TICKS != 0)
		return;
#if (65536 % SWARM_EVERY_TICKS) != 0
#error "the swarm's tick test takes the game tick's low 16 bits"
#endif
	if (g_HereN >= SWARM_ROOM_MAX || g_HereN + g_AwayN >= SWARM_SLOTS)
		return;
	i16 lx = SWARM_AREA_X + (i16)Rng_Next(SWARM_AREA_W);
	i16 ly = SWARM_AREA_Y + (i16)Rng_Next(SWARM_AREA_H);
	u8 roll = (u8)Rng_Next(SWARM_PICKS);
	u8 index = g_SwarmPick[roll < SWARM_PICK_OPEN + SWARM_PICK_OPEN_PER_QUARTER * s_Quarters ? roll
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

static void Repopulate()
{
	// Those here wait where they are; the puffs are done with.
	for (u8 k = 0; k < g_HereN; k++)
	{
		u8 i = g_Here[k];
		if (g_CMode[i] != CREATURE_DYING)
			g_Away[g_AwayN++] = i;
		else
			s_Used[i] = 0;
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

enum { TOUCH_NONE, TOUCH_STRUCK, TOUCH_HIT };

void Jungle_NewGame()
{
	for (u8 b = 0; b < BEASTS; b++)
	{
		u8 k = g_PlaceKind[b];
		SetKind(b, k);
		g_Beast[b].At = g_PlaceRoom[b];
		g_Beast[b].X = FIXED(g_PlaceX[b]);
		g_Beast[b].Y = FIXED(g_PlaceY[b]);
		i16 speed = g_BeastKind[k] == BEAST_RHINO ? P_PACE[k] : g_BeastKind[k] == BEAST_SLEEPER ? P_CHARGE[k] : 0;
		g_Beast[b].VX = g_PlaceHeading[b] * speed;
		g_Beast[b].VY = 0;
		g_BFace[b] = g_PlaceHeading[b] < 0 ? -1 : 1;
		g_BAsleep[b] = g_PlaceAsleep[b];
		g_BPause[b] = 0;
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
	s_Quarters = 0;
}

static i16 BeastCentreX(u8 b)
{
	u8 k = g_BKind[b];
	return FP_PX(g_Beast[b].X) + g_BeastBoxX[k] + g_BeastBoxW[k] / 2;
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
	g_Beast[b].VX = dir * speed;
	g_BFace[b] = dir;
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
	if (g_Beast[b].At != g_ValeRoom || g_ValeMode != MODE_ALIVE || g_ValeInvuln > 0 || !OverlapsVale(b))
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
	g_Beast[b].X = FIXED((i16)col * PLAYFIELD_W + g_SpotX[to] + g_BeastHalfW[g_BKind[b]]);
	g_Beast[b].Y = FIXED((i16)row * PLAYFIELD_H + g_SpotY[to]);
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
		g_Beast[b].VX = (g_Beast[b].VX < 0 ? -1 : 1) * P_PACE[k];
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
	// Level with a quarter, he calls: the quest's (M7).
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

void Jungle_Combat()
{
	if (g_SabreW != 0)
	{
		s_AX = g_SabreX;
		s_AY = g_SabreY;
		s_AW = g_SabreW;
		s_AH = g_SabreH;
		for (u8 k = 0; k < g_HereN; k++)
		{
			u8 i = g_Here[k];
			bool struck = g_CMode[i] == CREATURE_ALIVE || g_CMode[i] == CREATURE_APPEARING;
			if (!struck || s_LastHit[i] == g_SwingSerial || g_CreatureHp[g_CSpecies[i]] == 0)
				continue;
			CreatureBox(i);
			if (Overlaps())
			{
				s_LastHit[i] = g_SwingSerial;
				if (--g_CHp[i] == 0)
					Burst(i);
				else
					g_CHurt[i] = CREATURE_HURT_FLASH_TICKS;
			}
		}
	}
	if (g_ValeMode != MODE_ALIVE || g_ValeInvuln > 0)
		return;
	ValeBox();
	for (u8 k = 0; k < g_HereN; k++)
	{
		u8 i = g_Here[k];
		if (g_CMode[i] != CREATURE_ALIVE)
			continue;
		CreatureBox(i);
		if (Overlaps())
		{
			// A small creature that touches him bursts either way. Fencing and
			// facing it, that is all; otherwise it takes him with it.
			Burst(i);
			if (g_ValeSwing >= 0 && InFront(s_BX + s_BW / 2, s_BY + s_BH / 2))
				continue;
			Vale_Kill();
			return;
		}
	}
}
