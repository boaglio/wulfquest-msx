// Wulf Quest for MSX2: the scenes around the game (AGENTS.md §10, M12). See
// scenes.h.
//
// Banked code (ROM segment SCENES_SEG): it calls only data.c and sound.c
// below 8000h, keeps its tables in RAM, and multiplies and divides by adding
// and subtracting (AGENTS.md §3); what the Java scenes divide, the converter
// has worked out a tick at a time (scenes.bin).
//
// The closing scene and the intro: the backdrop is drawn from the scenery
// atlas, copied (its band, where the figures move) into the atlas's video
// RAM with the figures' images after it (scenes_img.bin); each tick the
// figures are drawn on the page not shown, over the band put back where they
// were two ticks before, and the pages swap. What is typed goes on both
// pages, under the band. Leaving, the atlas is loaded again.
//
// The credits roll scrolls the page by the VDP (R#23), a line a step: the
// line it uncovers above the band at the bottom is drawn, and the band
// itself again at its new place, as it does not move with the text. It runs
// through the page's lines under the screen too (sprite set 0's tables, the
// treasures' state), which a new game sets up again.

#include "scenes.h"
#include "scenes_data.h"
#include "shell.h"
#include "shell_data.h"
#include "panel_data.h"
#include "sound.h"
#include "game.h"
#include "panel.h"

u8  g_Scene = SCENE_NONE;
u8  g_RollPaused = 0;
u16 g_RollTick = 0;
u32 g_TallyBefore = 0;
u16 g_TallyEscape = 0;
u16 g_TallyTime = 0;
u16 g_TallyLives = 0;

static u8 s_Page = 0;   // the page drawn on
static u8 s_Shown = 0;  // the page on screen

//-----------------------------------------------------------------------------
// Drawing.

static void Copy(u16 sx, u16 sy, u16 dx, u16 dy, u16 w, u16 h, u8 cmd)
{
	g_VSX = sx;
	g_VSY = sy;
	g_VDX = dx;
	g_VDY = dy;
	g_VNX = w;
	g_VNY = h;
	g_VCmd = cmd;
	Data_Vdp();
}

static void Fill(u8 page, u8 x, u8 y, u16 w, u16 h)   // black, bytes: x and w even
{
	g_VDX = x;
	g_VDY = ((u16)page << 8) + y;
	g_VNX = w;
	g_VNY = h;
	g_VClr = (INK_GROUND << 4) | INK_GROUND;
	g_VCmd = VCMD_HMMV;
	Data_Vdp();
}

// A backdrop's copies (scenes.bin: sx, line, w, h, dx, dy), onto a page.
static void Backdrop(u16 at, u8 n, u8 page)
{
	u8 c[7];
	while (n--)
	{
		Data_ScenesBytes(at, c, 7);
		at += 7;
		Copy(c[0], c[1] | (u16)c[2] << 8, c[5], ((u16)page << 8) + c[6], c[3], c[4], VCMD_LMMM | VOP_TIMP);
	}
	Data_VdpWait();
}

// Where the figures were drawn on each page: put back from the band's copy next time.
#define DIRTY	8
static u8 s_DX[2][DIRTY];
static u8 s_DY[2][DIRTY];
static u8 s_DW[2][DIRTY];
static u8 s_DH[2][DIRTY];
static u8 s_DN[2] = { 0, 0 };

static void Restore(u8 page)
{
	for (u8 i = 0; i < s_DN[page]; i++)
	{
		u8 x = s_DX[page][i] & 0xFE;
		u16 w = (u16)s_DX[page][i] + s_DW[page][i] - x;
		w = (w + 1) & 0xFFFE;
		if (x + w > 256)
			w = 256 - x;
		Copy(x, SCENE_BACKDROP_Y + s_DY[page][i], x, ((u16)page << 8) + s_DY[page][i], w, s_DH[page][i], VCMD_HMMM);
	}
	s_DN[page] = 0;
}

