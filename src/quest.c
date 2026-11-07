// Wulf Quest for MSX2: the quest (AGENTS.md §10, M7; the Java game's §14):
// the amulet hidden in one of the eight sets, each lair's guardian circling
// its quarter, the quarters taken, the Keeper of the Arch, the way out.
//
// Banked code (wulfquest_s26_b1.c includes it): it runs with its ROM segment
// in bank 1, so it calls only code below 8000h (data.c, rng.c, move.c; build.sh
// checks), keeps its tables in RAM (tables.h), and divides and multiplies
// without SDCC's library. Left for their milestones: the score (M8), the
// items and treasures and shrines (M8), the reveal and its verse (M10).

#include "quest.h"
#include "vale.h"
#include "rng.h"
#include "move.h"
#include "data.h"
#include "constants.h"
#include "quest_data.h"
#include "tables.h"
#include "game.h"
#include "game_data.h"

#if KEEPER_NUDGE_ZONE_PX > 0
#error "the Keeper's nudge (resolveCombat, a brush from the front) is not in the ROM: its zone is 0"
#endif
#if (KEEPER_ASIDE_MAX_PX + 1) * 256 + KEEPER_STEP_ASIDE_FP > 65535
#error "the Keeper's place is a room-local 8.8 word"
#endif

u8 g_QuestOn = 0;
u8 g_QuestSet = 0;
u8 g_QuestTaken = 0;
u8 g_QuestSlots = 0;
u8 g_QuestPieces = 0;
i8 g_QuestLair = -1;
u8 g_QuestFlash = 0;
u8 g_QuestFly = 0;
u8 g_QuestFlyMask = 0;
i16 g_QuestFlyX = 0;
i16 g_QuestFlyY = 0;
u8 g_QuestHit = 0;

i32 g_GuardX = 0;
i32 g_GuardY = 0;
i32 g_GuardAX = 0;
i32 g_GuardAY = 0;
i16 g_GuardPhase = 0;
i16 g_GuardTimer = 0;
i16 g_GuardAux = 0;
i8  g_GuardDirX = 1;
i8  g_GuardDirY = 0;
i8  g_GuardFace = 1;
u8  g_GuardHurt = 0;
u16 g_GuardLastHit = 0xFFFF;
u16 g_GuardMoveTicks = 0;
u8  g_GuardRadius = 0;

u8  g_KeeperHere = 0;
u8  g_KeeperState = KEEPER_BLOCKING;
u8  g_KeeperSeated = 0;
u16 g_KeeperLX = 0;
i32 g_KeeperX = 0;
i32 g_KeeperY = 0;
u16 g_KeeperMoveTicks = 0;

static u8 s_Stream[8] = { AMULET_STREAM_BYTES };

static void Wulf_Place();
static void Wulf_Head(i8 heading);

//-----------------------------------------------------------------------------
// Small helpers

// Whether box A overlaps box B (as statics: SDCC passes eight arguments on
// the stack, slowly).
static i16 s_AX, s_AY, s_AW, s_AH, s_BX, s_BY, s_BW, s_BH;

static bool Overlaps()
{
	return s_AX < s_BX + s_BW && s_BX < s_AX + s_AW && s_AY < s_BY + s_BH && s_BY < s_AY + s_AH;
}

static void ValeBox()
{
	s_AX = FP_PX(g_ValeX) + PLAYER_BOX_X;
	s_AY = FP_PX(g_ValeY) + PLAYER_BOX_Y;
	s_AW = PLAYER_BOX_W;
	s_AH = PLAYER_BOX_H;
}

static i16 ValeCentreX()
{
	return FP_PX(g_ValeX) + (PLAYER_BOX_X + PLAYER_BOX_W / 2);
}

static i16 ValeCentreY()
{
	return FP_PX(g_ValeY) + (PLAYER_BOX_Y + PLAYER_BOX_H / 2);
}

static i8 SignOf(i16 v)
{
	return v > 0 ? 1 : v < 0 ? -1 : 0;
}

static i16 AbsOf(i16 v)
{
	return v < 0 ? -v : v;
}

// The facing's x: Direction8.dx().
static i8 FacingDX()
{
	return g_ValeFacing == FACE_N || g_ValeFacing == FACE_S ? 0 : g_ValeFacing >= FACE_SW ? -1 : 1;
}

