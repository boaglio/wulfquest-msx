// Wulf Quest for MSX2: the saves (AGENTS.md §10, M11). See save.h.
//
// Banked code (ROM segment SAVE_SEG): it calls only data.c below 8000h,
// keeps its tables in RAM, and does its sums by adding (AGENTS.md §3).

#include "save.h"
#include "shell.h"
#include "constants.h"
#include "vale.h"
#include "quest.h"
#include "quest_data.h"
#include "game.h"
#include "game_data.h"
#include "panel.h"
#include "sound.h"

u8 g_SaveMedium = MEDIUM_NONE;
u8 g_TestMedium = 0;
u8 g_SaveSlot = 0;
u8 g_SaveResult = SAVE_EMPTY;
SlotInfo g_Slots[SAVE_SLOTS];
i32 g_LoadX = 0;
i32 g_LoadY = 0;

// A game's record: where each field is.
#define F_MAGIC		0    // 'W', 'Q'
#define F_VERSION	2
#define F_KIND		3    // 'G' a game, 'T' the table
#define F_JOB		4
#define F_EASY		5
#define F_SET		6
#define F_TAKEN		7
#define F_SLOTS		8
#define F_PIECES	9
#define F_FACING	10
#define F_LIVES		11
#define F_CONTINUES	12
#define F_MAP		13
#define F_EYES		14
#define F_GLASSES	15
#define F_ROOM		16
#define F_SEED		17   // 8 bytes
#define F_X			25   // 4 bytes each, little-endian
#define F_Y			29
#define F_SCORE		33
#define F_BEST		37
#define F_TICK		41
#define F_TREASURES	45   // 2
#define F_SERIAL	47   // 2
#define F_VISITED	49   // 32: a bit a room
#define F_HINTS		81   // 32: a bit a room
#define F_EMPTIED	113  // 64: a bit a treasure's place, set where it is empty
#define F_END		177
// The table's: the options, then the hi-scores (shell.c's g_HiTable).
#define T_EASY		4
#define T_PROFILE	5
#define T_SOUND		6
#define T_SCORES	8
#define F_SUM		(SAVE_BYTES - 2)

#if F_END > F_SUM || T_SCORES + HISCORE_SIZE * HISCORE_ROW_BYTES > F_SUM
#error "a save does not fit SAVE_BYTES"
#endif

static void Copy(u8* to, const u8* from, u8 n)
{
	while (n--)
		*to++ = *from++;
}

// Fletcher's sum over the record, but its last two bytes.
static u16 Sum()
{
	u8 a = 0;
	u8 b = 0;
	for (u8 i = 0; i < F_SUM; i++)
	{
		a += g_SaveBuf[i];
		b += a;
	}
	return (u16)b << 8 | a;
}

static void Seal(u8 kind)
{
	g_SaveBuf[F_MAGIC] = 'W';
	g_SaveBuf[F_MAGIC + 1] = 'Q';
	g_SaveBuf[F_VERSION] = SAVE_VERSION;
	g_SaveBuf[F_KIND] = kind;
	u16 s = Sum();
	g_SaveBuf[F_SUM] = (u8)s;
	g_SaveBuf[F_SUM + 1] = (u8)(s >> 8);
}

// Whether g_SaveBuf holds a record of this kind: g_SaveResult.
static void Check(u8 kind)
{
	if (g_SaveBuf[F_MAGIC] != 'W' || g_SaveBuf[F_MAGIC + 1] != 'Q' || g_SaveBuf[F_KIND] != kind)
	{
		u8 any = 0;
		for (u8 i = 0; i < F_SUM; i++)
			any |= g_SaveBuf[i];
		g_SaveResult = any ? SAVE_DAMAGED : SAVE_EMPTY;
		return;
	}
	u16 s = Sum();
	if (g_SaveBuf[F_SUM] != (u8)s || g_SaveBuf[F_SUM + 1] != (u8)(s >> 8))
		g_SaveResult = SAVE_DAMAGED;
	else if (g_SaveBuf[F_VERSION] != SAVE_VERSION)
		g_SaveResult = SAVE_OTHER_VERSION;
	else
		g_SaveResult = SAVE_OK;
}

//-----------------------------------------------------------------------------
// The media: a record to slot g_SaveSlot, and back.

// The tests' medium: video RAM, page 0, under the screen (lines 224-231),
// a record each SAVE_BYTES from MEDIUM_VRAM_AT.
#define MEDIUM_VRAM_AT	0x7000
#if (SAVE_SLOTS + 1) * SAVE_BYTES > 0x400
#error "the video RAM medium holds four records"
#endif

