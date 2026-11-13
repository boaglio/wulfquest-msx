// Wulf Quest for MSX2: the looking glass's trail on the map (AGENTS.md §10,
// M12; the Java game's WayPainter.route and MapScreen.paintTrail).
//
// The Java game floods the whole jungle, 512x352 cells, from the quarters
// still out there and follows the flood down from Vale's feet: too much for a
// Z80. The converter (Trail.java, which is this file's reference, step for
// step) cuts the jungle into its rooms' openings: runs of cells along a
// room's edge where the feet fit on both sides, at most six a room, and the
// steps between them within each room (trail_N.bin, read a room at a time
// with Data_TrailRoom). Here, over a few calls a tick while the map is open:
//
//   1. each room with a quarter still out there (or, all taken, the arch's)
//      is flooded from it: its openings' steps to it;
//   2. each opening's steps to the nearest (D): the least, over its room's
//      openings, of the steps to one, one across, and the other side's D;
//      a queue of rooms, a room's openings worked out together;
//   3. room by room from Vale: the room flooded from its openings, each cell
//      of one starting at one across plus the other side's D (and from the
//      quarters there, at 0), followed down from where he stands or came in,
//      the Java game's way (its neighbours' order), and across where it ends.
//      The opening he came in by is left out: its cells share one D, and it
//      could otherwise send him back.
//
// Its arrays are the respawn's (trail.h: no respawn while the map is open):
// the flood's marks, a byte a cell (bits 0-1: its steps modulo 3, 3 not
// reached; bit 2: the feet fit; bits 3-5: the seed that started it, a side's
// index + 1, or TRAIL_KIND_TARGET), its queue, D, the trail's steps, the
// rooms it touches. The room's cells go where Vale's room keeps its own
// (vale.c), the rooms' queue too, put back at the end or by Trail_Stop. D is
// kept in video RAM (TRAIL_D_VRAM) for the next time the map is opened with
// the same quarters taken.

#include "trail.h"
#include "vale.h"
#include "quest.h"
#include "quest_data.h"
#include "constants.h"
#include "shell_data.h"

#if CELL_COLS != 32 || CELL_COLS != 2 * MINI_W || CELL_ROWS != 2 * MINI_H
#error "trail.c takes a room as 32 cells a row, a map pixel for 2x2"
#endif
#if RAM_TRAIL_END > RAM_TABLES || RAM_SPAWN_CLASS + RAM_SPAWN_CLASS_BYTES != RAM_SPAWN_SEEN \
	|| RAM_SPAWN_SEEN + RAM_SPAWN_SEEN_BYTES != RAM_SPAWN_QUEUE
#error "the trail's arrays do not fit the respawn's"
#endif
#if RAM_CELLS_BYTES < 256 + 32
#error "the rooms' queue does not fit the cells' place"
#endif

// trail.h's places.
static __at(RAM_TRAIL_MARK) u8 s_Mark[TRAIL_CELLS];
static __at(RAM_TRAIL_QUEUE) u16 s_Q[TRAIL_CELLS];
static __at(RAM_TRAIL_D) u16 s_D[TRAIL_SIDES];
static __at(RAM_CELLS) u8 s_TCells[RAM_CELLS_BYTES];   // the room's cells (Vale's, put back)
#define s_Ring		s_TCells           // the rooms' queue
#define s_InQ		(s_TCells + 256)   // a bit a room
#define SEEDS_MAX	TRAIL_SEEDS_MAX
static __at(RAM_TRAIL_SEEDS) u16 s_SeedCell[SEEDS_MAX];
static __at(RAM_TRAIL_SEEDS + SEEDS_MAX * 2) u16 s_SeedVal[SEEDS_MAX];
static __at(RAM_TRAIL_SEEDS + SEEDS_MAX * 4) u8 s_SeedKind[SEEDS_MAX];
static __at(RAM_TRAIL_REC) u8 s_Rec[TRAIL_RECORD_MAX];                       // the room's record (Trail.record)
static __at(RAM_TRAIL_W) u8 s_W[TRAIL_SIDES_MAX * TRAIL_SIDES_MAX];          // steps(i, j) at i * TRAIL_SIDES_MAX + j
static __at(RAM_TRAIL_OTHER) u16 s_Other[TRAIL_SIDES_MAX];                   // each side's other side
static __at(RAM_TRAIL_EDGE) u8 s_REdge[TRAIL_SIDES_MAX];                     // and its edge

