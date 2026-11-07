// Wulf Quest for MSX2. M5: Ranger Vale walks the whole map, in colour
// (AGENTS.md §10, M3 to M5).
//
// The game loop: 50 ticks a second, driven by the vertical interrupt; on a
// 60 Hz machine every sixth frame has no tick, so the game runs at the same
// speed (§6). A frame starts by showing the sprites the last frame drew (two
// sets of sprite tables, sprites.c: the blank only points the VDP at the
// other); then its ticks run, and the sprites they leave are drawn into the
// set not shown,
// each reading one input state and moving him (vale.c).
//
// When he walks into another room, it is drawn on the hidden page while the
// old one stays on screen; then, in one blank, the pages swap and he moves to
// his place in the new one. The drawing takes longer than the Java game's
// frozen ticks after a flip (§7.5), so those are run straight after it, for
// the frames it took: the hitch is the drawing, not the drawing and the freeze.
//
// Debug: F5 shows the next room by number, Vale standing at the same place in
// it (test/rooms.sh walks every room that way); the panel shows his room and
// position (overlay.c).
//
// Test hooks, for test/moves.sh (through the symbol map): a script writes a
// world position and g_TestStart = 1, and from then on sets g_TestKeys at a
// breakpoint on Test_BeforeTick, which runs before every tick, and reads
// Vale's state there.

#include "msxgl.h"
#include "constants.h"
#include "palette.h"
#include "player.h"
#include "data.h"
#include "room.h"
#include "vale.h"
#include "overlay.h"
#include "sprites.h"
#include "jungle.h"
#include "rng.h"
#include "jungle_data.h"
#include "sprite_bank.h"
#include "blade.h"
#include "tables.h"
#include "quest.h"
#include "quest_data.h"

// MSXgl's start-up does not clear RAM: a global with no initial value starts
// as whatever the RAM held. So every one here has one.

// Read by test/rooms.sh: which room is on screen, how many rooms have been
// drawn, and how many frames the last and the slowest drawing took.
u8  g_Room = 0;
u16 g_DrawCount = 0;
u8  g_DrawFrames = 0;
u8  g_DrawWorst = 0;

// Read by test/moves.sh: ticks run, frames seen, frames that came and went
// while the loop was still busy with the last one, flips, and the frames the
// flips took (not late: a flip is a hitch, §7.5).
u16 g_Ticks = 0;
u16 g_Frames = 0;
u16 g_LateFrames = 0;
u16 g_Flips = 0;
u16 g_FlipFrames = 0;

// Written by test/moves.sh.
u8  g_TestStart = 0;
u8  g_TestMode = 0;
u8  g_TestKeys = 0;
u8  g_TestOpen = 0;
i32 g_TestX = 0;
i32 g_TestY = 0;

static RoomData s_Room;
static u8 s_Shown = 0;   // the page on screen: 0 or 1
static i16 s_RoomX;  // the room's top-left, world pixels
static i16 s_RoomY;
// What each set of sprite tables has loaded (sprites.h): the back set's is drawn.
static u8 s_ValeFrame[2] = { 0xFF, 0xFF };

static void DrawSprites();

// test/moves.sh breaks here when a frame has been put on screen: the pages
// and Vale's sprites, all of it while the screen is blank.
void Test_Shown()
{
	__asm__("nop");
}

