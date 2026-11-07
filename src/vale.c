// Wulf Quest for MSX2: Ranger Vale, walking and fighting, from room to room
// (AGENTS.md §10, M3 and M4).
//
// The Java game's rules, in the Java game's order (its Simulation.updateSwing,
// move, updateRoom and computeSabre; AGENTS.md §7.4, §7.5, §11): integers and
// 8.8 fixed point only, so the MSX2 moves him exactly as the Java game does,
// tick for tick. test/moves.sh holds the two to that.
//
// He walks in world pixels, never room-local ones, so leaving a room by an
// edge enters the next one at the same place across it (§7.5); the room is
// the one his feet box's centre is in.

#include "vale.h"
#include "constants.h"
#include "player.h"
#include "blade.h"
#include "jungle.h"
#include "spawn.h"
#include "quest.h"
#include "quest_data.h"
#include "game.h"
#include "game_data.h"
#include "sound.h"

#define ONE			(1 << FIXED_SHIFT)
#define LEVELS		(PLAYER_MOMENTUM_MAX / PLAYER_MOMENTUM_STEP)
#define SWING_TICKS	(SABRE_WINDUP_TICKS + SABRE_ACTIVE_TICKS + SABRE_RECOVER_TICKS)
#define WORLD_COLS	(GRID_W * CELL_COLS)
#define WORLD_ROWS	(GRID_H * CELL_ROWS)
#if PLAYER_WALK_TICKS_PER_FRAME < 1 || PLAYER_MOMENTUM_TICKS_PER_FRAME < 1
#error "vale.c counts walk and momentum frames, so both must be at least a tick"
#endif
// While his box's centre is in the room, the box reaches past its edge by at
// most half the box; a tick's move looks a step further, and the diagonal
// test a pixel more. All of that must be in the cells around the room.
#define STEP_PX		((PLAYER_SPEED_X_FP > PLAYER_SPEED_Y_FP ? PLAYER_SPEED_X_FP : PLAYER_SPEED_Y_FP) / ONE + 1)
#if (PLAYER_BOX_W + 1) / 2 + STEP_PX + 1 > CELLS_MARGIN * CELL || (PLAYER_BOX_H + 1) / 2 + STEP_PX + 1 > CELLS_MARGIN * CELL
#error "Vale's box can reach past the cells around the room: CELLS_MARGIN (data.h) must grow"
#endif
#if PLAYFIELD_W != CELL_COLS * CELL || PLAYFIELD_H != CELL_ROWS * CELL
#error "vale.c takes a room's edges to be cell edges"
#endif

i32 g_ValeX;
i32 g_ValeY;
u8  g_ValeFacing;
i8  g_ValeSwing;
u8  g_ValeCooldown;
u16 g_ValeWalk;
i16 g_ValeVX;
i16 g_ValeVY;
i16 g_SabreX;
i16 g_SabreY;
u8  g_SabreW;
u8  g_SabreH;
u8  g_ValeRoom = 0;
u8  g_ValeFreeze = 0;
u8  g_ValeOpen = 0;   // the rest are set by Vale_Init and Vale_Place
u8  g_ValeMode = MODE_ALIVE;
u8  g_ValeModeTick = 0;
u8  g_ValeLives = 0;
u8  g_ValeInvuln = 0;
u8  g_ValeVisible = 1;
u16 g_SwingSerial = 0;
u32 g_SimTick = 0;
u8  g_ValeEndless = 0;   // test/moves.sh: the Java game's infiniteLives
static bool s_FireWas = FALSE;
static u8 s_Footstep = 0;   // Simulation.footstepTicks   // fire held the tick before: a press is fire now and not then
i16 g_RoomX = 0;
i16 g_RoomY = 0;
i16 g_CellsX = 0;
i16 g_CellsY = 0;

static i8 s_LevelX;
static i8 s_LevelY;
static u8 s_FramePhase;   // tick % PLAYER_MOMENTUM_TICKS_PER_FRAME
u8 g_ValeGaitTick = 0;     // g_ValeWalk % PLAYER_WALK_TICKS_PER_FRAME
u8 g_ValeGait = 0;         // g_ValeWalk / PLAYER_WALK_TICKS_PER_FRAME % PLAYER_GAIT
// The room's top-left, and the cells' around it, world pixels (vale.h).
#define s_RoomX		g_RoomX
#define s_RoomY		g_RoomY
#define s_CellsX	g_CellsX
#define s_CellsY	g_CellsY

