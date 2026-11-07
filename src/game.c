// Wulf Quest for MSX2: what makes a run a run (AGENTS.md §10, M8; the Java
// game's Simulation: layTreasures, enterRoom's score and orchids,
// tickOrchids, getUp's part; EffectState; OrchidField).
//
// Banked code (wulfquest_s27_b1.c includes it): its ROM segment is in bank 1
// while it runs, so it calls only code below 8000h (data.c, rng.c, move.c;
// build.sh checks), keeps its tables in RAM, and divides without SDCC's
// library.

#include "game.h"
#include "game_data.h"
#include "vale.h"
#include "rng.h"
#include "quest.h"
#include "quest_data.h"
#include "constants.h"
#include "tables.h"
#include "sound.h"

u8 g_GameOn = 0;
u8 g_Easy = 0;
u32 g_Score = 0;
u32 g_BestScore = 0;
u8 g_ContinuesUsed = 0;
u8 g_LivesStart = PLAYER_LIVES_START;

u8  g_EffectKind = EFFECT_NONE;
i8  g_EffectOrchid = -1;
u16 g_EffectTicks = 0;
u16 g_EffectTotal = 0;
i16 g_EffectScale = 256;
u8  g_EffectUntouchable = 0;

u8  g_OrchidHere = 0;
u8  g_OrchidX = 0;
u8  g_OrchidY = 0;
i32 g_OrchidStart = 0;
u16 g_OrchidPhase = 0;
u8  g_OrchidWhich = 0;

u8  g_RoomMore[16];
u8  g_RoomTreasure[2];
u16 g_TreasuresTaken = 0;

u8  g_MapTaken = 0;
u8  g_EyesTaken = 0;
u8  g_GlassesTaken = 0;
u16 g_WayTicks = 0;
u8  g_HintTicks = 0;
u8  g_HintDirection = 0;
u8  g_HintToExit = 0;
u8  g_HintUsed[32];
u8  g_Visited[32];
u16 g_FlameWait = 0;
u8  g_GameByFlip = 0;

static u8 s_TreasureStream[8] = { TREASURE_STREAM_BYTES };
// No cycle started yet (the Java game's Long.MIN_VALUE): in RAM, as data.c
// reads it while this segment is not mapped.
static i32 c_Unset = 0x80000000;

#if ORCHID_CYCLE_TICKS > 32767
#error "an orchid's phase is a 16-bit count"
#endif

//-----------------------------------------------------------------------------
// Small helpers

// n mod d without SDCC's library: shift and subtract, a bit at a time.
static u16 ModU32(u32 n, u16 d)
{
	u32 r = 0;
	for (u8 i = 0; i < 32; i++)
	{
		u8 top = ((u8*)&n)[3] & 0x80;
		r += r;
		if (top)
			r++;
		n += n;
		if (r >= d)
			r -= d;
	}
	return (u16)r;
}

// questRoom: a lair of this game's, or the way out: no orchid grows there.
static bool QuestRoom(u8 room)
{
	if (room == EXIT_ROOM)
		return TRUE;
	u8 k = g_QuestSet * LAIRS;
	for (u8 i = 0; i < LAIRS; i++)
		if (g_LairRoom[k + i] == room)
			return TRUE;
	return FALSE;
}

static void SeedValue(u8 i)
{
	for (u8 b = 0; b < 8; b++)
		g_RngValues[i][b] = g_TestSeed[b];
}

// The flame's wait: FlameData's, or Easy's none (FlameData.NONE, lingerTicks 0).
static u16 FlameLinger()
{
	return g_Easy && !EASY_FLAME ? 0 : FLAME_LINGER_TICKS;
}

static void EffectClear()
{
	g_EffectKind = EFFECT_NONE;
	g_EffectOrchid = -1;
	g_EffectTicks = 0;
	g_EffectTotal = 0;
	g_EffectScale = 256;
	g_EffectUntouchable = 0;
}

//-----------------------------------------------------------------------------
// A new game (Simulation's constructor, layTreasures)

void Game_NewGame()
{
	g_Score = 0;
	g_BestScore = 0;
	g_ContinuesUsed = 0;
	g_LivesStart = g_Easy ? EASY_LIVES : PLAYER_LIVES_START;
	EffectClear();
	g_MapTaken = 0;
	g_EyesTaken = 0;
	g_GlassesTaken = 0;
	g_WayTicks = 0;
	g_HintTicks = 0;
	g_HintDirection = 0;
	g_HintToExit = 0;
	for (u8 i = 0; i < 32; i++)
	{
		g_HintUsed[i] = 0;
		g_Visited[i] = 0;
	}
	g_TreasuresTaken = 0;
	g_FlameWait = FlameLinger();
	g_OrchidHere = 0;

	// §16.4: each place holds something fillPercent of the time, of an even
	// pick of kind; none in this game's lairs. Their own stream of the seed.
	u8 keep[16];
	u8 more[GAME_ROOM_BYTES];
	u8 laid[2];
	Rng_Save(keep);
	SeedValue(0);
	for (u8 b = 0; b < 8; b++)
		g_RngValues[1][b] = s_TreasureStream[b];
	Rng_HashSeed(2);
	u8 room = 0;
	do
	{
		Data_LoadRoomMore(room, more);
		bool lair = FALSE;
		u8 k = g_QuestSet * LAIRS;
		for (u8 i = 0; i < LAIRS; i++)
			if (g_LairRoom[k + i] == room)
				lair = TRUE;
		for (u8 p = 0; p < 2; p++)
		{
			bool filled = Rng_Next(10000) < TREASURE_FILL_PER10K;
			u8 kind = (u8)Rng_Next(TREASURE_KINDS);
			laid[p] = filled && more[2 + 2 * p] != 0xFF && !lair ? kind + 1 : 0;
		}
		Data_VramWrite(VRAM_TREASURES + ((u16)room << 1), laid, 2);
		// No orchid picked yet: every cycle start unset.
		Data_VramWrite(VRAM_ORCHIDS + ((u16)room << 2), (const u8*)&c_Unset, 4);
	} while (++room != 0);
	Rng_Restore(keep);
}

