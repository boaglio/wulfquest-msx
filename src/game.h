// Wulf Quest for MSX2: what makes a run a run (AGENTS.md §10, M8; the Java
// game's §14.5, §14.8, §14.10, §15, §16, §17.5, §17.8): the score, the
// orchids and their effects, the treasures, the map scroll, the eyes, the
// looking glasses, the shrines' hints, continues, and Easy.
//
// The code is banked (game.c, ROM segment GAME_SEG): call it with GAME(fn),
// arguments and results in these globals. The flame is the jungle's
// (jungle.c), the items and treasures are taken in the quest's tick (quest.c).
#pragma once

#include "msxgl.h"
#include "data.h"

#define GAME_SEG	27
#define GAME(fn)	BANKED(GAME_SEG, fn)

// Off, none of it (the tests before M8, as the Java game's ecosystems without it).
extern u8 g_GameOn;
extern u8 g_Easy;

// The score (§16), and the best of the run before a continue reset it.
extern u32 g_Score;
extern u32 g_BestScore;
static inline void Score_Add(u16 points)
{
	g_Score += points;
	if (g_Score > g_BestScore)
		g_BestScore = g_Score;
}
extern u8 g_ContinuesUsed;
extern u8 g_LivesStart;         // Hard's, or Easy's

// The effect in his blood (EffectState): EFFECT_* (game_data.h), from which
// orchid, how long it has left, the speed scale, untouchable.
extern u8  g_EffectKind;
extern i8  g_EffectOrchid;
extern u16 g_EffectTicks;
extern u16 g_EffectTotal;
extern i16 g_EffectScale;
extern u8  g_EffectUntouchable;
// immune in resolveCombat: no creature, guardian or Wulf can kill him.
#define EFFECT_IMMUNE()	(g_EffectUntouchable || g_EffectKind == EFFECT_IMMUNITY)

// The room's orchid (one a room, OrchidSpots): whether it has one, where
// (room-local feet), when its cycle began, where in it it is, and which
// orchid it grows this cycle.
extern u8  g_OrchidHere;
extern u8  g_OrchidX;
extern u8  g_OrchidY;
extern i32 g_OrchidStart;
extern u16 g_OrchidPhase;
extern u8  g_OrchidWhich;
enum { STAGE_SEED, STAGE_SPROUT, STAGE_BUD, STAGE_BLOOM, STAGE_WILT };
// OrchidField.stageAt(g_OrchidPhase).
#include "game_data.h"
static inline u8 Game_Stage()
{
	u16 at = g_OrchidPhase;
	if (at < ORCHID_SEED_TICKS)
		return STAGE_SEED;
	at -= ORCHID_SEED_TICKS;
	if (at < ORCHID_SPROUT_TICKS)
		return STAGE_SPROUT;
	at -= ORCHID_SPROUT_TICKS;
	if (at < ORCHID_BUD_TICKS)
		return STAGE_BUD;
	at -= ORCHID_BUD_TICKS;
	return at < ORCHID_BLOOM_TICKS ? STAGE_BLOOM : STAGE_WILT;
}

// The room's extra bytes (Data_LoadRoomMore), and its two treasures' kinds
// + 1 (0 none): the 512 places live in video RAM at VRAM_TREASURES, the
// orchids' picked cycle starts at VRAM_ORCHIDS (Simulation's arrays).
extern u8  g_RoomMore[16];
extern u8  g_RoomTreasure[2];
extern u16 g_TreasuresTaken;
#define VRAM_TREASURES	0x6A00
#define VRAM_ORCHIDS	0x6C00

// The items (Quest) and the shrines' hints.
extern u8  g_MapTaken;
extern u8  g_EyesTaken;         // a bit an eye
extern u8  g_GlassesTaken;      // a bit a glass, all of them (the whole amulet's after)
extern u16 g_WayTicks;
extern u8  g_HintTicks;
extern u8  g_HintDirection;     // Direction8's ordinal
extern u8  g_HintToExit;
extern u8  g_HintUsed[32];      // a bit a room
extern u8  g_Visited[32];       // a bit a room: it has been entered
extern u16 g_FlameWait;

// Banked (GAME): the Java game's parts.
void Game_NewGame();            // the treasures laid, the orchids unpicked, the score and items cleared
void Game_EnterRoom();          // enterRoom's: g_GameByFlip a flip (a first visit scores)
extern u8 g_GameByFlip;
void Game_GetUp();              // getUp's: the flame's wait, the hints once a life
void Game_Orchids();            // tickOrchids
void Game_Clock();              // a tick on: the orchid's phase (not banked: vale.c's)