// Worked out once, so a tick needs no multiplication or division (the Z80
// has neither): a speed level's pace on each axis, straight and on a
// diagonal (§11.2); the speed level of a velocity; the pose of a stroke's tick.
static i16 s_SpeedX[LEVELS + 1];
static i16 s_SpeedY[LEVELS + 1];
static i16 s_DiagX[LEVELS + 1];
static i16 s_DiagY[LEVELS + 1];
static u8 s_LevelOf[PLAYER_MOMENTUM_MAX + 1];
static u8 s_Pose[SWING_TICKS];

// Direction8 by (dy + 1) * 3 + (dx + 1), and back.
static const u8 c_FaceOf[9] = { FACE_NW, FACE_N, FACE_NE, FACE_W, 0xFF, FACE_E, FACE_SW, FACE_S, FACE_SE };
static const i8 c_FaceDX[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
static const i8 c_FaceDY[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };

// The cells of his room and a margin around it, a byte each, unpacked when
// he enters it: a byte is quicker to test than a bit.
u8 s_Cells[CELLS_H * CELLS_W];

static void EnterRoom(u8 room)
{
	g_ValeRoom = room;
	s_RoomX = (i16)(room % GRID_W) * PLAYFIELD_W;
	s_RoomY = (i16)(room / GRID_W) * PLAYFIELD_H;
	s_CellsX = s_RoomX - CELLS_MARGIN * CELL;
	s_CellsY = s_RoomY - CELLS_MARGIN * CELL;
	Data_LoadCells(room, s_Cells);
	if (g_JungleOn)
		JUNGLE(Jungle_EnterRoom);
	if (g_QuestOn)
		QUEST(Quest_EnterRoom);
	if (g_GameOn)
		GAME(Game_EnterRoom);
	g_GameByFlip = 0;
}

// The speed a momentum level gives, its diagonal too, with a flower's scale
// (EffectState.speedFp: haste and torpor scale his pace, (base * scale) >> 8).
static i16 s_ScaleFor = 0;

static void Speeds(i16 scale)
{
	s_ScaleFor = scale;
	i16 bx = (i16)(((i32)PLAYER_SPEED_X_FP * scale) >> 8);
	i16 by = (i16)(((i32)PLAYER_SPEED_Y_FP * scale) >> 8);
	for (u8 l = 0; l <= LEVELS; l++)
	{
		s_SpeedX[l] = (i16)((i32)bx * l / LEVELS);
		s_SpeedY[l] = (i16)((i32)by * l / LEVELS);
		// Fixed.mul: (a * b) >> 8
		s_DiagX[l] = (i16)(((i32)s_SpeedX[l] * PLAYER_DIAGONAL_SCALE_FP) >> FIXED_SHIFT);
		s_DiagY[l] = (i16)(((i32)s_SpeedY[l] * PLAYER_DIAGONAL_SCALE_FP) >> FIXED_SHIFT);
	}
}

void Vale_Init()
{
	Speeds(256);
	for (u8 l = 0; l <= LEVELS; l++)
	{
	}
	for (u8 v = 0; v <= PLAYER_MOMENTUM_MAX; v++)
		s_LevelOf[v] = v / PLAYER_MOMENTUM_STEP;
	// PlayerPainter.phaseOf: on guard, then the lunge, a window each; the recovery after the blade.
	for (u8 t = 0; t < SWING_TICKS; t++)
		s_Pose[t] = t < SABRE_WINDUP_TICKS ? 0
			: t - SABRE_WINDUP_TICKS >= SABRE_ACTIVE_TICKS ? 2
			: ((t - SABRE_WINDUP_TICKS) / SABRE_FENCE_EVERY_TICKS) % 2;
}

static void ResetTicks();

void Vale_Place(i32 x, i32 y)
{
	g_ValeX = x;
	g_ValeY = y;
	g_ValeFacing = FACE_S;
	g_ValeSwing = -1;
	g_ValeCooldown = 0;
	g_ValeWalk = 0;
	g_ValeVX = 0;
	g_ValeVY = 0;
	g_SabreW = 0;
	g_SabreH = 0;
	s_LevelX = 0;
	s_LevelY = 0;
	s_FramePhase = 0;
	g_ValeGaitTick = 0;
	g_ValeGait = 0;
	g_ValeFreeze = 0;
	g_ValeMode = MODE_ALIVE;
	g_ValeModeTick = 0;
	g_ValeLives = g_LivesStart;
	g_ValeInvuln = 0;
	s_FireWas = FALSE;
	g_ValeVisible = 1;
	g_SwingSerial = 0;
	g_SimTick = 0;
	Quest_ClockReset();
	ResetTicks();
	g_ValeFrame = Vale_Frame();
	g_ValeBlade = -1;
	// Simulation.roomColOf, roomRowOf
	i16 cx = FP_PX(x) + (PLAYER_BOX_X + PLAYER_BOX_W / 2);
	i16 cy = FP_PX(y) + (PLAYER_BOX_Y + PLAYER_BOX_H / 2);
	u8 col = cx < 0 ? 0 : cx / PLAYFIELD_W >= GRID_W ? GRID_W - 1 : (u8)(cx / PLAYFIELD_W);
	u8 row = cy < 0 ? 0 : cy / PLAYFIELD_H >= GRID_H ? GRID_H - 1 : (u8)(cy / PLAYFIELD_H);
	EnterRoom(row * GRID_W + col);
}

//-----------------------------------------------------------------------------
// Collision (§7.4): the feet box against the cells.

// Whether the feet box, with his feet at whole pixel (x, y) of the world,
// overlaps a solid cell: one of the cells around the room. Outside those,
// which the margin is wide enough never to need, it counts as solid.
#if CELL != 8
#error "Blocked divides by CELL with a shift"
#endif
// Cells_Blocked (move.c, below 8000h: banked code calls it) tests the cells.

static bool Blocked(i16 x, i16 y)
{
	i16 left = x + PLAYER_BOX_X;
	i16 top = y + PLAYER_BOX_Y;
	if (g_ValeOpen)
		return left < 0 || top < 0
			|| left + (PLAYER_BOX_W - 1) >= WORLD_COLS * CELL
			|| top + (PLAYER_BOX_H - 1) >= WORLD_ROWS * CELL;
	left -= s_CellsX;
	top -= s_CellsY;
	// Outside the window, below or past it, is solid: one unsigned compare each.
	if ((u16)left > CELLS_W * CELL - PLAYER_BOX_W || (u16)top > CELLS_H * CELL - PLAYER_BOX_H)
		return TRUE;
	g_CbLeft = (u16)left;
	g_CbTop = (u8)top;
	g_CbW = PLAYER_BOX_W;
	g_CbH = PLAYER_BOX_H;
	return Cells_Blocked();
}
#if CELLS_H * CELL > 255
#error "Blocked keeps a row of the cells in a byte"
#endif

// A whole pixel as 8.8 fixed point, without a 32-bit shift.
static i32 FixedOf(i16 px)
{
	i32 v;
	u8* b = (u8*)&v;
	b[0] = 0;
	*(i16*)(b + 1) = px;
	b[3] = px < 0 ? 0xFF : 0;
	return v;
}

// The open world's (the movement tests'): every candidate through Blocked.
static i32 ResolveSlow(i32 pos, i16 delta, i16 other, bool alongX)
{
	i32 target = pos + delta;
	i16 t = FP_PX(target);
	i16 p = FP_PX(pos);
	if (delta > 0)
	{
		for (i16 c = t; c > p; c--)
			if (!(alongX ? Blocked(c, other) : Blocked(other, c)))
				return FixedOf(c);
	}
	else
	{
		bool fraction = *(u8*)&pos != 0;
		for (i16 c = t + 1; c < p || (c == p && fraction); c++)
			if (!(alongX ? Blocked(c, other) : Blocked(other, c)))
				return FixedOf(c);
	}
	return pos;
}

// One axis (Collision.resolve): free, the target; blocked, the nearest free
// whole pixel between here and the target; none, here, fraction and all.
// The search runs in whole pixels: a whole pixel c is past pos going up when
// c > px(pos), and going down when c < px(pos), or c == px(pos) and pos has a
// fraction.
// Resolve's candidates: a candidate pixel along the axis plus s_RBase is the
// box's edge in the window's pixels; past s_RLimit (or below 0, as unsigned)
// it is outside the window, solid. The other axis is set in g_Cb* once.
static i16 s_RBase = 0;
static u16 s_RLimit = 0;
static bool s_RAlongX = FALSE;

static bool Try(i16 c)
{
	u16 e = (u16)(c + s_RBase);
	if (e > s_RLimit)
		return TRUE;
	if (s_RAlongX)
		g_CbLeft = e;
	else
		g_CbTop = (u8)e;
	return Cells_Blocked();
}

static i32 Resolve(i32 pos, i16 delta, i16 other, bool alongX)
{
	if (delta == 0)
		return pos;
	i32 target = pos + delta;
	i16 t = FP_PX(target);
	if (g_ValeOpen)
		return (alongX ? Blocked(t, other) : Blocked(other, t)) ? ResolveSlow(pos, delta, other, alongX) : target;
	// The other axis, once: outside the window, every candidate is solid.
	g_CbW = PLAYER_BOX_W;
	g_CbH = PLAYER_BOX_H;
	s_RAlongX = alongX;
	if (alongX)
	{
		u16 top = (u16)(other + PLAYER_BOX_Y - s_CellsY);
		if (top > CELLS_H * CELL - PLAYER_BOX_H)
			return pos;
		g_CbTop = (u8)top;
		s_RBase = PLAYER_BOX_X - s_CellsX;
		s_RLimit = CELLS_W * CELL - PLAYER_BOX_W;
	}
	else
	{
		u16 left = (u16)(other + PLAYER_BOX_X - s_CellsX);
		if (left > CELLS_W * CELL - PLAYER_BOX_W)
			return pos;
		g_CbLeft = left;
		s_RBase = PLAYER_BOX_Y - s_CellsY;
		s_RLimit = CELLS_H * CELL - PLAYER_BOX_H;
	}
	if (!Try(t))
		return target;
	i16 p = FP_PX(pos);
	if (delta > 0)
	{
		for (i16 c = t; c > p; c--)
			if (!Try(c))
				return FixedOf(c);
	}
	else
	{
		bool fraction = *(u8*)&pos != 0;
		for (i16 c = t + 1; c < p || (c == p && fraction); c++)
			if (!Try(c))
				return FixedOf(c);
	}
	return pos;
}

//-----------------------------------------------------------------------------
// The sabre (§11.6)

static void UpdateSwing(bool fire)
{
	if (g_ValeSwing >= 0)
	{
		if (++g_ValeSwing >= SWING_TICKS)
		{
			g_ValeSwing = -1;
			// Held, it goes again straight away; let go, it needs its cooldown.
			g_ValeCooldown = fire ? SABRE_HOLD_REPEAT_TICKS : SABRE_COOLDOWN_TICKS;
		}
	}
	else if (g_ValeCooldown > 0)
		g_ValeCooldown--;
	if (g_ValeSwing < 0 && g_ValeCooldown == 0 && fire)
	{
		g_ValeSwing = 0;
		Sound_Play(SFX_SABRE_SWING);
		g_SwingSerial++;   // each swing may hit each creature once
	}
}

static void ComputeSabre()
{
	i8 t = g_ValeSwing;
	if (t < SABRE_WINDUP_TICKS || t >= SABRE_WINDUP_TICKS + SABRE_ACTIVE_TICKS)
	{
		g_SabreW = 0;
		g_SabreH = 0;
		return;
	}
	i16 cx = FP_PX(g_ValeX) + (PLAYER_BOX_X + PLAYER_BOX_W / 2);
	i16 cy = FP_PX(g_ValeY) + (PLAYER_BOX_Y + PLAYER_BOX_H / 2);
	i8 dx = c_FaceDX[g_ValeFacing];
	i8 dy = c_FaceDY[g_ValeFacing];
	if (dx != 0)
	{
		// Diagonals project along the horizontal and shift vertically.
		g_SabreX = dx > 0 ? cx : cx - SABRE_REACH_PX;
		g_SabreY = cy - SABRE_THICKNESS_PX / 2;
		if (dy > 0)
			g_SabreY += SABRE_DIAGONAL_OFFSET_PX;
		else if (dy < 0)
			g_SabreY -= SABRE_DIAGONAL_OFFSET_PX;
		g_SabreW = SABRE_REACH_PX;
		g_SabreH = SABRE_THICKNESS_PX;
	}
	else
	{
		g_SabreX = cx - SABRE_THICKNESS_PX / 2;
		g_SabreY = dy > 0 ? cy : cy - SABRE_REACH_PX;
		g_SabreW = SABRE_THICKNESS_PX;
		g_SabreH = SABRE_REACH_PX;
	}
}

//-----------------------------------------------------------------------------
// The walk (§11.2, §11.3)

// One axis of one frame of the original: the held direction (-1, 0, 1) on the velocity.
static i16 Accelerate(i16 v, i8 d)
{
	if (g_ValeSwing >= 0)
		return d > 0 ? PLAYER_MOMENTUM_FIGHT : d < 0 ? -PLAYER_MOMENTUM_FIGHT : 0;   // fighting: a steady pace
	if (d > 0)
		v += PLAYER_MOMENTUM_ACCEL;
	else if (d < 0)
		v -= PLAYER_MOMENTUM_ACCEL;
	if (v > PLAYER_MOMENTUM_MAX)
		return PLAYER_MOMENTUM_MAX;
	if (v < -PLAYER_MOMENTUM_MAX)
		return -PLAYER_MOMENTUM_MAX;
	return v;
}

// signum(v) * (|v| / step)
static i8 LevelOf(i16 v)
{
	return v >= 0 ? (i8)s_LevelOf[(u8)v] : -(i8)s_LevelOf[(u8)-v];
}

// v toward 0 by the decay, never past it.
static i16 Decay(i16 v)
{
	if (v > 0)
		return v > PLAYER_MOMENTUM_DECAY ? v - PLAYER_MOMENTUM_DECAY : 0;
	if (v < 0)
		return v < -PLAYER_MOMENTUM_DECAY ? v + PLAYER_MOMENTUM_DECAY : 0;
	return 0;
}

// A tick walked: the walk's count, and the gait it shows.
static void Stride()
{
	g_ValeWalk++;
	if (++g_ValeGaitTick >= PLAYER_WALK_TICKS_PER_FRAME)
	{
		g_ValeGaitTick = 0;
		if (++g_ValeGait >= PLAYER_GAIT)
			g_ValeGait = 0;
	}
}

static void Move(u8 keys)
{
	i8 dx = ((keys & VALE_RIGHT) ? 1 : 0) - ((keys & VALE_LEFT) ? 1 : 0);
	i8 dy = ((keys & VALE_DOWN) ? 1 : 0) - ((keys & VALE_UP) ? 1 : 0);
	if (g_EffectKind == EFFECT_REVERSAL)
	{
		dx = -dx;   // the flower has the legs, not the keyboard
		dy = -dy;
	}
	i16 scale = g_EffectKind == EFFECT_HASTE || g_EffectKind == EFFECT_TORPOR ? g_EffectScale : 256;
	if (scale != s_ScaleFor)
		Speeds(scale);
	if (dx != 0 || dy != 0)
		g_ValeFacing = c_FaceOf[(u8)((dy + 1) * 3 + (dx + 1))];   // facing stays when the keys are let go

	if (++s_FramePhase >= PLAYER_MOMENTUM_TICKS_PER_FRAME)
		s_FramePhase = 0;
	if (s_FramePhase == 0)
	{
		// One frame of the original: accelerate, clamp, take the speed step, decay.
		g_ValeVX = Accelerate(g_ValeVX, dx);
		g_ValeVY = Accelerate(g_ValeVY, dy);
		s_LevelX = LevelOf(g_ValeVX);
		s_LevelY = LevelOf(g_ValeVY);
		if (g_ValeSwing < 0)
		{
			g_ValeVX = Decay(g_ValeVX);
			g_ValeVY = Decay(g_ValeVY);
		}
	}

	u8 lx = s_LevelX >= 0 ? (u8)s_LevelX : (u8)-s_LevelX;
	u8 ly = s_LevelY >= 0 ? (u8)s_LevelY : (u8)-s_LevelY;
	i16 speedX = s_SpeedX[lx];
	i16 speedY = s_SpeedY[ly];
	i16 px = FP_PX(g_ValeX);
	i16 py = FP_PX(g_ValeY);
	if (lx && ly
		&& !Blocked(s_LevelX > 0 ? px + 1 : px - 1, py)
		&& !Blocked(px, s_LevelY > 0 ? py + 1 : py - 1))
	{
		// Neither axis against a wall: the diagonal scale. Against one, he
		// slides along the other at its full rate.
		speedX = s_DiagX[lx];
		speedY = s_DiagY[ly];
	}
	i32 oldX = g_ValeX;
	i32 oldY = g_ValeY;
	// X then Y, each on its own: the wall slide.
	g_ValeX = Resolve(g_ValeX, s_LevelX < 0 ? -speedX : speedX, py, TRUE);
	g_ValeY = Resolve(g_ValeY, s_LevelY < 0 ? -speedY : speedY, FP_PX(g_ValeX), FALSE);
	if (g_ValeX != oldX || g_ValeY != oldY)
	{
		Stride();
		// A step every few ticks walked; in haste the count goes up twice as fast.
		s_Footstep += g_EffectKind == EFFECT_HASTE ? FOOTSTEP_HASTE_STEP : 1;
		if (s_Footstep >= FOOTSTEP_EVERY_TICKS)
		{
			s_Footstep = 0;
			Sound_Play(SFX_FOOTSTEP);
		}
	}
	else
	{
		s_Footstep = 0;
		g_ValeWalk = 0;
		g_ValeGaitTick = 0;
		g_ValeGait = 0;
	}
}

// Simulation.updateRoom: the room his feet box's centre is in. He moves
// less than a room a tick, so it is this one or a neighbour; the map's edge
// is a wall, but the room is kept on the map as the Java game keeps it.
static bool UpdateRoom()
{
	i16 cx = FP_PX(g_ValeX) + (PLAYER_BOX_X + PLAYER_BOX_W / 2) - s_RoomX;
	i16 cy = FP_PX(g_ValeY) + (PLAYER_BOX_Y + PLAYER_BOX_H / 2) - s_RoomY;
	u8 room = g_ValeRoom;
	u8 col = room % GRID_W;
	if (cx < 0 && col > 0)
		room--;
	else if (cx >= PLAYFIELD_W && col < GRID_W - 1)
		room++;
	if (cy < 0 && room >= GRID_W)
		room -= GRID_W;
	else if (cy >= PLAYFIELD_H && room < (GRID_H - 1) * GRID_W)
		room += GRID_W;
	if (room == g_ValeRoom)
		return FALSE;
	// §7.5: the position is untouched; the new room is current at once, and
	// the next ticks are frozen.
	g_ValeFreeze = g_ValeOpen ? 0 : TRANSITION_FREEZE_TICKS;
	Sound_Play(SFX_ROOM_FLIP);
	g_GameByFlip = 1;   // a first visit by a flip scores
	EnterRoom(room);
	if (g_QuestOn)
		QUEST(Wulf_Notice);   // wulfEntersRoom: the original's only notices
	return TRUE;
}

//-----------------------------------------------------------------------------
// What he is drawn as (M5): PlayerPainter's frame, and the blade's shape.

u8 g_ValeFrame = 0;
i8 g_ValeBlade = -1;

// The game tick, as the window of fence ticks it is in (Simulation.tick() /
// SABRE_FENCE_EVERY_TICKS), counted rather than divided, and its hash, added
// to rather than multiplied; and the pose it gives, worked out once a window.
static u8  s_InWindow = 0;
static u32 s_Hash = 0;    // (window + 1) * BLADE_HASH, 32 bits: a window adds BLADE_HASH
static u8  s_FencePose = 0;

#if BLADE_HASH_SHIFT < 24 || BLADE_HASH_SHIFT > 31
#error "the pose's hash takes its high bits from the top byte"
#endif
#if (BLADE_GUARDS & (BLADE_GUARDS - 1)) != 0
#error "the blade's pose is the hash's low bits: BLADE_GUARDS must be a power of two"
#endif

static void NextTick()
{
	if (++s_InWindow == SABRE_FENCE_EVERY_TICKS)
	{
		s_InWindow = 0;
		// PlayerPainter.pose: its 64-bit hash's low bits, from 32 (blade.h).
		s_Hash += BLADE_HASH;
		s_FencePose = ((u8)s_Hash ^ (((u8*)&s_Hash)[3] >> (BLADE_HASH_SHIFT - 24))) & (BLADE_GUARDS - 1);
	}
}

static void ResetTicks()
{
	s_InWindow = 0;
	s_Hash = BLADE_HASH;
	s_FencePose = ((u8)s_Hash ^ (((u8*)&s_Hash)[3] >> (BLADE_HASH_SHIFT - 24))) & (BLADE_GUARDS - 1);
}

// PlayerPainter.paint: the blade, while the sabre's hit box is out.
static void Looks()
{
	// PlayerPainter.visible: spawn invulnerability blinks, half a period shown.
	g_ValeVisible = g_ValeMode != MODE_ALIVE || g_ValeInvuln == 0
		|| g_ValeInvuln % PLAYER_SPAWN_BLINK_TICKS < (PLAYER_SPAWN_BLINK_TICKS + 1) / 2;
	g_ValeFrame = Vale_Frame();
	if (g_SabreW == 0)
	{
		g_ValeBlade = -1;
		return;
	}
	u8 into = (u8)g_ValeSwing - SABRE_WINDUP_TICKS;
	u8 thrust = (into / SABRE_FENCE_EVERY_TICKS) & 1;
	g_ValeBlade = (i8)(g_ValeFacing * BLADE_POSES + (thrust ? BLADE_GUARDS : 0) + s_FencePose);
}

// Vale_Kill is banked code (jungle.c): only the creatures kill him.

// Simulation.respawn and getUp: a life from the reserve, none left and the
// game is over; up where he fell, nudged to the nearest spot he can walk to
// that keeps him wholly on screen (spawn.c).
static void GetUp();

static void Respawn()
{
	if (!g_ValeEndless)
	{
		if (g_ValeLives == 0)
		{
			g_ValeMode = MODE_GAME_OVER;
			g_ValeModeTick = 0;
			return;
		}
		g_ValeLives--;
	}
	GetUp();
}

// Simulation.getUp: back on his feet where he fell, after a death or a continue.
static void GetUp()
{
	SPAWN(Spawn_NearestFree);
	g_ValeMode = MODE_ALIVE;
	g_ValeModeTick = 0;
	g_ValeInvuln = PLAYER_SPAWN_INVULN_TICKS;
	g_ValeVX = 0;
	g_ValeVY = 0;
	s_LevelX = 0;
	s_LevelY = 0;
	if (g_JungleOn)
		JUNGLE(Jungle_GetUp);
	if (g_QuestOn)
	{
		QUEST(Wulf_GetUp);
		QUEST(Quest_EnterRoom);   // the guardian back on its ring
	}
	if (g_GameOn)
		GAME(Game_GetUp);
}

// Simulation.stepEscape: control locked, he walks up into the arch's mouth,
// then the game is won.
static void Escape()
{
	g_ValeModeTick++;
	g_ValeFacing = FACE_N;
	Stride();
	i32 d = ((i32)(g_RoomX + EXIT_ZONE_X + EXIT_ZONE_W / 2) << FIXED_SHIFT) - g_ValeX;
	i32 step = d < 0 ? -d : d;
	if (step > PLAYER_SPEED_X_FP)
		step = PLAYER_SPEED_X_FP;
	g_ValeX += d < 0 ? -step : step;
	g_ValeY -= PLAYER_SPEED_Y_FP;   // no collision: the arch is the way out
	if (g_ValeModeTick >= ESCAPE_WALK_TICKS)
	{
		// win: the bonuses for getting out, for time, for the lives left.
		Score_Add(SCORE_ESCAPE);
		u32 t = g_SimTick / SCORE_TIME_DIVISOR;
		Score_Add(t >= SCORE_TIME_MAX ? 0 : (u16)(SCORE_TIME_MAX - t));
		Score_Add((u16)g_ValeLives * SCORE_LIFE_LEFT);
		g_ValeMode = MODE_WON;
		g_ValeModeTick = 0;
	}
}

// The guardians' clock (quest.h), a tick on: GuardOrbit.ringPoint's index
// into the sine table, tick * 256 / ORBIT_TICKS_PER_REV mod 256, counted.
#if ORBIT_TICKS_PER_REV > 256
#error "the orbit's clock takes a turn of 256 ticks at most"
#endif
u8 g_OrbitTick = 0;
u8 g_OrbitIndex = 0;
u8 g_OrbitRem = 0;

void Quest_ClockReset()
{
	g_OrbitTick = 0;
	g_OrbitIndex = 0;
	g_OrbitRem = 0;
}

void Quest_Clock()
{
	if (++g_OrbitTick == ORBIT_TICKS_PER_REV)
	{
		Quest_ClockReset();
		return;
	}
	u16 rem = g_OrbitRem + 256 % ORBIT_TICKS_PER_REV;
	g_OrbitIndex += 256 / ORBIT_TICKS_PER_REV;
	if (rem >= ORBIT_TICKS_PER_REV)
	{
		rem -= ORBIT_TICKS_PER_REV;
		g_OrbitIndex++;
	}
	g_OrbitRem = (u8)rem;
}

bool Vale_Tick(u8 keys)
{
	g_SimTick++;
	Quest_Clock();
	if (g_OrchidHere && ++g_OrchidPhase == ORCHID_CYCLE_TICKS)
		g_OrchidPhase = 0;   // the room's orchid's cycle (game.h)
	bool firePressed = (keys & VALE_FIRE) && !s_FireWas;
	s_FireWas = (keys & VALE_FIRE) != 0;
	NextTick();
	switch (g_ValeMode)
	{
	case MODE_GAME_OVER:
		// §17.5: fire, while continues are left, carries on: back where he
		// fell with a new game's lives, all he found kept, the score from nothing.
		if (g_GameOn && firePressed && g_ContinuesUsed < CONTINUES_START)
		{
			g_ContinuesUsed++;
			g_Score = 0;   // the best of the run is kept (g_BestScore)
			g_ValeLives = g_LivesStart;
			GetUp();
		}
		Looks();
		return FALSE;
	case MODE_DYING:
		// §11.7: the jungle carries on around him.
		if (g_JungleOn)
		{
			JUNGLE(Jungle_TickCreatures);
			JUNGLE(Jungle_TickBeasts);
		}
		if (g_QuestOn)
			QUEST(Wulf_Prowl);   // prowlWhileDown
		if (++g_ValeModeTick >= PLAYER_DEATH_ANIM_TICKS)
		{
			g_ValeMode = MODE_DOWN;
			g_ValeModeTick = 0;
		}
		Looks();
		return FALSE;
	case MODE_DOWN:
		if (g_JungleOn)
		{
			JUNGLE(Jungle_TickCreatures);
			JUNGLE(Jungle_TickBeasts);
		}
		if (g_QuestOn)
			QUEST(Wulf_Prowl);
		if (++g_ValeModeTick >= PLAYER_DEATH_FREEZE_TICKS)
			Respawn();
		Looks();
		return FALSE;
	case MODE_ESCAPING:
		Escape();
		Looks();
		return FALSE;
	case MODE_WON:
		Looks();
		return FALSE;
	}
	if (g_ValeFreeze)
	{
		g_ValeFreeze--;   // the flip's hitch: nothing moves
		Looks();
		return FALSE;
	}
	if (g_ValeInvuln > 0)
		g_ValeInvuln--;
	if (g_EffectKind == EFFECT_KNOCKDOWN)
	{
		keys = 0;           // knocked down: he neither walks nor fights until it wears off
		g_ValeSwing = -1;   // the blade too
	}
	UpdateSwing((keys & VALE_FIRE) != 0);
	Move(keys);
	if (UpdateRoom())
	{
		g_SabreW = 0;   // no blade on the tick he changes rooms
		g_SabreH = 0;
		Looks();
		return TRUE;
	}
	ComputeSabre();
	if (g_JungleOn)
	{
		JUNGLE(Jungle_TickCreatures);
		JUNGLE(Jungle_TickSwarm);
		JUNGLE(Jungle_TickBeasts);
		if (g_GameOn)
			JUNGLE(Jungle_TickFlame);
	}
	if (g_QuestOn)
	{
		QUEST(Wulf_Prowl);
		QUEST(Quest_Tick);
	}
	if (g_GameOn)
		GAME(Game_Orchids);
	if (g_JungleOn)
		JUNGLE(Jungle_Combat);
	Looks();
	return FALSE;
}

//-----------------------------------------------------------------------------
// The frame he is drawn in (PlayerPainter.frameFor, alive)

u8 Vale_Frame()
{
	// PlayerPainter.frameFor: falling, the die animation, held on its last frame once down.
	if (g_ValeMode == MODE_DYING)
	{
		u8 f = g_ValeModeTick / PLAYER_DIE_TICKS_PER_FRAME;
		return PLAYER_FRAME_DIE + (f < PLAYER_DIE_FRAMES ? f : PLAYER_DIE_FRAMES - 1);
	}
	if (g_ValeMode == MODE_DOWN || g_ValeMode == MODE_GAME_OVER)
		return PLAYER_FRAME_DIE + PLAYER_DIE_FRAMES - 1;
	i8 dx = c_FaceDX[g_ValeFacing];
	u8 f = dx > 0 ? PLAYER_VIEW_SIDE * PLAYER_FRAMES_PER_VIEW
		: dx < 0 ? PLAYER_VIEW_SIDE_L * PLAYER_FRAMES_PER_VIEW
		: c_FaceDY[g_ValeFacing] < 0 ? PLAYER_VIEW_UP * PLAYER_FRAMES_PER_VIEW : PLAYER_VIEW_DOWN * PLAYER_FRAMES_PER_VIEW;
	if (g_ValeSwing < 0)
		return f + g_ValeGait;
	u8 pose = s_Pose[(u8)g_ValeSwing];
	if (g_ValeWalk > 0)
		return f + PLAYER_FRAME_POSE_GAIT + pose * PLAYER_GAIT + g_ValeGait;   // the pose on walking legs
	return f + PLAYER_FRAME_POSE + pose;
}
