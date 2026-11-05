// Wulf Quest for MSX2: Ranger Vale, walking and fighting (AGENTS.md §10, M3).
//
// The Java game's rules, in the Java game's order (its Simulation.updateSwing,
// move and computeSabre; AGENTS.md §7.4, §11): integers and 8.8 fixed point
// only, so the MSX2 moves him exactly as the Java game does, tick for tick.
// test/moves.sh holds the two to that.
//
// For now the room on screen is the only room: outside it is solid. Flipping
// to the next room is M4.

#include "vale.h"
#include "constants.h"
#include "player.h"

#define ONE			(1 << FIXED_SHIFT)
#define LEVELS		(PLAYER_MOMENTUM_MAX / PLAYER_MOMENTUM_STEP)
#define SWING_TICKS	(SABRE_WINDUP_TICKS + SABRE_ACTIVE_TICKS + SABRE_RECOVER_TICKS)
#define WORLD_COLS	(GRID_W * CELL_COLS)
#define WORLD_ROWS	(GRID_H * CELL_ROWS)
#if PLAYER_WALK_TICKS_PER_FRAME < 1 || PLAYER_MOMENTUM_TICKS_PER_FRAME < 1
#error "vale.c counts walk and momentum frames, so both must be at least a tick"
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
u8  g_ValeOpen = 0;   // the rest are set by Vale_Init and Vale_Place

static i8 s_LevelX;
static i8 s_LevelY;
static u8 s_FramePhase;   // tick % PLAYER_MOMENTUM_TICKS_PER_FRAME
static u8 s_GaitTick;     // g_ValeWalk % PLAYER_WALK_TICKS_PER_FRAME
static u8 s_Gait;         // g_ValeWalk / PLAYER_WALK_TICKS_PER_FRAME % PLAYER_GAIT
static i16 s_RoomX;       // the room's top-left, world pixels
static i16 s_RoomY;

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

// The room's cells, a byte each, unpacked from its mask when he is placed:
// a byte is quicker to test than a bit.
static u8 s_Cells[CELL_ROWS * CELL_COLS];

static void UnpackCells(const u8* mask)
{
	u8* c = s_Cells;
	for (u8 i = 0; i < ROOM_MASK_BYTES; i++)
	{
		u8 m = mask[i];
		for (u8 b = 0; b < 8; b++)
		{
			*c++ = (m & 0x80) ? 1 : 0;
			m <<= 1;
		}
	}
}

void Vale_Init()
{
	for (u8 l = 0; l <= LEVELS; l++)
	{
		s_SpeedX[l] = (i16)(PLAYER_SPEED_X_FP * l / LEVELS);
		s_SpeedY[l] = (i16)(PLAYER_SPEED_Y_FP * l / LEVELS);
		// Fixed.mul: (a * b) >> 8
		s_DiagX[l] = (i16)(((i32)s_SpeedX[l] * PLAYER_DIAGONAL_SCALE_FP) >> FIXED_SHIFT);
		s_DiagY[l] = (i16)(((i32)s_SpeedY[l] * PLAYER_DIAGONAL_SCALE_FP) >> FIXED_SHIFT);
	}
	for (u8 v = 0; v <= PLAYER_MOMENTUM_MAX; v++)
		s_LevelOf[v] = v / PLAYER_MOMENTUM_STEP;
	// PlayerPainter.phaseOf: on guard, then the lunge, a window each; the recovery after the blade.
	for (u8 t = 0; t < SWING_TICKS; t++)
		s_Pose[t] = t < SABRE_WINDUP_TICKS ? 0
			: t - SABRE_WINDUP_TICKS >= SABRE_ACTIVE_TICKS ? 2
			: ((t - SABRE_WINDUP_TICKS) / SABRE_FENCE_EVERY_TICKS) % 2;
}

void Vale_Place(i32 x, i32 y, const RoomData* room, u8 roomIndex)
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
	s_GaitTick = 0;
	s_Gait = 0;
	UnpackCells(room->Mask);
	s_RoomX = (i16)(roomIndex % GRID_W) * PLAYFIELD_W;
	s_RoomY = (i16)(roomIndex / GRID_W) * PLAYFIELD_H;
}