static i8 FacingDY()
{
	return g_ValeFacing == FACE_E || g_ValeFacing == FACE_W ? 0
		: g_ValeFacing >= FACE_SE && g_ValeFacing <= FACE_SW ? 1 : -1;
}

// Direction8.toward: s_DX, s_DY from a pixel offset.
static i8 s_DX, s_DY;

static void Toward(i16 dx, i16 dy)
{
	i16 ax = AbsOf(dx);
	i16 ay = AbsOf(dy);
	if (ax == 0 && ay == 0)
	{
		s_DX = 0;
		s_DY = 1;   // S
		return;
	}
	s_DX = SignOf(dx);
	s_DY = SignOf(dy);
	if (ax > 2 * ay)
		s_DY = 0;
	else if (ay > 2 * ax)
		s_DX = 0;
}

// SinTable.sin(i): a quarter of it in g_Sin, folded.
static i16 Sin(u8 i)
{
	u8 a = i & 63;
	switch (i >> 6)
	{
	case 0: return g_Sin[a];
	case 1: return g_Sin[64 - a];
	case 2: return -g_Sin[a];
	default: return -g_Sin[64 - a];
	}
}

// r * s without SDCC's library: r a byte, s in -256..256 (Fixed.mul(fp(r), s)).
static i16 MulR(u8 r, i16 s)
{
	i16 p = 0;
	for (; r; r >>= 1, s <<= 1)
		if (r & 1)
			p += s;
	return p;
}

//-----------------------------------------------------------------------------
// The guardian (wulf.sim.ai.GuardOrbit; SimContext.move and step)

static i16 GuardCentreX()
{
	return FP_PX(g_GuardX) + g_GuardBoxX[g_QuestLair] + g_GuardBoxW[g_QuestLair] / 2;
}

static i16 GuardCentreY()
{
	return FP_PX(g_GuardY) + g_GuardBoxY[g_QuestLair] + g_GuardBoxH[g_QuestLair] / 2;
}

// SimContext.move: x then y, each resolved on its own, in the room.
static void GuardMove(i16 dx, i16 dy)
{
	u8 l = (u8)g_QuestLair;
	g_MoveWorld = MOVE_ROOM;
	g_MoveScenery = 1;
	if (dx != 0)
	{
		g_MovePos = g_GuardX;
		g_MoveDelta = dx;
		g_MoveOther = FP_PX(g_GuardY);
		g_MoveOff = g_GuardBoxX[l];
		g_MoveSize = g_GuardBoxW[l];
		g_MoveOffO = g_GuardBoxY[l];
		g_MoveSizeO = g_GuardBoxH[l];
		g_MoveAlongX = 1;
		Move_Axis();
		g_GuardX = g_MovePos;
	}
	if (dy != 0)
	{
		g_MovePos = g_GuardY;
		g_MoveDelta = dy;
		g_MoveOther = FP_PX(g_GuardX);
		g_MoveOff = g_GuardBoxY[l];
		g_MoveSize = g_GuardBoxH[l];
		g_MoveOffO = g_GuardBoxX[l];
		g_MoveSizeO = g_GuardBoxW[l];
		g_MoveAlongX = 0;
		Move_Axis();
		g_GuardY = g_MovePos;
	}
}

// Moves.step: a tick at its speed, scaled on a diagonal.
static void GuardStep(i8 dx, i8 dy)
{
	u8 l = (u8)g_QuestLair;
	i16 vx = dx && dy ? g_GuardDiagX[l] : g_GuardSpeedX[l];
	i16 vy = dx && dy ? g_GuardDiagY[l] : g_GuardSpeedY[l];
	GuardMove(dx < 0 ? -vx : dx > 0 ? vx : 0, dy < 0 ? -vy : dy > 0 ? vy : 0);
}

// Creature.direction: its heading, and its face if it has an x.
static void GuardDirection(i8 dx, i8 dy)
{
	g_GuardDirX = dx;
	g_GuardDirY = dy;
	if (dx != 0)
		g_GuardFace = dx;
}

static void GuardTowardVale()
{
	Toward(ValeCentreX() - GuardCentreX(), ValeCentreY() - GuardCentreY());
	GuardDirection(s_DX, s_DY);
}

