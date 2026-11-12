// Wulf Quest for MSX2: ROM segment 31, banked code (AGENTS.md §3; data.c's
// Bank_Call). MSXgl compiles this file into the segment its name gives, for
// bank 1 (8000h); the program calls into it through Bank_Call (SCENES(fn)).

#include "scenes.c"
