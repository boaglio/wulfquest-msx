// Wulf Quest for MSX2: where Vale gets up (AGENTS.md §10, M6; the Java
// game's SpawnFinder, §11.7).
//
// The Java game's answer, found another way. It floods every whole-pixel
// foot position he can walk to from where he fell, in the room and 8 px
// past its edges: 273 x 193 positions, each a box test, far too many for a
// Z80. But a position is blocked or not by the cells its box covers, and
// along x the box covers the same columns for a run of positions, along y
// the same rows: so the positions fall into classes (a run of x by a run of
// y), all of a class free or all blocked, and he walks from one class to
// the next exactly when both are free. A flood of the classes, a few
// hundred, is the Java game's flood of the positions.
//
// Then, as the Java game does, breadth-first across the room's cells from
// where he fell, the first cell whose bottom-centre fits (inside the room's
// margins, in the room, not blocked) and that he can walk to. If none keeps
// the margins, the margins give way; if he could not stand where he fell,
// the walk is not asked.

#include "spawn.h"
#include "vale.h"
#include "data.h"
#include "constants.h"

#define SLACK		8   // SpawnFinder.SLACK_PX: how far past the room's edges the walk goes
#define FEET_W		(PLAYFIELD_W + 2 * SLACK + 1)
#define FEET_H		(PLAYFIELD_H + 2 * SLACK + 1)
#define COLS		CELL_COLS
#define ROWS		CELL_ROWS
// Runs along an axis of n positions for a box s long: a box whose far edge
// is a whole number of cells from its near one changes cells once in 8
// positions, any other twice.
#define RUNS(n, s)	(((s) - 1) % CELL == 0 ? (n) / CELL + 2 : 2 * ((n) / CELL) + 3)
#define RUNS_X		RUNS(FEET_W, PLAYER_BOX_W)
#define RUNS_Y		RUNS(FEET_H, PLAYER_BOX_H)
#define CLASSES		(RUNS_X * RUNS_Y)
#if CELL != 8
#error "spawn.c divides by CELL with shifts"
#endif

// The runs along x and along y: the first foot position of each (from -SLACK).
static u8 s_RunX[RUNS_X + 1];
static u8 s_RunY[RUNS_Y + 1];
static u8 s_NX;
static u8 s_NY;
// Per class (run x, run y): free, and reached; 2 bits a class.
static u8 s_Class[(CLASSES + 3) / 4];
// The flood's queue: the classes', then the cells' (fewer).
static u16 s_Queue[CLASSES > COLS * ROWS ? CLASSES : COLS * ROWS];
static u8 s_Seen[COLS * ROWS];

static i16 s_RoomX;
static i16 s_RoomY;

static bool Blocked(i16 x, i16 y)
{
	return Data_BoxBlocked(x + PLAYER_BOX_X, y + PLAYER_BOX_Y, PLAYER_BOX_W, PLAYER_BOX_H, FALSE);
}

static u8 Get(u16 c)
{
	return (s_Class[c >> 2] >> ((c & 3) << 1)) & 3;
}

static void Set(u16 c, u8 bits)
{
	s_Class[c >> 2] |= bits << ((c & 3) << 1);
}

// Runs: positions p (from -SLACK) whose box covers the same cells, (p + off) >> 3
// to (p + off + size - 1) >> 3, with off the box's offset from the room's edge.
static u8 Runs(u8* runs, u16 positions, i16 off, u8 size)
{
	u8 n = 0;
	i16 lo = -1000;
	i16 hi = -1000;
	for (u16 p = 0; p < positions; p++)
	{
		i16 a = (i16)p - SLACK + off;
		i16 c0 = a >> 3;
		i16 c1 = (a + size - 1) >> 3;
		if (c0 != lo || c1 != hi)
		{
			runs[n++] = (u8)p;
			lo = c0;
			hi = c1;
		}
	}
	runs[n] = (u8)(positions > 255 ? 255 : positions);   // the end (only compared, never reached as a start)
	return n;
}

static u8 RunOf(const u8* runs, u8 n, u16 p)
{
	u8 r = 0;
	while (r + 1 < n && runs[r + 1] <= p)
		r++;
	return r;
}

// Whether the class's box, any of its positions, is free.
static bool ClassFree(u8 rx, u8 ry)
{
	return !Blocked(s_RoomX + (i16)s_RunX[rx] - SLACK, s_RoomY + (i16)s_RunY[ry] - SLACK);
}

// The classes he can walk to from a foot position (room-local): flooded into s_Class.
static bool Walkable(i16 sx, i16 sy)
{
	if (sx < -SLACK || sy < -SLACK || sx > PLAYFIELD_W + SLACK || sy > PLAYFIELD_H + SLACK
		|| Blocked(s_RoomX + sx, s_RoomY + sy))
		return FALSE;
	s_NX = Runs(s_RunX, FEET_W, PLAYER_BOX_X, PLAYER_BOX_W);
	s_NY = Runs(s_RunY, FEET_H, PLAYER_BOX_Y, PLAYER_BOX_H);
	for (u16 i = 0; i < sizeof(s_Class); i++)
		s_Class[i] = 0;
	for (u8 ry = 0; ry < s_NY; ry++)
		for (u8 rx = 0; rx < s_NX; rx++)
			if (ClassFree(rx, ry))
				Set((u16)ry * s_NX + rx, 1);
	u16 start = (u16)RunOf(s_RunY, s_NY, sy + SLACK) * s_NX + RunOf(s_RunX, s_NX, sx + SLACK);
	u16 head = 0;
	u16 tail = 0;
	Set(start, 2);
	s_Queue[tail++] = start;
	while (head < tail)
	{
		u16 c = s_Queue[head++];
		u8 rx = c % s_NX;
		u8 ry = c / s_NX;
		for (u8 k = 0; k < 4; k++)
		{
			i8 nx = (i8)rx + (k == 0 ? 1 : k == 1 ? -1 : 0);
			i8 ny = (i8)ry + (k == 2 ? 1 : k == 3 ? -1 : 0);
			if (nx < 0 || ny < 0 || (u8)nx >= s_NX || (u8)ny >= s_NY)
				continue;
			u16 n = (u16)ny * s_NX + (u8)nx;
			if (Get(n) == 1)
			{
				Set(n, 2);
				s_Queue[tail++] = n;
			}
		}
	}
	return TRUE;
}

