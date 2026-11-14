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

// new Rng(Rng.hash(v0, .., vn-1)): the generator seeded from the hash of n
// longs in g_RngValues (8 bytes each, little-endian; Rng_Value sets one from
// an int, sign and all). Save and restore the game's generator around it.
extern u8 g_RngValues[4][8];
void Rng_Value(u8 i, i32 v);
void Rng_HashSeed(u8 n);
// Its steps apart (M13: hashes that share their first values share them):
// h = the hash of g_RngValues[0..n-1], before the seeding; then from h,
// values[from..n-1] more, and the generator seeded.
void Rng_HashPrefix(u8 n, u8* h);
void Rng_HashFrom(u8* h, u8 from, u8 n);
void Rng_Save(u8* keep);
void Rng_Restore(const u8* keep);