u8  g_TrailX0 = 0;
u8  g_TrailY0 = 0;
u16 g_TrailN = 0;
u8  g_TrailWhole = 0;
u8  g_TrailStage = TRAIL_IDLE;
u8  g_TrailKey = 0xFF;
u8  g_TrailDone = 0;

static u8 s_K;                       // its sides
static u16 s_First;                  // its first side's number
static u8 s_Edge[TRAIL_SIDES_MAX];   // the sides found on the room's edges
static u8 s_Start[TRAIL_SIDES_MAX];
static u8 s_Len[TRAIL_SIDES_MAX];
static u8 s_N;                       // seeds
static u8 s_Base;                    // this game's lairs in the tables: set * LAIRS
static u8 s_Rooms[LAIRS + 1];        // the quarters' rooms (or the arch's)
static u8 s_RoomsN;
static u8 s_RoomsI;
static u8 s_Head;                    // the rooms' queue
static u8 s_Tail;
static u8 s_Room;                    // the trail's room now
static i8 s_SX;                      // where it starts in it (cells)
static i8 s_SY;
static u8 s_First1;                  // the first room: from his feet
static u16 s_Entered;                // the side it came in by
static u8 s_Hops;
static u8 s_LastU;
static u8 s_LastV;
static u16 s_Tail16;                 // the flood's queue's end

// v modulo 3, by subtraction (banked code: no library).
static u8 Mod3(u16 v)
{
	while (v >= 768)
		v -= 768;
	while (v >= 96)
		v -= 96;
	while (v >= 12)
		v -= 12;
	while (v >= 3)
		v -= 3;
	return (u8)v;
}

static u8 RowOf(u8 room) { return room >> 4; }

// The room's record (Data_TrailRoom), unpacked: its sides, the first's number,
// each one's other side and edge, the steps between them (i < j in the record).
static void Record(u8 room)
{
	Data_TrailRoom(room, s_Rec);
	s_K = s_Rec[0];
	s_First = s_Rec[1] | ((u16)s_Rec[2] << 8);
	const u8* p = s_Rec + 3;
	for (u8 i = 0; i < s_K; i++, p += 2)
	{
		s_Other[i] = p[0] | ((u16)(p[1] & 3) << 8);
		s_REdge[i] = (p[1] >> 4) & 3;
	}
	u8* row = s_W;
	for (u8 i = 0; i < s_K; i++, row += TRAIL_SIDES_MAX)
	{
		row[i] = 0;
		u8* col = s_W + i + (u8)(i + 1) * TRAIL_SIDES_MAX;
		for (u8 j = i + 1; j < s_K; j++, col += TRAIL_SIDES_MAX)
		{
			row[j] = *p;
			*col = *p++;
		}
	}
}

#define Partner(i)	s_Other[i]
#define EdgeOf(i)	s_REdge[i]

static u8 Neighbour(u8 room, u8 edge)
{
	return edge == 0 ? room - GRID_W : edge == 1 ? room + 1 : edge == 2 ? room + GRID_W : room - 1;
}

// WorldGrid.blockPassable at a cell of the room, its edges' neighbours included.
static u8 TFits(i8 x, i8 y)
{
	const u8* c = s_TCells + (u16)(u8)(y + CELLS_MARGIN) * CELLS_W + (u8)(x + CELLS_MARGIN);
	return !(c[0] | c[1] | c[CELLS_W] | c[CELLS_W + 1]);
}

