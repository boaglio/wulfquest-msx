// Wulf Quest for MSX2: the quest and the original's Wulf (AGENTS.md §10, M7;
// the Java game's §13.8, §14).
//
// The code is banked (quest.c and wulf.c, in ROM segment QUEST_SEG): call it
// with QUEST(fn), arguments and results in these globals.
#pragma once

#include "msxgl.h"
#include "data.h"
#include "quest_data.h"
#include "game_data.h"

#define QUEST_SEG	26
#define QUEST(fn)	BANKED(QUEST_SEG, fn)

// Off, no quest and no Wulf (the movement and jungle tests', as the Java
// game's QuestRules.NONE and WulfRules.NONE).
extern u8 g_QuestOn;

// The amulet (Quest): the set the game hid it in, the lairs whose quarter is
// taken (a bit each), the panel's slots held, the pieces held.
extern u8 g_QuestSet;
extern u8 g_QuestTaken;
extern u8 g_QuestSlots;
extern u8 g_QuestPieces;
extern i8 g_QuestLair;          // the lair Vale is in, -1 for none
extern u8 g_QuestFlash;         // the pickup's white flash, ticks left
extern u8 g_QuestFly;           // the quarter's flight to the panel, ticks left
extern u8 g_QuestFlyMask;
extern i16 g_QuestFlyX;         // where it flies from, world pixels
extern i16 g_QuestFlyY;
// New Game+ (§14.9): the job (0, the game itself), set before a new game;
// the lairs it keeps (a bit each, Simulation.shareOut), and the slots each
// lair's piece fills in this job.
extern u8 g_Job;
extern u8 g_LairKept;
extern u8 g_LairSlots[LAIRS];
extern u8 g_JobFrame[QUEST_SLOTS];  // the amulet's frame for each slot, the job's own
// The lairs with nothing for Vale: their piece taken, or none in this job.
#define Quest_Done()	(g_QuestTaken | (u8)(~g_LairKept & ((1 << LAIRS) - 1)))
// Quest.quarters(): the slots held.
static inline u8 Quest_Quarters()
{
	u8 n = 0;
	for (u8 m = g_QuestSlots; m; m >>= 1)
		n += m & 1;
	return n;
}

// Simulation.ramp: how far on the jungle thinks the quest is, the job's head
// start added.
static inline u8 Quest_Ramp()
{
	u8 n = Quest_Quarters() + JOB_HEAD_START(g_Job);
	return n < QUEST_SLOTS ? n : QUEST_SLOTS;
}

// The lair's guardian (GUARD_ORBIT), a creature of its own.
enum { ORBITING, LUNGING };
extern i32 g_GuardX;
extern i32 g_GuardY;
extern i32 g_GuardAX;           // its anchor, the pedestal
extern i32 g_GuardAY;
extern i16 g_GuardPhase;
extern i16 g_GuardTimer;
extern i16 g_GuardAux;          // stunned, ticks left
extern i8  g_GuardDirX;
extern i8  g_GuardDirY;
extern i8  g_GuardFace;
extern u8  g_GuardHurt;
extern u16 g_GuardLastHit;
extern u16 g_GuardMoveTicks;
extern u8  g_GuardRadius;       // its circle here

// The Keeper of the Arch, in the way out's room.
enum { KEEPER_BLOCKING, KEEPER_STEPPING_ASIDE, KEEPER_ASIDE };
extern u8  g_KeeperHere;
extern u8  g_KeeperState;
extern u8  g_KeeperSeated;      // keeperLocalXFp is set: it keeps its place
extern u16 g_KeeperLX;          // its room-local feet x, 8.8
extern i32 g_KeeperX;
extern i32 g_KeeperY;
extern u16 g_KeeperMoveTicks;

// The guardians' clock (GuardOrbit.ringPoint): the game tick's place in a
// turn, as an index into a sine table of 256; vale.c moves it with the tick.
extern u8  g_OrbitTick;         // Simulation.tick() % ORBIT_TICKS_PER_REV
extern u8  g_OrbitIndex;        // g_OrbitTick * 256 / ORBIT_TICKS_PER_REV
extern u8  g_OrbitRem;          // and the remainder
void Quest_Clock();             // a tick on (not banked: vale.c's)
void Quest_ClockReset();

// The Wulf (Wulf, the original's prowler).
enum { WULF_ABSENT, WULF_WARNING, WULF_PURSUE, WULF_ARRIVING, WULF_LEAVING, WULF_PROWLING, WULF_SITTING,
	WULF_RISING };
enum { LATCH_FREE, LATCH_STEERED, LATCH_SPENT };
extern u8  g_WulfState;
extern u8  g_WulfStateTick;
extern u8  g_WulfAt;            // its room
extern i8  g_WulfHeading;
extern i16 g_WulfSpeed;
extern u8  g_WulfLatch;
extern u8  g_WulfHere;
extern i32 g_WulfX;
extern i32 g_WulfY;
extern u16 g_WulfMoveTicks;

// The game's seed (wulfquest.c), the Java game's long, little-endian.
extern u8 g_TestSeed[8];

// Banked (QUEST): the Java game's parts, in its order.
void Quest_NewGame();           // the set from the game's seed (g_TestSeed), the Wulf placed
void Quest_EnterRoom();         // questEntersRoom: the guardian or the Keeper seated
void Quest_Tick();              // tickQuest (M7's part)
void Quest_Parry();             // resolveCombat's parryGuardian
void Quest_Touch();             // resolveCombat's guardian, Keeper and Wulf: g_QuestHit
void Wulf_Prowl();              // prowl
void Wulf_Notice();             // noticeProwler, on a flip
void Wulf_GetUp();              // getUp's part: it caught him, so it is gone
extern u8 g_QuestHit;           // Quest_Touch: he is to die