// Figure image idx with its origin at (fx, fy), clipped to the band.
static void Figure(u8 idx, i16 fx, i16 fy)
{
	u8 r[SCENE_IMG_BYTES];
	u16 at = SCENE_IMG_TABLE + ((u16)idx << 3) - idx;   // idx * 7
	Data_ScenesBytes(at, r, SCENE_IMG_BYTES);
	i16 left = fx - r[5];
	i16 top = fy - r[6];
	u16 sx = r[0];
	u16 sy = r[1] | (u16)r[2] << 8;
	i16 w = r[3];
	i16 h = r[4];
	if (left < 0)
	{
		sx -= left;
		w += left;
		left = 0;
	}
	if (top < 0)
	{
		sy -= top;
		h += top;
		top = 0;
	}
	if (left + w > 256)
		w = 256 - left;
	if (top + h > SCENE_BAND)
		h = SCENE_BAND - top;
	if (w <= 0 || h <= 0)
		return;
	Copy(sx, sy, (u16)left, ((u16)s_Page << 8) + (u16)top, (u16)w, (u16)h, VCMD_LMMM | VOP_TIMP);
	u8 n = s_DN[s_Page];
	if (n < DIRTY)
	{
		s_DX[s_Page][n] = (u8)left;
		s_DY[s_Page][n] = (u8)top;
		s_DW[s_Page][n] = (u8)w;
		s_DH[s_Page][n] = (u8)h;
		s_DN[s_Page] = n + 1;
	}
}

// A string of scenes.bin into g_Text.
static void Words(u16 at)
{
	u8 n;
	Data_ScenesBytes(at, &n, 1);
	if (n > sizeof(g_Text))
		n = sizeof(g_Text);
	Data_ScenesBytes(at + 1, g_Text, n);
	g_TextN = n;
}

static u8 Centre(u8 big)   // the left of g_Text centred
{
	u16 w = big ? (u16)g_TextN << 3 : (u16)g_TextN << 2;
	return (u8)((256 - w) >> 1) & 0xFE;
}

static void Both(u8 big, u8 x, u8 y, u8 ink)   // g_Text on both pages
{
	Data_VdpWait();
	Data_TextDraw(big, 0, x, y, ink);
	Data_TextDraw(big, 1, x, y, ink);
}

static u16 Over(u16 t, u8 d)   // t / d
{
	u16 q = 0;
	while (t >= d)
	{
		t -= d;
		q++;
	}
	return q;
}

static i16 Read16(u16 at)
{
	u8 b[2];
	Data_ScenesBytes(at, b, 2);
	return (i16)(b[0] | (u16)b[1] << 8);
}

//-----------------------------------------------------------------------------
// Typing (EndingScreen.paintLines, IntroScreen.paintText): each line a
// character every so many ticks from its own tick, row after row, each row
// centred whole. A line here: at, y, ink, Mr Jones's, rows, the rows.

#define LINES_MAX	4
static u8 s_Typed[LINES_MAX];

static void Type(u16 lines, u8 n, u8 perChar, u8 rowStep)
{
	u16 at = lines;
	for (u8 i = 0; i < n && i < LINES_MAX; i++)
	{
		u8 head[6];
		Data_ScenesBytes(at, head, 6);
		u16 from = head[0] | (u16)head[1] << 8;
		u8 rows = head[5];
		at += 6;
		u16 shown = g_ShellTick >= from ? Over(g_ShellTick - from, perChar) : 0;
		u8 y = head[2];
		u16 k = 0;   // the line's characters before this row
		for (u8 r = 0; r < rows; r++)
		{
			u8 len;
			Data_ScenesBytes(at, &len, 1);
			for (u8 j = 0; j < len; j++, k++)
			{
				if (k < s_Typed[i] || k >= shown)
					continue;
				u8 ch;
				Data_ScenesBytes(at + 1 + j, &ch, 1);
				g_TextN = len;
				u8 left = Centre(TRUE);
				g_Text[0] = ch;
				g_TextN = 1;
				Both(TRUE, left + (j << 3), y, head[3]);
			}
			at += 1 + len;
			y += rowStep;
		}
		if (shown > s_Typed[i])
			s_Typed[i] = shown > 255 ? 255 : (u8)shown;
	}
}

// Whether Mr Jones's line is typing at tick t (EndingScreen.speaking).
static u8 JonesSpeaking(u16 lines, u8 n, u8 perChar)
{
	u16 at = lines;
	for (u8 i = 0; i < n; i++)
	{
		u8 head[6];
		Data_ScenesBytes(at, head, 6);
		u16 from = head[0] | (u16)head[1] << 8;
		at += 6;
		u16 end = from;
		for (u8 r = 0; r < head[5]; r++)
		{
			u8 len;
			Data_ScenesBytes(at, &len, 1);
			at += 1 + len;
			for (u8 c = 0; c < perChar; c++)
				end += len;
		}
		if (head[4] && g_ShellTick >= from && g_ShellTick < end)
			return TRUE;
	}
	return FALSE;
}

