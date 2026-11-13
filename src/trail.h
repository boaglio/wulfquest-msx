// Wulf Quest for MSX2: the looking glass's trail on the map (AGENTS.md §10,
// M12; the Java game's WayPainter.route and MapScreen.paintTrail).
#pragma once

#include "msxgl.h"
#include "data.h"
#include "spawn.h"
#include "trail_data.h"
#include "tables.h"   // and ram.h
#include "constants.h"

// Banked code, beside spawn.c (it borrows its arrays): call with TRAIL(fn).
#define TRAIL(fn)	BANKED(SPAWN_SEG, fn)

// The trail as found so far: its first pixel on the map, then a step a pixel,
// 2 bits each (0 right, 1 left, 2 down, 3 up), from Vale to the quarter or the
// arch. TRAIL_MAX_PX (trail_data.h) holds the longest the converter finds (TrailTest).
extern u8  g_TrailX0;
extern u8  g_TrailY0;
extern u16 g_TrailN;                       // its pixels

// While the map is open the search borrows the respawn's arrays (spawn.c's,
// ram.h's RAM_SPAWN_CLASS up to RAM_TABLES): no respawn comes then.
#define TRAIL_CELLS		(CELL_COLS * CELL_ROWS)
#define TRAIL_SEEDS_MAX	(TRAIL_SIDES_MAX * TRAIL_RUN_MAX + LAIRS + 1)
#define RAM_TRAIL_MARK	RAM_SPAWN_CLASS                              // a byte a cell
#define RAM_TRAIL_QUEUE	(RAM_TRAIL_MARK + TRAIL_CELLS)                // the flood's queue
#define RAM_TRAIL_D		(RAM_TRAIL_QUEUE + TRAIL_CELLS * 2)           // each side's steps (D)
#define RAM_TRAIL_STEPS	(RAM_TRAIL_D + TRAIL_SIDES * 2)               // the trail's steps
#define TRAIL_STEP_BYTES	(TRAIL_MAX_PX / 4)
#define TRAIL_ROOM_BYTES	(GRID_W * GRID_H / 8)
#define RAM_TRAIL_ROOMS	(RAM_TRAIL_STEPS + TRAIL_STEP_BYTES)          // the rooms it touches
#define RAM_TRAIL_SEEDS	(RAM_TRAIL_ROOMS + TRAIL_ROOM_BYTES)          // a flood's seeds: cell, steps, kind
#define RAM_TRAIL_REC	(RAM_TRAIL_SEEDS + TRAIL_SEEDS_MAX * 5)         // a room's record, and it unpacked:
#define RAM_TRAIL_W		(RAM_TRAIL_REC + TRAIL_RECORD_MAX)            //   the steps between its sides,
#define RAM_TRAIL_OTHER	(RAM_TRAIL_W + TRAIL_SIDES_MAX * TRAIL_SIDES_MAX)   // each one's other side,
#define RAM_TRAIL_EDGE	(RAM_TRAIL_OTHER + TRAIL_SIDES_MAX * 2)       //   and its edge
#define RAM_TRAIL_END	(RAM_TRAIL_EDGE + TRAIL_SIDES_MAX)
#define g_TrailSteps	((u8*)RAM_TRAIL_STEPS)   // TRAIL_STEP_BYTES (a macro: no sizeof)
#define g_TrailRooms	((u8*)RAM_TRAIL_ROOMS)   // TRAIL_ROOM_BYTES, a bit a room
extern u8  g_TrailWhole;                   // it got there: the cross where it ends
extern u8  g_TrailStage;                   // TRAIL_* below

#define TRAIL_IDLE		0
#define TRAIL_TARGETS	1   // the quarters' rooms flooded, a room a call
#define TRAIL_OPENINGS	2   // each opening's steps to them, some rooms a call
#define TRAIL_ROUTE		3   // the trail, a room a call
#define TRAIL_DONE		4
// Rooms worked out a call in TRAIL_OPENINGS (the port's pace, not the game's).
#define TRAIL_SIDES_PER_CALL	8

// The openings' steps (D) are kept in video RAM, page 1 below the screen,
// for the same set and quarters taken: opened again, the map has them.
// (test/record-demo's REC_VRAM uses it too: g_TrailKey is forgotten there.)
#define TRAIL_D_VRAM	0xEA00
#if TRAIL_D_VRAM + TRAIL_SIDES * 2 > 0xF400
#error "the openings' steps reach the sprites' second set"
#endif
extern u8 g_TrailKey;   // set << 4 | the lairs done the steps are for; 0xFF none

// Start finding the trail from Vale (g_ValeRoom, g_ValeX, g_ValeY) for this
// game's set and the lairs done (quest.h's Quest_Done) in g_TrailDone.
extern u8 g_TrailDone;
void Trail_Start();
// A slice of the work: g_TrailStage says where it is; the pixels found grow.
void Trail_Step();
// The map left: Vale's room's cells back (the search borrows their place).
void Trail_Stop();
