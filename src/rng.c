// Wulf Quest for MSX2: the game's random numbers (AGENTS.md §10, M6).
//
// The Java game's wulf.engine.Rng, bit for bit: xorshift128+ on two 64-bit
// words, seeded through splitmix64; nextInt(bound) = (nextLong() >>> 1) %
// bound. The words are 8 bytes each, little-endian. A draw is in assembly
// (SDCC's 64-bit integers cost 3 KB of library and thousands of cycles); the
// seeding, once a game, is small C.

#include "rng.h"

u8 g_RngState[16] = { 0 };   // s0, then s1
#define S0	(g_RngState)
#define S1	(g_RngState + 8)

static u8 s_X[8];     // xorshift's x; the draw's result
static u8 s_T[8];
static u16 s_Bound = 0;

//-----------------------------------------------------------------------------
// Seeding: splitmix64, with 64-bit words as byte arrays.

static void Copy(u8* d, const u8* s)
{
	for (u8 i = 0; i < 8; i++)
		d[i] = s[i];
}

static void Add(u8* a, const u8* b)
{
	u16 c = 0;
	for (u8 i = 0; i < 8; i++)
	{
		c += (u16)a[i] + b[i];
		a[i] = (u8)c;
		c >>= 8;
	}
}

// a ^= a >> n, n < 64
static void XorShr(u8* a, u8 n)
{
	u8 t[8];
	for (u8 i = 0; i < 8; i++)
	{
		u8 lo = i + (n >> 3);
		u8 s = n & 7;
		u8 v = lo < 8 ? a[lo] >> s : 0;
		if (s && lo + 1 < 8)
			v |= a[lo + 1] << (8 - s);
		t[i] = v;
	}
	for (u8 i = 0; i < 8; i++)
		a[i] ^= t[i];
}

// a *= m (mod 2^64): shift and add, in assembly (the C was 340 000 cycles a
// multiplication, and a new game seeds a dozen times: M8).
static u8 s_MA[8];     // the multiplicand, doubled each bit
static u8 s_MB[8];     // the multiplier, shifted out a bit at a time
static u8 s_MAcc[8];   // the product

static void MulAsm() __naked
{
	__asm
		ld	hl, #_s_MAcc
		xor	a
		ld	b, #8
	00001$:
		ld	(hl), a
		inc	hl
		djnz	00001$
		ld	c, #64
	00010$:
		; carry = the multiplier's lowest bit, shifted out
		ld	hl, #_s_MB + 7
		or	a
		ld	b, #8
	00011$:
		rr	(hl)
		dec	hl
		djnz	00011$
		jr	nc, 00020$
		; the product += the multiplicand
		ld	hl, #_s_MAcc
		ld	de, #_s_MA
		or	a
		ld	b, #8
	00012$:
		ld	a, (de)
		adc	a, (hl)
		ld	(hl), a
		inc	hl
		inc	de
		djnz	00012$
	00020$:
		; the multiplicand doubled
		ld	hl, #_s_MA
		or	a
		ld	b, #8
	00021$:
		rl	(hl)
		inc	hl
		djnz	00021$
		dec	c
		jr	nz, 00010$
		ret
	__endasm;
}

static void Mul(u8* a, const u8* m)
{
	Copy(s_MA, a);
	Copy(s_MB, m);
	MulAsm();
	Copy(a, s_MAcc);
}

static const u8 c_Golden[8] = { 0x15, 0x7C, 0x4A, 0x7F, 0xB9, 0x79, 0x37, 0x9E };   // 0x9E3779B97F4A7C15
static const u8 c_Mix1[8] = { 0xB9, 0xE5, 0xE4, 0x1C, 0x6D, 0x47, 0x58, 0xBF };     // 0xBF58476D1CE4E5B9
static const u8 c_Mix2[8] = { 0xEB, 0x11, 0x31, 0x13, 0xBB, 0x49, 0xD0, 0x94 };     // 0x94D049BB133111EB

static void Splitmix(u8* z)
{
	XorShr(z, 30);
	Mul(z, c_Mix1);
	XorShr(z, 27);
	Mul(z, c_Mix2);
	XorShr(z, 31);
}

void Rng_Seed(const u8* seed)
{
	u8 z[8];
	Copy(z, seed);
	Add(z, c_Golden);
	Copy(S0, z);
	Splitmix(S0);
	Add(z, c_Golden);
	Copy(S1, z);
	Splitmix(S1);
	u8 any = 0;
	for (u8 i = 0; i < 16; i++)
		any |= g_RngState[i];
	if (!any)
		S1[0] = 1;
}

//-----------------------------------------------------------------------------
// A draw: xorshift128+, then (x >>> 1) % bound.

