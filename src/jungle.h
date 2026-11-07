// Wulf Quest for MSX2: the original's jungle, alive (AGENTS.md §10, M6; the
// Java game's §12.8, §12.9).
#pragma once

#include "msxgl.h"

// Off, the jungle is empty (the movement tests', as the Java game's
// Ecosystem.NONE): no swarm, no beasts.
extern u8 g_JungleOn;

// The swarm's creatures in Vale's room, in the Java game's list order, and
// those waiting in rooms he left (§12.8): indices into the pool below.
#define CREATURE_POOL	8
enum { CREATURE_ALIVE, CREATURE_DYING, CREATURE_APPEARING };
extern u8 g_HereN;
extern u8 g_Here[CREATURE_POOL];
extern u8 g_AwayN;
extern u8 g_Away[CREATURE_POOL];
extern u8  g_CSpecies[CREATURE_POOL];
extern u8  g_CMode[CREATURE_POOL];
extern u8  g_CModeTick[CREATURE_POOL];
extern i32 g_CX[CREATURE_POOL];
extern i32 g_CY[CREATURE_POOL];
extern i16 g_CTimer[CREATURE_POOL];
extern i16 g_CPhase[CREATURE_POOL];
extern i16 g_CAux[CREATURE_POOL];
extern i8  g_CFace[CREATURE_POOL];
extern u8  g_CHp[CREATURE_POOL];
extern u8  g_CHurt[CREATURE_POOL];
extern u8  g_CTint[CREATURE_POOL];      // a colour number
extern u8  g_CHome[CREATURE_POOL];      // its room
extern u8  g_CAwayTicks[CREATURE_POOL];
extern u16 g_CMoveTicks[CREATURE_POOL];
extern u16 g_CId[CREATURE_POOL];        // the order they came in: draw order's tie-break

// The big beasts (§12.9), all of them, wherever they are. What a step
// moves is in one struct a beast, for move.c's assembly (offsets fixed): an
// axis a block of 9 bytes, x's at 0 and y's at 9, the same in both.
typedef struct
{
	i32 X;            // 0: world 8.8 fixed point
	i16 VX;           // 4
	i8  BoxX;         // 6: its kind's box
	u8  BoxW;         // 7
	i8  FarX;         // 8: BoxX + BoxW - 1, the box's last column from the feet
	i32 Y;            // 9
	i16 VY;           // 13
	i8  BoxY;         // 15
	u8  BoxH;         // 16
	i8  FarY;         // 17
	u8  At;           // 18: its room
	u16 MoveTicks;    // 19
	u8  Free;         // 21: its box where it stands is known free (move.c)
	u8  Blocked[7];   // 22: the last strip it found blocked (move.c)
} Beast;          // 29 bytes (test/moves.tcl reads them)
#define BEAST_SIZE	29
extern Beast g_Beast[];
extern u8  g_BKind[];       // index into the kinds (a sleeper changes kind)
extern i8  g_BFace[];
extern u8  g_BAsleep[];
extern u8  g_BPause[];
extern u8  g_BHint[];        // a tribesman level with a quarter, lit up

extern u8 g_SwarmQuiet;

// Banked code (jungle.c, segment JUNGLE_SEG): call with JUNGLE(fn).
#include "data.h"
#define JUNGLE_SEG	24
#define JUNGLE(fn)	BANKED(JUNGLE_SEG, fn)

// A new game: the beasts where the actor table sets them, no creatures.
void Jungle_NewGame();

// Simulation.repopulate: on entering a room, the creatures here wait in it;
// those waiting in the new one come back.
void Jungle_EnterRoom();

// Simulation.getUp's part: the swarm's quiet after he gets up, and repopulate.
void Jungle_GetUp();

// The tick's parts, in the Java game's order (vale.c calls them).
void Jungle_TickCreatures();
void Jungle_TickSwarm();
void Jungle_TickBeasts();
void Jungle_Combat();
void Jungle_TickFlame();     // tickFlame (M8), between the beasts and the Wulf