// Draw Vale's room on the hidden page, then show it, with him in it, in the
// next blank.
static void ShowRoom()
{
	u16 start = g_JIFFY;
	u8 hidden = 1 - s_Shown;
	u8 room = g_ValeRoom;
	Data_LoadRoom(room, &s_Room);
	Room_Draw(&s_Room, hidden);
	Overlay_Draw(hidden);
	g_DrawFrames = (u8)(g_JIFFY - start);
	if (g_DrawFrames > g_DrawWorst)
		g_DrawWorst = g_DrawFrames;
	g_Room = room;
	s_RoomX = (i16)(room % GRID_W) * PLAYFIELD_W;
	s_RoomY = (i16)(room / GRID_W) * PLAYFIELD_H;
	DrawSprites();   // Vale in it, into the back set
	// The page and the sprites, both in the next blank (data.c's hook).
	g_FlipR2 = (hidden << 5) | 0x1F;   // R#2 in SCREEN 5: the page's address bits 16-15
	__asm__("di");
	g_FlipPending |= FLIP_PAGE;
	__asm__("ei");
	while (g_FlipPending)
		Halt();
	s_Shown = hidden;
	Test_Shown();
}

// Stand Vale at a world position and show the room it is in. g_DrawCount
// goes up only when all of it is done, so test/rooms.sh's next key comes after.
static void EnterRoom(i32 x, i32 y)
{
	Vale_Place(x, y);
	ShowRoom();
	g_DrawCount++;
}

// A key that has just gone down.
static bool Pressed(u8 key, bool* was)
{
	bool now = Keyboard_IsKeyPressed(key);
	bool edge = now && !*was;
	*was = now;
	return edge;
}

// One input state (§7): the cursor keys or Q A O P, Space; or joystick 1.
static u8 ReadKeys()
{
	u8 joy = Joystick_Read(JOY_PORT_1);
	u8 k = JOY_GET_DIR(joy);   // its bits are VALE_UP, _DOWN, _LEFT, _RIGHT
	if (JOY_GET_TRIG1(joy))
		k |= VALE_FIRE;
	if (Keyboard_IsKeyPressed(KEY_UP) || Keyboard_IsKeyPressed(KEY_Q))
		k |= VALE_UP;
	if (Keyboard_IsKeyPressed(KEY_DOWN) || Keyboard_IsKeyPressed(KEY_A))
		k |= VALE_DOWN;
	if (Keyboard_IsKeyPressed(KEY_LEFT) || Keyboard_IsKeyPressed(KEY_O))
		k |= VALE_LEFT;
	if (Keyboard_IsKeyPressed(KEY_RIGHT) || Keyboard_IsKeyPressed(KEY_P))
		k |= VALE_RIGHT;
	if (Keyboard_IsKeyPressed(KEY_SPACE))
		k |= VALE_FIRE;
	return k;
}

// test/moves.sh breaks here before every tick. Not static, so it is in the map.
void Test_BeforeTick()
{
	__asm__("nop");
}

// test/moves.sh breaks here when a frame's work starts and when it ends, to
// measure it.
void Test_FrameStart()
{
	__asm__("nop");
}

void Test_FrameEnd()
{
	__asm__("nop");
}

// test/sprites.sh's bench (M5): the VDP's time to restore a big creature's
// background (HMMM, op 0) or to draw one over it (LMMM, transparent, op 1),
// g_BenchW x g_BenchH, 16 times on the hidden page, between two breakpoints.
u8 g_BenchGo = 0;
u8 g_BenchOp = 0;
u8 g_BenchW = 0;
u8 g_BenchH = 0;

void Test_BenchStart()
{
	__asm__("nop");
}

void Test_BenchEnd()
{
	__asm__("nop");
}

static void Bench()
{
	u16 top = (u16)(1 - s_Shown) * 256;
	Test_BenchStart();
	for (u8 i = 0; i < 16; i++)
	{
		if (g_BenchOp == 0)
			VDP_CommandHMMM(0, 3 * 256 + 128, 64, top + 64, g_BenchW, g_BenchH);
		else
			VDP_CommandLMMM(0, 2 * 256, 64, top + 64, g_BenchW, g_BenchH, VDP_OP_TIMP);
		VDP_CommandWait();
	}
	Test_BenchEnd();
}

// One tick; TRUE when Vale has walked into another room.
static bool Tick()
{
	Test_BeforeTick();
	g_Ticks++;
	return Vale_Tick(g_TestMode ? g_TestKeys : ReadKeys());
}