//-----------------------------------------------------------------------------
// The room's orchid (OrchidField)

// orchidAt: which orchid this cycle grows, from the seed, the room and when it began.
static void OrchidAt()
{
	u8 keep[16];
	Rng_Save(keep);
	SeedValue(0);
	Rng_Value(1, g_ValeRoom);
	Rng_Value(2, 0);
	Rng_Value(3, g_OrchidStart);
	Rng_HashSeed(4);
	i16 roll = (i16)Rng_Next(ORCHID_WEIGHTS);
	u8 which = ORCHIDS - 1;
	for (u8 i = 0; i < ORCHIDS; i++)
	{
		roll -= g_OrchidWeight[i];
		if (roll < 0)
		{
			which = i;
			break;
		}
	}
	g_OrchidWhich = which;
	Rng_Restore(keep);
}

// enterOrchidRoom: its spot, the cycle it is in (staggered from before the
// game began, the first time), where in it it is now.
static void OrchidRoom()
{
	g_OrchidHere = g_RoomMore[0] != 0xFF && !QuestRoom(g_ValeRoom);
	if (!g_OrchidHere)
		return;
	g_OrchidX = g_RoomMore[0];
	g_OrchidY = g_RoomMore[1];
	Data_VramRead(VRAM_ORCHIDS + ((u16)g_ValeRoom << 2), (u8*)&g_OrchidStart, 4);
	if (g_OrchidStart == c_Unset)
	{
		// firstCycleStart: -nextInt(total), from its own hash.
		u8 keep[16];
		Rng_Save(keep);
		SeedValue(0);
		Rng_Value(1, g_ValeRoom);
		Rng_Value(2, 0);
		Rng_Value(3, ORCHID_FIRST_STREAM);
		Rng_HashSeed(4);
		g_OrchidStart = -(i32)Rng_Next(ORCHID_CYCLE_TICKS);
		Rng_Restore(keep);
		Data_VramWrite(VRAM_ORCHIDS + ((u16)g_ValeRoom << 2), (const u8*)&g_OrchidStart, 4);
	}
	g_OrchidPhase = ModU32(g_SimTick - (u32)g_OrchidStart, ORCHID_CYCLE_TICKS);
	OrchidAt();
}

//-----------------------------------------------------------------------------
// Rooms

static void Visit()
{
	g_Visited[g_ValeRoom >> 3] |= 1 << (g_ValeRoom & 7);
}

void Game_EnterRoom()
{
	g_FlameWait = FlameLinger();   // every room entered starts the wait again
	if (g_GameByFlip && !(g_Visited[g_ValeRoom >> 3] & (1 << (g_ValeRoom & 7))))
		Score_Add(SCORE_FIRST_VISIT);
	Visit();
	Data_LoadRoomMore(g_ValeRoom, g_RoomMore);
	Data_VramRead(VRAM_TREASURES + ((u16)g_ValeRoom << 1), g_RoomTreasure, 2);
	OrchidRoom();
}

void Game_GetUp()
{
	g_FlameWait = FlameLinger();   // getting up starts the wait again
	Visit();                            // repopulate's visit
#if HINT_ONCE_PER_LIFE
	for (u8 i = 0; i < 32; i++)
		g_HintUsed[i] = 0;   // the shrines answer again
#endif
	g_HintTicks = 0;
}

//-----------------------------------------------------------------------------
// tickOrchids: the effect ages; a bloom he touches is his.

void Game_Orchids()
{
	if (g_EffectKind != EFFECT_NONE && --g_EffectTicks == 0)
		EffectClear();
	if (!g_OrchidHere || g_EffectKind == EFFECT_KNOCKDOWN)
		return;   // sat on a bloom that grows back, he would be knocked down again forever
	if (Game_Stage() != STAGE_BLOOM)
		return;
	i16 px = FP_PX(g_ValeX) + PLAYER_BOX_X;
	i16 py = FP_PX(g_ValeY) + PLAYER_BOX_Y;
	i16 bx = g_RoomX + g_OrchidX - 8;
	i16 by = g_RoomY + g_OrchidY - 16;
	if (!(px < bx + 16 && bx < px + PLAYER_BOX_W && py < by + 16 && by < py + PLAYER_BOX_H))
		return;
	u8 o = g_OrchidWhich;
	if (g_OrchidEffect[o] == EFFECT_CURE)
		EffectClear();
	else
	{
		g_EffectKind = g_OrchidEffect[o];
		g_EffectUntouchable = g_OrchidUntouchable[o];
		g_EffectOrchid = o;
		g_EffectTicks = g_OrchidTicks[o];
		g_EffectTotal = g_OrchidTicks[o];
		g_EffectScale = g_OrchidScale[o];
	}
	if (g_EffectKind == EFFECT_KNOCKDOWN)
	{
		g_ValeVX = 0;   // down at once, no glide
		g_ValeVY = 0;
		g_ValeSwing = -1;
	}
	Score_Add(g_OrchidScore[o]);
	Sound_Play(SFX_ORCHID_PICK);
	// Picked: straight back to seed.
	g_OrchidStart = (i32)g_SimTick;
	g_OrchidPhase = 0;
	Data_VramWrite(VRAM_ORCHIDS + ((u16)g_ValeRoom << 2), (const u8*)&g_OrchidStart, 4);
	OrchidAt();
}
