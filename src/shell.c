// Wulf Quest for MSX2: the shell around the game (AGENTS.md §10, M10). Its
// states and their ways from one to the next are the Java game's Shell's
// (and GameSession's reveal and map); its screens are its ShellPainter's,
// fitted to 256x212 with the playfield at the top. See shell.h.
//
// Banked code (ROM segment SHELL_SEG): it calls only data.c and sound.c
// below 8000h, keeps its tables in RAM, and multiplies and divides by adding
// and subtracting (AGENTS.md §3). It draws through data.c: the VDP's
// commands, the fonts, shell.bin's strings, the images in video RAM.
//
// A screen of its own (the title, the options from the title, the
// hi-scores) is drawn on the page not shown and then shown. A screen over
// the game (the pause menu and its pages, the reveal, the map, game over) is
// drawn on the other page over a copy of the game's, which is left as it
// was: going back to the game is showing its page again.

#include "shell.h"
#include "shell_data.h"
#include "constants.h"
#include "sound.h"
#include "vale.h"
#include "quest.h"
#include "quest_data.h"
#include "game.h"
#include "game_data.h"
#include "panel.h"
#include "tables.h"

u8  g_Shell = SH_TITLE;
u16 g_ShellTick = 0;
u8  g_ShellIn = 0;
u8  g_ShellKeys = 0;
u8  g_ShellDo = 0;
u8  g_ShellPlay = 0;
u8  g_Profile = 0;
u8  g_PageShown = 0;
u8  g_DrawPage = 0;
u8  g_TestMapOpen = 0;

// The hi-score table, ranked (HighScores): HISCORE_SIZE rows of a name,
// the score and ticks (32 bits, little-endian), the quarters, Easy.
#define R_NAME		0
#define R_SCORE		3
#define R_PIECES	7
#define R_TICKS		8
#define R_EASY		12
static u8 s_Table[HISCORE_SIZE * HISCORE_ROW_BYTES];

// Tables, in RAM (banked code reads none from ROM).
static u16 s_OptionStr[] = { OPTION_STRS };
static u16 s_PauseStr[] = { PAUSE_STRS };
static u8  s_PauseId[] = { PAUSE_IDS };
static u16 s_SlotEmpty[] = { SLOT_EMPTY_STRS };
static u16 s_VerseStr[] = { VERSE_STRS };
static u8  s_Cycle[] = { CYCLE_COLOURS };
static u8  s_EyeShows[] = { EYE_SHOWS };
static u8  s_WordW[WORDMARK_WORDS];
static u8  s_Alphabet[HISCORE_ALPHABET_N + 1];

// The machine's state (Shell's fields).
static u8  s_Drawn = 0xFF;      // the screen drawn, 0xFF none yet
static u8  s_Cursor = 0;
static u8  s_Back = SH_TITLE;   // where Escape goes back to
static u16 s_Note = 0;          // a word for the player on a menu (a shell.bin string), 0 none
static u8  s_Held = 0;          // last tick's g_ShellIn, for the presses
static u8  s_KeysHeld = 0;
static u8  s_Press = 0;         // this tick's presses
static u8  s_KeyPress = 0;
static u16 s_Idle = 0;
static u8  s_Easy = 0;          // the next game's mode (§17.8)
static u8  s_Pieces = 0;        // GameSession.piecesSeen
static u16 s_PlayTick = 0;      // ticks since the game came back (Shell.stateTick in PLAYING)
static i8  s_Highlight = -1;
static u8  s_Page = 0;          // the page being drawn on
static u8  s_On = 0;            // the page on screen

//-----------------------------------------------------------------------------
// Arithmetic without the library.

static u16 DivBy(u16 v, u16 d, u8* rem)
{
	u16 q = 0;
	while (v >= d)
	{
		v -= d;
		q++;
	}
	*rem = (u8)v;
	return q;
}

static u16 Times(u8 a, u8 b)
{
	u16 v = 0;
	while (b--)
		v += a;
	return v;
}

static u8* Row(u8 i)
{
	u8* p = s_Table;
	while (i--)
		p += HISCORE_ROW_BYTES;
	return p;
}

static u32 Get32(const u8* p)
{
	u32 v;
	u8* b = (u8*)&v;
	b[0] = p[0];
	b[1] = p[1];
	b[2] = p[2];
	b[3] = p[3];
	return v;
}

static void Put32(u8* p, u32 v)
{
	const u8* b = (const u8*)&v;
	p[0] = b[0];
	p[1] = b[1];
	p[2] = b[2];
	p[3] = b[3];
}

//-----------------------------------------------------------------------------
// Drawing (data.c's VDP commands and text).

static void Fill(u8 x, u16 y, u16 w, u8 h, u8 ink)   // bytes: x and w even; y from the page's top
{
	g_VDX = x;
	g_VDY = ((u16)s_Page << 8) + y;
	g_VNX = w;
	g_VNY = h;
	g_VClr = (u8)((ink << 4) | ink);
	g_VCmd = VCMD_HMMV;
	Data_Vdp();
}

static void Dot(u8 x, u8 y, u8 w, u8 h, u8 ink)   // pixels
{
	g_VDX = x;
	g_VDY = ((u16)s_Page << 8) + y;
	g_VNX = w;
	g_VNY = h;
	g_VClr = ink;
	g_VCmd = VCMD_LMMV;
	Data_Vdp();
}

static void Image(u16 sx, u16 sy, u8 w, u8 h, i16 x, i16 y)   // video RAM's image, its clear pixels left
{
	g_VSX = sx;
	g_VSY = sy;
	g_VDX = (u16)x;
	g_VDY = ((u16)s_Page << 8) + (u16)y;
	g_VNX = w;
	g_VNY = h;
	g_VCmd = VCMD_LMMM | VOP_TIMP;
	Data_Vdp();
}

static void Say(u16 str)
{
	g_TextN = 0;
	Data_TextStr(str);
}

static void Put(u8 ch)
{
	if (g_TextN < sizeof(g_Text))
		g_Text[g_TextN++] = ch;
}

static void Draw(u8 big, u8 x, u8 y, u8 ink)
{
	Data_VdpWait();
	Data_TextDraw(big, s_Page, x, y, ink);
}

static u16 WidthOf(u8 big)
{
	return big ? (u16)g_TextN << 3 : (u16)g_TextN << 2;
}

// FontSet.drawCentred across the screen; its left back.
static u8 Centre(u8 big, u8 y, u8 ink)
{
	u8 x = (u8)((256 - WidthOf(big)) >> 1) & 0xFE;
	Draw(big, x, y, ink);
	return x;
}