// GuardOrbit.ringPoint, for this tick: into s_RX, s_RY.
static i32 s_RX, s_RY;

static void RingPoint()
{
	s_RX = g_GuardAX + MulR(g_GuardRadius, Sin(g_OrbitIndex + 64));
	s_RY = g_GuardAY + MulR(g_GuardRadius, Sin(g_OrbitIndex));
}

static void GuardTick()
{
	if (g_GuardAux > 0)
	{
		g_GuardAux--;   // stunned by a swing
		return;
	}
	bool alive = g_ValeMode == MODE_ALIVE;
	if (g_GuardPhase == LUNGING)
	{
		if (g_GuardTimer > 0)
		{
			g_GuardTimer--;
			if (alive)
			{
				GuardTowardVale();
				GuardStep(g_GuardDirX, g_GuardDirY);
			}
			return;
		}
		g_GuardPhase = ORBITING;
	}
	if (alive)
	{
		i16 dx = AbsOf(ValeCentreX() - GuardCentreX());
		i16 dy = AbsOf(ValeCentreY() - GuardCentreY());
		if ((dx > dy ? dx : dy) <= ORBIT_LUNGE_PX)
		{
			g_GuardPhase = LUNGING;
			g_GuardTimer = ORBIT_LUNGE_TICKS;
			GuardTowardVale();
			return;
		}
	}
	RingPoint();
	i32 dx = s_RX - g_GuardX;
	i32 dy = s_RY - g_GuardY;
	Toward(FP_PX(dx), FP_PX(dy));
	if (dx > -256 && dx < 256 && dy > -256 && dy < 256)
		return;
	GuardDirection(s_DX, s_DY);
	GuardStep(s_DX, s_DY);
}

//-----------------------------------------------------------------------------
// The quest (Simulation's questEntersRoom, tickQuest, takePiece)

void Quest_NewGame()
{
	g_QuestSet = (u8)Rng_Stream(g_TestSeed, s_Stream, AMULET_SETS);
	g_QuestTaken = 0;
	g_QuestSlots = 0;
	g_QuestPieces = 0;
	g_QuestLair = -1;
	g_QuestFlash = 0;
	g_QuestFly = 0;
	g_QuestFlyMask = 0;
	g_KeeperHere = 0;
	g_KeeperState = KEEPER_BLOCKING;
	g_KeeperSeated = 0;
	g_KeeperMoveTicks = 0;
	Wulf_Place();
}

static void SeatKeeper()
{
	if (!g_KeeperSeated)
	{
		g_KeeperLX = (u16)KEEPER_X << 8;
		g_KeeperSeated = 1;
	}
	g_KeeperX = ((i32)g_RoomX << 8) + g_KeeperLX;
	g_KeeperY = (i32)(g_RoomY + KEEPER_Y) << 8;
}

void Quest_EnterRoom()
{
	g_QuestLair = -1;
	u8 at = g_QuestSet * LAIRS;
	for (u8 i = 0; i < LAIRS; i++)
		if (g_LairRoom[at + i] == g_ValeRoom)
		{
			g_QuestLair = i;
			break;
		}
	if (g_QuestLair >= 0)
	{
		// A new creature, on its pedestal: GuardOrbit.spawn, then seat.
		u8 k = at + g_QuestLair;
		g_GuardAX = (i32)(g_RoomX + g_LairPedX[k]) << 8;
		g_GuardAY = (i32)(g_RoomY + g_LairPedY[k]) << 8;
		g_GuardRadius = g_LairOrbit[k];
		g_GuardDirX = 1;
		g_GuardDirY = 0;
		g_GuardFace = 1;
		g_GuardHurt = 0;
		g_GuardLastHit = 0xFFFF;
		g_GuardMoveTicks = 0;
		g_GuardPhase = ORBITING;
		g_GuardTimer = 0;
		g_GuardAux = 0;
		RingPoint();
		g_GuardX = s_RX;
		g_GuardY = s_RY;
	}
	g_KeeperHere = g_ValeRoom == EXIT_ROOM;
	if (g_KeeperHere)
		SeatKeeper();
}