// The disk: one file, WULFQ.SAV, a record of SAVE_BYTES each (the table,
// then the slots), through MSX-DOS's FCB calls (data.c's Data_DiskCall,
// which has the disk ROM's work area back for each).
#define BDOS_SET_DMA		0x1A
#define BDOS_OPEN			0x0F
#define BDOS_CLOSE			0x10
#define BDOS_CREATE			0x16
#define BDOS_RANDOM_WRITE	0x26
#define BDOS_RANDOM_READ	0x27
#define FCB_RECORD_SIZE		0x0E
#define FCB_RANDOM			0x21
static u8 s_Fcb[37];
static u8 s_FcbName[11] = { 'W', 'U', 'L', 'F', 'Q', ' ', ' ', ' ', 'S', 'A', 'V' };

static u8 Bdos(u8 c, const void* de, u16 hl)
{
	g_BdosC = c;
	g_BdosDE = (u16)de;
	g_BdosHL = hl;
	Data_DiskCall();
	return g_BdosA;
}

// The file open, its records SAVE_BYTES, the transfer to g_SaveBuf; made
// (four empty records) when there is none and make says so. 0 when open.
static u8 DiskOpen(u8 make)
{
	for (u8 i = 0; i < sizeof(s_Fcb); i++)
		s_Fcb[i] = 0;
	Copy(s_Fcb + 1, s_FcbName, 11);
	Bdos(BDOS_SET_DMA, g_SaveBuf, 0);
	if (Bdos(BDOS_OPEN, s_Fcb, 0) == 0)
	{
		s_Fcb[FCB_RECORD_SIZE] = (u8)SAVE_BYTES;
		s_Fcb[FCB_RECORD_SIZE + 1] = (u8)(SAVE_BYTES >> 8);
		return 0;
	}
	if (!make || Bdos(BDOS_CREATE, s_Fcb, 0) != 0)
		return 0xFF;
	s_Fcb[FCB_RECORD_SIZE] = (u8)SAVE_BYTES;
	s_Fcb[FCB_RECORD_SIZE + 1] = (u8)(SAVE_BYTES >> 8);
	// Every record written once, empty, so none reads as what the disk held
	// before. The record to save waits in video RAM meanwhile (the tests'
	// medium's place: not in use with a disk; RAM has no room for it).
	Data_VramWrite(MEDIUM_VRAM_AT, g_SaveBuf, 128);
	Data_VramWrite(MEDIUM_VRAM_AT + 128, g_SaveBuf + 128, 128);
	for (u16 i = 0; i < SAVE_BYTES; i++)
		g_SaveBuf[i] = 0;
	for (u8 r = 0; r <= SAVE_SLOTS; r++)
	{
		s_Fcb[FCB_RANDOM] = r;
		s_Fcb[FCB_RANDOM + 1] = 0;
		s_Fcb[FCB_RANDOM + 2] = 0;
		s_Fcb[FCB_RANDOM + 3] = 0;
		if (Bdos(BDOS_RANDOM_WRITE, s_Fcb, 1) != 0)
			return 0xFF;
	}
	Data_VramRead(MEDIUM_VRAM_AT, g_SaveBuf, 128);
	Data_VramRead(MEDIUM_VRAM_AT + 128, g_SaveBuf + 128, 128);
	return 0;
}

static void DiskRecord(u8 op)
{
	s_Fcb[FCB_RANDOM] = g_SaveSlot;
	s_Fcb[FCB_RANDOM + 1] = 0;
	s_Fcb[FCB_RANDOM + 2] = 0;
	s_Fcb[FCB_RANDOM + 3] = 0;
	u8 a = Bdos(op, s_Fcb, 1);
	Bdos(BDOS_CLOSE, s_Fcb, 0);
	g_SaveResult = a == 0 ? SAVE_OK : SAVE_FAILED;
}

static void Put()
{
	g_SaveResult = SAVE_FAILED;
	if (g_SaveMedium == MEDIUM_DISK)
	{
		if (DiskOpen(TRUE) == 0)
			DiskRecord(BDOS_RANDOM_WRITE);
		return;
	}
	if (g_SaveMedium == MEDIUM_VRAM)
	{
		u16 at = MEDIUM_VRAM_AT + ((u16)g_SaveSlot << 8);
		Data_VramWrite(at, g_SaveBuf, 128);
		Data_VramWrite(at + 128, g_SaveBuf + 128, 128);
		g_SaveResult = SAVE_OK;
	}
}