// g_Text in the big font, each pixel scale x scale (FontSet.drawScaled).
static void Scaled(u8 x, u8 y, u8 scale, u8 ink)
{
	u8 bits[8];
	for (u8 i = 0; i < g_TextN; i++)
	{
		Data_GlyphBits(g_Text[i], bits);
		u8 py = y;
		for (u8 gy = 0; gy < 8; gy++)
		{
			u8 b = bits[gy];
			u8 px = x;
			u8 run = 0;
			u8 from = 0;
			for (u8 gx = 0; gx <= 8; gx++)
			{
				if (gx < 8 && (b & 0x80))
				{
					if (!run)
						from = px;
					run += scale;
				}
				else if (run)
				{
					Dot(from, py, run, scale, ink);
					run = 0;
				}
				b <<= 1;
				px += scale;
			}
			py += scale;
		}
		x += scale << 3;
	}
}

// Chrome.page: the page cleared, its heading; the first line under it back.
static u8 Heading(u16 str)
{
	Fill(0, 0, 256, 212, INK_GROUND);
	Say(str);
	Centre(TRUE, 16, INK_HEADING);
	return 16 + BIG_LINE + 8;
}

// Chrome.footer.
static void Footer(u16 str, u8 ink)
{
	Say(str);
	Centre(FALSE, 212 - 14, ink);
}

// MenuScreen.drawLine: g_Text centred, and the marker before it if chosen.
static void Line(u8 y, u8 chosen)
{
	u8 x = Centre(TRUE, y, chosen ? INK_BRIGHT : INK_BODY);
	if (chosen)
		Image(IMG_MARKER_X, IMG_MARKER_Y, IMG_MARKER_W, IMG_MARKER_H, (i16)x - MARKER_GAP - IMG_MARKER_W,
			(i16)y + (8 + IMG_MARKER_H) / 2 - IMG_MARKER_H);
}

// A screen of its own goes on the page not shown; one over the game on the
// other page, over a copy of the game's (dimmed for the menus).
static void Begin(u8 over, u8 dim)
{
	s_Page = 1 - g_PageShown;
	if (!over)
		return;
	g_VSX = 0;
	g_VSY = (u16)g_PageShown << 8;
	g_VDX = 0;
	g_VDY = (u16)s_Page << 8;
	g_VNX = 256;
	g_VNY = dim ? 212 - PLAYFIELD_H : 212;
	if (dim)
	{
		g_VSY += PLAYFIELD_H;
		g_VDY += PLAYFIELD_H;
	}
	g_VCmd = VCMD_HMMM;
	Data_Vdp();
	if (dim)
	{
		Data_VdpWait();
		Data_Dim(g_PageShown, s_Page, PLAYFIELD_H);
	}
}

static void Show(u8 over)
{
	Data_VdpWait();
	Data_ShowPage(s_Page);
	s_On = s_Page;
	if (!over)
		g_PageShown = s_Page;
}

//-----------------------------------------------------------------------------
// The machine (Shell's go, openMenu, backOut, steer).

static u8 InGame()   // Shell.menuInGame
{
	return g_Shell == SH_PAUSED || g_Shell == SH_SAVE || s_Back == SH_PAUSED;
}

static u8 TuneFor(u8 state)   // Shell.tuneFor
{
	switch (state)
	{
	case SH_PLAYING: case SH_REVEAL: case SH_MAP:
		return 0xFF;
	case SH_GAME_OVER:
		return TUNE_GAME_OVER;
	case SH_WON:
		return TUNE_WIN;
	default:
		return TUNE_TITLE;
	}
}

static void Go(u8 next)
{
	u8 from = g_Shell;
	if (next == SH_TITLE || next == SH_PLAYING)
	{
		s_Back = SH_TITLE;
		if (next == SH_TITLE)
			s_Cursor = 0;
	}
	if (next == SH_PLAYING && from != SH_MAP && from != SH_REVEAL)
		s_PlayTick = 0;
	g_Shell = next;
	g_ShellTick = 0;
	s_Drawn = 0xFF;
	// The reveal and the map are GameSession's, inside Shell's PLAYING: the
	// reveal's tune, and silence after it (Shell.playing).
	if (next == SH_REVEAL)
	{
		Sound_Tune(TUNE_REVEAL);
		return;
	}
	if (next == SH_MAP || (next == SH_PLAYING && from == SH_MAP))
		return;
	Sound_Tune(InGame() ? 0xFF : TuneFor(next));
}

static void OpenMenu(u8 page, u8 from)
{
	s_Cursor = 0;
	s_Back = from;
	s_Note = 0;
	Go(page);
}

static u8 Fire()
{
	return (s_Press & VALE_FIRE) || (s_KeyPress & SK_ENTER);
}

// Up and down through n lines, wrapping, on the press.
static u8 Steer(u8 n)
{
	i8 dy = ((s_Press & VALE_DOWN) ? 1 : 0) - ((s_Press & VALE_UP) ? 1 : 0);
	if (dy == 0 || n == 0)
		return FALSE;
	s_Cursor = dy > 0 ? (s_Cursor + 1 >= n ? 0 : s_Cursor + 1) : (s_Cursor == 0 ? n - 1 : s_Cursor - 1);
	s_Note = 0;
	Sound_Play(SFX_MENU_MOVE);
	return TRUE;
}

static u8 OptionLines()
{
	return s_Back == SH_PAUSED ? OPTION_LINES_IN_GAME : OPTION_LINES;
}

static u8 OptionId(u8 line)   // over a game, only the lines not the title's own
{
	if (s_Back != SH_PAUSED)
		return line;
	return line == 0 ? OPT_SOUND : OPT_BACK;
}

static u8 OptionLine(u8 id)
{
	for (u8 i = 0; i < OptionLines(); i++)
		if (OptionId(i) == id)
			return i;
	return 0;
}

static void BackOut()
{
	u8 to = s_Back;
	u8 from = g_Shell;
	if (to == SH_PAUSED)
	{
		OpenMenu(SH_PAUSED, SH_PLAYING);
		u8 id = from == SH_SAVE ? PAUSE_SAVE : from == SH_LOAD ? PAUSE_LOAD : PAUSE_OPTIONS;
		for (u8 i = 0; i < PAUSE_LINES; i++)
			if (s_PauseId[i] == id)
				s_Cursor = i;
	}
	else if (to == SH_OPTIONS)
	{
		OpenMenu(SH_OPTIONS, SH_TITLE);
		s_Cursor = OptionLine(from == SH_LOAD ? OPT_LOAD : OPT_HISCORES);
	}
	else
	{
		Go(SH_TITLE);
		if (from == SH_OPTIONS)
			s_Cursor = 1;   // the title's OPTIONS line
	}
}

//-----------------------------------------------------------------------------
// The title (TitleScreen).

#define TITLE_LINES	2   // START, OPTIONS: CONTINUE comes with the saves (M11), EXIT is a PC's
static u8 s_Step = 0;
static u8 s_StepTicks = 0;

static u8 WordY(u8 i)
{
	u8 y = WORDMARK_Y;
	while (i--)
		y += IMG_WORD_H + WORDMARK_GAP;
	return y;
}