// Each cell of the room: 7 where the feet fit (none of its 2x2 cells solid), not
// reached; 3 where they do not. In assembly: it runs for every room of the trail.
#if CELLS_W - CELL_COLS != 4
#error "Marks steps four cells from a row's end to the next's start"
#endif
static void Marks() __naked
{
	__asm
		push	ix
		ld	hl, #_s_TCells + (CELLS_MARGIN * CELLS_W + CELLS_MARGIN)         ; row 0, cell 0
		ld	ix, #_s_TCells + ((CELLS_MARGIN + 1) * CELLS_W + CELLS_MARGIN)   ; the row under it
		ld	de, #_s_Mark
		ld	c, #CELL_ROWS
	00001$:
		ld	b, #CELL_COLS
	00002$:
		ld	a, (hl)
		inc	hl
		or	a, (hl)
		or	a, 0 (ix)
		or	a, 1 (ix)
		inc	ix
		ld	a, #3
		jr	nz, 00003$
		ld	a, #7
	00003$:
		ld	(de), a
		inc	de
		djnz	00002$
		inc	hl                                      ; to the next row's cell 0
		inc	hl
		inc	hl
		inc	hl
		inc	ix
		inc	ix
		inc	ix
		inc	ix
		dec	c
		jr	nz, 00001$
		pop	ix
		ret
	__endasm;
}

// The room's cells, its marks (fits, not reached), its sides on its edges (Trail.runs).
static void Load(u8 room)
{
	Data_LoadCells(room, s_TCells);
	Marks();
	u8 col = room & 15;
	u8 row = RowOf(room);
	u8 n = 0;
	for (u8 edge = 0; edge < 4; edge++)
	{
		if ((edge == 0 && row == 0) || (edge == 1 && col == GRID_W - 1)
			|| (edge == 2 && row == GRID_H - 1) || (edge == 3 && col == 0))
			continue;
		u8 len = edge == 0 || edge == 2 ? CELL_COLS : CELL_ROWS;
		i8 start = -1;
		for (u8 a = 0; a <= len; a++)
		{
			u8 open = 0;
			if (a < len)
			{
				i8 x = edge == 0 || edge == 2 ? (i8)a : edge == 1 ? CELL_COLS - 1 : 0;
				i8 y = edge == 1 || edge == 3 ? (i8)a : edge == 2 ? CELL_ROWS - 1 : 0;
				i8 ox = x + (edge == 1 ? 1 : edge == 3 ? -1 : 0);
				i8 oy = y + (edge == 2 ? 1 : edge == 0 ? -1 : 0);
				open = TFits(x, y) && TFits(ox, oy);
			}
			if (open && start < 0)
				start = (i8)a;
			if (!open && start >= 0)
			{
				if (n < TRAIL_SIDES_MAX)
				{
					s_Edge[n] = edge;
					s_Start[n] = (u8)start;
					s_Len[n] = a - (u8)start;
				}
				n++;
				start = -1;
			}
		}
	}
	if (n != s_K)
		s_K = 0;   // not the converter's: no trail through here
}

static u16 SideCell(u8 i, u8 c)
{
	u8 a = s_Start[i] + c;
	u8 e = s_Edge[i];
	u8 x = e == 0 || e == 2 ? a : e == 1 ? CELL_COLS - 1 : 0;
	u8 y = e == 1 || e == 3 ? a : e == 2 ? CELL_ROWS - 1 : 0;
	return ((u16)y << 5) | x;
}