// Frames into ticks: one a frame, none in every sixth on a 60 Hz machine.
static bool s_Skip60 = FALSE;
static u8 s_Sixth = 0;

static u8 TicksIn(u8 frames)
{
	u8 ticks = 0;
	while (frames--)
	{
		if (s_Skip60 && ++s_Sixth == 6)
		{
			s_Sixth = 0;
			continue;
		}
		ticks++;
	}
	return ticks;
}

// He has walked into another room: show it, then run the frozen ticks that
// fall in the frames the drawing took (as many as there were, and no more).
static void Flip()
{
	u16 start = g_JIFFY;
	g_Flips++;   // before, for test/moves.sh to see at Test_Shown
	ShowRoom();
	u16 frames = g_JIFFY - start;
	g_FlipFrames += frames;
	u8 ticks = TicksIn(frames > 255 ? 255 : (u8)frames);
	while (ticks-- && g_ValeFreeze)
		Tick();
}

// Test hooks for test/sprites.sh (M5): with g_TestGallery set the game
// stands still and shows, instead of Vale where he is, his frame
// g_TestValeFrame with blade g_TestValeBlade (-1 none) at g_TestValeX,
// g_TestValeY (the sprite's top-left on the screen), and g_TestCount frames
// of the sprite bank, g_TestFrame[i] at g_TestSpriteX[i], g_TestSpriteY[i].
u8  g_TestGallery = 0;
u8  g_TestValeFrame = 0;
i8  g_TestValeBlade = -1;
i16 g_TestValeX = 0;
i16 g_TestValeY = 0;
u8  g_TestCount = 0;
u8  g_TestFrame[SPR_SMALL + SPR_BIG];
i16 g_TestSpriteX[SPR_SMALL + SPR_BIG];
i16 g_TestSpriteY[SPR_SMALL + SPR_BIG];

// test/jungle.sh (M6): with g_TestJungle set, a test's start is a new game
// with the jungle, its random numbers seeded from g_TestSeed (the Java
// game's long, little-endian); without it, the empty jungle.
u8  g_TestJungle = 0;
u8  g_TestSeed[8];
// M7: with g_TestQuest too, the quest and the Wulf; g_TestEndless, deaths
// without using up lives (the Java game's infiniteLives).
u8  g_TestQuest = 0;
u8  g_TestEndless = 0;

static i8 s_BladeShape[2] = { -1, -1 };
static bool s_Loaded = FALSE;   // DrawSprites loaded a frame or a shape this frame
// The frame each slot from SLOT_SMALL on holds, and the tint it is in.
static u8 s_SlotFrame[2][SPR_SMALL + SPR_BIG];
static u8 s_SlotTint[2][SPR_SMALL + SPR_BIG];
// The frame and tint whose colours the manager's colour buffer holds for each
// slot (sprites.c's s_Colour, one for both sets): a set's colour table is
// rewritten from it when a sprite's look changes (the early clock, the clip),
// so it must hold that set's frame, not the other set's.
static u8 s_BufFrame[SPR_SMALL + SPR_BIG];
static u8 s_BufTint[SPR_SMALL + SPR_BIG];
static u8 s_ValeBuf = 0xFF;

// Show bank frame f, in tint (0: its own colours; a silhouette takes the
// tint), its feet at (x, y) on the screen, in slot SLOT_SMALL + n.
static void PutFrame(u8 n, u8 f, u8 tint, i16 x, i16 y)
{
	u8 slot = SLOT_SMALL + n;
	u8 set = Sprites_Back();
	if (f != s_SlotFrame[set][n] || tint != s_SlotTint[set][n] || f != s_BufFrame[n] || tint != s_BufTint[n])
	{
		s_Loaded = TRUE;
		u8* c = Sprites_Colours(slot);
		u8 blocks = Data_LoadSpriteFrame(f, g_SpritePatternHigh, Sprites_PatternAddress(slot), c);
		if (tint)
		{
			u8 n2 = (blocks >> 4) * (blocks & 15);
			for (u8 b = 0; b < n2; b++, c += 32)
				for (u8 l = 0; l < 16; l++)
					c[l] = tint;   // the base's lines; the overlay is empty
		}
		Sprites_SetBlocks(slot, blocks);
		Sprites_Changed(slot);
		s_SlotFrame[set][n] = f;
		s_SlotTint[set][n] = tint;
		s_BufFrame[n] = f;
		s_BufTint[n] = tint;
	}
	Sprites_Put(slot, x - g_SpriteOriginX[f], y - g_SpriteOriginY[f]);
}

