// Wulf Quest for MSX2: the hardware sprites (AGENTS.md §5, §10 M5).
#pragma once

#include "msxgl.h"

// The V9938's 32 sprites are shared out among slots, each with its own
// sprites, its patterns in video RAM and its colours in RAM:
//   SLOT_BLADE  Vale's blade and crossguard, two one-colour sprites
//   SLOT_VALE   Vale, 16x32: two blocks of two layered sprites
//   SLOT_SMALL + i, i < SPR_SMALL: a 16x16 block of two layered sprites
//   SLOT_BIG + i, i < SPR_BIG: up to 2x2 blocks of them (a big beast)
// A layered block is a base and an overlay with the CC bit (SpriteCut.java):
// the overlay directly follows its base in the attribute table. A slot's
// sprites are numbered 0..SPR_SPRITES-1, each with its own pattern; the
// attribute table has room for SPR_PLACES of them a frame.
#define SLOT_BLADE	0
#define SLOT_VALE	1
#define SLOT_SMALL	2
#define SPR_SMALL	6
#define SLOT_BIG	(SLOT_SMALL + SPR_SMALL)
#define SPR_BIG		5
#define SPR_SLOTS	(SLOT_BIG + SPR_BIG)
#define SPR_SPRITES	58
#define SPR_PLACES	32

// Sets both sets of tables up, set 0 shown.
void Sprites_Init();

// Before a frame's sprites: draw into the set not shown (the back set). The
// patterns and colours loaded into a slot are the back set's; End hands it
// to the next blank to show (data.h's flips).
void Sprites_Prepare();
u8 Sprites_Back();

// The slot's first sprite in the back set: its patterns are 32 bytes a
// sprite from there.
u16 Sprites_PatternAddress(u8 slot);

// Its colour tables in RAM, 16 bytes a sprite, as the frame has them (no
// early clock): load a frame into them, then call Sprites_Changed.
u8* Sprites_Colours(u8 slot);
void Sprites_Changed(u8 slot);

// Where sprite i of the slot is drawn from the slot's position.
void Sprites_SetOffset(u8 slot, u8 i, i8 dx, i8 dy);

// A slot of layered blocks: how many it shows now (columns << 4 | rows, as
// Data_LoadSpriteFrame gives them), each block 16 px from the next.
void Sprites_SetBlocks(u8 slot, u8 blocks);

// A frame's sprites, front to back: Begin, a Put for each slot to show (its
// top-left on the screen), End. End writes the back set's attribute table
// and any of its colours that changed, and has the next blank show it. Slots before
// SLOT_SMALL always come first; when a line has more than the V9938's 8
// sprites, or there are more than its 32, the others take turns, frame by
// frame, to be first.
void Sprites_Begin();
void Sprites_Put(u8 slot, i16 x, i16 y);
void Sprites_End();

// Frames that had a line with more than 8 sprites; read by the tests.
extern u16 g_SpritesCrowded;