// Wayfinder.nearest within the room: the fitting cell nearest (lx, ly) within
// TRAIL_NEAR, by steps on both axes, the first in rows then columns; reached
// too if asked. 0xFFFF: none.
static u16 TNearest(i8 lx, i8 ly, u8 reached)
{
	u16 best = 0xFFFF;
	u8 bestD = 0xFF;
	for (i8 y = ly - TRAIL_NEAR; y <= ly + TRAIL_NEAR; y++)
	{
		if (y < 0 || y >= CELL_ROWS)
			continue;
		for (i8 x = lx - TRAIL_NEAR; x <= lx + TRAIL_NEAR; x++)
		{
			if (x < 0 || x >= CELL_COLS)
				continue;
			u16 ci = ((u16)(u8)y << 5) | (u8)x;
			u8 m = s_Mark[ci];
			if (!(m & 4) || (reached && (m & 3) == 3))
				continue;
			u8 d = (u8)(x > lx ? x - lx : lx - x) + (u8)(y > ly ? y - ly : ly - y);
			if (d < bestD)
			{
				bestD = d;
				best = ci;
			}
		}
	}
	return best;
}

static void Seed(u16 cell, u16 val, u8 kind)
{
	if (s_N < SEEDS_MAX)
	{
		s_SeedCell[s_N] = cell;
		s_SeedVal[s_N] = val;
		s_SeedKind[s_N] = kind;
		s_N++;
	}
}

// The quarters still out there in the room (the lairs' order), or the arch's mouth: seeds at 0.
static void Targets(u8 room)
{
	for (u8 i = 0; i < LAIRS; i++)
	{
		if ((g_TrailDone & (1 << i)) || g_LairRoom[s_Base + i] != room)
			continue;
		u16 c = TNearest((i8)((i16)(g_LairPedX[s_Base + i] + PLAYER_BOX_X) >> 3),
			(i8)((i16)(g_LairPedY[s_Base + i] + PLAYER_BOX_Y) >> 3), FALSE);
		if (c != 0xFFFF)
			Seed(c, 0, TRAIL_KIND_TARGET);
	}
	if (g_TrailDone == (1 << LAIRS) - 1 && room == EXIT_ROOM)
	{
		u16 c = TNearest((i8)((EXIT_ZONE_X + EXIT_ZONE_W / 2 + PLAYER_BOX_X) >> 3),
			(i8)((EXIT_ZONE_Y + EXIT_ZONE_H + PLAYER_BOX_Y) >> 3), FALSE);
		if (c != 0xFFFF)
			Seed(c, 0, TRAIL_KIND_TARGET);
	}
}

static void Try(u16 n, u8 nm)
{
	u8 m = s_Mark[n];
	if ((m & 7) == 7)
	{
		s_Mark[n] = (m & ~3) | nm;
		s_Q[s_Tail16++] = n;
	}
}

// Trail.flood: from the seeds in their steps' order (stable), each joining as the flood comes to it.
static void Flood()
{
	// The seeds sorted by their steps, the first kept first among equals.
	for (u8 i = 1; i < s_N; i++)
	{
		u16 c = s_SeedCell[i];
		u16 v = s_SeedVal[i];
		u8 k = s_SeedKind[i];
		u8 j = i;
		while (j > 0 && s_SeedVal[j - 1] > v)
		{
			s_SeedCell[j] = s_SeedCell[j - 1];
			s_SeedVal[j] = s_SeedVal[j - 1];
			s_SeedKind[j] = s_SeedKind[j - 1];
			j--;
		}
		s_SeedCell[j] = c;
		s_SeedVal[j] = v;
		s_SeedKind[j] = k;
	}
	if (s_N == 0)
		return;
	u16 head = 0;
	s_Tail16 = 0;
	u8 si = 0;
	u16 val = s_SeedVal[0];
	u8 vm = Mod3(val);
	for (;;)
	{
		while (si < s_N && s_SeedVal[si] <= val)
		{
			u16 c = s_SeedCell[si];
			u8 m = s_Mark[c];
			if ((m & 7) == 7)
			{
				s_Mark[c] = (m & 4) | vm | (s_SeedKind[si] << 3);
				s_Q[s_Tail16++] = c;
			}
			si++;
		}
		if (head == s_Tail16)
		{
			if (si == s_N)
				break;
			val = s_SeedVal[si];
			vm = Mod3(val);
			continue;
		}
		u16 end = s_Tail16;
		u8 nm = vm == 2 ? 0 : vm + 1;
		while (head < end)
		{
			u16 c = s_Q[head++];
			u8 x = (u8)c & 31;
			u8 y = (u8)(c >> 5);
			if (x < CELL_COLS - 1)
				Try(c + 1, nm);
			if (x > 0)
				Try(c - 1, nm);
			if (y < CELL_ROWS - 1)
				Try(c + CELL_COLS, nm);
			if (y > 0)
				Try(c - CELL_COLS, nm);
		}
		val++;
		vm = nm;
	}
}

