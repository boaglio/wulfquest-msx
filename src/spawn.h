// Wulf Quest for MSX2: where Vale gets up (AGENTS.md §10, M6).
#pragma once

#include "msxgl.h"

// Banked code: call with SPAWN(fn).
#include "data.h"
#include "constants.h"
#define SPAWN_SEG	25

// SpawnFinder's walk (spawn.c), and the sizes of its arrays (placed by ram.h).
#define SPAWN_SLACK		8   // SpawnFinder.SLACK_PX: how far past the room's edges the walk goes
#define SPAWN_FEET_W		(PLAYFIELD_W + 2 * SPAWN_SLACK + 1)
#define SPAWN_FEET_H		(PLAYFIELD_H + 2 * SPAWN_SLACK + 1)
#define SPAWN_COLS		CELL_COLS
#define SPAWN_ROWS		CELL_ROWS
// Runs along an axis of n positions for a box s long: a box whose far edge
// is a whole number of cells from its near one changes cells once in 8
// positions, any other twice.
#define SPAWN_RUNS(n, s)	(((s) - 1) % CELL == 0 ? (n) / CELL + 2 : 2 * ((n) / CELL) + 3)
#define SPAWN_RUNS_X		SPAWN_RUNS(SPAWN_FEET_W, PLAYER_BOX_W)
#define SPAWN_RUNS_Y		SPAWN_RUNS(SPAWN_FEET_H, PLAYER_BOX_H)
#define SPAWN_CLASSES		(SPAWN_RUNS_X * SPAWN_RUNS_Y)
#if CELL != 8
#error "spawn.c divides by CELL with shifts"
#endif

#define SPAWN_QUEUE_N	(SPAWN_CLASSES > SPAWN_COLS * SPAWN_ROWS ? SPAWN_CLASSES : SPAWN_COLS * SPAWN_ROWS)

#define SPAWN(fn)	BANKED(SPAWN_SEG, fn)

// SpawnFinder.nearestFree in Vale's room (vale.h's g_ValeRoom): the nearest
// place he can stand and walk to from where he is (g_ValeX, g_ValeY), and he
// is moved there; left as he is when it already qualifies.
void Spawn_NearestFree();
