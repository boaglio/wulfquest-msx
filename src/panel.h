// Wulf Quest for MSX2: the panel under the playfield (AGENTS.md §10, M9; the
// Java game's PanelPainter and GameSession.message).
#pragma once

#include "msxgl.h"

// Once, at start.
void Panel_Init();

// The whole panel into a page (the hidden one, as a room is drawn).
void Panel_Draw(u8 page);

// The page on show: what changed since, a few glyphs a frame.
void Panel_Update(u8 page);

// Debug: the panel's line shows the room and Vale's feet, as the Java
// game's dev mode does (F4 toggles it).
extern u8 g_PanelDebug;

// The hi-score table's best (the shell's, M10): the panel's HI.
extern u32 g_HiBest;