// Down the flood one step from cell c, the Java game's order; 0xFFFF where it ends.
static u16 Down(u16 c)
{
	u8 m = s_Mark[c] & 3;
	u8 want = m ? m - 1 : 2;   // the steps one fewer, modulo 3 (banked code: no tables)
	u8 x = (u8)c & 31;
	u8 y = (u8)(c >> 5);
	if (x < CELL_COLS - 1 && (s_Mark[c + 1] & 3) == want)
		return c + 1;
	if (x > 0 && (s_Mark[c - 1] & 3) == want)
		return c - 1;
	if (y < CELL_ROWS - 1 && (s_Mark[c + CELL_COLS] & 3) == want)
		return c + CELL_COLS;
	if (y > 0 && (s_Mark[c - CELL_COLS] & 3) == want)
		return c - CELL_COLS;
	return 0xFFFF;
}

// The openings' steps to and from video RAM, kept for the same set and lairs done.
static void DMove(u8 put)
{
	for (u16 at = 0; at < TRAIL_SIDES * 2; at += 128)
	{
		u8 n = TRAIL_SIDES * 2 - at < 128 ? (u8)(TRAIL_SIDES * 2 - at) : 128;
		if (put)
			Data_VramWrite(TRAIL_D_VRAM + at, (const u8*)s_D + at, n);
		else
			Data_VramRead(TRAIL_D_VRAM + at, (u8*)s_D + at, n);
	}
}

static void Push(u8 room)
{
	u8 bit = 1 << (room & 7);
	if (s_InQ[room >> 3] & bit)
		return;
	s_InQ[room >> 3] |= bit;
	s_Ring[s_Tail++] = room;
}

// A map pixel of the trail, and its room's mark.
static void Mark(u8 u, u8 v)
{
	u8 row = 0;
	u8 y = v;
	while (y >= MINI_H)
	{
		y -= MINI_H;
		row++;
	}
	u8 room = (row << 4) | (u >> 4);
	g_TrailRooms[room >> 3] |= 1 << (room & 7);
}

static void Pixel(u8 u, u8 v)
{
	if (g_TrailN)
	{
		if (u == s_LastU && v == s_LastV)
			return;
		if (g_TrailN == TRAIL_MAX_PX)
			return;
		u8 d = u > s_LastU ? 0 : u < s_LastU ? 1 : v > s_LastV ? 2 : 3;
		u16 i = g_TrailN - 1;
		g_TrailSteps[i >> 2] |= d << ((i & 3) << 1);
	}
	else
	{
		g_TrailX0 = u;
		g_TrailY0 = v;
	}
	g_TrailN++;
	s_LastU = u;
	s_LastV = v;
	Mark(u, v);
}

