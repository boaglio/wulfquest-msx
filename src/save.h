// Wulf Quest for MSX2: the saves (AGENTS.md §10, M11): three slots of a game
// and a record of the hi-scores and the options, on a medium.
//
// A save is the game as AGENTS.md lists it, not a picture of the whole
// machine (the user's choice, 2026-11-07): its seed (so the same jungle, the
// same amulet set and the same treasures are laid again), the rooms walked,
// the treasures taken, the quarters, the items, the shrines used, where Vale
// is, his lives, the continues, the score, the clock, the job, the mode. A
// game loaded is a new game of that seed with those put back; the creatures
// start afresh around him. Fixed size (SAVE_BYTES), versioned, checksummed.
//
// The code is banked (save.c, ROM segment SAVE_SEG): SAVE(fn), the slot in
// g_SaveSlot, the record in g_SaveBuf, the outcome in g_SaveResult.
#pragma once

#include "msxgl.h"
#include "data.h"
#include "shell_data.h"

#define SAVE_SEG	29
#define SAVE(fn)	BANKED(SAVE_SEG, fn)

#define SAVE_BYTES		256
#define SAVE_VERSION	1
#define SAVE_TABLE		0   // g_SaveSlot for the hi-scores and the options; games from 1

// The medium: none (saving is greyed out, and says why), the tests' (video
// RAM, gone at power-off), the disk.
enum { MEDIUM_NONE, MEDIUM_VRAM, MEDIUM_DISK };
extern u8 g_SaveMedium;
extern u8 g_TestMedium;   // a test sets it before the title: the video RAM medium

enum { SAVE_OK, SAVE_EMPTY, SAVE_DAMAGED, SAVE_OTHER_VERSION, SAVE_FAILED };
extern u8 g_SaveSlot;
extern u8 g_SaveResult;
#define g_SaveBuf	g_Scratch   // data.c's: the pause menu's dimming uses it too, never at once
#if SAVE_BYTES > 256
#error "a save's record is g_Scratch"
#endif

// Each slot as its line shows it (MenuScreen.slotLine), from Save_Peek.
typedef struct
{
	u8  Used;
	u8  Easy;
	u8  Job;
	u8  Quarters;
	u8  Room;
	u16 Serial;     // goes up with each save: the newest is the title's CONTINUE
	u32 Score;
} SlotInfo;
extern SlotInfo g_Slots[SAVE_SLOTS];

// Where a loaded game puts Vale (Save_Before), for the main loop.
extern i32 g_LoadX;
extern i32 g_LoadY;

void Save_Init();    // once: the medium, the table (hi-scores, options), the slots' lines
void Save_Peek();    // the slots' lines again
void Save_Game();    // the game in play into slot g_SaveSlot
void Save_Clear();   // slot g_SaveSlot emptied
void Save_Load();    // slot g_SaveSlot into g_SaveBuf: g_SaveResult says if it will do
void Save_Table();   // the hi-scores and the options written
void Save_Before();  // a loaded game: its seed and mode, before the new game is made (g_SaveBuf)
void Save_Apply();   // ... its state, once the new game is made, before its room is entered
void Save_After();   // ... and once Vale is in it: his lives, the clock