static bool Reached(i16 lx, i16 ly)
{
	return Get((u16)RunOf(s_RunY, s_NY, ly + SLACK) * s_NX + RunOf(s_RunX, s_NX, lx + SLACK)) & 2;
}

// SpawnFinder.fits: inside the margins (all, or none), in the room, not blocked.
static bool Fits(i16 x, i16 y, bool margins)
{
	i16 lx = x - s_RoomX;
	i16 ly = y - s_RoomY;
	if (margins ? (lx < PLAYER_SPAWN_INSIDE_LEFT || lx > PLAYFIELD_W - PLAYER_SPAWN_INSIDE_RIGHT
			|| ly < PLAYER_SPAWN_INSIDE_TOP || ly > PLAYFIELD_H - PLAYER_SPAWN_INSIDE_BOTTOM)
		: (lx < 0 || lx > PLAYFIELD_W || ly < 0 || ly > PLAYFIELD_H))
		return FALSE;
	// Simulation.roomColOf, roomRowOf: the feet box's centre is in this room.
	i16 cx = lx + PLAYER_BOX_X + PLAYER_BOX_W / 2;
	i16 cy = ly + PLAYER_BOX_Y + PLAYER_BOX_H / 2;
	if (cx < 0 || cx >= PLAYFIELD_W || cy < 0 || cy >= PLAYFIELD_H)
	{
		// At the map's edge the room is clamped: the centre may be past it.
		u8 col = g_ValeRoom % GRID_W;
		u8 row = g_ValeRoom / GRID_W;
		if ((cx < 0 && col > 0) || (cx >= PLAYFIELD_W && col < GRID_W - 1)
			|| (cy < 0 && row > 0) || (cy >= PLAYFIELD_H && row < GRID_H - 1))
			return FALSE;
	}
	return !Blocked(x, y);
}

// SpawnFinder.nearest: breadth-first across the room's cells.
static bool Nearest(i16 x, i16 y, bool margins, bool walk, i16* ox, i16* oy)
{
	i16 sx = (x - s_RoomX) / CELL;   // Java's division: toward zero
	i16 sy = (y - 1 - s_RoomY) / CELL;
	u8 startCx = sx < 0 ? 0 : sx > COLS - 1 ? COLS - 1 : (u8)sx;
	u8 startCy = sy < 0 ? 0 : sy > ROWS - 1 ? ROWS - 1 : (u8)sy;
	for (u16 i = 0; i < COLS * ROWS; i++)
		s_Seen[i] = 0;
	u16 head = 0;
	u16 tail = 0;
	u16 start = (u16)startCy * COLS + startCx;
	s_Seen[start] = 1;
	s_Queue[tail++] = start;
	while (head < tail)
	{
		u16 i = s_Queue[head++];
		u8 cx = i % COLS;
		u8 cy = i / COLS;
		i16 lx = (i16)cx * CELL + CELL / 2;
		i16 ly = (i16)cy * CELL + CELL;
		if (Fits(s_RoomX + lx, s_RoomY + ly, margins) && (!walk || Reached(lx, ly)))
		{
			*ox = s_RoomX + lx;
			*oy = s_RoomY + ly;
			return TRUE;
		}
		for (u8 k = 0; k < 4; k++)
		{
			i8 nx = (i8)cx + (k == 0 ? 1 : k == 1 ? -1 : 0);
			i8 ny = (i8)cy + (k == 2 ? 1 : k == 3 ? -1 : 0);
			if (nx < 0 || ny < 0 || nx >= COLS || ny >= ROWS)
				continue;
			u16 n = (u16)ny * COLS + (u8)nx;
			if (!s_Seen[n])
			{
				s_Seen[n] = 1;
				s_Queue[tail++] = n;
			}
		}
	}
	return FALSE;
}

void Spawn_NearestFree(i32* xFp, i32* yFp)
{
	s_RoomX = g_RoomX;
	s_RoomY = g_RoomY;
	i16 x = FP_PX(*xFp);
	i16 y = FP_PX(*yFp);
	if (Fits(x, y, TRUE))
		return;   // where he fell, fraction and all
	i16 nx;
	i16 ny;
	bool found;
	if (!Walkable(x - s_RoomX, y - s_RoomY))
		found = Nearest(x, y, TRUE, FALSE, &nx, &ny);   // nowhere to walk from: cells alone
	else
	{
		found = Nearest(x, y, TRUE, TRUE, &nx, &ny);
		if (!found)
			found = Nearest(x, y, FALSE, TRUE, &nx, &ny);
	}
	if (!found)
		return;   // where he stood is at least not a cell
	*xFp = (i32)nx << FIXED_SHIFT;
	*yFp = (i32)ny << FIXED_SHIFT;
}
