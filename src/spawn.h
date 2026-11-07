// Wulf Quest for MSX2: where Vale gets up (AGENTS.md §10, M6).
#pragma once

#include "msxgl.h"

// Banked code: call with SPAWN(fn).
#include "data.h"
#define SPAWN_SEG	25
#define SPAWN(fn)	BANKED(SPAWN_SEG, fn)

// SpawnFinder.nearestFree in Vale's room (vale.h's g_ValeRoom): the nearest
// place he can stand and walk to from where he is (g_ValeX, g_ValeY), and he
// is moved there; left as he is when it already qualifies.
void Spawn_NearestFree();