//-----------------------------------------------------------------------------
// The closing scene and the intro.

// The backdrop on the page not shown, its band kept, the figures loaded; both pages alike.
static void Stage(u16 back0, u8 n0, u16 back1, u8 n1)
{
	s_Page = 1 - g_PageShown;
	Fill(s_Page, 0, 0, 256, 212);
	Data_VdpWait();
	Backdrop(back0, n0, s_Page);
	Data_LoadSceneImages();
	Backdrop(back1, n1, s_Page);
	Copy(0, (u16)s_Page << 8, 0, SCENE_BACKDROP_Y, 256, SCENE_BAND, VCMD_HMMM);
	Copy(0, (u16)s_Page << 8, 0, (u16)(1 - s_Page) << 8, 256, 212, VCMD_HMMM);
	Data_VdpWait();
	Data_ShowPage(s_Page);
	s_Shown = s_Page;
	s_DN[0] = 0;
	s_DN[1] = 0;
	for (u8 i = 0; i < LINES_MAX; i++)
		s_Typed[i] = 0;
}

static void Swap()
{
	Data_VdpWait();
	Data_ShowPage(s_Page);
	s_Shown = s_Page;
}

static void EndingStep()
{
	u16 t = g_ShellTick;
	s_Page = 1 - s_Shown;
	Restore(s_Page);
	// Mr Jones: holding the amulet once it is his, his mouth open on every other beat of his line.
	u8 holding = t >= ENDING_HAND_AT + ENDING_HAND_TICKS;
	u8 talking = JonesSpeaking(ENDING_LINES, ENDING_LINES_N, ENDING_TICKS_PER_CHAR)
		&& (Over(t, ENDING_TALK_EVERY) & 1);
	Figure(holding ? (talking ? SCI_JONES_HOLD_TALK : SCI_JONES_HOLD) : (talking ? SCI_JONES_TALK : SCI_JONES_STAND),
		ENDING_JONES_X, ENDING_GROUND);
	// Vale: walking in, then standing.
	u16 w = t < ENDING_WALK_TICKS ? t : ENDING_WALK_TICKS;
	i16 vx = Read16(ENDING_WALK_X + (w << 1));
	u8 frame = SCI_VALE_STAND;
	if (t < ENDING_WALK_TICKS)
	{
		u16 step = Over(t, ENDING_WALK_EVERY);
		while (step >= ENDING_WALK_FRAMES)
			step -= ENDING_WALK_FRAMES;
		frame = SCI_VALE_SIDE_WALK0 + (u8)step;
	}
	Figure(frame, vx, ENDING_GROUND);
	// The amulet, whole: carried, handed over, held.
	i16 cx = vx + ENDING_CARRY_DX;
	i16 cy = ENDING_GROUND + ENDING_CARRY_DY;
	if (holding)
	{
		cx = ENDING_HOLD_X;
		cy = ENDING_HOLD_Y;
	}
	else if (t >= ENDING_HAND_AT)
	{
		u16 k = t - ENDING_HAND_AT;
		cx = Read16(ENDING_HAND_PATH + (k << 2));
		cy = Read16(ENDING_HAND_PATH + (k << 2) + 2);
	}
	for (u8 slot = 0; slot < 4; slot++)
		Figure(SCI_QUARTER_0 + slot, cx - ENDING_HALF + ((slot & 1) ? ENDING_HALF : 0),
			cy - ENDING_HALF + ((slot & 2) ? ENDING_HALF : 0));
	Swap();
	Type(ENDING_LINES, ENDING_LINES_N, ENDING_TICKS_PER_CHAR, BIG_LINE);
	if (t == ENDING_TUNE_AT)
		Sound_Tune(TUNE_ENDING);
	if (t == ENDING_PROMPT_AT)
	{
		Words(STR_ENDING_PROMPT);
		Both(TRUE, Centre(TRUE), ENDING_PROMPT_Y, ENDING_PROMPT_INK);
	}
}

