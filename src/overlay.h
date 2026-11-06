// Wulf Quest for MSX2: the debug overlay on the panel (AGENTS.md §10, M4):
// the room Vale is in and where he stands, until the real panel (M9).
#pragma once

#include "msxgl.h"

// Once, at start: the glyphs, into RAM.
void Overlay_Init();

// The whole overlay, on the panel of page 0 or 1: for a page about to be shown.
void Overlay_Draw(u8 page);

// What has changed since it was last drawn, on the page on screen.
void Overlay_Update(u8 page);
