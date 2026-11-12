// Wulf Quest for MSX2: the shell around the game (AGENTS.md §10, M10; the
// Java game's Shell, GameSession and their screens): the title, the attract
// demo, the options, the pause menu, the amulet's reveal, the map screen,
// game over, the hi-scores and their three letters.
//
// The code is banked (shell.c, ROM segment SHELL_SEG): the main loop calls
// Shell_Tick once a tick with the input in g_ShellIn and g_ShellKeys, and
// does what g_ShellDo asks: a tick of the game with g_ShellPlay's keys, a new
// game, the demo. After a tick of the game it calls Shell_AfterTick, which
// sees a game over, a win or a quarter taken.
#pragma once

#include "msxgl.h"
#include "data.h"
#include "shell_data.h"

#define SHELL_SEG	28
#define SHELL(fn)	BANKED(SHELL_SEG, fn)

// The screens (Shell.State, and GameSession's reveal and map).
enum
{
	SH_TITLE, SH_ATTRACT, SH_HISCORES, SH_OPTIONS, SH_PLAYING, SH_PAUSED, SH_SAVE, SH_LOAD,
	SH_REVEAL, SH_MAP, SH_GAME_OVER, SH_WON, SH_ENTRY, SH_NEW_GAME,
	SH_ENDING, SH_TALLY, SH_ROLL, SH_INTRO,  // M12 (scenes.c); SH_WON, M10's stand-in, unused
	SH_OFFER                                 // M12: Mr Jones's next job (§14.9)
};
extern u8  g_Shell;
extern u16 g_ShellTick;   // ticks on this screen (Shell.stateTick)

// The tick's input: VALE_UP .. VALE_FIRE held (the keys of the profile
// chosen, or the joystick), and these keys.
extern u8 g_ShellIn;
extern u8 g_ShellKeys;
#define SK_ESC		0x01   // back a step
#define SK_PAUSE	0x02   // STOP or F1
#define SK_MAP		0x04   // M or the joystick's second button
#define SK_DIFF		0x08   // D: Hard or Easy, on the title
#define SK_ENTER	0x10   // chooses on every page, as Space does
#define SK_JOB		0x20   // N: through the jobs opened, on the title (§14.9)
#define SK_ANY		0x80   // any key at all

// What the main loop is to do this tick.
extern u8 g_ShellDo;
#define DO_TICK		0x01   // a tick of the game, with g_ShellPlay
#define DO_NEW_GAME	0x02   // a new game (g_Easy as chosen), then show its room
#define DO_DEMO		0x04   // the demo's game (DEMO_*), then show its room
#define DO_CARRY_ON	0x08   // with DO_TICK: fire pressed anew (Vale_FireReleased first)
#define DO_LOAD		0x10   // the game in save.c's g_SaveBuf (Save_Before, _Apply, _After), then show it
extern u8 g_ShellPlay;

// The options: the keys (0: the first profile, the cursors; 1: Q A O P); the
// next game's mode. The hi-score table (HighScores): HISCORE_SIZE rows. The
// saves keep them (save.c).
extern u8 g_Profile;
extern u8 g_ShellEasy;
extern u8 g_ShellJob;
extern u8 g_Unlocked;      // the furthest job a win has opened (§14.9); kept with the table
extern u8 g_Offered;       // the job Mr Jones offers after the roll (Shell.offered), 0 none
extern u8 g_HiTable[HISCORE_SIZE * HISCORE_ROW_BYTES];

// The page the game is on (wulfquest.c's), 0 or 1: the shell's screens over
// the game go on the other and leave it as it was.
extern u8 g_PageShown;

void Shell_Init();       // once, at start: the hi-score table, the title
void Shell_Tick();
void Shell_AfterTick();
void Shell_Dress();      // the demo's room, drawn on g_DrawPage: dimmed, its bands
void Shell_DemoKeys();   // the demo's keys for the next tick, into g_ShellPlay
void Shell_Shown();      // the new game or the demo is on screen (g_PageShown)
extern u8 g_DrawPage;

// test/shell.sh: the demo's ticks played; set to open the map (from any screen).
extern u16 g_DemoTicks;
extern u8 g_Booted;
extern u8 g_TestMapOpen;