static void IntroStep()
{
	u16 t = g_ShellTick;
	s_Page = 1 - s_Shown;
	Restore(s_Page);
	u16 at = t < INTRO_LANDED ? t : INTRO_LANDED;
	i16 x = Read16(INTRO_PLANE_PATH + (at << 2));
	i16 y = Read16(INTRO_PLANE_PATH + (at << 2) + 2);
	u8 frame = SCI_PLANE_PARKED;
	if (t < INTRO_LANDED)
	{
		u16 step = Over(t, INTRO_FLY_EVERY);
		while (step >= INTRO_FLY_FRAMES)
			step -= INTRO_FLY_FRAMES;
		frame = SCI_PLANE_FLY0 + (u8)step;
	}
	Figure(frame, x, y);
	Swap();
	// The words, a row after another (one "line" whose rows step INTRO_ROW_STEP apart).
	if (t < INTRO_TEXT_AT)
		return;
	u16 shown = Over(t - INTRO_TEXT_AT, INTRO_TICKS_PER_CHAR);
	u16 row = INTRO_TEXT;
	u8 ry = INTRO_TEXT_Y;
	u16 k = 0;
	for (u8 r = 0; r < INTRO_ROWS; r++)
	{
		u8 len;
		Data_ScenesBytes(row, &len, 1);
		for (u8 j = 0; j < len; j++, k++)
		{
			if (k < s_Typed[0] || k >= shown)
				continue;
			u8 ch;
			Data_ScenesBytes(row + 1 + j, &ch, 1);
			g_TextN = len;
			u8 left = Centre(TRUE);
			g_Text[0] = ch;
			g_TextN = 1;
			Both(TRUE, left + (j << 3), ry, INTRO_INK);
		}
		row += 1 + len;
		ry += INTRO_ROW_STEP;
	}
	if (shown > s_Typed[0])
		s_Typed[0] = shown > 255 ? 255 : (u8)shown;
}

//-----------------------------------------------------------------------------
// The tally (TallyScreen): the escape added up, a line every so many ticks.

static void TallyLine(u8 i, u16 label, u32 value, u8 ink)
{
	u8 y = TALLY_TOP + 3 * BIG_LINE;
	for (u8 k = 0; k < i; k++)
		y += 2 * BIG_LINE;
	if (i == TALLY_LABELS)
		y += BIG_LINE;
	Words(label);
	Data_VdpWait();
	Data_TextDraw(TRUE, s_Shown, 128 - TALLY_HALF, y, ink);
	g_TextN = 0;
	Data_TextNum(value);
	Data_TextDraw(TRUE, s_Shown, (u8)(128 + TALLY_HALF - ((u16)g_TextN << 3)), y, TALLY_FIGURE_INK);
}

static u16 s_TallyLabel[] = { TALLY_LABEL_STRS };

static void TallyStep()
{
	u16 t = g_ShellTick;
	u16 every = 0;
	for (u8 i = 0; i <= TALLY_LABELS; i++, every += TALLY_LINE_EVERY)
	{
		if (t != every)
			continue;
		if (i < TALLY_LABELS)
		{
			u32 v = i == 0 ? g_TallyBefore : i == 1 ? g_TallyEscape : i == 2 ? g_TallyTime : g_TallyLives;
			TallyLine(i, s_TallyLabel[i], v, TALLY_LABEL_INK);
		}
		else
		{
			TallyLine(i, STR_TALLY_TOTAL, g_Score, TALLY_HEADING_INK);
			u8 y = TALLY_TOP + 3 * BIG_LINE + TALLY_LABELS * 2 * BIG_LINE + BIG_LINE + 2 * BIG_LINE;
			if (g_Score >= g_HiBest)
			{
				Words(STR_TALLY_NEW_BEST);
				Data_TextDraw(TRUE, s_Shown, Centre(TRUE), y, TALLY_FIGURE_INK);
			}
			Words(STR_TALLY_PROMPT);
			Data_TextDraw(TRUE, s_Shown, Centre(TRUE), TALLY_PROMPT_Y, TALLY_FIGURE_INK);
		}
	}
}

//-----------------------------------------------------------------------------
// The credits roll (CreditsRoll).

#define ROLL_PAGE	0
#define ROLL_BAND	(SMALL_LINE + 2 * ROLL_BAND_PAD)
#define ROLL_BAND_TOP	(212 - ROLL_BAND)
static u16 s_RollPx = 0;     // how far it has risen
static u8  s_RollStep = 0;   // ticks towards the next pixel

// Roll row i into g_Text; whether it is the body's (the small font).
static u8 RollRow(u8 i)
{
	u16 at = ROLL_TEXT;
	u8 b[2];
	for (u8 k = 0; k < i; k++)
	{
		Data_ScenesBytes(at, b, 2);
		at += 2 + b[1];
	}
	Data_ScenesBytes(at, b, 2);
	Words(at + 1);
	return b[0];
}