// The walk's frame: (s_Clock / CREATURE_WALK_TICKS_PER_FRAME) & 1, the Java
// game's, without SDCC's division (2 000 cycles): s_Clock mod 12 >= 6. A
// u32's bytes b0..b3 are b0 + 4 (b1 + b2 + b3) mod 12, 256 being 4 mod 12.
#if CREATURE_WALK_TICKS_PER_FRAME != 6
#error "WalkPhase divides by 6"
#endif
static u32 s_Clock;
static u8 WalkPhase() __naked
{
	__asm
		ld	hl, #_s_Clock + 1
		ld	a, (hl)
		inc	hl
		ld	e, a
		ld	d, #0
		ld	a, (hl)
		inc	hl
		add	a, e
		ld	e, a
		jr	nc, 00001$
		inc	d
	00001$:
		ld	a, (hl)
		add	a, e
		ld	e, a
		jr	nc, 00002$
		inc	d
	00002$:
		ex	de, hl                  ; b1 + b2 + b3
		add	hl, hl
		add	hl, hl
		ld	a, (_s_Clock)
		ld	e, a
		ld	d, #0
		add	hl, de                  ; + b0: up to 3 315
		ld	de, #-768               ; 12 * 64
	00010$:
		add	hl, de
		jr	c, 00010$
		ld	de, #768
		add	hl, de
		ld	de, #-96
	00020$:
		add	hl, de
		jr	c, 00020$
		ld	de, #96
		add	hl, de
		ld	a, l                    ; under 96
	00030$:
		sub	a, #12
		jr	nc, 00030$
		add	a, #12                  ; mod 12
		cp	a, #6
		ld	a, #0
		ret	c
		inc	a
		ret
	__endasm;
}

// CreaturePainter: the room's beasts, then its small creatures back to front
// by their feet; put here front to back, after Vale.
static u8 s_Order[CREATURE_POOL];

// The quest's big ones, front to back as the Java game paints them back to
// front: the Wulf, the Keeper, the guardian. n: the big slots used.
static u8 DrawQuest()
{
	u8 n = 0;
	if (g_WulfState >= WULF_PROWLING && g_WulfHere)
	{
		u8 f;
		if (g_WulfState == WULF_SITTING)
			f = g_WulfHeading < 0 ? WULF_FRAME_HOWL_L : WULF_FRAME_HOWL;
		else
		{
			s_Clock = g_WulfMoveTicks;
			u8 v = s_Clock ? WalkPhase() : 0;
			f = g_WulfHeading < 0 ? (v ? WULF_FRAME_WALK1_L : WULF_FRAME_WALK0_L)
				: (v ? WULF_FRAME_WALK1 : WULF_FRAME_WALK0);
		}
		PutFrame(SPR_SMALL + n++, f, 0, FP_PX(g_WulfX) - s_RoomX, FP_PX(g_WulfY) - s_RoomY);
	}
	if (g_KeeperHere)
	{
		u8 f = KEEPER_FRAME_STAND;
		if (g_KeeperState == KEEPER_ASIDE)
			f = KEEPER_FRAME_ASIDE;
		else if (g_KeeperState == KEEPER_STEPPING_ASIDE)
		{
			s_Clock = g_KeeperMoveTicks;
			f = WalkPhase() ? KEEPER_FRAME_STEP1 : KEEPER_FRAME_STEP0;
		}
		PutFrame(SPR_SMALL + n++, f, 0, FP_PX(g_KeeperX) - s_RoomX, FP_PX(g_KeeperY) - s_RoomY);
	}
	if (g_QuestLair >= 0)
	{
		s_Clock = g_GuardMoveTicks;
		u8 v = s_Clock ? WalkPhase() : 0;
		PutFrame(SPR_SMALL + n++, g_GuardFrame[g_QuestLair * 4 + v + (g_GuardFace < 0 ? 2 : 0)], 0,
			FP_PX(g_GuardX) - s_RoomX, FP_PX(g_GuardY) - s_RoomY);
	}
	return n;
}