// The wordmark: each word coloured band by band in the scratch lines (a row
// of the font a band, its colour the cycle's), the stencil ORed over it,
// then copied onto the page.
static void Wordmark()
{
	u16 sy = IMG_WORD_Y;
	for (u8 i = 0; i < WORDMARK_WORDS; i++)
	{
		u8 w = s_WordW[i];
		u16 y = SHELL_SCRATCH_Y;
		u8 c = s_Step;
		for (u8 r = 0; r < 8; r++)
		{
			g_VDX = 0;
			g_VDY = y;
			g_VNX = w;
			g_VNY = WORDMARK_SCALE;
			g_VClr = (u8)((s_Cycle[c] << 4) | s_Cycle[c]);
			g_VCmd = VCMD_HMMV;
			Data_Vdp();
			y += WORDMARK_SCALE;
			if (++c == CYCLE_STEPS)
				c = 0;
		}
		g_VSX = 0;
		g_VSY = sy;
		g_VDX = 0;
		g_VDY = SHELL_SCRATCH_Y;
		g_VNX = w;
		g_VNY = IMG_WORD_H;
		g_VCmd = VCMD_LMMM | VOP_OR;
		Data_Vdp();
		g_VSX = 0;
		g_VSY = SHELL_SCRATCH_Y;
		g_VDX = (256 - w) >> 1;
		g_VDY = ((u16)s_Page << 8) + WordY(i);
		g_VCmd = VCMD_HMMM;
		Data_Vdp();
		sy += IMG_WORD_H;
	}
}

static void TitleMenu()
{
	u8 y = TITLE_MENU_Y;
	Fill(0, y - 2, 256, 212 - 14 - 2 - (y - 2), INK_GROUND);
	for (u8 i = 0; i < TITLE_LINES; i++)
	{
		Say(i == 0 ? STR_START : STR_OPTIONS);
		Line(y, i == s_Cursor);
		y += BIG_LINE + 2;
		if (i == 0)
		{
			Say(s_Easy ? STR_MODE_EASY : STR_MODE_HARD);
			Centre(FALSE, y, s_Easy ? INK_EASY : INK_DIM);
			y += SMALL_LINE + 2;
		}
		y += 4;
	}
}

static void DrawTitle()
{
	s_Step = 0;
	s_StepTicks = 0;
	Begin(FALSE, FALSE);
	Fill(0, 0, 256, 212, INK_GROUND);
	Wordmark();
	Say(STR_TAGLINE);
	Centre(FALSE, WordY(WORDMARK_WORDS) + 4 - WORDMARK_GAP, INK_DIM);
	TitleMenu();
	Footer(STR_FOOTER, INK_DIM);
	Show(FALSE);
}

static void Title()
{
	if (++s_StepTicks >= CYCLE_TICKS)
	{
		s_StepTicks = 0;
		if (++s_Step >= CYCLE_STEPS)   // TitleScreen: tick / ticksPerStep, round the colours
			s_Step = 0;
		s_Page = s_On;
		Wordmark();
	}
	if (Steer(TITLE_LINES))
	{
		s_Idle = 0;
		s_Page = s_On;
		TitleMenu();
		return;
	}
	if (s_KeyPress & SK_DIFF)
	{
		s_Easy ^= 1;
		Sound_Play(SFX_MENU_MOVE);
		s_Idle = 0;
		s_Page = s_On;
		TitleMenu();
		return;
	}
	if (Fire())
	{
		Sound_Play(SFX_MENU_PICK);
		s_Idle = 0;
		if (s_Cursor == 0)
		{
			g_Easy = s_Easy;
			s_Pieces = 0;
			g_ShellDo |= DO_NEW_GAME;
			Go(SH_PLAYING);
		}
		else
			OpenMenu(SH_OPTIONS, SH_TITLE);
		return;
	}
	if ((g_ShellKeys & SK_ANY) || g_ShellIn)
		s_Idle = 0;
	else if (++s_Idle >= ATTRACT_IDLE_TICKS)
	{
		s_Idle = 0;
		g_ShellDo |= DO_DEMO;
		Go(SH_ATTRACT);
	}
}

//-----------------------------------------------------------------------------
// The attract demo (Shell.attract, ShellPainter.attract).

static u16 s_DemoRun = 0;
static u8  s_DemoLeft = 0;
static u8  s_DemoBits = 0;
u16 g_DemoTicks = 0;   // read by test/shell.sh

void Shell_DemoKeys()
{
	if (s_DemoLeft == 0)
	{
		u8 run[2] = { 0, 0 };
		if (s_DemoRun < DEMO_RUN_COUNT)
			Data_ShellBytes(DEMO_RUNS + (s_DemoRun << 1), run, 2);
		s_DemoRun++;
		s_DemoLeft = run[0];
		s_DemoBits = run[1] & (DEMO_UP | DEMO_DOWN | DEMO_LEFT | DEMO_RIGHT | DEMO_FIRE);
	}
	if (s_DemoLeft)
		s_DemoLeft--;
	g_DemoTicks++;
	g_ShellPlay = s_DemoBits;
}

#if DEMO_UP != VALE_UP || DEMO_DOWN != VALE_DOWN || DEMO_LEFT != VALE_LEFT || DEMO_RIGHT != VALE_RIGHT || DEMO_FIRE != VALE_FIRE
#error "the demo's bits are not the game's keys"
#endif

// The demo's room as it is drawn: the playfield dimmed (by the palette, the
// headings' inks kept) and every other row black, the wordmark and the line
// that says how to stop it on bands of their own; no panel.
void Shell_Dress()
{
	s_Page = g_DrawPage;
	for (u8 y = 0; y < PLAYFIELD_H; y += 2)
		Fill(0, y, 256, 1, INK_GROUND);
	Fill(0, PLAYFIELD_H, 256, 212 - PLAYFIELD_H, INK_GROUND);
	Fill(0, 20, 256, 8 + 6, INK_GROUND);
	Say(STR_WORDMARK);
	Centre(TRUE, 23, INK_HEADING);
	u8 y = PLAYFIELD_H - 26;
	Fill(0, y - 3, 256, 6 + 6, INK_GROUND);
	Say(STR_BANNER);
	Centre(FALSE, y, INK_BRIGHT);
	Data_VdpWait();
}

static void StopDemo()
{
	Data_Palette(FALSE);
	s_Idle = 0;
	Go(SH_TITLE);
}

static void Attract()
{
	if ((s_KeyPress & (SK_ANY | SK_ESC)) || (s_Press & VALE_FIRE) || g_DemoTicks >= DEMO_TICKS)
	{
		StopDemo();
		return;
	}
	g_ShellDo |= DO_TICK;
}

//-----------------------------------------------------------------------------
// The menus (MenuScreen): the pause menu, the save and load pages, the options.