// Screen line s as the roll has it now, onto the page line it shows from.
static void RollLine(u8 s)
{
	u8 y = (u8)(s + (u8)s_RollPx);   // the page's line (R#23 = s_RollPx, round 256)
	u16 at = s + s_RollPx;           // the roll's own place
	if (at >= ROLL_FROM_Y)
	{
		u16 into = at - ROLL_FROM_Y;
		u8 i = 0;
		while (into >= ROLL_LINE && i < ROLL_ROWS)
		{
			into -= ROLL_LINE;
			i++;
		}
		if (i < ROLL_ROWS)
		{
			u8 body = RollRow(i);
			if (g_TextN && into < (body ? FONT_SMALL_H : 8))
			{
				Data_TextLine(!body, ROLL_PAGE, Centre(!body), y, (u8)into, body ? ROLL_BODY_INK : ROLL_HEADING_INK);
				return;
			}
		}
	}
	Fill(ROLL_PAGE, 0, y, 256, 1);
	Data_VdpWait();
}

// The band at the bottom, where it is on the page now: its words, or "paused".
static void RollBand()
{
	Words(g_RollPaused ? STR_ROLL_PAUSED : STR_ROLL_HINT);
	u8 left = Centre(FALSE);
	u8 ink = g_RollPaused ? ROLL_HEADING_INK : ROLL_BODY_INK;
	for (u8 j = 0; j < ROLL_BAND; j++)
	{
		u8 y = (u8)(ROLL_BAND_TOP + j + (u8)s_RollPx);
		if (j < ROLL_BAND_PAD || j >= ROLL_BAND_PAD + FONT_SMALL_H)
		{
			Fill(ROLL_PAGE, 0, y, 256, 1);
			Data_VdpWait();
		}
		else
			Data_TextLine(FALSE, ROLL_PAGE, left, y, j - ROLL_BAND_PAD, ink);
	}
}

static void RollEnter()
{
	s_RollPx = 0;
	s_RollStep = 0;
	g_RollTick = 0;
	g_RollPaused = 0;
	Data_VdpReg(23, 0);
	Fill(ROLL_PAGE, 0, 0, 256, 256);
	Data_VdpWait();
	Data_ShowPage(ROLL_PAGE);
	g_PageShown = ROLL_PAGE;
	RollBand();
}

static void RollStep()
{
	static u8 s_WasPaused = 0;
	if (g_RollPaused != s_WasPaused)
	{
		s_WasPaused = g_RollPaused;
		RollBand();
	}
	if (g_RollPaused)
		return;
	if (++s_RollStep < ROLL_TICKS_PER_PX)
		return;
	s_RollStep = 0;
	s_RollPx++;
	Data_VdpReg(23, (u8)s_RollPx);
	RollLine(ROLL_BAND_TOP - 1);   // the line the band has left
	RollBand();
}

//-----------------------------------------------------------------------------

void Scenes_Enter()
{
	switch (g_Scene)
	{
	case SCENE_ENDING:
		Stage(ENDING_BACK0, ENDING_BACK0_N, ENDING_BACK1, ENDING_BACK1_N);
		EndingStep();
		break;
	case SCENE_INTRO:
		Stage(INTRO_BACK0, INTRO_BACK0_N, INTRO_BACK1, INTRO_BACK1_N);
		IntroStep();
		break;
	case SCENE_TALLY:
		s_Page = 1 - g_PageShown;
		Fill(s_Page, 0, 0, 256, 212);
		Data_VdpWait();
		Words(STR_TALLY_HEADING);
		Data_TextDraw(TRUE, s_Page, Centre(TRUE), TALLY_TOP, TALLY_HEADING_INK);
		Data_ShowPage(s_Page);
		s_Shown = s_Page;
		g_PageShown = s_Page;
		TallyStep();
		break;
	case SCENE_ROLL:
		RollEnter();
		break;
	}
}

void Scenes_Step()
{
	switch (g_Scene)
	{
	case SCENE_ENDING:
		EndingStep();
		break;
	case SCENE_INTRO:
		IntroStep();
		break;
	case SCENE_TALLY:
		TallyStep();
		break;
	case SCENE_ROLL:
		RollStep();
		break;
	}
}

void Scenes_Leave()
{
	if (g_Scene == SCENE_ROLL)
		Data_VdpReg(23, 0);
	if (g_Scene == SCENE_ENDING || g_Scene == SCENE_INTRO)
	{
		Data_LoadScenery();   // the atlas, which the figures borrowed
		g_PageShown = s_Shown;
	}
	g_Scene = SCENE_NONE;
}