static void DrawJungle()
{
	u8 n = g_QuestOn ? DrawQuest() : 0;
	for (i8 b = BEASTS - 1; b >= 0 && n < SPR_BIG; b--)
	{
		if (g_Beast[b].At != g_ValeRoom)
			continue;
		u8 k = g_BKind[b];
		u8 v = 4;
		if (!g_BAsleep[b])
		{
			s_Clock = g_Beast[b].MoveTicks;
			v = s_Clock ? WalkPhase() : 0;
		}
		if (g_BFace[b] < 0)
			v += v == 4 ? 1 : 2;
		PutFrame(SPR_SMALL + n, g_BeastFrame[k * 6 + v], 0,
			FP_PX(g_Beast[b].X) - s_RoomX, FP_PX(g_Beast[b].Y) - s_RoomY);
		n++;
	}
	// Front to back: the creature nearest the bottom first, the later of two level.
	u8 m = g_HereN > SPR_SMALL ? SPR_SMALL : g_HereN;
	for (u8 k = 0; k < g_HereN; k++)
		s_Order[k] = g_Here[k];
	for (u8 a = 0; a < g_HereN; a++)
		for (u8 b = a + 1; b < g_HereN; b++)
		{
			u8 i = s_Order[a];
			u8 j = s_Order[b];
			if (g_CY[j] > g_CY[i] || (g_CY[j] == g_CY[i] && g_CId[j] > g_CId[i]))
			{
				s_Order[a] = j;
				s_Order[b] = i;
			}
		}
	for (u8 k = 0; k < m; k++)
	{
		u8 i = s_Order[k];
		u8 f;
		u8 tint = 0;
		u8 half = g_CModeTick[i] < CREATURE_PUFF_TICKS / 2;
		if (g_CMode[i] == CREATURE_APPEARING)
		{
			f = g_PuffFrame[half ? 3 : 2];   // the puff run backwards, in its tint
			tint = g_CTint[i];
		}
		else if (g_CMode[i] == CREATURE_DYING)
			f = g_PuffFrame[half ? 0 : 1];
		else
		{
			u8 s = g_CSpecies[i];
			bool flier = g_CreatureFlier[s];
			s_Clock = flier ? g_SimTick : g_CMoveTicks[i];
			u8 v = (flier || g_CMoveTicks[i] > 0) ? WalkPhase() : 0;
			f = g_CreatureFrame[s * 4 + v + (g_CFace[i] < 0 ? 2 : 0)];
		}
		PutFrame(k, f, tint, FP_PX(g_CX[i]) - s_RoomX, FP_PX(g_CY[i]) - s_RoomY);
	}
	// The quarter on its pedestal, under the creatures (loot before creatures).
	if (g_QuestOn && g_QuestLair >= 0 && !(g_QuestTaken & (1 << g_QuestLair)) && m < SPR_SMALL)
	{
		u8 k = g_QuestSet * LAIRS + g_QuestLair;
		PutFrame(m, g_PieceFrame[g_QuestLair], 0, g_LairPedX[k], g_LairPedY[k]);
	}
}