// stepKeeperAside: away from his half of the room, to its end.
static void StepAside()
{
	bool right = ValeCentreX() - g_RoomX < PLAYFIELD_W / 2;
	u16 was = g_KeeperLX;
	if (right)
	{
		u16 limit = (u16)KEEPER_ASIDE_MAX_PX << 8;
		g_KeeperLX = was + KEEPER_STEP_ASIDE_FP > limit ? limit : was + KEEPER_STEP_ASIDE_FP;
	}
	else
	{
		u16 limit = (u16)KEEPER_ASIDE_MIN_PX << 8;
		g_KeeperLX = was < limit + KEEPER_STEP_ASIDE_FP ? limit : was - KEEPER_STEP_ASIDE_FP;
	}
	bool moving = g_KeeperLX != was;
	g_KeeperMoveTicks = moving ? g_KeeperMoveTicks + 1 : 0;
	g_KeeperState = moving ? KEEPER_STEPPING_ASIDE : KEEPER_ASIDE;
	SeatKeeper();
}

//-----------------------------------------------------------------------------
// M8: the items, the treasures, the shrines (takeItems, takeTreasures, pointTheWay)

// Box B at a spot in this room, the items' pickup box around it.
static void ItemBox(u8 x, u8 y)
{
	s_BX = g_RoomX + x + ITEM_BOX_X;
	s_BY = g_RoomY + y + ITEM_BOX_Y;
	s_BW = ITEM_BOX_W;
	s_BH = ITEM_BOX_H;
}

// Direction8's ordinal by (dy + 1) * 3 + (dx + 1): N NE E SE S SW W NW. In
// RAM: a const table would be in the program's segments, not this one.
static u8 s_Dir[9] = { 7, 0, 1, 6, 4, 2, 5, 4, 3 };

static void ItemTaken()
{
	g_QuestFlash = AMULET_PICKUP_FLASH_TICKS;   // the same white flash as a quarter
}

static void TakeItems()
{
	if (!g_MapTaken && g_ValeRoom == MAP_ROOM)
	{
		ItemBox(MAP_X, MAP_Y);
		if (Overlaps())
		{
			g_MapTaken = 1;
			ItemTaken();
		}
	}
	for (u8 i = 0; i < EYES; i++)
		if (!(g_EyesTaken & (1 << i)) && g_ItemRoom[i] == g_ValeRoom)
		{
			ItemBox(g_ItemX[i], g_ItemY[i]);
			if (Overlaps())
			{
				g_EyesTaken |= 1 << i;
				ItemTaken();
			}
		}
	// A looking glass shows the way afresh; the amulet whole, more lie about.
	bool whole = Quest_Quarters() >= EXIT_REQUIRES_PIECES;
	for (u8 i = 0; i < GLASSES; i++)
		if (!(g_GlassesTaken & (1 << i)) && (i < GLASSES_BEFORE_WHOLE || whole)
			&& g_ItemRoom[EYES + i] == g_ValeRoom)
		{
			ItemBox(g_ItemX[EYES + i], g_ItemY[EYES + i]);
			if (Overlaps())
			{
				g_GlassesTaken |= 1 << i;
				g_WayTicks = g_Easy ? EASY_WAY_TICKS : GLASS_WAY_TICKS;
				ItemTaken();
			}
		}
}

// A treasure touched is taken for the rest of the game, and scores; a life
// too, while there is room for one.
static void TakeTreasures()
{
	for (u8 p = 0; p < 2; p++)
	{
		u8 t = g_RoomTreasure[p];
		if (t == 0)
			continue;
		s_BX = g_RoomX + g_RoomMore[2 + 2 * p] + TREASURE_BOX_X;
		s_BY = g_RoomY + g_RoomMore[3 + 2 * p] + TREASURE_BOX_Y;
		s_BW = TREASURE_BOX_W;
		s_BH = TREASURE_BOX_H;
		if (!Overlaps())
			continue;
		g_RoomTreasure[p] = 0;
		Data_VramWrite(VRAM_TREASURES + ((u16)g_ValeRoom << 1) + p, &g_RoomTreasure[p], 1);
		g_TreasuresTaken++;
		if (g_TreasureLife[t - 1] && g_ValeLives < PLAYER_LIVES_MAX)
			g_ValeLives++;
		Score_Add(TREASURE_SCORE);
	}
}

