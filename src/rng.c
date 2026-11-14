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
static u16 s_Bound = 0;

//-----------------------------------------------------------------------------
// Seeding: splitmix64, with 64-bit words as byte arrays.

static u8 s_MA[8];     // the multiplicand
static u8 s_MB[8];     // the multiplier
static u8 s_MAcc[10];  // the product (and two spare bytes: MulAsm)

// The words' helpers in assembly (M13: SDCC's byte loops were 1 500 cycles a
// copy, 6 400 a shift; an orchid's cycle start hashes six times). d and a in
// hl, s and b in de.
static void Copy(u8* d, const u8* s) __naked
{
	d; s;
	__asm
		ex	de, hl
		ld	bc, #8
		ldir
		ret
	__endasm;
}

static void Add(u8* a, const u8* b) __naked
{
	a; b;
	__asm
		ld	b, #8
		or	a
	00001$:
		ld	a, (de)
		adc	a, (hl)
		ld	(hl), a
		inc	hl
		inc	de
		djnz	00001$
		ret
	__endasm;
}

// a ^= a >> n, n < 64: XorShr(a, n). The shifted word in s_MAcc (free
// between multiplications: Mul copies its product out).
static u8 s_XN;
static void XorShrAsm(u8* a) __naked
{
	a;
	__asm
		push	hl
		ld	a, (_s_XN)
		rrca
		rrca
		rrca
		and	a, #7
		ld	e, a
		ld	d, #0
		add	hl, de                  ; a + n / 8
		ld	a, #8
		sub	a, e
		ld	c, a
		ld	b, #0
		ld	de, #_s_MAcc
		ldir                        ; its bytes, n / 8 down
		ld	a, (_s_XN)
		rrca
		rrca
		rrca
		and	a, #7
		jr	z, 00002$
		ld	b, a
		xor	a
	00001$:
		ld	(de), a                 ; the top ones 0
		inc	de
		djnz	00001$
	00002$:
		ld	a, (_s_XN)
		and	a, #7
		jr	z, 00020$
		ld	c, a                    ; n % 8 bits more
	00010$:
		ld	hl, #_s_MAcc + 7
		or	a
		ld	b, #8
	00011$:
		rr	(hl)
		dec	hl
		djnz	00011$
		dec	c
		jr	nz, 00010$
	00020$:
		pop	hl
		ld	de, #_s_MAcc
		ld	b, #8
	00021$:
		ld	a, (de)
		xor	a, (hl)
		ld	(hl), a
		inc	hl
		inc	de
		djnz	00021$
		ret
	__endasm;
}
#define XorShr(a, n)	do { s_XN = (n); XorShrAsm(a); } while (0)

// a *= m (mod 2^64), in assembly (the C was 340 000 cycles a multiplication,
// and a new game seeds a dozen times: M8). Byte by byte (M13: bit by bit it
// was 34 000 cycles): each a_i * m_j with i + j under 8, an unrolled 8 x 8
// multiplication, added into the product's bytes i + j up; the row's places
// in the other registers (exx). The product has two bytes spare, past the
// eight kept, so its top products need no case of their own.
static u8 s_MI;        // i

