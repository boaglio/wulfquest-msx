// Wulf Quest for MSX2: the debug overlay on the panel (AGENTS.md §10, M4).
//
// One line below the playfield: "ROOM cc,rr  X xxxx  Y yyyy", Vale's room
// (col,row) and his feet in world pixels. The glyphs are MSXgl's 6x8 sample
// font: the 17 characters the line uses are expanded once to SCREEN 5 bytes
// in RAM, and a glyph is written to video RAM through the VDP's ports, three
// bytes a line. A frame redoes only the numbers that changed, queueing the
// characters that did, and then writes at most OVERLAY_GLYPHS of the queue,
// so the overlay lags a walking Vale by a frame or two and is exact when he
// stops. Debug only: the real panel, with the Java game's font, is M9.
//
// Measured on the way here (SDCC keeps a loop's variables on the stack, at
// 19 cycles an access): the font expanded per glyph in C cost about 3 500
// cycles a glyph, 16-bit digit loops 9 000 for two numbers, a search of the
// line for what differs 10 000, the whole line redone every frame 30 000.

#include "overlay.h"
#include "constants.h"
#include "player.h"
#include "vale.h"
#include "font/font_mgl_sample6.h"

#define FONT		(g_Font_MGL_Sample6 + 4)   // after its header: 8 bytes a character from 0
#define GLYPH_W		6
#define GLYPH_H		8
#define GLYPH_BYTES	(GLYPH_W / 2 * GLYPH_H)
#define INK			PLAYER_INK                 // bright white, as Vale for now
#define TEXT_X		8
#define TEXT_Y		(PLAYFIELD_H + 4)
#define TEXT_LEN	26
// Where the numbers go in the line.
#define AT_COL		5
#define AT_ROW		8
#define AT_X		14
#define AT_Y		22
#define OVERLAY_GLYPHS	2   // written a frame at most

#if PLAYFIELD_H + 4 + GLYPH_H > 212
#error "the overlay does not fit under the playfield"
#endif
// A page's text line is in one 16 KB block of video RAM, so R#14 is set once a glyph.
#if (TEXT_Y * 128) / 0x4000 != ((TEXT_Y + GLYPH_H) * 128) / 0x4000
#error "the overlay's line crosses a 16 KB block of video RAM"
#endif
#if TEXT_LEN > 32
#error "the overlay's queue holds 32 places"
#endif
#if GLYPH_H != 8 || GLYPH_W != 6
#error "Blit and Glyph take a glyph to be 8 lines of 3 bytes"
#endif

// The characters the line uses; a character is its place here. The digits
// must be in order, from SPACE + 2.
static const c8 c_Alphabet[] = " ,0123456789MORXY";
#define CHARS		(sizeof(c_Alphabet) - 1)
#define SPACE		0
#define DIGIT0		2

static u8 s_Glyphs[CHARS * GLYPH_BYTES];   // set by Overlay_Init
static u8 s_Text[TEXT_LEN] = { 0 };   // what the page on screen shows
static u8 s_Want[TEXT_LEN] = { 0 };   // what it should show
// The places whose s_Want may differ from s_Text, in the order they changed;
// each at most once. QUEUE is a power of two of at least TEXT_LEN.
#define QUEUE		32
static u8 s_Queue[QUEUE] = { 0 };
static u8 s_Queued[TEXT_LEN] = { 0 };
static u8 s_Head = 0;
static u8 s_Tail = 0;
static u16 s_Top = 0;                 // the address of the page's first byte (pages 0 and 1)
static u8 s_Room = 0xFF;              // what the numbers were made from
static i16 s_X = -1;
static i16 s_Y = -1;

// Blit's arguments.
static const u8* s_Src = 0;
static u16 s_Dst = 0;

