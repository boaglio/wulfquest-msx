// Wulf Quest for MSX2: the panel under the playfield (AGENTS.md §10, M9).
// Banked code since M11 (segment PANEL_SEG: PANEL(fn)): it calls only data.c,
// keeps its tables in RAM, and leaves SDCC's library to the main loop.
//
// The Java game's PanelPainter's contents, laid out anew for the MSX2's 36
// lines under the playfield: the score and the run's best (the big font),
// the lives as heads and the Easy label, the amulet so far at half size in
// the middle, and a line at the bottom: a message (the shrine's hint, in the
// small font) or the flower's bar.
//
// As the debug overlay before it (M4): each place on the panel (a cell) has
// what it should show and what the page on show shows; a frame writes at
// most PANEL_CELLS_A_FRAME of those that differ, through the VDP's ports
// (data.c), so the panel lags a busy change by a frame or two. A room drawn
// on the hidden page gets the whole panel.

#include "panel.h"
#include "panel_data.h"
#include "data.h"
#include "vale.h"
#include "game.h"
#include "game_data.h"
#include "quest.h"
#include "quest_data.h"
#include "constants.h"

#define TOP			PLAYFIELD_H
#define ROW_A		(TOP + 2)    // score, best
#define ROW_B		(TOP + 14)   // lives, Easy
#define ROW_C		(TOP + 28)   // a message or the bar
#define AMULET_X	((256 - 16) / 2)
#define AMULET_Y	(TOP + 10)
#define BAR_X		((256 - EFFECT_BAR_W) / 2)
#define PANEL_CELLS_A_FRAME	2
#define MESSAGE_MAX	40

#if TOP + 36 > 212 || ROW_C + 6 > 212
#error "the panel does not fit under the playfield"
#endif
#if PLAYER_LIVES_MAX > 9
#error "the panel shows the lives past five as one digit"
#endif

// The cells: kind by range.
#define C_ROW_A		0            // 31 big glyphs, x = 4 + 8i
#define C_LIVES		31           // 5 big glyphs: LIVES
#define C_HEADS		36           // 5 images, x = 48 + 8i
#define C_TIMES		41           // 2 big glyphs: X and a digit, after the heads
#define C_EASY		43           // 4 big glyphs at the right
#define C_AMULET	47           // 4 images, the quarters
#define C_MESSAGE	51           // MESSAGE_MAX small glyphs, from s_MsgX
#define CELLS		(C_MESSAGE + MESSAGE_MAX)
#define BLANK		' '

static u8 s_Want[CELLS];
static u8 s_Shown[CELLS];
static u8 s_Queue[128];
static u8 s_Queued[CELLS];
static u8 s_Head = 0;
static u8 s_Tail = 0;
static u16 s_Top = 0;          // the page's first byte
static u8 s_MsgX = 0;
u8 g_PanelDebug = 0;
u32 g_HiBest = 0;
u8  g_PanelNote[PANEL_NOTE_MAX];
u8  g_PanelNoteN = 0;
u8  g_PanelNoteTicks = 0;
u8  g_PanelNoteSerial = 0;
static bool s_Drawn = FALSE;   // the page on show has the whole panel: a room's page copies it

// What the cells were made from.
static u32 s_Score = 0xFFFFFFFF;
static u32 s_Best = 0xFFFFFFFF;
static u8 s_Lives = 0xFF;
static u8 s_Slots = 0xFF;
static u8 s_Easy = 0xFF;
static u8 s_Message = 0xFE;     // the message's key: 0xFF none, else a hint's number, 0xF0 debug
static u16 s_DebugKey = 0;
static u8 s_Bar = 0xFF;         // the bar's fill in pixels, 0xFF none
static u8 s_BarInk = 0;

// Tables in RAM: banked code (segment PANEL_SEG, M11) reads none from ROM.
static u16 s_Hints[16] = { STR_HINTS };
static u8 s_OrchidInk[] = { ORCHID_INKS };
u8 g_PanelPage = 0;
u8 g_PanelBusy = 0;
u8 g_PanelBarFill = 0xFF;

static void Put(u8 i, u8 v)
{
	if (s_Want[i] == v)
		return;
	s_Want[i] = v;
	if (!s_Queued[i])
	{
		s_Queued[i] = 1;
		s_Queue[s_Tail++ & 127] = i;
	}
}

static void Text(u8 first, const u8* s, u8 n, u8 cells)
{
	for (u8 i = 0; i < cells; i++)
		Put(first + i, i < n ? s[i] : BLANK);
}

// A number as digits, zero-padded to SCORE_DIGITS (PanelPainter.pad; data.c).
static void Digits(u8 first, u32 v)
{
	u8 d[SCORE_DIGITS];
	Data_Pad(v, SCORE_DIGITS, d);
	Text(first, d, SCORE_DIGITS, SCORE_DIGITS);
}

static void Label(u8 first, u16 str, u8 cells)
{
	u8 buf[MESSAGE_MAX];
	u8 n = Data_String(str, buf);
	Text(first, buf, n, cells);
}