// s_X = the next long; the state moves on.
static void NextLong() __naked
{
	__asm
		push	ix
		; x = s0; s0 = s1 (y)
		ld	hl, #_g_RngState
		ld	de, #_s_X
		ld	bc, #8
		ldir                        ; hl = s1
		ld	de, #_g_RngState
		ld	bc, #8
		ldir
		; t = x << 23: x << 24 (3 bytes up), then right 1, bit 63 from x bit 40
		ld	hl, #_s_T
		xor	a
		ld	(hl), a
		inc	hl
		ld	(hl), a
		inc	hl
		ld	(hl), a
		inc	hl
		ex	de, hl
		ld	hl, #_s_X
		ld	bc, #5
		ldir                        ; t[3..7] = x[0..4]
		ld	a, (_s_X + 5)
		rra                         ; carry = x bit 40
		ld	hl, #_s_T + 7
		ld	b, #8
	00001$:
		rr	(hl)
		dec	hl
		djnz	00001$
		; x ^= t
		ld	hl, #_s_X
		ld	de, #_s_T
		ld	b, #8
	00002$:
		ld	a, (de)
		xor	a, (hl)
		ld	(hl), a
		inc	hl
		inc	de
		djnz	00002$
		; s1 = x ^ y: y is s0 now
		ld	hl, #_s_X
		ld	de, #_g_RngState
		ld	ix, #_g_RngState + 8
		ld	b, #8
	00003$:
		ld	a, (de)
		xor	a, (hl)
		ld	0 (ix), a
		inc	hl
		inc	de
		inc	ix
		djnz	00003$
		; t = x >> 17: 2 bytes down, then right 1
		ld	hl, #_s_X + 2
		ld	de, #_s_T
		ld	bc, #6
		ldir
		xor	a
		ld	(de), a
		inc	de
		ld	(de), a
		ld	hl, #_s_T + 7
		ld	b, #8
		or	a
	00004$:
		rr	(hl)
		dec	hl
		djnz	00004$
		; s1 ^= t
		ld	hl, #_g_RngState + 8
		ld	de, #_s_T
		ld	b, #8
	00005$:
		ld	a, (de)
		xor	a, (hl)
		ld	(hl), a
		inc	hl
		inc	de
		djnz	00005$
		; t = y >> 26: 3 bytes down, then right 2
		ld	hl, #_g_RngState + 3
		ld	de, #_s_T
		ld	bc, #5
		ldir
		xor	a
		ld	(de), a
		inc	de
		ld	(de), a
		inc	de
		ld	(de), a
		ld	c, #2
	00006$:
		ld	hl, #_s_T + 7
		ld	b, #8
		or	a
	00007$:
		rr	(hl)
		dec	hl
		djnz	00007$
		dec	c
		jr	nz, 00006$
		; s1 ^= t
		ld	hl, #_g_RngState + 8
		ld	de, #_s_T
		ld	b, #8
	00008$:
		ld	a, (de)
		xor	a, (hl)
		ld	(hl), a
		inc	hl
		inc	de
		djnz	00008$
		; the result, s_X = s1 + y
		ld	hl, #_g_RngState + 8
		ld	de, #_g_RngState
		ld	ix, #_s_X
		ld	b, #8
		or	a
	00009$:
		ld	a, (de)
		adc	a, (hl)
		ld	0 (ix), a
		inc	hl
		inc	de
		inc	ix
		djnz	00009$
		pop	ix
		ret
	__endasm;
}

// (s_X >>> 1) % s_Bound, bound < 32768: long division, a bit at a time,
// x's bits 63 down to 1 (bit 0 is shifted out). The remainder stays under
// the bound, so twice it and a bit fit 16 bits.
static u16 Mod() __naked
{
	__asm
		push	ix
		ld	de, (_s_Bound)
		ld	hl, #0                  ; the remainder
		ld	ix, #_s_X + 7
		ld	c, #8                   ; bytes left
	00010$:
		ld	a, 0 (ix)
		ld	b, #8
		dec	c
		jr	nz, 00014$
		ld	b, #7                   ; x[0]: its bit 0 is shifted out
	00014$:
		rla                             ; the next bit, into carry (the rest stay in a:
		adc	hl, hl                  ; none of these touch it) remainder * 2 + bit
		or	a
		sbc	hl, de
		jr	nc, 00015$
		add	hl, de                  ; too small: back
	00015$:
		djnz	00014$
		dec	ix
		ld	a, c
		or	a
		jr	nz, 00010$
		ex	de, hl                  ; the result in de
		pop	ix
		ret
	__endasm;
}

u16 Rng_Next(u16 bound)
{
	NextLong();
	if ((bound & (bound - 1)) == 0)
	{
		// A power of two: the low bits of x >>> 1.
		u16 v = (u16)(s_X[0] >> 1) | ((u16)s_X[1] << 7) | ((u16)s_X[2] << 15);
		return v & (bound - 1);
	}
	s_Bound = bound;
	return Mod();
}

//-----------------------------------------------------------------------------
// Rng.hash(seed, stream): splitmix(splitmix(GOLDEN ^ seed) ^ stream).

static void Xor(u8* a, const u8* b)
{
	for (u8 i = 0; i < 8; i++)
		a[i] ^= b[i];
}

u8 g_RngValues[4][8];   // Rng_HashSeed's values

void Rng_HashSeed(u8 n)
{
	u8 h[8];
	Copy(h, c_Golden);
	for (u8 i = 0; i < n; i++)
	{
		Xor(h, g_RngValues[i]);
		Splitmix(h);
	}
	Rng_Seed(h);
}

void Rng_Save(u8* keep)
{
	for (u8 i = 0; i < 16; i++)
		keep[i] = g_RngState[i];
}

void Rng_Restore(const u8* keep)
{
	for (u8 i = 0; i < 16; i++)
		g_RngState[i] = keep[i];
}

void Rng_Value(u8 i, i32 v)
{
	u8* d = g_RngValues[i];
	const u8* b = (const u8*)&v;   // little-endian, as the long's low half
	d[0] = b[0];
	d[1] = b[1];
	d[2] = b[2];
	d[3] = b[3];
	u8 sign = (b[3] & 0x80) ? 0xFF : 0;
	d[4] = sign;
	d[5] = sign;
	d[6] = sign;
	d[7] = sign;
}

u16 Rng_Stream(const u8* seed, const u8* stream, u16 bound)
{
	u8 keep[16];
	Rng_Save(keep);
	Copy(g_RngValues[0], seed);
	Copy(g_RngValues[1], stream);
	Rng_HashSeed(2);
	u16 v = Rng_Next(bound);
	Rng_Restore(keep);
	return v;
}