static void Get()
{
	g_SaveResult = SAVE_FAILED;
	if (g_SaveMedium == MEDIUM_DISK)
	{
		if (DiskOpen(FALSE) != 0)
		{
			// No file yet: every slot empty.
			for (u16 i = 0; i < SAVE_BYTES; i++)
				g_SaveBuf[i] = 0;
			g_SaveResult = SAVE_OK;
			return;
		}
		DiskRecord(BDOS_RANDOM_READ);
		if (g_SaveResult != SAVE_OK)
		{
			// Past the end of the file: an empty slot.
			for (u16 i = 0; i < SAVE_BYTES; i++)
				g_SaveBuf[i] = 0;
			g_SaveResult = SAVE_OK;
		}
		return;
	}
	if (g_SaveMedium == MEDIUM_VRAM)
	{
		u16 at = MEDIUM_VRAM_AT + ((u16)g_SaveSlot << 8);
		Data_VramRead(at, g_SaveBuf, 128);
		Data_VramRead(at + 128, g_SaveBuf + 128, 128);
		g_SaveResult = SAVE_OK;
	}
}

//-----------------------------------------------------------------------------

void Save_Peek()
{
	u8 slot = g_SaveSlot;
	for (u8 n = 0; n < SAVE_SLOTS; n++)
	{
		SlotInfo* s = &g_Slots[n];
		s->Used = 0;
		if (g_SaveMedium == MEDIUM_NONE)
			continue;
		g_SaveSlot = n + 1;
		Get();
		if (g_SaveResult != SAVE_OK)
			continue;
		Check('G');
		if (g_SaveResult != SAVE_OK)
		{
			s->Used = g_SaveResult == SAVE_EMPTY ? 0 : 2;   // 2: there, but will not load
			continue;
		}
		s->Used = 1;
		s->Easy = g_SaveBuf[F_EASY];
		s->Quarters = g_SaveBuf[F_PIECES];
		s->Room = g_SaveBuf[F_ROOM];
		Copy((u8*)&s->Serial, g_SaveBuf + F_SERIAL, 2);
		Copy((u8*)&s->Score, g_SaveBuf + F_SCORE, 4);
	}
	g_SaveSlot = slot;
}

void Save_Init()
{
	g_SaveMedium = g_TestMedium ? MEDIUM_VRAM : g_DiskReady ? MEDIUM_DISK : MEDIUM_NONE;
	if (g_SaveMedium != MEDIUM_NONE)
	{
		g_SaveSlot = SAVE_TABLE;
		Get();
		if (g_SaveResult == SAVE_OK)
			Check('T');
		if (g_SaveResult == SAVE_OK)
		{
			g_ShellEasy = g_SaveBuf[T_EASY];
			g_Profile = g_SaveBuf[T_PROFILE];
			g_SoundOn = g_SaveBuf[T_SOUND];
			Copy(g_HiTable, g_SaveBuf + T_SCORES, sizeof(g_HiTable));
		}
	}
	Save_Peek();
}

void Save_Table()
{
	for (u16 i = 0; i < SAVE_BYTES; i++)
		g_SaveBuf[i] = 0;
	g_SaveBuf[T_EASY] = g_ShellEasy;
	g_SaveBuf[T_PROFILE] = g_Profile;
	g_SaveBuf[T_SOUND] = g_SoundOn;
	Copy(g_SaveBuf + T_SCORES, g_HiTable, sizeof(g_HiTable));
	Seal('T');
	g_SaveSlot = SAVE_TABLE;
	Put();
}

