// Wulf Quest for MSX2: the panel under the playfield (AGENTS.md §10, M9; the
// Java game's PanelPainter and GameSession.message).
#pragma once

#include "msxgl.h"

// At start, and before each new game (M11: the page on show may be the
// shell's, not a panel to copy): the next Panel_Draw draws it whole.
void Panel_Init();

// The whole panel into a page (the hidden one, as a room is drawn).
void Panel_Draw(u8 page);

// The page on show: what changed since, a few glyphs a frame (one fewer in a
// busy frame).
void Panel_Update(u8 page, u8 busy);

// Debug: the panel's line shows the room and Vale's feet, as the Java
// game's dev mode does (F4 toggles it).
extern u8 g_PanelDebug;

// The hi-score table's best (the shell's, M10): the panel's HI.
extern u32 g_HiBest;

// A note on the panel's line for g_PanelNoteTicks ticks (GameSession.note:
// a save made, a game loaded), before a hint; the shell writes it and
// g_PanelNoteSerial goes up.
#define PANEL_NOTE_MAX	40
extern u8 g_PanelNote[PANEL_NOTE_MAX];
extern u8 g_PanelNoteN;
extern u8 g_PanelNoteTicks;
extern u8 g_PanelNoteSerial;