// Row r's words into g_Text: bit 0, big; bit 1, chosen.
static u8 RowText(u8 r)
{
	g_TextN = 0;
	switch (g_Shell)
	{
	case SH_PAUSED:
		Data_TextStr(s_PauseStr[r]);
		return 1 | (r == s_Cursor ? 2 : 0);
	case SH_OPTIONS:
	{
		u8 id = OptionId(r);
		Data_TextStr(s_OptionStr[id]);
		u16 v = id == OPT_DIFFICULTY ? (s_Easy ? STR_MODE_EASY : STR_MODE_HARD)
			: id == OPT_KEYS ? (g_Profile ? STR_KEYS_1 : STR_KEYS_0)
			: id == OPT_SOUND ? (g_SoundOn ? STR_ON : STR_OFF) : 0;
		if (v)
		{
			Put(' ');
			Put(' ');
			Data_TextStr(v);
		}
		return 1 | (r == s_Cursor ? 2 : 0);
	}
	default:   // the slots: none is filled until the saves (M11)
		if (r & 1)
			return 0;   // the slot's detail line: empty
		Data_TextStr(s_SlotEmpty[r >> 1]);
		return 1 | ((r >> 1) == s_Cursor ? 2 : 0);
	}
}

static u8 Rows()
{
	return g_Shell == SH_PAUSED ? PAUSE_LINES : g_Shell == SH_OPTIONS ? OptionLines() : SAVE_SLOTS * 2;
}

static u16 MenuHeading()
{
	return g_Shell == SH_PAUSED ? STR_PAUSE_HEADING : g_Shell == SH_OPTIONS ? STR_OPT_HEADING
		: g_Shell == SH_SAVE ? STR_SAVE_HEADING : STR_LOAD_HEADING;
}

static u16 MenuFooter()
{
	return g_Shell == SH_PAUSED ? STR_PAUSE_FOOTER : g_Shell == SH_OPTIONS ? STR_OPT_FOOTER : STR_SAVE_FOOTER;
}

static u8 s_RowsY;   // where the rows start, and the box's top and height
static u8 s_BoxTop;
static u8 s_BoxH;

static void MenuRows()
{
	u8 y = s_RowsY;
	u8 n = Rows();
	u8 h = 0;
	for (u8 r = 0; r < n; r++)
		h += (RowText(r) & 1) ? BIG_LINE + 2 : SMALL_LINE + 2;
	if (InGame())
		Fill(MENU_PAD + 2, y - 2, 256 - 2 * MENU_PAD - 4, h + 2 + SMALL_LINE + 2, INK_GROUND);
	else
		Fill(0, y - 2, 256, h + 2 + BIG_LINE + SMALL_LINE, INK_GROUND);
	for (u8 r = 0; r < n; r++)
	{
		u8 f = RowText(r);
		if (f & 1)
		{
			Line(y, f & 2);
			y += BIG_LINE + 2;
		}
		else
		{
			Centre(FALSE, y, INK_DIM);
			y += SMALL_LINE + 2;
		}
	}
	if (s_Note)
	{
		Say(s_Note);
		Centre(FALSE, InGame() ? y + 2 : y + BIG_LINE, INK_HEADING);
	}
}

static void DrawMenu()
{
	if (InGame())
	{
		// paintBox: the jungle dimmed, the menu in a box in the middle of it.
		u8 n = Rows();
		u8 h = 2 * BIG_LINE;
		for (u8 r = 0; r < n; r++)
			h += (RowText(r) & 1) ? BIG_LINE + 2 : SMALL_LINE + 2;
		h += BIG_LINE + SMALL_LINE + 2 * MENU_PAD;
		u8 top = h < PLAYFIELD_H ? (PLAYFIELD_H - h) >> 1 : 0;
		u8 again = s_On != g_PageShown;   // a menu over the game already: dim only the last box's lines again
		if (again)
		{
			s_Page = s_On;
			Data_VdpWait();
			Data_Dim(g_PageShown, s_Page, PLAYFIELD_H);
		}
		else
			Begin(TRUE, TRUE);
		s_BoxTop = top;
		s_BoxH = h;
		Fill(MENU_PAD, top, 256 - 2 * MENU_PAD, h, INK_GROUND);
		Dot(MENU_PAD, top, 256 - 2 * MENU_PAD, 1, INK_DIM);
		Dot(MENU_PAD, top + h - 1, 256 - 2 * MENU_PAD, 1, INK_DIM);
		Dot(MENU_PAD, top, 1, h, INK_DIM);
		Dot(256 - MENU_PAD - 1, top, 1, h, INK_DIM);
		Say(MenuHeading());
		Centre(TRUE, top + MENU_PAD, INK_HEADING);
		s_RowsY = top + MENU_PAD + 2 * BIG_LINE;
		MenuRows();
		Say(MenuFooter());
		Centre(FALSE, top + h - MENU_PAD - SMALL_LINE, INK_DIM);
		if (!again)
			Show(TRUE);
		return;
	}
	// paintPage: a page of its own.
	Begin(FALSE, FALSE);
	s_RowsY = Heading(MenuHeading()) + BIG_LINE;
	MenuRows();
	Footer(MenuFooter(), INK_BRIGHT);
	Show(FALSE);
}

static void Redraw()
{
	s_Page = s_On;
	MenuRows();
}

static void EndRun();

static void Paused()
{
	if (Steer(PAUSE_LINES))
	{
		Redraw();
		return;
	}
	if (s_KeyPress & (SK_ESC | SK_PAUSE))
	{
		Go(SH_PLAYING);
		return;
	}
	if (!Fire())
		return;
	Sound_Play(SFX_MENU_PICK);
	switch (s_PauseId[s_Cursor])
	{
	case PAUSE_RESUME:
		Go(SH_PLAYING);
		break;
	case PAUSE_SAVE:
		OpenMenu(SH_SAVE, SH_PAUSED);
		break;
	case PAUSE_LOAD:
		OpenMenu(SH_LOAD, SH_PAUSED);
		break;
	case PAUSE_OPTIONS:
		OpenMenu(SH_OPTIONS, SH_PAUSED);
		break;
	default:
		EndRun();
		break;
	}
}

// The save and load pages: no slots to fill or read until the saves (M11),
// as the Java shell with none: fire does nothing.
static void Slots()
{
	if (Steer(SAVE_SLOTS))
	{
		Redraw();
		return;
	}
	if (s_KeyPress & SK_ESC)
		BackOut();
}