// Vale (four layered sprites, PlayerPainter's frame), his blade out of his
// hand while it is out, and whatever else is to be shown, in a frame's blank.
static void DrawSprites()
{
	Sprites_Prepare();
	u8 f = g_ValeFrame;
	i8 blade = g_ValeBlade;
	i16 sx = FP_PX(g_ValeX) - s_RoomX - PLAYER_ORIGIN_X;
	i16 sy = FP_PX(g_ValeY) - s_RoomY - PLAYER_ORIGIN_Y;
	if (g_TestGallery)
	{
		f = g_TestValeFrame;
		blade = g_TestValeBlade;
		sx = g_TestValeX;
		sy = g_TestValeY;
	}
	s_Loaded = FALSE;
	u8 set = Sprites_Back();
	if (f != s_ValeFrame[set] || f != s_ValeBuf)
	{
		s_Loaded = TRUE;
		Data_LoadValeFrame(f, g_SpritePatternHigh, Sprites_PatternAddress(SLOT_VALE), Sprites_Colours(SLOT_VALE));
		Sprites_Changed(SLOT_VALE);
		s_ValeFrame[set] = f;
		s_ValeBuf = f;
	}
	Sprites_Begin();
	if (blade < 0 || g_PlayerHand[f * 2] < 0 || (!g_TestGallery && !g_ValeVisible))   // blinked out: no blade either
		Sprites_Put(SLOT_BLADE, -128, -128);   // off the screen: held below it
	else
	{
		if (blade != s_BladeShape[set])
		{
			s_Loaded = TRUE;
			Data_LoadBlade((u8)blade, g_SpritePatternHigh, Sprites_PatternAddress(SLOT_BLADE));
			s_BladeShape[set] = blade;
		}
		const i8* at = g_BladeAt + ((u8)blade << 2);
		i8 hx = g_PlayerHand[f * 2];
		i8 hy = g_PlayerHand[f * 2 + 1];
		Sprites_SetOffset(SLOT_BLADE, 0, hx + at[0], hy + at[1]);
		Sprites_SetOffset(SLOT_BLADE, 1, hx + at[2], hy + at[3]);
		Sprites_Put(SLOT_BLADE, sx, sy);
	}
	if (g_TestGallery || g_ValeVisible)
		Sprites_Put(SLOT_VALE, sx, sy);
	else
		Sprites_Put(SLOT_VALE, -128, -128);   // blinked out: held below the screen
	if (g_TestGallery)
	{
		// Bank frames at their top-lefts: the origin added back.
		for (u8 i = 0; i < g_TestCount; i++)
			PutFrame(i, g_TestFrame[i], 0, g_TestSpriteX[i] + g_SpriteOriginX[g_TestFrame[i]],
				g_TestSpriteY[i] + g_SpriteOriginY[g_TestFrame[i]]);
	}
	else if (g_JungleOn)
		DrawJungle();
	Sprites_End();
}

// Once: the slots' fixed parts. Vale's lower block is 16 px below his upper;
// the blade is one colour a sprite; the bank's frames are not loaded yet.
static void SpritesInit()
{
	Sprites_Init();
	Sprites_SetOffset(SLOT_VALE, 2, 0, 16);
	Sprites_SetOffset(SLOT_VALE, 3, 0, 16);
	u8* c = Sprites_Colours(SLOT_BLADE);
	for (u8 l = 0; l < 16; l++)
	{
		c[l] = BLADE_INK;
		c[16 + l] = BLADE_HILT;
	}
	Sprites_Changed(SLOT_BLADE);
	for (u8 i = 0; i < SPR_SMALL + SPR_BIG; i++)
	{
		for (u8 set = 0; set < 2; set++)
		{
			s_SlotFrame[set][i] = 0xFF;
			s_SlotTint[set][i] = 0;
		}
		s_BufFrame[i] = 0xFF;
		s_BufTint[i] = 0;
	}
}