// One cell to the page.
static void Write(u8 i)
{
	u8 v = s_Want[i];
	s_Shown[i] = v;
	u16 row = s_Top;
	if (i < C_LIVES)
		Data_Glyph(s_Top + ROW_A * 128 + 2 + (i - C_ROW_A) * 4, TRUE, v, i < C_ROW_A + 13 ? PANEL_BRIGHT : PANEL_INK,
			PANEL_GROUND);
	else if (i < C_HEADS)
		Data_Glyph(s_Top + ROW_B * 128 + 2 + (i - C_LIVES) * 4, TRUE, v, PANEL_INK, PANEL_GROUND);
	else if (i < C_TIMES)
	{
		u16 at = s_Top + ROW_B * 128 + 24 + (i - C_HEADS) * 4;
		if (v == BLANK)
			Data_Glyph(at, TRUE, BLANK, PANEL_INK, PANEL_GROUND);
		else
			Data_Image(at, IMAGE_HEAD);
	}
	else if (i < C_EASY)
		Data_Glyph(s_Top + ROW_B * 128 + 44 + (i - C_TIMES) * 4, TRUE, v, PANEL_INK, PANEL_GROUND);
	else if (i < C_AMULET)
		Data_Glyph(s_Top + ROW_B * 128 + 110 + (i - C_EASY) * 4, TRUE, v, PANEL_EASY_INK, PANEL_GROUND);
	else if (i < C_MESSAGE)
	{
		u8 k = i - C_AMULET;
		Data_Image(s_Top + (AMULET_Y + (k >> 1) * 8) * 128 + (AMULET_X >> 1) + (k & 1) * 4, v);
	}
	else
		Data_Glyph(s_Top + ROW_C * 128 + (s_MsgX >> 1) + (i - C_MESSAGE) * 2, FALSE, v, PANEL_BRIGHT, PANEL_GROUND);
	(void)row;
}

// The bottom line: a rectangle of ground, the bar's frame and fill.
static void Fill(u8 page, u8 x, u8 y, u8 w, u8 h, u8 colour)
{
	g_VDX = x;
	g_VDY = ((u16)page << 8) + y;
	g_VNX = w;
	g_VNY = h;
	g_VClr = colour;
	g_VCmd = VCMD_LMMV;
	Data_Vdp();
	Data_VdpWait();
}

static void Bar(u8 page)
{
	Fill(page, 0, ROW_C, 255, 6, PANEL_GROUND);
	Fill(page, 255, ROW_C, 1, 6, PANEL_GROUND);
	if (s_Bar == 0xFF)
		return;
	// PanelPainter.drawEffectBar: a frame, the fill in the flower's colour.
	Fill(page, BAR_X, ROW_C, EFFECT_BAR_W, 6, PANEL_DIM);
	Fill(page, BAR_X + 1, ROW_C + 1, EFFECT_BAR_W - 2, 4, PANEL_GROUND);
	if (s_Bar)
		Fill(page, BAR_X + 1, ROW_C + 1, s_Bar, 4, s_BarInk);
}