void Save_Game()
{
	u16 serial = 0;
	for (u8 n = 0; n < SAVE_SLOTS; n++)
		if (g_Slots[n].Used == 1 && g_Slots[n].Serial >= serial)
			serial = g_Slots[n].Serial + 1;
	for (u16 i = 0; i < SAVE_BYTES; i++)
		g_SaveBuf[i] = 0;
	u8* b = g_SaveBuf;
	b[F_JOB] = 0;
	b[F_EASY] = g_Easy;
	b[F_SET] = g_QuestSet;
	b[F_TAKEN] = g_QuestTaken;
	b[F_SLOTS] = g_QuestSlots;
	b[F_PIECES] = g_QuestPieces;
	b[F_FACING] = g_ValeFacing;
	b[F_LIVES] = g_ValeLives;
	b[F_CONTINUES] = g_ContinuesUsed;
	b[F_MAP] = g_MapTaken;
	b[F_EYES] = g_EyesTaken;
	b[F_GLASSES] = g_GlassesTaken;
	b[F_ROOM] = g_ValeRoom;
	Copy(b + F_SEED, g_TestSeed, 8);
	Copy(b + F_X, (const u8*)&g_ValeX, 4);
	Copy(b + F_Y, (const u8*)&g_ValeY, 4);
	Copy(b + F_SCORE, (const u8*)&g_Score, 4);
	Copy(b + F_BEST, (const u8*)&g_BestScore, 4);
	Copy(b + F_TICK, (const u8*)&g_SimTick, 4);
	Copy(b + F_TREASURES, (const u8*)&g_TreasuresTaken, 2);
	Copy(b + F_SERIAL, (const u8*)&serial, 2);
	Copy(b + F_VISITED, g_Visited, 32);
	Copy(b + F_HINTS, g_HintUsed, 32);
	// The treasures' places (VRAM_TREASURES, a byte each): a bit each, set where it is empty.
	u8 places[64];
	for (u8 part = 0; part < 8; part++)
	{
		Data_VramRead(VRAM_TREASURES + ((u16)part << 6), places, 64);
		for (u8 i = 0; i < 64; i++)
			if (places[i] == 0)
				b[F_EMPTIED + (part << 3) + (i >> 3)] |= (u8)(1 << (i & 7));
	}
	Seal('G');
	Put();
	u8 result = g_SaveResult;   // the slots' lines again, the outcome kept
	Save_Peek();
	g_SaveResult = result;
}

void Save_Clear()
{
	for (u16 i = 0; i < SAVE_BYTES; i++)
		g_SaveBuf[i] = 0;
	Put();
	u8 result = g_SaveResult;
	Save_Peek();
	g_SaveResult = result;
}

void Save_Load()
{
	Get();
	if (g_SaveResult == SAVE_OK)
		Check('G');
}

void Save_Before()
{
	g_Easy = g_SaveBuf[F_EASY];
	Copy(g_TestSeed, g_SaveBuf + F_SEED, 8);
	Copy((u8*)&g_LoadX, g_SaveBuf + F_X, 4);
	Copy((u8*)&g_LoadY, g_SaveBuf + F_Y, 4);
}

void Save_Apply()
{
	const u8* b = g_SaveBuf;
	g_QuestTaken = b[F_TAKEN];
	g_QuestSlots = b[F_SLOTS];
	g_QuestPieces = b[F_PIECES];
	g_ContinuesUsed = b[F_CONTINUES];
	g_MapTaken = b[F_MAP];
	g_EyesTaken = b[F_EYES];
	g_GlassesTaken = b[F_GLASSES];
	Copy((u8*)&g_Score, b + F_SCORE, 4);
	Copy((u8*)&g_BestScore, b + F_BEST, 4);
	Copy((u8*)&g_TreasuresTaken, b + F_TREASURES, 2);
	Copy(g_Visited, b + F_VISITED, 32);
	Copy(g_HintUsed, b + F_HINTS, 32);
	u8 none[2] = { 0, 0 };
	for (u16 p = 0; p < 512; p += 2)
	{
		u8 bits = b[F_EMPTIED + (p >> 3)] >> (p & 7);
		if (bits & 1)
			Data_VramWrite(VRAM_TREASURES + p, none, 1);
		if (bits & 2)
			Data_VramWrite(VRAM_TREASURES + p + 1, none, 1);
	}
}

void Save_After()
{
	const u8* b = g_SaveBuf;
	g_ValeLives = b[F_LIVES];
	g_ValeFacing = b[F_FACING];
	Copy((u8*)&g_SimTick, b + F_TICK, 4);
	// Quest_Clock's counters, as so many ticks leave them (vale.c's, done again here).
	u32 t = g_SimTick;
	while (t >= ORBIT_TICKS_PER_REV)
		t -= ORBIT_TICKS_PER_REV;
	g_OrbitTick = 0;
	g_OrbitIndex = 0;
	g_OrbitRem = 0;
	for (u8 i = 0; i < (u8)t; i++)
	{
		g_OrbitTick++;
		u16 rem = g_OrbitRem + 256 % ORBIT_TICKS_PER_REV;
		g_OrbitIndex += 256 / ORBIT_TICKS_PER_REV;
		if (rem >= ORBIT_TICKS_PER_REV)
		{
			rem -= ORBIT_TICKS_PER_REV;
			g_OrbitIndex++;
		}
		g_OrbitRem = (u8)rem;
	}
}