// 24 bytes from s_Src to video RAM at s_Dst (bit 16 clear), 3 a line, 8
// lines 128 bytes apart.
static void Blit() __naked
{
	__asm
		ld	hl, (_s_Src)
		ld	de, (_s_Dst)
		ld	c, #0x98
		ld	b, #8
		di
		ld	a, d
		rlca
		rlca
		and	a, #0x03
		out	(#0x99), a      ; R#14: address bits 16-14
		ld	a, #0x8E
		out	(#0x99), a
	00001$:
		ld	a, e
		out	(#0x99), a      ; address bits 7-0
		ld	a, d
		and	a, #0x3F
		or	a, #0x40
		out	(#0x99), a      ; bits 13-8, and "write"
		ld	a, (hl)
		out	(c), a
		inc	hl
		ld	a, (hl)
		out	(c), a
		inc	hl
		ld	a, (hl)
		out	(c), a
		inc	hl
		ld	a, e
		add	a, #0x80
		ld	e, a
		jr	nc, 00002$
		inc	d
	00002$:
		djnz	00001$
		ei
		ret
	__endasm;
}

static void Glyph(u8 i, u8 c)
{
	u16 c8x = (u16)c << 3;
	s_Src = s_Glyphs + c8x + c8x + c8x;   // c * GLYPH_BYTES, without a multiplication
	s_Dst = s_Top + (TEXT_Y * 128 + TEXT_X / 2) + i + i + i;
	Blit();
}

void Overlay_Init()
{
	// Two pixels of the font (bits 7 and 6 of the line) as a SCREEN 5 byte.
	static const u8 c_Pair[4] = { 0, INK, INK << 4, (INK << 4) | INK };
	u8* out = s_Glyphs;
	for (u8 c = 0; c < CHARS; c++)
	{
		const u8* g = FONT + ((u16)(u8)c_Alphabet[c] << 3);
		for (u8 y = 0; y < GLYPH_H; y++)
		{
			u8 b = *g++;
			*out++ = c_Pair[b >> 6];
			*out++ = c_Pair[(b >> 4) & 3];
			*out++ = c_Pair[(b >> 2) & 3];
		}
	}
}

static void Put(u8 i, u8 c)
{
	if (s_Want[i] != c)
	{
		s_Want[i] = c;
		if (!s_Queued[i])
		{
			s_Queued[i] = 1;
			s_Queue[s_Tail++ & (QUEUE - 1)] = i;
		}
	}
}

// n < 100 as two digits.
static void Two(u8 i, u8 n)
{
	u8 t = DIGIT0;
	while (n >= 10)
	{
		n -= 10;
		t++;
	}
	Put(i, t);
	Put(i + 1, DIGIT0 + n);
}

// n < 10 000 as four digits, leading zeros as spaces. Byte loops where
// they can be (SDCC's 16-bit ones cost 9 000 cycles for two numbers): below
// 1 000, n / 100 is (n / 4) / 25, and n / 4 fits a byte.
static void Four(u8 i, u16 n)
{
	static const u16 c_Hundreds[10] = { 0, 100, 200, 300, 400, 500, 600, 700, 800, 900 };
	u8 th = DIGIT0;
	while (n >= 1000)
	{
		n -= 1000;
		th++;
	}
	u8 q = (u8)(n >> 2);
	u8 hn = 0;
	while (q >= 25)
	{
		q -= 25;
		hn++;
	}
	u8 h = DIGIT0 + hn;
	u8 m = (u8)(n - c_Hundreds[hn]);
	u8 t = DIGIT0;
	while (m >= 10)
	{
		m -= 10;
		t++;
	}
	bool lead = th == DIGIT0;
	Put(i, lead ? SPACE : th);
	lead = lead && h == DIGIT0;
	Put(i + 1, lead ? SPACE : h);
	lead = lead && t == DIGIT0;
	Put(i + 2, lead ? SPACE : t);
	Put(i + 3, DIGIT0 + m);
}

// The numbers that changed, into s_Want.
static void Want()
{
	if (g_ValeRoom != s_Room)
	{
		s_Room = g_ValeRoom;
		Two(AT_COL, s_Room % GRID_W);
		Two(AT_ROW, s_Room / GRID_W);
	}
	if (FP_PX(g_ValeX) != s_X)
	{
		s_X = FP_PX(g_ValeX);
		Four(AT_X, (u16)s_X);
	}
	if (FP_PX(g_ValeY) != s_Y)
	{
		s_Y = FP_PX(g_ValeY);
		Four(AT_Y, (u16)s_Y);
	}
}

void Overlay_Update(u8 page)
{
	Want();
	s_Top = page ? 0x8000 : 0;
	u8 left = OVERLAY_GLYPHS;
	while (left && s_Head != s_Tail)
	{
		u8 i = s_Queue[s_Head++ & (QUEUE - 1)];
		s_Queued[i] = 0;
		u8 c = s_Want[i];
		if (s_Text[i] != c)
		{
			s_Text[i] = c;
			Glyph(i, c);
			left--;
		}
	}
}

void Overlay_Draw(u8 page)
{
	VDP_CommandHMMV(0, (u16)page * 256 + PLAYFIELD_H, 256, 212 - PLAYFIELD_H, 0);
	VDP_CommandWait();
	s_Top = page ? 0x8000 : 0;
	const c8* f = "ROOM   ,    X       Y     ";
	for (u8 i = 0; i < TEXT_LEN; i++)
	{
		u8 c = 0;
		while (c_Alphabet[c] != f[i])
			c++;
		s_Want[i] = c;
	}
	s_Room = 0xFF;
	s_X = -1;
	s_Y = -1;
	Want();
	// All of it: the page is not on screen yet.
	for (u8 i = 0; i < TEXT_LEN; i++)
	{
		s_Text[i] = s_Want[i];
		s_Queued[i] = 0;
		if (s_Want[i] != SPACE)
			Glyph(i, s_Want[i]);
	}
	s_Head = s_Tail;
}