// pointTheWay: the compass way to the nearest quarter still out there, or
// to the arch once none are.
static void PointTheWay()
{
	i8 bestDx = 0;
	i8 bestDy = 0;
	u8 best = 0xFF;
	u8 k = g_QuestSet * LAIRS;
	i8 col = g_ValeRoom % GRID_W;
	i8 row = g_ValeRoom / GRID_W;
	for (u8 i = 0; i < LAIRS; i++)
	{
		if (g_QuestTaken & (1 << i))
			continue;
		u8 r = g_LairRoom[k + i];
		i8 dx = (i8)(r % GRID_W) - col;
		i8 dy = (i8)(r / GRID_W) - row;
		u8 d = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
		if (d < best)
		{
			best = d;
			bestDx = dx;
			bestDy = dy;
		}
	}
	g_HintToExit = best == 0xFF;
	if (g_HintToExit)
	{
		bestDx = (i8)(EXIT_ROOM % GRID_W) - col;
		bestDy = (i8)(EXIT_ROOM / GRID_W) - row;
	}
	Toward(bestDx, bestDy);
	g_HintDirection = s_Dir[(s_DY + 1) * 3 + (s_DX + 1)];
}

// A shrine's zone, in a shrine's room, outside a lair: it points the way,
// once a life (Easy may answer every time).
static void Shrine()
{
	s_BX = g_RoomX + SHRINE_ZONE_X;
	s_BY = g_RoomY + SHRINE_ZONE_Y;
	s_BW = SHRINE_ZONE_W;
	s_BH = SHRINE_ZONE_H;
	bool asking = !g_Easy || EASY_HINTS_ONCE_PER_LIFE;
	u8 bit = 1 << (g_ValeRoom & 7);
	u8* used = &g_HintUsed[g_ValeRoom >> 3];
	if (CAVE_HINTS && Overlaps() && (g_RoomMore[7] & GAME_ROOM_SHRINE) && g_QuestLair < 0 && !(asking && (*used & bit)))
	{
		if (asking)
			*used |= bit;
		else
			*used &= ~bit;
		g_HintTicks = HINT_TICKS;
		PointTheWay();
	}
}

static void TakePiece(i16 cx, i16 cy)
{
	u8 l = (u8)g_QuestLair;
	g_QuestTaken |= 1 << l;
	g_QuestPieces++;
	g_QuestSlots |= g_LairMask[l];
	g_QuestFlash = AMULET_PICKUP_FLASH_TICKS;
	g_QuestFly = AMULET_FLY_TICKS;
	g_QuestFlyMask = g_LairMask[l];
	g_QuestFlyX = cx;
	g_QuestFlyY = cy;
	g_WayTicks = 0;   // the glass showed the way to a piece; with one taken, its work is done
	Score_Add(SCORE_PIECE);
	if (Quest_Quarters() >= EXIT_REQUIRES_PIECES && g_KeeperState == KEEPER_BLOCKING)
		g_KeeperState = KEEPER_STEPPING_ASIDE;   // harmless now; it moves only while he is in its room
}

void Quest_Tick()
{
	if (g_QuestFlash > 0)
		g_QuestFlash--;
	if (g_QuestFly > 0)
		g_QuestFly--;
	if (g_HintTicks > 0)
		g_HintTicks--;
	if (g_WayTicks > 0)
		g_WayTicks--;
	if (g_QuestLair >= 0)
	{
		if (g_GuardHurt > 0)
			g_GuardHurt--;
		i32 ox = g_GuardX;
		i32 oy = g_GuardY;
		GuardTick();
		g_GuardMoveTicks = g_GuardX != ox || g_GuardY != oy ? g_GuardMoveTicks + 1 : 0;
	}
	if (g_KeeperState != KEEPER_BLOCKING && g_KeeperHere && g_ValeMode == MODE_ALIVE)
		StepAside();
	ValeBox();
	if (g_GameOn)
	{
		TakeItems();
		TakeTreasures();
	}
	if (g_QuestLair >= 0 && !(g_QuestTaken & (1 << g_QuestLair)))
	{
		u8 k = g_QuestSet * LAIRS + g_QuestLair;
		s_BX = g_RoomX + g_LairPedX[k] + AMULET_BOX_X;
		s_BY = g_RoomY + g_LairPedY[k] + AMULET_BOX_Y;
		s_BW = AMULET_BOX_W;
		s_BH = AMULET_BOX_H;
		if (Overlaps())
			TakePiece(s_BX + AMULET_BOX_W / 2, s_BY + AMULET_BOX_H / 2);
	}
	if (g_GameOn)
		Shrine();
	// With the amulet whole, past the Keeper and above the doorway, he is out.
	s_BX = g_RoomX + EXIT_ZONE_X;
	s_BY = g_RoomY + EXIT_ZONE_Y;
	s_BW = EXIT_ZONE_W;
	s_BH = EXIT_ZONE_H;
	if (g_ValeRoom == EXIT_ROOM && Overlaps() && Quest_Quarters() >= EXIT_REQUIRES_PIECES)
	{
		g_ValeMode = MODE_ESCAPING;
		g_ValeModeTick = 0;
		g_ValeSwing = -1;
		g_ValeCooldown = 0;
		g_SabreW = 0;
		g_SabreH = 0;
	}
}