static void Options()
{
	if (Steer(OptionLines()))
	{
		Redraw();
		return;
	}
	if (s_KeyPress & SK_ESC)
	{
		BackOut();
		return;
	}
	if (!Fire() || (g_ShellTick < SHELL_HOLD_TICKS && s_Back == SH_TITLE))
		return;
	switch (OptionId(s_Cursor))
	{
	case OPT_BACK:
		Sound_Play(SFX_MENU_PICK);
		BackOut();
		return;
	case OPT_DIFFICULTY:
		s_Easy ^= 1;
		Sound_Play(SFX_MENU_MOVE);
		break;
	case OPT_KEYS:
		g_Profile ^= 1;
		Sound_Play(SFX_MENU_PICK);
		break;
	case OPT_SOUND:
		if (g_SoundOn)
		{
			Sound_Tune(0xFF);
			g_SoundOn = 0;
		}
		else
		{
			g_SoundOn = 1;
			Sound_Tune(InGame() ? 0xFF : TUNE_TITLE);
			Sound_Play(SFX_MENU_MOVE);
		}
		break;
	case OPT_LOAD:
		Sound_Play(SFX_MENU_PICK);
		OpenMenu(SH_LOAD, SH_OPTIONS);
		return;
	default:
		Sound_Play(SFX_MENU_PICK);
		OpenMenu(SH_HISCORES, SH_OPTIONS);
		return;
	}
	Redraw();
}

//-----------------------------------------------------------------------------
// The game (Shell.playing, GameSession.tick).

static void Playing()
{
	s_PlayTick++;
	if (s_KeyPress & SK_MAP)
	{
		Sound_Play(SFX_MENU_PICK);
		Go(SH_MAP);
		return;
	}
	if (s_KeyPress & SK_ESC)
	{
		EndRun();
		return;
	}
	if (s_KeyPress & SK_PAUSE)
	{
		Sound_Play(SFX_MENU_PICK);
		OpenMenu(SH_PAUSED, SH_PLAYING);
		return;
	}
	g_ShellDo |= DO_TICK;
}

void Shell_AfterTick()
{
	if (g_Shell == SH_GAME_OVER)
	{
		if (g_ValeMode != MODE_GAME_OVER)
		{
			Sound_Play(SFX_MENU_PICK);   // continued: back where he fell
			Go(SH_PLAYING);
		}
		return;
	}
	if (g_Shell != SH_PLAYING)
		return;
	if (g_QuestPieces > s_Pieces)
	{
		s_Pieces = g_QuestPieces;
		Go(SH_REVEAL);
	}
	if (g_ValeMode == MODE_WON)
		Go(SH_WON);
	else if (g_ValeMode == MODE_GAME_OVER)
		Go(SH_GAME_OVER);
}

// The reveal (AmuletRevealScreen): the playfield cleared, the quarters held
// drawn large, the verse for how many.
static u8 s_PromptY;

static void DrawReveal()
{
	Begin(TRUE, FALSE);
	Fill(0, 0, 256, PLAYFIELD_H, INK_GROUND);
	u8 size = 2 * IMG_QUARTER_W;
	u8 block = size + BIG_LINE + (VERSE_LINES + 1) * BIG_LINE;
	u8 top = (PLAYFIELD_H - block) >> 1;
	u8 left = (256 - size) >> 1;
	u16 sx = IMG_QUARTER_X;
	for (u8 slot = 0; slot < QUEST_SLOTS; slot++)
	{
		if (g_QuestSlots & (1 << slot))
			Image(sx, IMG_QUARTER_Y, IMG_QUARTER_W, IMG_QUARTER_H, left + ((slot & 1) ? IMG_QUARTER_W : 0),
				top + ((slot & 2) ? IMG_QUARTER_H : 0));
		sx += IMG_QUARTER_W;
	}
	u8 y = top + size + BIG_LINE;
	u8 at = 0;
	for (u8 v = 1; v < s_Pieces && v < VERSES; v++)
		at += VERSE_LINES;
	for (u8 l = 0; l < VERSE_LINES; l++)
	{
		Say(s_VerseStr[at + l]);
		Centre(TRUE, y, INK_BODY);
		y += BIG_LINE;
	}
	s_PromptY = y;
	Show(TRUE);
}

static void Reveal()
{
	if (s_KeyPress & SK_ESC)
	{
		EndRun();
		return;
	}
	if (g_ShellTick == REVEAL_SKIP_TICKS)
	{
		s_Page = s_On;
		Say(STR_REVEAL_PROMPT);
		Centre(TRUE, s_PromptY, INK_BRIGHT);
	}
	if (g_ShellTick >= REVEAL_SKIP_TICKS && Fire())
		Go(SH_PLAYING);
	if (g_ShellTick > REVEAL_SKIP_TICKS)
		g_ShellTick = REVEAL_SKIP_TICKS + 1;
}

// The map (MapScreen): the jungle in miniature, a room 16x11.
static u8 s_Blink;      // the room he is in shows its frame
static u8 s_BlinkT;
static u8 s_IconStep;   // the quarters' shiny frame
static u8 s_IconT;
static u8 s_ArchStep;   // the arch's open frame
static u8 s_ArchT;

static u8 Visited(u8 room)
{
	return g_Visited[room >> 3] & (1 << (room & 7));
}

static void Icon(u16 sx, u8 room, u8 w, u8 h, u8 ox, u8 oy)
{
	u8 row = room >> 4;
	Image(sx, IMG_ICON_Y, w, h, (i16)((room & 15) << 4) + 8 - ox,
		(i16)((row << 3) + (row << 1) + row) + MINI_H - oy);
}

static void MapCell(u8 room, u8 alone)
{
	if (alone || Visited(room) || g_MapTaken)
	{
		if (Visited(room))
			Data_MapRoom(room, s_Page, INK_MAP_VISITED);
		else if (g_MapTaken)
			Data_MapRoom(room, s_Page, INK_MAP_REVEALED);
		else
		{
			u8 row = room >> 4;
			Fill((room & 15) << 4, (row << 3) + (row << 1) + row, MINI_W, MINI_H, INK_GROUND);
		}
	}
	// paintQuarters: a quarter an eye has shown, shiny; one taken, dull.
	const u8* lairs = g_LairRoom;
	u8 base = 0;
	for (u8 k = 0; k < g_QuestSet; k++)
		base += LAIRS;
	for (u8 l = 0; l < LAIRS; l++)
	{
		if (lairs[base + l] != room)
			continue;
		u8 taken = g_QuestTaken & (1 << l);
		u8 shown = 0;
		for (u8 e = 0; e < EYES; e++)
			if ((g_EyesTaken & (1 << e)) && (s_EyeShows[e] & (1 << l)))
				shown = 1;
		if (!taken && !shown)
			continue;
		u8 f = MAP_ICON_SHINY;
		if (!taken)
		{
			f = s_IconStep + l;
			while (f >= MAP_ICON_SHINY)
				f -= MAP_ICON_SHINY;
		}
		Icon(IMG_DIAMOND_X + Times(f, IMG_ICON_STEP), room, IMG_DIAMOND_W, IMG_DIAMOND_H, IMG_DIAMOND_OX, IMG_DIAMOND_OY);
	}
	// paintArch: always; its doorway glowing once the amulet is whole.
	if (room == MAP_ARCH_ROOM)
	{
		u8 f = g_QuestPieces >= EXIT_REQUIRES_PIECES ? 1 + s_ArchStep : 0;
		Icon(IMG_ARCH_X + Times(f, IMG_ICON_STEP), room, IMG_ARCH_W, IMG_ARCH_H, IMG_ARCH_OX, IMG_ARCH_OY);
	}
	if (room == g_ValeRoom && s_Blink)
	{
		u8 x = (room & 15) << 4;
		u8 row = room >> 4;
		u8 y = (row << 3) + (row << 1) + row;
		Dot(x, y, MINI_W, 1, INK_MAP_HERE);
		Dot(x, y + MINI_H - 1, MINI_W, 1, INK_MAP_HERE);
		Dot(x, y, 1, MINI_H, INK_MAP_HERE);
		Dot(x + MINI_W - 1, y, 1, MINI_H, INK_MAP_HERE);
	}
	Data_VdpWait();
}