void Trail_Start()
{
	u8 set = g_QuestSet;
	s_Base = 0;
	for (u8 k = 0; k < set; k++)
		s_Base += LAIRS;
	g_TrailN = 0;
	g_TrailWhole = 0;
	for (u16 i = 0; i < TRAIL_STEP_BYTES; i++)
		g_TrailSteps[i] = 0;
	for (u8 i = 0; i < TRAIL_ROOM_BYTES; i++)
		g_TrailRooms[i] = 0;
	// The quarters' rooms (each once), or the arch's.
	s_RoomsN = 0;
	for (u8 i = 0; i < LAIRS; i++)
	{
		if (g_TrailDone & (1 << i))
			continue;
		u8 r = g_LairRoom[s_Base + i];
		u8 seen = 0;
		for (u8 j = 0; j < s_RoomsN; j++)
			seen |= s_Rooms[j] == r;
		if (!seen)
			s_Rooms[s_RoomsN++] = r;
	}
	if (s_RoomsN == 0)
		s_Rooms[s_RoomsN++] = EXIT_ROOM;
	s_RoomsI = 0;
	// The way from his feet (WayPainter.route: the feet box's top-left cell).
	s_Room = g_ValeRoom;
	s_SX = (i8)((FP_PX(g_ValeX) - g_RoomX + PLAYER_BOX_X) >> 3);
	s_SY = (i8)((FP_PX(g_ValeY) - g_RoomY + PLAYER_BOX_Y) >> 3);
	s_First1 = TRUE;
	s_Entered = 0xFFFF;
	s_Hops = 0;
	u8 key = (set << 4) | g_TrailDone;
	if (key == g_TrailKey)
	{
		DMove(FALSE);   // the openings' steps are in video RAM already
		g_TrailStage = TRAIL_ROUTE;
		return;
	}
	g_TrailKey = 0xFF;
	for (u16 i = 0; i < TRAIL_SIDES; i++)
		s_D[i] = TRAIL_NONE;   // every opening's steps unknown
	g_TrailStage = TRAIL_TARGETS;
}

// 1. A quarter's room flooded from it: its openings' steps (the least over their cells).
static void TargetRoom()
{
	u8 room = s_Rooms[s_RoomsI++];
	Record(room);
	Load(room);
	s_N = 0;
	Targets(room);
	Flood();
	for (u8 i = 0; i < s_K; i++)
	{
		u16 best = TRAIL_NONE;
		for (u8 c = 0; c < s_Len[i]; c++)
		{
			u16 ci = SideCell(i, c);
			if ((s_Mark[ci] & 3) == 3)
				continue;
			u16 n = 0;
			while ((ci = Down(ci)) != 0xFFFF)
				n++;
			if (n < best)
				best = n;
		}
		s_D[s_First + i] = best;
	}
}

// 2. The rooms' queue: a room's openings worked out from their neighbours'.
static void SidesStart()
{
	for (u8 i = 0; i < TRAIL_ROOM_BYTES; i++)
		s_InQ[i] = 0;
	s_Head = 0;
	s_Tail = 0;
	for (u8 r = 0; r < s_RoomsN; r++)
	{
		u8 room = s_Rooms[r];
		Record(room);
		Push(room);
		for (u8 i = 0; i < s_K; i++)
			if (s_D[s_First + i] != TRAIL_NONE)
				Push(Neighbour(room, EdgeOf(i)));
	}
}

static void SidesRoom()
{
	u8 room = s_Ring[s_Head++];
	s_InQ[room >> 3] &= ~(1 << (room & 7));
	Record(room);
	const u8* w = s_W;
	for (u8 i = 0; i < s_K; i++, w += TRAIL_SIDES_MAX)
	{
		u16 cur = s_D[s_First + i];
		u16 best = cur;
		for (u8 j = 0; j < s_K; j++)
		{
			u16 dp = s_D[s_Other[j]];
			if (dp == TRAIL_NONE || w[j] == 255)
				continue;
			u16 c = dp + w[j] + 1;
			if (c < best)
				best = c;
		}
		if (best < cur)
		{
			s_D[s_First + i] = best;
			Push(Neighbour(room, EdgeOf(i)));
		}
	}
}

static void SidesEnd()
{
	DMove(TRUE);
	g_TrailKey = (g_QuestSet << 4) | g_TrailDone;
}

