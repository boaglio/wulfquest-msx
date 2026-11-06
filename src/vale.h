// Wulf Quest for MSX2: Ranger Vale, walking and fighting, from room to room
// (AGENTS.md §10, M3 and M4; the Java game's AGENTS.md §7.4, §7.5, §11).
#pragma once

#include "msxgl.h"
#include "data.h"

// The whole pixels of an 8.8 fixed-point i32 variable: its middle two bytes,
// which is (v >> 8) without a 32-bit shift. For positions, which fit.
#define FP_PX(v)	(*(i16*)((u8*)&(v) + 1))

// One tick's input, as the Java game's InputState: a direction and the fire button.
#define VALE_UP		0x01
#define VALE_DOWN	0x02
#define VALE_LEFT	0x04
#define VALE_RIGHT	0x08
#define VALE_FIRE	0x10

// Facing, in the Java game's Direction8 order.
enum { FACE_N, FACE_NE, FACE_E, FACE_SE, FACE_S, FACE_SW, FACE_W, FACE_NW };

// His state. Positions are his feet, in world pixels, 8.8 fixed point, as the
// Java game keeps them. The test scripts read these through the symbol map.
extern i32 g_ValeX;
extern i32 g_ValeY;
extern u8  g_ValeFacing;
extern i8  g_ValeSwing;        // ticks into the stroke, -1 when the blade is not out
extern u8  g_ValeCooldown;
extern u16 g_ValeWalk;         // ticks of actual movement in a row
extern i16 g_ValeVX;           // velocity per axis, the original's units (§11.3)
extern i16 g_ValeVY;
// The sabre's hit box this tick, world pixels; width 0 when there is none.
extern i16 g_SabreX;
extern i16 g_SabreY;
extern u8  g_SabreW;
extern u8  g_SabreH;
// The room he is in (row * GRID_W + col), and the ticks of the flip's
// freeze still to go (§7.5).
extern u8  g_ValeRoom;
extern u8  g_ValeFreeze;
// With this set nothing but the map's edge is solid, and a flip freezes
// nothing (the movement tests' open world).
extern u8  g_ValeOpen;

// Once, at start: the speed tables.
void Vale_Init();

// Stand him at rest at a world position, in the room it is in.
void Vale_Place(i32 x, i32 y);

// One tick of the game: the sabre, then the walk, then the room he is in,
// then the blade's reach. TRUE when he has walked into another room:
// g_ValeRoom is the new one, and the next ticks are frozen.
bool Vale_Tick(u8 keys);

// The frame of player_frames.bin to show him in (PlayerPainter.frameFor).
u8 Vale_Frame();