//-----------------------------------------------------------------------------
// Collision (§7.4): the feet box against the cells.

// Whether the feet box, with his feet at whole pixel (x, y) of the world,
// overlaps a solid cell. The room's edges are cell edges, so a box outside
// the room in pixels is in a cell outside it, which is solid until M4.
static bool Blocked(i16 x, i16 y)
{
	i16 left = x + PLAYER_BOX_X;
	i16 top = y + PLAYER_BOX_Y;
	if (g_ValeOpen)
		return left < 0 || top < 0
			|| left + (PLAYER_BOX_W - 1) >= WORLD_COLS * CELL
			|| top + (PLAYER_BOX_H - 1) >= WORLD_ROWS * CELL;
	left -= s_RoomX;
	top -= s_RoomY;
	if (left < 0 || top < 0 || left + (PLAYER_BOX_W - 1) >= PLAYFIELD_W || top + (PLAYER_BOX_H - 1) >= PLAYFIELD_H)
		return TRUE;
	u8 cx0 = (u8)left >> 3;
	u8 cx1 = (u8)(left + (PLAYER_BOX_W - 1)) >> 3;
	u8 cy0 = (u8)top >> 3;
	u8 cy1 = (u8)(top + (PLAYER_BOX_H - 1)) >> 3;
	const u8* row = s_Cells + (u16)cy0 * CELL_COLS;
	for (u8 cy = cy0; cy <= cy1; cy++)
	{
		for (u8 cx = cx0; cx <= cx1; cx++)
			if (row[cx])
				return TRUE;
		row += CELL_COLS;
	}
	return FALSE;
}

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

// One axis (Collision.resolve): free, the target; blocked, the nearest free
// whole pixel between here and the target; none, here, fraction and all.
// The search runs in whole pixels: a whole pixel c is past pos going up when
// c > px(pos), and going down when c < px(pos), or c == px(pos) and pos has a
// fraction.
static i32 Resolve(i32 pos, i16 delta, i16 other, bool alongX)
{
	if (delta == 0)
		return pos;
	i32 target = pos + delta;
	i16 t = FP_PX(target);
	if (!(alongX ? Blocked(t, other) : Blocked(other, t)))
		return target;
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
		g_ValeSwing = 0;
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

static void Move(u8 keys)
{
	i8 dx = ((keys & VALE_RIGHT) ? 1 : 0) - ((keys & VALE_LEFT) ? 1 : 0);
	i8 dy = ((keys & VALE_DOWN) ? 1 : 0) - ((keys & VALE_UP) ? 1 : 0);
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
		g_ValeWalk++;
		if (++s_GaitTick >= PLAYER_WALK_TICKS_PER_FRAME)
		{
			s_GaitTick = 0;
			if (++s_Gait >= PLAYER_GAIT)
				s_Gait = 0;
		}
	}
	else
	{
		g_ValeWalk = 0;
		s_GaitTick = 0;
		s_Gait = 0;
	}
}

void Vale_Tick(u8 keys)
{
	UpdateSwing((keys & VALE_FIRE) != 0);
	Move(keys);
	ComputeSabre();
}

//-----------------------------------------------------------------------------
// The frame he is drawn in (PlayerPainter.frameFor, alive)

u8 Vale_Frame()
{
	i8 dx = c_FaceDX[g_ValeFacing];
	u8 f = dx > 0 ? PLAYER_VIEW_SIDE * PLAYER_FRAMES_PER_VIEW
		: dx < 0 ? PLAYER_VIEW_SIDE_L * PLAYER_FRAMES_PER_VIEW
		: c_FaceDY[g_ValeFacing] < 0 ? PLAYER_VIEW_UP * PLAYER_FRAMES_PER_VIEW : PLAYER_VIEW_DOWN * PLAYER_FRAMES_PER_VIEW;
	if (g_ValeSwing < 0)
		return f + s_Gait;
	u8 pose = s_Pose[(u8)g_ValeSwing];
	if (g_ValeWalk > 0)
		return f + PLAYER_FRAME_POSE_GAIT + pose * PLAYER_GAIT + s_Gait;   // the pose on walking legs
	return f + PLAYER_FRAME_POSE + pose;
}