static void MulAsm() __naked
{
	__asm
		ld	hl, #_s_MAcc
		xor	a
		ld	b, #10
	00001$:
		ld	(hl), a
		inc	hl
		djnz	00001$
		ld	(_s_MI), a
	00010$:
		ld	a, (_s_MI)
		ld	e, a
		ld	d, #0
		ld	hl, #_s_MA
		add	hl, de
		ld	a, (hl)
		or	a
		jp	z, 00045$               ; a_i = 0 adds nothing
		ld	c, a                    ; c = a_i
		ld	hl, #_s_MAcc
		add	hl, de                  ; the product's byte i
		ld	a, #8
		sub	a, e
		push	hl
		exx
		pop	hl                      ; hl' = its byte i + j
		ld	de, #_s_MB              ; de' = m_j
		ld	b, a                    ; b' = 8 - i products
	00020$:                         ; (in the other registers)
		ld	a, (de)
		inc	de
		exx
		ld	e, a
		ld	d, #0
		ld	h, c
		ld	l, d                    ; hl = a_i * m_j:
		add	hl, hl
		jr	nc, 00050$
		add	hl, de
	00050$:
		add	hl, hl
		jr	nc, 00051$
		add	hl, de
	00051$:
		add	hl, hl
		jr	nc, 00052$
		add	hl, de
	00052$:
		add	hl, hl
		jr	nc, 00053$
		add	hl, de
	00053$:
		add	hl, hl
		jr	nc, 00054$
		add	hl, de
	00054$:
		add	hl, hl
		jr	nc, 00055$
		add	hl, de
	00055$:
		add	hl, hl
		jr	nc, 00056$
		add	hl, de
	00056$:
		add	hl, hl
		jr	nc, 00057$
		add	hl, de
	00057$:
		ld	a, l
		exx
		add	a, (hl)
		ld	(hl), a
		inc	hl
		push	hl
		exx
		ld	a, h
		exx
		adc	a, (hl)
		ld	(hl), a
		jr	nc, 00040$
	00030$:
		ld	a, l                    ; the carry on up, to the spare byte at most
		cp	a, #<(_s_MAcc + 9)
		jr	z, 00040$
		inc	hl
		ld	a, (hl)
		add	a, #1
		ld	(hl), a
		jr	c, 00030$
	00040$:
		pop	hl
		djnz	00020$
		exx
	00045$:
		ld	hl, #_s_MI
		inc	(hl)
		ld	a, (hl)
		cp	a, #8
		jp	c, 00010$
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

// s_X = the next long; the state moves on. Unrolled, each byte of a shifted
// word from two of the word's own (M13: shifted a bit at a time through
// memory, 5 500 cycles a draw).
static void NextLong() __naked
{
	__asm
		; x = s0, s0 = s1 (y)
		ld	hl, #_g_RngState
		ld	de, #_s_X
		ld	bc, #8
		ldir
		ld	de, #_g_RngState
		ld	bc, #8
		ldir
		; x ^= x << 23, from the top byte down: byte i of it is
		; (x[i - 3] >> 1) | (x[i - 2] << 7)
		ld	a, (_s_X + 5)
		rra
		ld	a, (_s_X + 4)
		rra
		ld	b, a
		ld	a, (_s_X + 7)
		xor	a, b
		ld	(_s_X + 7), a
		ld	a, (_s_X + 4)
		rra
		ld	a, (_s_X + 3)
		rra
		ld	b, a
		ld	a, (_s_X + 6)
		xor	a, b
		ld	(_s_X + 6), a
		ld	a, (_s_X + 3)
		rra
		ld	a, (_s_X + 2)
		rra
		ld	b, a
		ld	a, (_s_X + 5)
		xor	a, b
		ld	(_s_X + 5), a
		ld	a, (_s_X + 2)
		rra
		ld	a, (_s_X + 1)
		rra
		ld	b, a
		ld	a, (_s_X + 4)
		xor	a, b
		ld	(_s_X + 4), a
		ld	a, (_s_X + 1)
		rra
		ld	a, (_s_X)
		rra
		ld	b, a
		ld	a, (_s_X + 3)
		xor	a, b
		ld	(_s_X + 3), a
		ld	a, (_s_X)
		rrca
		and	a, #0x80
		ld	b, a
		ld	a, (_s_X + 2)
		xor	a, b
		ld	(_s_X + 2), a
		; s1 = x ^ y ^ (x >>> 17) ^ (y >>> 26), a byte at a time: (x >>> 17)'s
		; byte i is (x[i + 2] >> 1) | (x[i + 3] << 7), (y >>> 26)'s
		; (y[i + 3] >> 2) | (y[i + 4] << 6)
		ld	a, (_s_X)
		ld	c, a
		ld	a, (_g_RngState)
		xor	a, c
		ld	c, a
		ld	a, (_s_X + 3)
		rra
		ld	a, (_s_X + 2)
		rra
		xor	a, c
		ld	c, a
		ld	a, (_g_RngState + 4)
		rrca
		rrca
		and	a, #0xC0
		ld	b, a
		ld	a, (_g_RngState + 3)
		rrca
		rrca
		and	a, #0x3F
		or	a, b
		xor	a, c
		ld	c, a
		ld	a, c
		ld	(_g_RngState + 8), a
		ld	a, (_s_X + 1)
		ld	c, a
		ld	a, (_g_RngState + 1)
		xor	a, c
		ld	c, a
		ld	a, (_s_X + 4)
		rra
		ld	a, (_s_X + 3)
		rra
		xor	a, c
		ld	c, a
		ld	a, (_g_RngState + 5)
		rrca
		rrca
		and	a, #0xC0
		ld	b, a
		ld	a, (_g_RngState + 4)
		rrca
		rrca
		and	a, #0x3F
		or	a, b
		xor	a, c
		ld	c, a
		ld	a, c
		ld	(_g_RngState + 9), a
		ld	a, (_s_X + 2)
		ld	c, a
		ld	a, (_g_RngState + 2)
		xor	a, c
		ld	c, a
		ld	a, (_s_X + 5)
		rra
		ld	a, (_s_X + 4)
		rra
		xor	a, c
		ld	c, a
		ld	a, (_g_RngState + 6)
		rrca
		rrca
		and	a, #0xC0
		ld	b, a
		ld	a, (_g_RngState + 5)
		rrca
		rrca
		and	a, #0x3F
		or	a, b
		xor	a, c
		ld	c, a
		ld	a, c
		ld	(_g_RngState + 10), a
		ld	a, (_s_X + 3)
		ld	c, a
		ld	a, (_g_RngState + 3)
		xor	a, c
		ld	c, a
		ld	a, (_s_X + 6)
		rra
		ld	a, (_s_X + 5)
		rra
		xor	a, c
		ld	c, a
		ld	a, (_g_RngState + 7)
		rrca
		rrca
		and	a, #0xC0
		ld	b, a
		ld	a, (_g_RngState + 6)
		rrca
		rrca
		and	a, #0x3F
		or	a, b
		xor	a, c
		ld	c, a
		ld	a, c
		ld	(_g_RngState + 11), a
		ld	a, (_s_X + 4)
		ld	c, a
		ld	a, (_g_RngState + 4)
		xor	a, c
		ld	c, a
		ld	a, (_s_X + 7)
		rra
		ld	a, (_s_X + 6)
		rra
		xor	a, c
		ld	c, a
		ld	a, (_g_RngState + 7)
		rrca
		rrca
		and	a, #0x3F
		xor	a, c
		ld	c, a
		ld	a, c
		ld	(_g_RngState + 12), a
		ld	a, (_s_X + 5)
		ld	c, a
		ld	a, (_g_RngState + 5)
		xor	a, c
		ld	c, a
		ld	a, (_s_X + 7)
		srl	a
		xor	a, c
		ld	c, a
		ld	a, c
		ld	(_g_RngState + 13), a
		ld	a, (_s_X + 6)
		ld	c, a
		ld	a, (_g_RngState + 6)
		xor	a, c
		ld	c, a
		ld	a, c
		ld	(_g_RngState + 14), a
		ld	a, (_s_X + 7)
		ld	c, a
		ld	a, (_g_RngState + 7)
		xor	a, c
		ld	c, a
		ld	a, c
		ld	(_g_RngState + 15), a
		; the result, s_X = s1 + y
		ld	a, (_g_RngState + 8)
		ld	b, a
		ld	a, (_g_RngState)
		add	a, b
		ld	(_s_X), a
		ld	a, (_g_RngState + 9)
		ld	b, a
		ld	a, (_g_RngState + 1)
		adc	a, b
		ld	(_s_X + 1), a
		ld	a, (_g_RngState + 10)
		ld	b, a
		ld	a, (_g_RngState + 2)
		adc	a, b
		ld	(_s_X + 2), a
		ld	a, (_g_RngState + 11)
		ld	b, a
		ld	a, (_g_RngState + 3)
		adc	a, b
		ld	(_s_X + 3), a
		ld	a, (_g_RngState + 12)
		ld	b, a
		ld	a, (_g_RngState + 4)
		adc	a, b
		ld	(_s_X + 4), a
		ld	a, (_g_RngState + 13)
		ld	b, a
		ld	a, (_g_RngState + 5)
		adc	a, b
		ld	(_s_X + 5), a
		ld	a, (_g_RngState + 14)
		ld	b, a
		ld	a, (_g_RngState + 6)
		adc	a, b
		ld	(_s_X + 6), a
		ld	a, (_g_RngState + 15)
		ld	b, a
		ld	a, (_g_RngState + 7)
		adc	a, b
		ld	(_s_X + 7), a
		ret
	__endasm;
}

// (s_X >>> 1) % s_Bound, bound < 32768: long division, a bit at a time,
// x's bits 63 down to 1 (bit 0 is shifted out). The remainder stays under
// the bound, so twice it and a bit fit 16 bits with no carry out: no carry
// to clear before the subtraction. A byte's eight steps unrolled (M13: with
// a loop a bit, 5 600 cycles a call).
static u16 Mod() __naked
{
	__asm
		ld	de, (_s_Bound)
		ld	hl, #0                  ; the remainder
		ld	a, (_s_X + 7)
		call	00590$
		ld	a, (_s_X + 6)
		call	00590$
		ld	a, (_s_X + 5)
		call	00590$
		ld	a, (_s_X + 4)
		call	00590$
		ld	a, (_s_X + 3)
		call	00590$
		ld	a, (_s_X + 2)
		call	00590$
		ld	a, (_s_X + 1)
		call	00590$
		ld	a, (_s_X)               ; its bit 0 is shifted out: seven
		rla
		adc	hl, hl
		sbc	hl, de
		jr	nc, 00620$
		add	hl, de
	00620$:
		rla
		adc	hl, hl
		sbc	hl, de
		jr	nc, 00621$
		add	hl, de
	00621$:
		rla
		adc	hl, hl
		sbc	hl, de
		jr	nc, 00622$
		add	hl, de
	00622$:
		rla
		adc	hl, hl
		sbc	hl, de
		jr	nc, 00623$
		add	hl, de
	00623$:
		rla
		adc	hl, hl
		sbc	hl, de
		jr	nc, 00624$
		add	hl, de
	00624$:
		rla
		adc	hl, hl
		sbc	hl, de
		jr	nc, 00625$
		add	hl, de
	00625$:
		rla
		adc	hl, hl
		sbc	hl, de
		jr	nc, 00626$
		add	hl, de
	00626$:
		ex	de, hl                  ; the result in de
		ret
	00590$:                         ; ---- a's eight bits
		rla
		adc	hl, hl
		sbc	hl, de
		jr	nc, 00600$
		add	hl, de
	00600$:
		rla
		adc	hl, hl
		sbc	hl, de
		jr	nc, 00601$
		add	hl, de
	00601$:
		rla
		adc	hl, hl
		sbc	hl, de
		jr	nc, 00602$
		add	hl, de
	00602$:
		rla
		adc	hl, hl
		sbc	hl, de
		jr	nc, 00603$
		add	hl, de
	00603$:
		rla
		adc	hl, hl
		sbc	hl, de
		jr	nc, 00604$
		add	hl, de
	00604$:
		rla
		adc	hl, hl
		sbc	hl, de
		jr	nc, 00605$
		add	hl, de
	00605$:
		rla
		adc	hl, hl
		sbc	hl, de
		jr	nc, 00606$
		add	hl, de
	00606$:
		rla
		adc	hl, hl
		sbc	hl, de
		jr	nc, 00607$
		add	hl, de
	00607$:
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

static void Xor(u8* a, const u8* b) __naked
{
	a; b;
	__asm
		ld	b, #8
	00001$:
		ld	a, (de)
		xor	a, (hl)
		ld	(hl), a
		inc	hl
		inc	de
		djnz	00001$
		ret
	__endasm;
}

u8 g_RngValues[4][8];   // Rng_HashSeed's values

// Its first step, splitmix(GOLDEN ^ values[0]), kept for the values[0] it was
// for (M13: a game hashes its own seed first every time, an orchid's cycle
// start too).
static u8 s_H1Key[8];
static u8 s_H1[8];
static u8 s_H1Ok = 0;

void Rng_HashPrefix(u8 n, u8* h)
{
	u8 i = 0;
	bool same = s_H1Ok && n >= 1;
	for (u8 b = 0; same && b < 8; b++)
		if (s_H1Key[b] != g_RngValues[0][b])
			same = FALSE;
	if (same)
	{
		Copy(h, s_H1);
		i = 1;
	}
	else
		Copy(h, c_Golden);
	for (; i < n; i++)
	{
		Xor(h, g_RngValues[i]);
		Splitmix(h);
		if (i == 0)
		{
			Copy(s_H1Key, g_RngValues[0]);
			Copy(s_H1, h);
			s_H1Ok = 1;
		}
	}
}

void Rng_HashFrom(u8* h, u8 from, u8 n)
{
	for (u8 i = from; i < n; i++)
	{
		Xor(h, g_RngValues[i]);
		Splitmix(h);
	}
	Rng_Seed(h);
}

void Rng_HashSeed(u8 n)
{
	u8 h[8];
	Rng_HashPrefix(n, h);
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
