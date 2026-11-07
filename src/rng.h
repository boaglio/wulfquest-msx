// Wulf Quest for MSX2: the game's random numbers (AGENTS.md §10, M6).
#pragma once

#include "msxgl.h"

// The Java game's Rng, bit for bit: xorshift128+, seeded through splitmix64.
// A game's draws come in the Java game's order, so they are the same numbers.
void Rng_Seed(const u8* seed);   // 8 bytes, little-endian: the Java game's long

// Uniform in [0, bound): (nextLong() >>> 1) % bound.
u16 Rng_Next(u16 bound);

// The state's low bytes, for the tests: s0's and s1's lowest 16 bits.
extern u8 g_RngState[16];

// new Rng(Rng.hash(seed, stream)).nextInt(bound): a draw from a stream of its
// own (the amulet set's, M7), the game's generator left as it was.
u16 Rng_Stream(const u8* seed, const u8* stream, u16 bound);