void main()
{
	Data_LoadTables();
	Data_InstallFlip();
	VDP_SetMode(VDP_MODE_SCREEN5);
	VDP_EnableVBlank(TRUE);
	VDP_ClearVRAM();
	VDP_SetPalette(g_Palette);
	VDP_SetColor(0);
	VDP_SetSpriteTables(VDP_G5_ADDR_SPT, VDP_G5_ADDR_SAT);
	VDP_SetSpriteFlag(VDP_SPRITE_SIZE_16);
	SpritesInit();
	VDP_EnableSprite(TRUE);
	Data_LoadScenery();
	Overlay_Init();
	Vale_Init();
	s_Skip60 = VDP_GetFrequency() == VDP_FREQ_60HZ;

	// A new game: the jungle, its random numbers seeded from the clock until
	// the title screen gives a better one (M10).
	for (u8 i = 0; i < 8; i++)
		g_TestSeed[i] = 0;
	g_TestSeed[0] = (u8)g_JIFFY;
	g_TestSeed[1] = (u8)(g_JIFFY >> 8);
	Rng_Seed(g_TestSeed);
	JUNGLE(Jungle_NewGame);
	g_JungleOn = 1;
	g_QuestOn = 1;
	QUEST(Quest_NewGame);
	EnterRoom(PLAYER_START_X_FP, PLAYER_START_Y_FP);

	bool f5 = FALSE;
	u16 last = g_JIFFY;
	while (TRUE)
	{
		while (g_JIFFY == last)
			Halt();
		u16 now = g_JIFFY;
		u8 frames = (u8)(now - last);
		last = now;
		g_Frames += frames;
		Test_FrameStart();
		if (frames > 1)
			g_LateFrames += frames - 1;

		// What the last frame drew is on screen: the blank's hook showed it.
		Test_Shown();

		u8 ticks = g_TestGallery ? 0 : TicksIn(frames);
		if (ticks > MAX_CATCHUP_TICKS)
			ticks = MAX_CATCHUP_TICKS;
		bool flipped = FALSE;
		while (ticks--)
			if (Tick())
			{
				Flip();
				flipped = TRUE;
				last = g_JIFFY;   // the frames the drawing took are not late
				break;
			}
		// What these ticks left, into the back set, for the next frame
		// (a flip has drawn it already).
		if (!flipped)
			DrawSprites();
		// The debug line, but not in a frame that loaded sprites: those are the busy ones.
		if (!s_Loaded)
			Overlay_Update(s_Shown);
		Test_FrameEnd();

		if (g_BenchGo)
		{
			Bench();
			g_BenchGo = 0;
			last = g_JIFFY;
		}
		if (g_TestStart)
		{
			g_TestMode = 1;
			g_ValeOpen = g_TestOpen;
			g_JungleOn = g_TestJungle;
			g_QuestOn = g_TestJungle && g_TestQuest;
			g_ValeEndless = g_TestEndless;
			if (g_JungleOn)
			{
				Rng_Seed(g_TestSeed);
				JUNGLE(Jungle_NewGame);
				if (g_QuestOn)
					QUEST(Quest_NewGame);
			}
			EnterRoom(g_TestX, g_TestY);
			g_TestStart = 0;
			g_LateFrames = 0;
			last = g_JIFFY;
		}
		else if (Pressed(KEY_F5, &f5))
		{
			// Debug: the next room, Vale where he stood in this one.
			i32 x = g_ValeX - ((i32)s_RoomX << FIXED_SHIFT);
			i32 y = g_ValeY - ((i32)s_RoomY << FIXED_SHIFT);
			u8 next = g_Room + 1;
			EnterRoom(x + ((i32)((i16)(next % GRID_W) * PLAYFIELD_W) << FIXED_SHIFT),
				y + ((i32)((i16)(next / GRID_W) * PLAYFIELD_H) << FIXED_SHIFT));
			last = g_JIFFY;
		}
	}
}
