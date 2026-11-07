// Wulf Quest for MSX2: the sound on the PSG (AGENTS.md §10, M9; the Java
// game's Mixer and BeeperSynth).
//
// In ROM segment 0 (linked early): it runs in the frame's interrupt, with any
// bank switched, and banked code calls it.
#pragma once

#include "msxgl.h"
#include "sound_data.h"

extern u8 g_SoundOn;   // off: nothing new is played (the options' sound, M10)

// Mixer.play: an effect, on a free voice, or on the least important busy one
// if it is more important; else not at all.
void Sound_Play(u8 id);

// Mixer.loop: an effect that repeats while on (the Wulf's growl).
void Sound_Loop(u8 id, u8 on);

// Mixer.tune: the tune to play on the third channel (0xFF none). A tune that
// does not loop ends by itself.
void Sound_Tune(u8 id);

// The interrupt's part (data.c's hook): a frame of every voice to the PSG.
void Sound_Frame();

// At start: the rate the tables are for (the VDP's), and the PSG quiet.
void Sound_Init(u8 hz60);