// The rooms whose marks move: the quarters' and the arch's.
static void MapMarks(u8 quarters, u8 arch)
{
	const u8* lairs = g_LairRoom;
	u8 base = 0;
	for (u8 k = 0; k < g_QuestSet; k++)
		base += LAIRS;
	if (quarters)
		for (u8 l = 0; l < LAIRS; l++)
			MapCell(lairs[base + l], TRUE);
	if (arch)
		MapCell(MAP_ARCH_ROOM, TRUE);
}

static void DrawMap()
{
	// Each animation's phase as the game's ticks have it (MapScreen reads Shell.stateTick).
	u8 rem;
	u16 q = DivBy(s_PlayTick, MAP_BLINK_TICKS, &rem);
	s_Blink = !(q & 1);
	s_BlinkT = rem;
	q = DivBy(s_PlayTick, MAP_ICON_TICKS, &rem);
	s_IconT = rem;
	DivBy(q, MAP_ICON_SHINY, &s_IconStep);
	q = DivBy(s_PlayTick, MAP_ARCH_TICKS, &rem);
	s_ArchT = rem;
	DivBy(q, MAP_ARCH_OPEN, &s_ArchStep);

	Begin(TRUE, FALSE);
	Fill(0, 0, 256, PLAYFIELD_H, INK_GROUND);
	for (u16 r = 0; r < GRID_W * GRID_H; r++)
		if (Visited((u8)r) || g_MapTaken)
			Data_MapRoom((u8)r, s_Page, Visited((u8)r) ? INK_MAP_VISITED : INK_MAP_REVEALED);
	MapMarks(TRUE, TRUE);
	MapCell(g_ValeRoom, TRUE);
	// The panel's line says how to go back (GameSession.message's, from the shell).
	Fill(0, PLAYFIELD_H + 28, 256, 6, INK_GROUND);
	Say(STR_MAP);
	Centre(FALSE, PLAYFIELD_H + 28, INK_BRIGHT);
	Show(TRUE);
}

static void Map()
{
	s_PlayTick++;
	if (s_KeyPress & SK_MAP)
	{
		Sound_Play(SFX_MENU_PICK);
		Go(SH_PLAYING);
		return;
	}
	if (s_KeyPress & SK_ESC)
	{
		EndRun();
		return;
	}
	s_Page = s_On;
	u8 quarters = FALSE;
	u8 arch = FALSE;
	if (++s_IconT >= MAP_ICON_TICKS)
	{
		s_IconT = 0;
		if (++s_IconStep >= MAP_ICON_SHINY)
			s_IconStep = 0;
		quarters = TRUE;
	}
	if (++s_ArchT >= MAP_ARCH_TICKS)
	{
		s_ArchT = 0;
		if (++s_ArchStep >= MAP_ARCH_OPEN)
			s_ArchStep = 0;
		arch = g_QuestPieces >= EXIT_REQUIRES_PIECES;
	}
	if (++s_BlinkT >= MAP_BLINK_TICKS)
	{
		s_BlinkT = 0;
		s_Blink ^= 1;
		MapCell(g_ValeRoom, TRUE);
	}
	if (quarters || arch)
		MapMarks(quarters, arch);
	if (quarters || arch)
		MapCell(g_ValeRoom, TRUE);   // its frame over any mark
}

//-----------------------------------------------------------------------------
// Game over (GameOverScreen) and the win's sum (TallyScreen's heading and
// total, until the endings, M12).

static u8 ContinuesLeft()
{
	return g_GameOn && g_ContinuesUsed < CONTINUES_START ? CONTINUES_START - g_ContinuesUsed : 0;
}

static u8 s_OverY;
static u8 s_Seconds;

static u8 Percent()   // GameOverScreen.percentDone
{
	u16 n = g_QuestPieces;
	for (u8 i = 0; i < 32; i++)
		for (u8 b = g_Visited[i]; b; b >>= 1)
			n += b & 1;
	u16 hundred = (n << 6) + (n << 5) + (n << 2);
	u8 rem;
	return (u8)DivBy(hundred, PERCENT_OF, &rem);
}

static void Countdown()
{
	u8 rem;
	u16 left = g_ShellTick < GAME_OVER_CONTINUE_TICKS ? GAME_OVER_CONTINUE_TICKS - g_ShellTick : 0;
	u8 s = (u8)DivBy(left + TICK_HZ - 1, TICK_HZ, &rem);
	if (s == s_Seconds)
		return;
	s_Seconds = s;
	u8 y = s_OverY + 8 * BIG_LINE;
	Fill(0, y, 256, 8 * GAME_OVER_COUNTDOWN_SCALE, INK_GROUND);
	g_TextN = 0;
	Data_TextNum(s);
	u16 w = Times(g_TextN, 8 * GAME_OVER_COUNTDOWN_SCALE);
	Scaled((u8)((256 - w) >> 1), y, GAME_OVER_COUNTDOWN_SCALE, INK_HEADING);
}

static void DrawGameOver()
{
	u8 left = ContinuesLeft();
	u8 lines = left ? 6 : 2;
	u8 countdown = left ? BIG_LINE + 8 * GAME_OVER_COUNTDOWN_SCALE : 0;
	u8 tall = 8 + Times(lines, BIG_LINE) + countdown;
	u8 y = (PLAYFIELD_H - tall) >> 1;
	Begin(TRUE, FALSE);
	Fill(0, y - 4, 256, tall + 8, INK_GROUND);
	Say(STR_GAME_OVER);
	Centre(TRUE, y, INK_HEADING);
	g_TextN = 0;
	Data_TextStr(STR_PROGRESS_A);
	Data_TextNum(Percent());
	Data_TextStr(STR_PROGRESS_B);
	Centre(TRUE, y + 2 * BIG_LINE, INK_BODY);
	s_OverY = y;
	s_Seconds = 0xFF;
	if (left)
	{
		Say(STR_CONTINUE);
		Centre(TRUE, y + 4 * BIG_LINE, INK_BRIGHT);
		g_TextN = 0;
		Data_TextStr(STR_CONTINUES_A);
		Data_TextNum(left);
		Data_TextStr(STR_CONTINUES_B);
		Centre(TRUE, y + 6 * BIG_LINE, INK_BODY);
		Countdown();
	}
	Show(TRUE);
}

