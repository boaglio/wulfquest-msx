// Wulf Quest for MSX2: where Vale gets up (AGENTS.md §10, M6).
#pragma once

#include "msxgl.h"

// SpawnFinder.nearestFree in Vale's room (vale.h's g_ValeRoom): the nearest
// place he can stand and walk to from (x, y), world 8.8 fixed point; left as
// it is when it already qualifies.
void Spawn_NearestFree(i32* xFp, i32* yFp);
