// Wulf Quest for MSX2: where the big arrays live in RAM (AGENTS.md §10, M11).
//
// On a machine with a disk drive the game starts after the disk ROM (MSXgl's
// delayed boot), which keeps its work area from HIMEM (DE79h on a Philips
// NMS 8245) up to F37Fh, under the BIOS's. MSXgl's start-up puts the stack at
// HIMEM and copies the globals' start values (its _INITIALIZED area) before
// main runs: so the linker's RAM (_DATA, then _INITIALIZED) must end below
// any HIMEM, with room for that stack, and the big arrays, which nothing
// writes before main, sit above it, at these fixed places (__at) under the
// game's own stack (512 bytes under F380h). main first copies the disk ROM's
// work area aside (data.c's Data_DiskAside), then takes the top of RAM; a
// disk call swaps it back for the call (Data_DiskCall).
//
// build.sh checks the linker's RAM ends below RAM_LOW (g_RamLow in the map).
// The sizes are the modules' own expressions.
#pragma once

// TABLES_BYTES is the converter's: tables.h defines it, then includes this.
#ifndef TABLES_BYTES
#error "include tables.h, which includes ram.h"
#endif

#define RAM_STACK		0xF380   // the game's stack, 512 bytes down from here
#define RAM_TOP			0xF180

#include "spawn.h"
#include "sprites.h"
#include "data.h"

#define RAM_TABLES_BYTES		TABLES_BYTES
#define RAM_SPAWN_QUEUE_BYTES	(SPAWN_QUEUE_N * 2)        // spawn.c: s_Queue
#define RAM_SPAWN_SEEN_BYTES	(SPAWN_COLS * SPAWN_ROWS)              // spawn.c: s_Seen
#define RAM_SPAWN_CLASS_BYTES	((SPAWN_CLASSES + 3) / 4)        // spawn.c: s_Class
#define RAM_COLOUR_BYTES		(SPR_SPRITES * 16)         // sprites.c: s_Colour
#define RAM_CELLS_BYTES			(CELLS_H * CELLS_W)        // vale.c: s_Cells

#define RAM_TABLES		(RAM_TOP - RAM_TABLES_BYTES)
#define RAM_SPAWN_QUEUE	(RAM_TABLES - RAM_SPAWN_QUEUE_BYTES)
#define RAM_SPAWN_SEEN	(RAM_SPAWN_QUEUE - RAM_SPAWN_SEEN_BYTES)
#define RAM_SPAWN_CLASS	(RAM_SPAWN_SEEN - RAM_SPAWN_CLASS_BYTES)
#define RAM_COLOUR		(RAM_SPAWN_CLASS - RAM_COLOUR_BYTES)
#define RAM_CELLS		(RAM_COLOUR - RAM_CELLS_BYTES)
#define RAM_LOW			RAM_CELLS