static void GameOver()
{
	if (ContinuesLeft())
	{
		if ((s_KeyPress & SK_ESC) || g_ShellTick >= GAME_OVER_CONTINUE_TICKS)
			EndRun();
		else if (g_ShellTick >= SHELL_HOLD_TICKS && (s_Press & VALE_FIRE))
		{
			g_ShellPlay = g_ShellIn | VALE_FIRE;   // GameSession.carryOn: a tick with the press
			g_ShellDo |= DO_TICK | DO_CARRY_ON;
		}
		else
		{
			s_Page = s_On;
			Countdown();
		}
		return;
	}
	if ((g_ShellTick >= SHELL_HOLD_TICKS && Fire()) || g_ShellTick >= GAME_OVER_HOLD_TICKS)
		EndRun();
}

static void DrawWon()
{
	Begin(FALSE, FALSE);
	u8 y = Heading(STR_TALLY);
	g_TextN = 0;
	Data_TextStr(STR_TOTAL);
	Put(' ');
	Put(' ');
	Data_TextNum(g_Score);
	Centre(TRUE, y + 4 * BIG_LINE, INK_BRIGHT);
	Show(FALSE);
}

static void Won()
{
	if (g_ShellTick == TALLY_HOLD_TICKS)
	{
		s_Page = s_On;
		Footer(STR_PRESS_FIRE, INK_BRIGHT);
	}
	if ((s_KeyPress & SK_ESC) || (g_ShellTick >= TALLY_HOLD_TICKS && Fire()))
		EndRun();
}

//-----------------------------------------------------------------------------
// The hi-scores (HighScores, HiScoreScreen, HiScoreEntry, HiScoreEntryScreen).

static u32 s_PendScore;
static u32 s_PendTicks;
static u8  s_PendPieces;
static u8  s_PendEasy;
static u8  s_Name[HISCORE_NAME];
static u8  s_NameAt;
static i8  s_RepeatDir;
static u8  s_RepeatHeld;
static u8  s_RepeatStep;

// Whether a run is ahead of row p (best first, then quickest).
static u8 Ahead(u32 score, u32 ticks, const u8* p)
{
	u32 s = Get32(p + R_SCORE);
	return score > s || (score == s && ticks < Get32(p + R_TICKS));
}

// The row the run takes: after every one as good or better (a stable sort).
static u8 PlaceOf(u32 score, u32 ticks)
{
	u8 i = 0;
	while (i < HISCORE_SIZE && !Ahead(score, ticks, Row(i)))
		i++;
	return i;
}

// Shell.endRun: the run offered to the table, and back to the title.
static void EndRun()
{
	s_PendScore = g_BestScore;
	s_PendTicks = g_SimTick;
	s_PendPieces = g_QuestPieces;
	s_PendEasy = g_Easy;
	if (s_PendScore > 0 && PlaceOf(s_PendScore, s_PendTicks) < HISCORE_SIZE)
	{
		for (u8 i = 0; i < HISCORE_NAME; i++)
			s_Name[i] = 0;
		s_NameAt = 0;
		s_RepeatDir = 0;
		s_RepeatHeld = 0;
		Go(SH_ENTRY);
	}
	else
		Go(SH_TITLE);
}

static void Best()
{
	g_HiBest = Get32(Row(0) + R_SCORE);
}

static u8 s_NameX;
static u8 s_NameY;

// The name (letters: drawn again) and the cursor under it, blinking so a
// space, a letter too, still shows where it is.
static void EntryName(u8 letters)
{
	u8 adv = 8 * NAME_SCALE;
	u8 bar = s_NameY + adv - NAME_SCALE;
	if (letters)
	{
		Fill(0, s_NameY, 256, adv - NAME_SCALE, INK_GROUND);
		g_TextN = 0;
		for (u8 i = 0; i < HISCORE_NAME; i++)
			Put(s_Alphabet[s_Name[i]]);
		Scaled(s_NameX, s_NameY, NAME_SCALE, INK_BRIGHT);
	}
	Fill(0, bar, 256, NAME_SCALE, INK_GROUND);
	u8 rem;
	if (!(DivBy(g_ShellTick, HISCORE_BLINK_TICKS, &rem) & 1))
		Dot(s_NameX + Times(s_NameAt, adv), bar, adv - 2 * NAME_SCALE, NAME_SCALE, INK_HEADING);
	Data_VdpWait();
}

static void DrawEntry()
{
	Begin(FALSE, FALSE);
	u8 y = Heading(STR_NEW_BEST);
	g_TextN = 0;
	Data_TextStr(STR_ENTRY_SCORE);
	Data_TextNum(s_PendScore);
	Data_TextStr(STR_ENTRY_PLACE);
	Data_TextNum(PlaceOf(s_PendScore, s_PendTicks) + 1);
	Centre(FALSE, y, INK_BODY);
	s_NameX = (u8)((256 - Times(HISCORE_NAME, 8 * NAME_SCALE)) >> 1);
	s_NameY = y + Times(SMALL_LINE, 6);
	EntryName(TRUE);
	Footer(STR_ENTRY_PROMPT, INK_BRIGHT);
	Show(FALSE);
}

static void Letter(i8 dir)
{
	u8 v = s_Name[s_NameAt];
	if (dir > 0)
		v = v + 1 >= HISCORE_ALPHABET_N ? 0 : v + 1;
	else
		v = v == 0 ? HISCORE_ALPHABET_N - 1 : v - 1;
	s_Name[s_NameAt] = v;
}

