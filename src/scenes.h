// Wulf Quest for MSX2: the scenes around the game (AGENTS.md §10, M12; the
// Java game's EndingScreen, TallyScreen, CreditsRoll and IntroScreen): the
// closing scene, the tally, the credits roll, the plane's intro. The shell
// (shell.c) runs them as its states; this draws them.
//
// Banked code (scenes.c, ROM segment SCENES_SEG): SCENES(fn), the scene in
// g_Scene and its ticks in g_ShellTick.
#pragma once

#include "msxgl.h"
#include "data.h"

#define SCENES_SEG	31
#define SCENES(fn)	BANKED(SCENES_SEG, fn)

enum { SCENE_NONE, SCENE_ENDING, SCENE_TALLY, SCENE_ROLL, SCENE_INTRO, SCENE_OFFER };
extern u8 g_Scene;
extern u8 g_RollPaused;    // the roll stood still to read (fire)
extern u16 g_RollTick;     // the roll's ticks, not counting those stood still

// The tally's sums, as the win added them (vale.c's Escape; Quest's): the
// score before the bonuses, and each bonus.
extern u32 g_TallyBefore;
extern u16 g_TallyEscape;
extern u16 g_TallyTime;
extern u16 g_TallyLives;

void Scenes_Enter();   // g_Scene's first picture: its backdrop, its figures
void Scenes_Step();    // a tick of it (g_ShellTick): what moved, what was typed
void Scenes_Leave();   // the atlas back for the jungle; the screen's scroll back
