// Wulf Quest for MSX2: one axis of a creature's or a beast's move (AGENTS.md
// §10, M6): the Java game's Collision.resolve.
#pragma once

#include "msxgl.h"

// The move: set these, call Move_Axis, read g_MovePos.
extern i32 g_MovePos;       // the coordinate that moves, world 8.8 fixed point
extern i16 g_MoveDelta;     // by how much
extern i16 g_MoveOther;     // the other coordinate, world whole pixels
extern i8  g_MoveOff;       // the box's offset from the feet along the axis
extern u8  g_MoveSize;      //            and its size along it
extern i8  g_MoveOffO;      //            and the same across it
extern u8  g_MoveSizeO;
extern u8  g_MoveAlongX;    // x (else y)
extern u8  g_MoveWorld;     // MOVE_ROOM or MOVE_MAP
extern u8  g_MoveScenery;   // MOVE_ROOM: the scenery counts (else only the room's edges)
extern u8  g_MoveFree;      // set by the caller: the box is free where it stands (a step under
                            // a cell). Move_Axis clears it.

// MOVE_ROOM: a creature's world, its room (g_RoomX/Y, vale.h): the edges
// walls, the cells around it (s_Cells) its scenery.
// MOVE_MAP: Wulf's and a beast's, the whole map round its ends (Data_BoxTest;
// with g_MoveFree, the new strip's cells, Data_StripTest). Beast_Step needs it
// only for a beast whose box is not free where it stands.
#define MOVE_ROOM	0
#define MOVE_MAP	1

// Collision.resolve: free, g_MovePos + g_MoveDelta; blocked, the nearest free
// whole pixel between, or where it was, fraction and all.
void Move_Axis();

// Simulation.stepBeast on the Beast at g_BeastPtr (jungle.h): 1 if it moved.
// Its box known free where it stands (Free), a step of less than a cell (the
// converter checks the speeds) tests only the strip of cells it steps into.
#include "jungle.h"
extern Beast* g_BeastPtr;
u8 Beast_Step();