// What should show, into s_Want; true when the bottom line must be redone.
static bool Want()
{
	bool line = FALSE;
	if (s_Score != g_Score)
	{
		s_Score = g_Score;
		Label(C_ROW_A, STR_SCORE, 6);
		Digits(C_ROW_A + 6, s_Score);
	}
	if (s_Best != g_HiBest)
	{
		s_Best = g_HiBest;
		Label(C_ROW_A + 21, STR_HI, 3);
		Digits(C_ROW_A + 24, s_Best);
	}
	if (s_Lives != g_ValeLives)
	{
		s_Lives = g_ValeLives;
		Label(C_LIVES, STR_LIVES, 5);
		for (u8 i = 0; i < LIVES_SHOWN; i++)
			Put(C_HEADS + i, i < s_Lives ? 1 : BLANK);
		Put(C_TIMES, s_Lives > LIVES_SHOWN ? 'X' : BLANK);
		Put(C_TIMES + 1, s_Lives > LIVES_SHOWN ? '0' + s_Lives : BLANK);
	}
	if (s_Easy != g_Easy)
	{
		s_Easy = g_Easy;
		if (s_Easy)
			Label(C_EASY, STR_EASY, 4);
		else
			Text(C_EASY, 0, 0, 4);
	}
	if (s_Slots != g_QuestSlots)
	{
		s_Slots = g_QuestSlots;
		for (u8 k = 0; k < QUEST_SLOTS; k++)
			Put(C_AMULET + k, (s_Slots & (1 << k)) ? IMAGE_QUARTER + k : IMAGE_QUARTER_DIM + k);
	}
	// The bottom line: a shrine's hint, the debug line, or the flower's bar.
	u8 msg = 0xFF;
	if (g_PanelNoteTicks > 0)
		msg = 0xF3;   // GameSession.message: a note first (the saves', M11)
	else if (g_HintTicks > 0)
		msg = (g_HintToExit ? 8 : 0) + g_HintDirection;
	else if (g_PanelDebug)
		msg = 0xF0;
	u16 debugKey = msg == 0xF3 ? g_PanelNoteSerial
		: g_PanelDebug ? (u16)(FP_PX(g_ValeX) ^ (FP_PX(g_ValeY) << 3)) : 0;
	if (msg != s_Message || ((msg == 0xF0 || msg == 0xF3) && debugKey != s_DebugKey))
	{
		bool redo = msg != s_Message;
		s_Message = msg;
		s_DebugKey = debugKey;
		u8 buf[MESSAGE_MAX];
		u8 n = 0;
		if (msg < 16)
			n = Data_String(s_Hints[msg], buf);
		else if (msg == 0xF3)
		{
			n = g_PanelNoteN;
			for (u8 i = 0; i < n; i++)
				buf[i] = g_PanelNote[i];
		}
		else if (msg == 0xF0)
		{
			// ROOM cc,rr  X xxxx Y yyyy: GameSession.message's dev line.
			buf[n++] = 'R';
			buf[n++] = 'O';
			buf[n++] = 'O';
			buf[n++] = 'M';
			buf[n++] = ' ';
			Data_Pad(g_ValeRoom & (GRID_W - 1), 2, buf + n);
			n += 2;
			buf[n++] = ',';
			Data_Pad(g_ValeRoom >> 4, 2, buf + n);
			n += 2;
			buf[n++] = ' ';
			buf[n++] = 'X';
			Data_Pad((u16)FP_PX(g_ValeX), 4, buf + n);
			n += 4;
			buf[n++] = ' ';
			buf[n++] = 'Y';
			Data_Pad((u16)FP_PX(g_ValeY), 4, buf + n);
			n += 4;
		}
		if (redo)
		{
			line = TRUE;
			s_MsgX = (u8)(128 - (n << 1)) & 0xFE;   // drawCentred
			for (u8 i = 0; i < MESSAGE_MAX; i++)
			{
				s_Shown[C_MESSAGE + i] = BLANK;   // the line is cleared below
				s_Want[C_MESSAGE + i] = BLANK;
			}
		}
		Text(C_MESSAGE, buf, n, MESSAGE_MAX);
	}
	// The flower's bar: what is left of it, in its colour (remainingPerMille).
	// (The fill is the main loop's, g_PanelBarFill: its sums take SDCC's library.)
	u8 bar = 0xFF;
	if (msg == 0xFF && g_PanelBarFill != 0xFF)
	{
		bar = g_PanelBarFill;
		s_BarInk = s_OrchidInk[g_EffectOrchid];
	}
	if (bar != s_Bar)
	{
		s_Bar = bar;
		line = TRUE;
	}
	return line;
}

void Panel_Update()
{
	u8 page = g_PanelPage;
	s_Top = page ? 0x8000 : 0;
	if (Want())
		Bar(page);   // the line cleared, and the bar if no message is on it
	u8 left = g_PanelBusy ? PANEL_CELLS_A_FRAME - 1 : PANEL_CELLS_A_FRAME;
	while (left && s_Head != s_Tail)
	{
		u8 i = s_Queue[s_Head++ & 127];
		s_Queued[i] = 0;
		if (s_Shown[i] != s_Want[i])
		{
			Write(i);
			left--;
		}
	}
}

void Panel_Init()
{
	for (u8 i = 0; i < CELLS; i++)
	{
		s_Want[i] = BLANK;
		s_Shown[i] = BLANK;
		s_Queued[i] = 0;
	}
	s_Head = s_Tail = 0;
	s_Drawn = FALSE;
	s_Score = 0xFFFFFFFF;
	s_Best = 0xFFFFFFFF;
	s_Lives = 0xFF;
	s_Slots = 0xFF;
	s_Easy = 0xFF;
	s_Message = 0xFE;
}

void Panel_Draw()
{
	u8 page = g_PanelPage;
	if (s_Drawn)
	{
		// The page on show's panel, as it is (its cells' shown state stays true):
		// the VDP copies it in a few milliseconds, where drawing it took 140.
		g_VSX = 0;
		g_VSY = ((u16)(page ^ 1) << 8) + TOP;
		g_VDX = 0;
		g_VDY = ((u16)page << 8) + TOP;
		g_VNX = 256;
		g_VNY = 212 - TOP;
		g_VCmd = VCMD_HMMM;
		Data_Vdp();
		Data_VdpWait();
		return;
	}
	s_Drawn = TRUE;
	g_VDX = 0;
	g_VDY = ((u16)page << 8) + TOP;
	g_VNX = 256;
	g_VNY = 212 - TOP;
	g_VClr = (PANEL_GROUND << 4) | PANEL_GROUND;
	g_VCmd = VCMD_HMMV;
	Data_Vdp();
	Data_VdpWait();
	s_Top = page ? 0x8000 : 0;
	Want();
	Bar(page);   // the line first: a message on it is drawn over it, not cleared by it
	for (u8 i = 0; i < CELLS; i++)
	{
		s_Queued[i] = 0;
		s_Shown[i] = BLANK;
		if (s_Want[i] != BLANK)
			Write(i);
		else
			s_Shown[i] = BLANK;
	}
	s_Head = s_Tail;
}