// 3. The trail through a room: flooded from its openings (and its quarters), followed down.
static u8 RouteRoom()
{
	u8 room = s_Room;
	Record(room);
	Load(room);
	if (s_K == 0 && s_Rec[0] != 0)
		return FALSE;
	s_N = 0;
	Targets(room);
	for (u8 i = 0; i < s_K; i++)
	{
		if (s_First + i == s_Entered)
			continue;
		u16 dp = s_D[Partner(i)];
		if (dp == TRAIL_NONE)
			continue;
		for (u8 c = 0; c < s_Len[i]; c++)
			Seed(SideCell(i, c), dp + 1, i + 1);
	}
	Flood();
	u16 ci;
	if (s_First1)
	{
		ci = TNearest(s_SX, s_SY, TRUE);
		if (ci == 0xFFFF)
			return FALSE;
		s_First1 = FALSE;
	}
	else
	{
		ci = ((u16)(u8)s_SY << 5) | (u8)s_SX;
		if ((s_Mark[ci] & 3) == 3)
			return FALSE;
	}
	u8 col = room & 15;
	u8 row = RowOf(room);
	u8 u0 = col << 4;
	u8 v0 = (row << 3) + (row << 1) + row;
	for (;;)
	{
		if (g_TrailN == TRAIL_MAX_PX)
			return FALSE;
		Pixel(u0 + (((u8)ci & 31) >> 1), v0 + ((u8)(ci >> 5) >> 1));
		u16 n = Down(ci);
		if (n == 0xFFFF)
			break;
		ci = n;
	}
	u8 kind = (s_Mark[ci] >> 3) & 7;
	if (kind == TRAIL_KIND_TARGET)
	{
		g_TrailWhole = TRUE;
		// The cross's rooms too (MapScreen draws it two pixels each way).
		u8 u = s_LastU;
		u8 v = s_LastV;
		if (u >= 2)
			Mark(u - 2, v);
		if (u <= 253)
			Mark(u + 2, v);
		if (v >= 2)
			Mark(u, v - 2);
		if (v + 2 < PLAYFIELD_H)
			Mark(u, v + 2);
		return FALSE;
	}
	if (kind == 0)
		return FALSE;
	u8 i = kind - 1;
	u8 e = s_Edge[i];
	s_SX = (i8)((u8)ci & 31) + (e == 1 ? 1 : e == 3 ? -1 : 0);
	s_SY = (i8)(u8)(ci >> 5) + (e == 2 ? 1 : e == 0 ? -1 : 0);
	s_SX = s_SX < 0 ? CELL_COLS - 1 : s_SX >= CELL_COLS ? 0 : s_SX;
	s_SY = s_SY < 0 ? CELL_ROWS - 1 : s_SY >= CELL_ROWS ? 0 : s_SY;
	s_Entered = Partner(i);
	s_Room = Neighbour(room, e);
	return ++s_Hops != 0;
}

void Trail_Step()
{
	switch (g_TrailStage)
	{
	case TRAIL_TARGETS:
		TargetRoom();
		if (s_RoomsI == s_RoomsN)
		{
			SidesStart();
			g_TrailStage = TRAIL_OPENINGS;
		}
		break;
	case TRAIL_OPENINGS:
		for (u8 n = 0; n < TRAIL_SIDES_PER_CALL && s_Head != s_Tail; n++)
			SidesRoom();
		if (s_Head == s_Tail)
		{
			SidesEnd();
			g_TrailStage = TRAIL_ROUTE;
		}
		break;
	case TRAIL_ROUTE:
		if (!RouteRoom())
		{
			Data_LoadCells(g_ValeRoom, s_TCells);   // Vale's room's cells back
			g_TrailStage = TRAIL_DONE;
		}
		break;
	}
}

void Trail_Stop()
{
	if (g_TrailStage != TRAIL_IDLE && g_TrailStage != TRAIL_DONE)
		Data_LoadCells(g_ValeRoom, s_TCells);   // cut short: found again next time
	g_TrailStage = TRAIL_IDLE;
}