static void Entry()
{
	u8 was = s_Name[s_NameAt];
	u8 wasAt = s_NameAt;
	i8 dir = ((g_ShellIn & VALE_DOWN) ? 1 : 0) - ((g_ShellIn & VALE_UP) ? 1 : 0);
	if (dir != 0 && dir == s_RepeatDir)
	{
		if (s_RepeatHeld < 255)
			s_RepeatHeld++;
		if (s_RepeatHeld >= HISCORE_REPEAT_DELAY)
		{
			if (s_RepeatHeld == HISCORE_REPEAT_DELAY)
				s_RepeatStep = 0;
			if (s_RepeatStep == 0)
				Letter(dir);
			if (++s_RepeatStep >= HISCORE_REPEAT_EVERY)
				s_RepeatStep = 0;
			if (s_RepeatHeld == 255)
				s_RepeatHeld = HISCORE_REPEAT_DELAY + 1;   // held long: keep stepping
		}
	}
	else if (dir != 0)
	{
		s_RepeatDir = dir;
		s_RepeatHeld = 0;
		Letter(dir);
	}
	else
	{
		s_RepeatDir = 0;
		s_RepeatHeld = 0;
	}
	if ((s_Press & VALE_LEFT) && s_NameAt > 0)
		s_NameAt--;
	if ((s_Press & VALE_RIGHT) && s_NameAt < HISCORE_NAME - 1)
		s_NameAt++;
	u8 done = FALSE;
	if (Fire())
	{
		if (++s_NameAt >= HISCORE_NAME)
		{
			s_NameAt = HISCORE_NAME - 1;
			done = TRUE;
		}
	}
	if (s_Name[wasAt] != was || s_NameAt != wasAt)
		Sound_Play(SFX_MENU_MOVE);
	if (done)
	{
		Sound_Play(SFX_MENU_PICK);
		u8 p = PlaceOf(s_PendScore, s_PendTicks);
		for (u8 i = HISCORE_SIZE - 1; i > p; i--)
		{
			u8* to = Row(i);
			const u8* from = Row(i - 1);
			for (u8 b = 0; b < HISCORE_ROW_BYTES; b++)
				to[b] = from[b];
		}
		u8* row = Row(p);
		for (u8 i = 0; i < HISCORE_NAME; i++)
			row[R_NAME + i] = s_Alphabet[s_Name[i]];
		Put32(row + R_SCORE, s_PendScore);
		row[R_PIECES] = s_PendPieces;
		Put32(row + R_TICKS, s_PendTicks);
		row[R_EASY] = s_PendEasy;
		Best();
		s_Highlight = (i8)p;
		s_Back = SH_TITLE;
		Go(SH_HISCORES);
		s_Back = SH_TITLE;
		return;
	}
	u8 rem;
	DivBy(g_ShellTick, HISCORE_BLINK_TICKS, &rem);
	u8 changed = s_Name[wasAt] != was || s_NameAt != wasAt;
	if (changed || rem == 0)
	{
		s_Page = s_On;
		EntryName(changed);
	}
}

static void DrawHiScores()
{
	Begin(FALSE, FALSE);
	u8 y = Heading(STR_HISCORES);
	u8 left = 128 - 88;
	u8 right = 128 + 88;
	for (u8 i = 0; i < HISCORE_SIZE; i++)
	{
		const u8* row = Row(i);
		u8 ink = (i8)i == s_Highlight ? INK_BRIGHT : INK_BODY;
		g_TextN = 0;
		if (i + 1 < 10)
			Put(' ');
		Data_TextNum(i + 1);
		Put('.');
		Draw(FALSE, left, y, INK_DIM);
		g_TextN = 0;
		for (u8 c = 0; c < HISCORE_NAME; c++)
			Put(row[R_NAME + c]);
		Draw(FALSE, left + 20, y, ink);
		if (row[R_EASY])
		{
			Say(STR_EASY_MARK);
			Draw(FALSE, left + 20 + HISCORE_NAME * SMALL_ADVANCE + 6, y, INK_EASY);
		}
		g_TextN = 0;
		Data_TextNum(Get32(row + R_SCORE));
		Draw(FALSE, right - (u8)WidthOf(FALSE), y, ink);
		g_TextN = 0;
		Data_TextNum(row[R_PIECES]);
		Data_TextStr(STR_OF_QUARTERS);
		Draw(FALSE, right - PIECES_FROM_RIGHT, y, INK_DIM);
		y += 2 * SMALL_LINE;
	}
	Footer(STR_PRESS_FIRE, INK_BRIGHT);
	Show(FALSE);
}

static void HiScores()
{
	if (g_ShellTick >= SHELL_HOLD_TICKS && (Fire() || (s_KeyPress & SK_ESC)))
	{
		BackOut();
		s_Highlight = -1;
	}
}

//-----------------------------------------------------------------------------

void Shell_Init()
{
	Data_ShellBytes(HISCORE_DEFAULTS, s_Table, sizeof(s_Table));
	Best();
	Data_ShellBytes(WORD_WIDTHS, s_WordW, WORDMARK_WORDS);
	Data_ShellBytes(STR_ALPHABET + 1, s_Alphabet, HISCORE_ALPHABET_N);
	g_PageShown = 0;
	s_On = 0;
	g_Shell = SH_TITLE;
	Go(SH_TITLE);
}

static void Enter()
{
	s_Drawn = g_Shell;
	switch (g_Shell)
	{
	case SH_TITLE:
		DrawTitle();
		break;
	case SH_ATTRACT:
		s_DemoRun = 0;
		s_DemoLeft = 0;
		g_DemoTicks = 0;
		Data_Palette(TRUE);
		break;
	case SH_HISCORES:
		DrawHiScores();
		break;
	case SH_OPTIONS: case SH_PAUSED: case SH_SAVE: case SH_LOAD:
		DrawMenu();
		break;
	case SH_PLAYING:
		if (s_On != g_PageShown)
		{
			Data_VdpWait();
			Data_ShowPage(g_PageShown);
			s_On = g_PageShown;
		}
		break;
	case SH_REVEAL:
		DrawReveal();
		break;
	case SH_MAP:
		DrawMap();
		break;
	case SH_GAME_OVER:
		DrawGameOver();
		break;
	case SH_WON:
		DrawWon();
		break;
	case SH_ENTRY:
		DrawEntry();
		break;
	}
}

void Shell_Tick()
{
	g_ShellTick++;
	s_Press = g_ShellIn & ~s_Held;
	s_Held = g_ShellIn;
	s_KeyPress = g_ShellKeys & ~s_KeysHeld;
	s_KeysHeld = g_ShellKeys;
	g_ShellDo = 0;
	g_ShellPlay = g_ShellIn;
	if (g_TestMapOpen)
	{
		// test/shell.sh: the map over the game as it stands (the demo's, ended), at its first tick.
		g_TestMapOpen = 0;
		s_PlayTick = 0;
		Go(SH_MAP);
	}
	if (s_Drawn != g_Shell)
		Enter();   // a screen Shell_AfterTick went to
	switch (g_Shell)
	{
	case SH_TITLE:
		Title();
		break;
	case SH_ATTRACT:
		Attract();
		break;
	case SH_HISCORES:
		HiScores();
		break;
	case SH_OPTIONS:
		Options();
		break;
	case SH_PLAYING:
		Playing();
		break;
	case SH_PAUSED:
		Paused();
		break;
	case SH_SAVE: case SH_LOAD:
		Slots();
		break;
	case SH_REVEAL:
		Reveal();
		break;
	case SH_MAP:
		Map();
		break;
	case SH_GAME_OVER:
		GameOver();
		break;
	case SH_WON:
		Won();
		break;
	case SH_ENTRY:
		Entry();
		break;
	}
	if (s_Drawn != g_Shell && !(g_ShellDo & (DO_NEW_GAME | DO_DEMO)))
		Enter();
}

// After the new game or the demo's game is on screen (wulfquest.c).
void Shell_Shown()
{
	s_On = g_PageShown;
	if (s_Drawn != g_Shell)
		Enter();
}