// parryGuardian: never hurt; a swing pushes it back along the blow and stuns it.
void Quest_Parry()
{
	if (g_QuestLair < 0 || g_GuardLastHit == g_SwingSerial)
		return;
	u8 l = (u8)g_QuestLair;
	s_AX = g_SabreX;
	s_AY = g_SabreY;
	s_AW = g_SabreW;
	s_AH = g_SabreH;
	s_BX = FP_PX(g_GuardX) + g_GuardBoxX[l];
	s_BY = FP_PX(g_GuardY) + g_GuardBoxY[l];
	s_BW = g_GuardBoxW[l];
	s_BH = g_GuardBoxH[l];
	if (!Overlaps())
		return;
	g_GuardLastHit = g_SwingSerial;
	i8 dx = FacingDX();
	i8 dy = FacingDY();
	i16 push = dx && dy ? ORBIT_PUSH_DIAG_FP : ORBIT_PUSH_FP;
	GuardMove(dx < 0 ? -push : dx > 0 ? push : 0, dy < 0 ? -push : dy > 0 ? push : 0);
	g_GuardAux = ORBIT_STUN_TICKS;
	g_GuardHurt = ORBIT_HURT_FLASH_TICKS;
}

// resolveCombat's touches, before the small creatures': the guardian, the
// Keeper while it bars the arch, the Wulf. One kills; the Wulf then turns
// away from him unless he was fighting.
void Quest_Touch()
{
	g_QuestHit = 0;
	ValeBox();
	bool immune = g_GameOn && EFFECT_IMMUNE();   // the green flower; the Keeper still bars the arch
	if (g_QuestLair >= 0 && !immune)
	{
		u8 l = (u8)g_QuestLair;
		s_BX = FP_PX(g_GuardX) + g_GuardBoxX[l];
		s_BY = FP_PX(g_GuardY) + g_GuardBoxY[l];
		s_BW = g_GuardBoxW[l];
		s_BH = g_GuardBoxH[l];
		if (Overlaps())
		{
			g_QuestHit = 1;
			return;
		}
	}
	if (g_KeeperHere && g_KeeperState == KEEPER_BLOCKING)
	{
		s_BX = FP_PX(g_KeeperX) + KEEPER_BOX_X;
		s_BY = FP_PX(g_KeeperY) + KEEPER_BOX_Y;
		s_BW = KEEPER_BOX_W;
		s_BH = KEEPER_BOX_H;
		if (Overlaps())
		{
			g_QuestHit = 1;
			return;
		}
	}
	bool visible = g_WulfState >= WULF_PROWLING ? g_WulfHere : g_WulfState != WULF_ABSENT && g_WulfState != WULF_ARRIVING;
	if (visible && !immune)
	{
		s_BX = FP_PX(g_WulfX) + WULF_BOX_X;
		s_BY = FP_PX(g_WulfY) + WULF_BOX_Y;
		s_BW = WULF_BOX_W;
		s_BH = WULF_BOX_H;
		if (Overlaps())
		{
			g_QuestHit = 1;
			if (g_SabreW == 0)
				Wulf_Head(-g_WulfHeading);   // it turns away from a player it has hit
		}
	}
}

#include "wulf.c"
