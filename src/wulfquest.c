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
// it (test/rooms.sh walks every room that way); F4 puts his room and
// position on the panel's bottom line (panel.c).
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
#include "panel.h"
#include "sound.h"
#include "sprites.h"
#include "jungle.h"
#include "rng.h"
#include "jungle_data.h"
#include "sprite_bank.h"
#include "blade.h"
#include "tables.h"
#include "quest.h"
#include "quest_data.h"
#include "game.h"
#include "game_data.h"
#include "panel_data.h"
#include "shell.h"
#include "shell_data.h"

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
static i16 s_RoomX;  // the room's top-left, world pixels
static i16 s_RoomY;
// What each set of sprite tables has loaded (sprites.h): the back set's is drawn.
static u16 s_ValeFrame[2] = { 0xFFFF, 0xFFFF };   // frame | ink << 8

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
	u8 hidden = 1 - g_PageShown;
	u8 room = g_ValeRoom;
	Data_LoadRoom(room, &s_Room);
	Room_Draw(&s_Room, hidden);
	Panel_Draw(hidden);
	g_DrawFrames = (u8)(g_JIFFY - start);
	if (g_DrawFrames > g_DrawWorst)
		g_DrawWorst = g_DrawFrames;
	g_Room = room;
	s_RoomX = (i16)(room % GRID_W) * PLAYFIELD_W;
	s_RoomY = (i16)(room / GRID_W) * PLAYFIELD_H;
	DrawSprites();   // Vale in it, into the back set
	if (g_Shell == SH_ATTRACT && !g_TestMode)
	{
		g_DrawPage = hidden;   // the demo's room, dressed as the attract mode shows it
		SHELL(Shell_Dress);
	}
	// The page and the sprites, both in the next blank (data.c's hook).
	g_FlipR2 = (hidden << 5) | 0x1F;   // R#2 in SCREEN 5: the page's address bits 16-15
	__asm__("di");
	g_FlipPending |= FLIP_PAGE;
	__asm__("ei");
	while (g_FlipPending)
		Halt();
	g_PageShown = hidden;
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
	// The options' keys (M10): the cursors, or Q A O P.
	if (Keyboard_IsKeyPressed(g_Profile ? KEY_Q : KEY_UP))
		k |= VALE_UP;
	if (Keyboard_IsKeyPressed(g_Profile ? KEY_A : KEY_DOWN))
		k |= VALE_DOWN;
	if (Keyboard_IsKeyPressed(g_Profile ? KEY_O : KEY_LEFT))
		k |= VALE_LEFT;
	if (Keyboard_IsKeyPressed(g_Profile ? KEY_P : KEY_RIGHT))
		k |= VALE_RIGHT;
	if (Keyboard_IsKeyPressed(KEY_SPACE))
		k |= VALE_FIRE;
	return k;
}

// The shell's input (M10): the game's keys, and its own (shell.h).
static void ShellInput()
{
	g_ShellIn = ReadKeys();
	u8 k = 0;
	if (Keyboard_IsKeyPressed(KEY_ESC))
		k |= SK_ESC;
	if (Keyboard_IsKeyPressed(KEY_STOP) || Keyboard_IsKeyPressed(KEY_F1))
		k |= SK_PAUSE;
	if (Keyboard_IsKeyPressed(KEY_M) || JOY_GET_TRIG2(Joystick_Read(JOY_PORT_1)))
		k |= SK_MAP;
	if (Keyboard_IsKeyPressed(KEY_D))
		k |= SK_DIFF;
	if (Keyboard_IsKeyPressed(KEY_RETURN))
		k |= SK_ENTER;
	// Any key at all: a row of the BIOS's keyboard matrix (NEWKEY) with a key down.
	const u8* rows = (const u8*)0xFBE5;
	for (u8 r = 0; r < 11; r++)
		if (rows[r] != 0xFF)
			k |= SK_ANY;
	g_ShellKeys = k;
}

// The keys a tick of the game plays: a test's, the demo's, the shell's (a
// continue), the player's.
static u8 GameKeys()
{
	if (g_TestMode)
		return g_TestKeys;
	if (g_Shell == SH_ATTRACT)
	{
		SHELL(Shell_DemoKeys);
		return g_ShellPlay;
	}
	if (g_Shell == SH_GAME_OVER)
		return g_ShellPlay;
	return ReadKeys();
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
	u16 top = (u16)(1 - g_PageShown) * 256;
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

// tools/record-demo.sh (M10, AGENTS.md §7: the demo is recorded on the MSX):
// with g_TestRecord 1, the next new game from the title starts as a test's
// (g_TestSeed, g_TestX, g_TestY, Hard) and the keys each tick plays are kept
// as runs (a count, the keys) in video RAM at REC_VRAM, g_RecRuns of them,
// for g_TestRecordTicks ticks; then g_TestRecord is 2.
#define REC_VRAM	0xEA00   // page 1, below the screen: free up to the sprites' set 1
#define REC_RUNS	1280
u8  g_TestRecord = 0;
u16 g_TestRecordTicks = 0;
u16 g_RecTicks = 0;
u16 g_RecRuns = 0;
static u8 s_Rec[2] = { 0, 0 };   // the run going on: its count, its keys

static void RecordRun()
{
	if (s_Rec[0] && g_RecRuns < REC_RUNS)
		Data_VramWrite(REC_VRAM + (g_RecRuns << 1), s_Rec, 2);
	g_RecRuns++;
	s_Rec[0] = 0;
}

static void Record(u8 k)
{
	if (s_Rec[0] && (s_Rec[1] != k || s_Rec[0] == 255))
		RecordRun();
	s_Rec[0]++;
	s_Rec[1] = k;
	if (++g_RecTicks == g_TestRecordTicks)
	{
		RecordRun();
		g_TestRecord = 2;
	}
}

// One tick; TRUE when Vale has walked into another room.
static bool Tick()
{
	Test_BeforeTick();
	g_Ticks++;
	u8 k = GameKeys();
	if (g_TestRecord == 1)
		Record(k);
	return Vale_Tick(k);
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
// M8: with g_TestGame too, the whole game (Content.ecosystem()); g_TestEasy, in Easy.
u8  g_TestGame = 0;
u8  g_TestEasy = 0;
// M9: test/sound.sh writes a sound's number + 1 here (0xFF stops the tune).
u8  g_TestSound = 0;

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
// Each set's slot's frame's origin (frames.bin), as loaded: drawn from every frame.
static u8 s_SlotOX[2][SPR_SMALL + SPR_BIG];
static u8 s_SlotOY[2][SPR_SMALL + SPR_BIG];
static u8 s_BufTint[SPR_SMALL + SPR_BIG];
static u16 s_ValeBuf = 0xFFFF;

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
		s_SlotOX[set][n] = g_FrameOX;
		s_SlotOY[set][n] = g_FrameOY;
	}
	Sprites_Put(slot, x - s_SlotOX[set][n], y - s_SlotOY[set][n]);
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
		u8 v = (s_Clock ? WalkPhase() : 0) + (g_GuardFace < 0 ? 2 : 0);
		// Stunned by a swing, it flashes white on alternate ticks (CreaturePainter.flashing).
		bool flash = g_GameOn && g_GuardHurt > 0 && !(g_GuardHurt & 1);
		PutFrame(SPR_SMALL + n++, flash ? g_GuardFlash[g_QuestLair * 4 + v] : g_GuardFrame[g_QuestLair * 4 + v],
			flash ? FLASH_COLOUR : 0, FP_PX(g_GuardX) - s_RoomX, FP_PX(g_GuardY) - s_RoomY);
	}
	return n;
}

// QuestPainter's items and treasures, OrchidPainter's orchid: into small
// slots from m on; the slots used after them.
static const u8 c_Glance[4] = { 0, 1, 0, 2 };   // look ahead, right, ahead, left
static u8 s_WayRoom = 0xFF;   // the way out of this room, read once a room and a quarter
static u8 s_WayTaken = 0xFF;
static u8 s_Way = 0xFF;

static u8 DrawLoot(u8 m)
{
	u8 room = g_ValeRoom;
	u8 glance = c_Glance[(u8)(g_SimTick / ITEM_GLANCE_TICKS) & 3];
	if (!g_MapTaken && room == MAP_ROOM && m < SPR_SMALL)
		PutFrame(m++, MAP_FRAME, 0, MAP_X, MAP_Y);
	for (u8 i = 0; i < EYES; i++)
		if (!(g_EyesTaken & (1 << i)) && g_ItemRoom[i] == room && m < SPR_SMALL)
			PutFrame(m++, g_GlanceFrame[glance], 0, g_ItemX[i], g_ItemY[i]);
	bool whole = Quest_Quarters() >= EXIT_REQUIRES_PIECES;
	for (u8 i = 0; i < GLASSES; i++)
		if (!(g_GlassesTaken & (1 << i)) && (i < GLASSES_BEFORE_WHOLE || whole) && g_ItemRoom[EYES + i] == room
			&& m < SPR_SMALL)
			PutFrame(m++, g_GlanceFrame[3 + glance], 0, g_ItemX[EYES + i], g_ItemY[EYES + i]);
	for (u8 p = 0; p < 2; p++)
	{
		u8 t = g_RoomTreasure[p];
		if (t && m < SPR_SMALL)
			PutFrame(m++, g_TreasureFrame[t - 1], g_TreasureInk[t - 1], g_RoomMore[2 + 2 * p], g_RoomMore[3 + 2 * p]);
	}
	// The glass's arrow (WayPainter.paintArrow): at the edge where the way
	// leaves the room, blinking, faster once time is short.
	if (g_WayTicks > 0 && m < SPR_SMALL)
	{
		u16 blink = g_WayTicks <= WAY_WARN_TICKS ? WAY_WARN_BLINK_TICKS : WAY_BLINK_TICKS;
		if (((u16)(g_SimTick / blink) & 1) == 0)
		{
			if (s_WayRoom != room || s_WayTaken != g_QuestTaken)
			{
				s_WayRoom = room;
				s_WayTaken = g_QuestTaken;
				s_Way = Data_Way(g_QuestSet, g_QuestTaken, room);
			}
			if (s_Way != 0xFF)
			{
				u8 edge = s_Way >> 6;
				i16 along = (i16)(s_Way & 63) * CELL + CELL;
				i16 x = edge == 1 ? PLAYFIELD_W - WAY_INSET_PX : edge == 3 ? WAY_INSET_PX
					: along < WAY_INSET_PX ? WAY_INSET_PX : along > PLAYFIELD_W - WAY_INSET_PX ? PLAYFIELD_W - WAY_INSET_PX : along;
				i16 y = edge == 2 ? PLAYFIELD_H - WAY_INSET_PX : edge == 0 ? WAY_INSET_PX
					: along < WAY_INSET_PX ? WAY_INSET_PX : along > PLAYFIELD_H - WAY_INSET_PX ? PLAYFIELD_H - WAY_INSET_PX : along;
				PutFrame(m++, g_ArrowFrame[edge], 0, x, y + WAY_ARROW_HALF_H);
			}
		}
	}
	if (g_OrchidHere && m < SPR_SMALL)
	{
		u8 stage = Game_Stage();
		u8 o = g_OrchidWhich * 4;
		u8 f = 0xFF;
		if (stage == STAGE_SPROUT)
			f = ORCHID_FRAME_SPROUT;
		else if (stage == STAGE_BUD)
			f = g_OrchidFrame[o];
		else if (stage == STAGE_BLOOM)
		{
			// The bloom sways on its two frames (stageTick / swayTicks).
			u16 t = g_OrchidPhase - (ORCHID_SEED_TICKS + ORCHID_SPROUT_TICKS + ORCHID_BUD_TICKS);
			u8 sway = 0;
			while (t >= ORCHID_SWAY_TICKS)
			{
				t -= ORCHID_SWAY_TICKS;
				sway ^= 1;
			}
			f = g_OrchidFrame[o + 1 + sway];
		}
		else if (stage == STAGE_WILT)
			f = g_OrchidFrame[o + 3];
		if (f != 0xFF)
			PutFrame(m++, f, 0, g_OrchidX, g_OrchidY);
	}
	return m;
}

// The border's flashes (Simulation.borderFlash, GamePainter): a death's
// alarm, the white of a quarter or an item taken; else the border's own.
static u8 s_Border = 0xFF;

static void Border()
{
	u8 c = BORDER_IDLE;
	if (BORDER_FLASHES)
	{
		if (g_ValeMode == MODE_DYING && g_ValeModeTick < BORDER_ALARM_TICKS
			&& g_ValeModeTick % BORDER_ALARM_PERIOD < BORDER_ALARM_ON)
			c = BORDER_ALARM;
		else if (g_QuestFlash > 0)
			c = BORDER_PICKUP;
	}
	if (c != s_Border)
	{
		s_Border = c;
		VDP_SetColor(c);
	}
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
		// A tribesman level with a quarter, lit in yellow (CreaturePainter.paintBeast).
		u8 f = g_BHint[b] && v < 4 ? g_HintFrame[v] : g_BeastFrame[k * 6 + v];
		PutFrame(SPR_SMALL + n, f, 0, FP_PX(g_Beast[b].X) - s_RoomX, FP_PX(g_Beast[b].Y) - s_RoomY);
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
			u8 v = ((flier || g_CMoveTicks[i] > 0) ? WalkPhase() : 0) + (g_CFace[i] < 0 ? 2 : 0);
			f = g_CreatureFrame[s * 4 + v];
			if (g_GameOn && g_CHurt[i] > 0 && !(g_CHurt[i] & 1) && s < CREATURES)
			{
				f = g_CreatureFlash[s * 4 + v];   // hurt: white on alternate ticks
				tint = FLASH_COLOUR;
			}
		}
		PutFrame(k, f, tint, FP_PX(g_CX[i]) - s_RoomX, FP_PX(g_CY[i]) - s_RoomY);
	}
	// Under the creatures, in the slots they leave: the quarter on its
	// pedestal, the items and treasures (loot before creatures), the orchid
	// (under everything that moves).
	if (g_QuestOn && g_QuestLair >= 0 && !(g_QuestTaken & (1 << g_QuestLair)) && m < SPR_SMALL)
	{
		u8 k = g_QuestSet * LAIRS + g_QuestLair;
		PutFrame(m++, g_PieceFrame[g_QuestLair], 0, g_LairPedX[k], g_LairPedY[k]);
	}
	if (g_GameOn)
		m = DrawLoot(m);
	// A taken quarter flies to its place on the panel (QuestPainter.paintFlight):
	// from where it lay to the slot's middle, a straight line over the flight's
	// ticks. Sprites stop at the playfield's foot, so it goes out of sight there.
	if (g_QuestOn && g_QuestFly > 0 && m < SPR_SMALL)
	{
		u8 slot = 0;
		while (!(g_QuestFlyMask & (1 << slot)))
			slot++;
		u8 lair = 0;
		while (lair < LAIRS && g_LairMask[lair] != g_QuestFlyMask)
			lair++;
		if (lair < LAIRS)
		{
			i16 done = AMULET_FLY_TICKS - g_QuestFly;
			i16 fx = g_QuestFlyX - s_RoomX;
			i16 fy = g_QuestFlyY - s_RoomY;
			i16 tx = (256 - 16) / 2 + (slot & 1) * 8 + 4;
			i16 ty = PLAYFIELD_H + 10 + (slot >> 1) * 8 + 4;
			i16 x = fx + (tx - fx) * done / AMULET_FLY_TICKS;
			i16 y = fy + (ty - fy) * done / AMULET_FLY_TICKS;
			PutFrame(m++, g_PieceFrame[lair], 0, x, y + 8);   // its feet: the 16x16's foot
		}
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
	// PlayerPainter (M8): knocked down by a flower he sits, with no hand for a
	// blade; wearing an untouchable flower he is inked in its colour, flashing
	// back to himself every ORCHID_TINT_FLASH_TICKS for its last WARN ticks.
	u8 ink = 0xFF;
	if (!g_TestGallery && g_GameOn)
	{
		if (g_EffectKind == EFFECT_KNOCKDOWN)
		{
			f = PLAYER_FRAME_SIT + (g_ValeFacing >= FACE_SW ? 1 : 0);
			blade = -1;
		}
		if (g_EffectUntouchable
			&& !(g_EffectTicks <= ORCHID_TINT_WARN_TICKS && ((g_EffectTicks / ORCHID_TINT_FLASH_TICKS) & 1) == 0))
			ink = g_ValeInked[g_EffectOrchid];
	}
	s_Loaded = FALSE;
	u8 set = Sprites_Back();
	u16 key = f | ((u16)ink << 8);
	if (key != s_ValeFrame[set] || key != s_ValeBuf)
	{
		s_Loaded = TRUE;
		g_ValeInk = ink;
		Data_LoadValeFrame(f, g_SpritePatternHigh, Sprites_PatternAddress(SLOT_VALE), Sprites_Colours(SLOT_VALE));
		Sprites_Changed(SLOT_VALE);
		s_ValeFrame[set] = key;
		s_ValeBuf = key;
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
		{
			Data_FrameOrigin(g_TestFrame[i]);
			PutFrame(i, g_TestFrame[i], 0, g_TestSpriteX[i] + g_FrameOX, g_TestSpriteY[i] + g_FrameOY);
		}
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

// A new game of the whole jungle (Simulation's constructor): seeded with
// seed (the Java game's long, little-endian), Vale at (x, y); its room shown.
static void NewGame(const u8* seed, i32 x, i32 y)
{
	for (u8 i = 0; i < 8; i++)
		g_TestSeed[i] = seed[i];
	g_ValeOpen = 0;
	g_JungleOn = 1;
	g_QuestOn = 1;
	g_GameOn = 1;
	g_ValeEndless = 0;
	g_EffectKind = EFFECT_NONE;   // nothing left over from the last game
	g_EffectScale = 256;
	g_EffectUntouchable = 0;
	g_OrchidHere = 0;
	Rng_Seed(g_TestSeed);
	JUNGLE(Jungle_NewGame);
	QUEST(Quest_NewGame);
	GAME(Game_NewGame);
	EnterRoom(x, y);
}

// The demo's game (shell.json's attract replay): Hard, its seed, its room.
static u8 s_DemoSeed[8] = { DEMO_SEED_BYTES };

// The test hooks that stop the shell where it is (the sound's, the sprites'
// gallery, the blitter's bench): set once one is used.
static bool s_Frozen = FALSE;
static bool s_SpritesOn = TRUE;

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
	Data_LoadShellImages();
	Panel_Init();
	Vale_Init();
	s_Skip60 = VDP_GetFrequency() == VDP_FREQ_60HZ;
	Sound_Init(s_Skip60);
	SHELL(Shell_Init);   // the hi-scores, and the title

	bool f5 = FALSE;
	bool f4 = FALSE;
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

		if (g_TestSound || g_TestGallery || g_BenchGo)
		{
			if (!s_Frozen)
				Sound_Tune(0xFF);
			s_Frozen = TRUE;
		}
		u8 ticks = g_TestGallery ? 0 : TicksIn(frames);
		if (ticks > MAX_CATCHUP_TICKS)
			ticks = MAX_CATCHUP_TICKS;
		bool flipped = FALSE;
		if (g_TestMode)
		{
			while (ticks--)
				if (Tick())
				{
					Flip();
					flipped = TRUE;
					last = g_JIFFY;   // the frames the drawing took are not late
					break;
				}
		}
		else if (!s_Frozen)
		{
			// The shell (M10): a tick of it, and of the game when it says.
			while (ticks--)
			{
				ShellInput();
				SHELL(Shell_Tick);
				if (g_ShellDo & (DO_NEW_GAME | DO_DEMO))
				{
					if (g_TestRecord == 1)
					{
						g_Easy = 0;
						NewGame(g_TestSeed, g_TestX, g_TestY);
					}
					else if (g_ShellDo & DO_DEMO)
					{
						g_Easy = 0;
						NewGame(s_DemoSeed, DEMO_START_X_FP, DEMO_START_Y_FP);
					}
					else
					{
						// Seeded from the clock: how long the title was up.
						u8 seed[8] = { (u8)g_JIFFY, (u8)(g_JIFFY >> 8), (u8)g_Frames, (u8)(g_Frames >> 8), 0, 0, 0, 0 };
						NewGame(seed, PLAYER_START_X_FP, PLAYER_START_Y_FP);
					}
					SHELL(Shell_Shown);
					flipped = TRUE;
					last = g_JIFFY;
					break;
				}
				if (!(g_ShellDo & DO_TICK))
					continue;
				if (g_ShellDo & DO_CARRY_ON)
					Vale_FireReleased();
				bool flip = Tick();
				if (!g_TestRecord)   // a recording is the game alone, as the demo plays it back
					SHELL(Shell_AfterTick);
				if (flip)
				{
					Flip();
					flipped = TRUE;
					last = g_JIFFY;
					break;
				}
				if (g_Shell != SH_PLAYING && g_Shell != SH_ATTRACT)
					break;
			}
		}
		// The game on screen: its sprites, its panel, its border.
		bool game = g_TestMode || s_Frozen || g_Shell == SH_PLAYING || g_Shell == SH_ATTRACT;
		if (game != s_SpritesOn)
		{
			s_SpritesOn = game;
			VDP_EnableSprite(game);
		}
		// What these ticks left, into the back set, for the next frame
		// (a flip has drawn it already).
		if (game && !flipped)
			DrawSprites();
		// The panel, but not in a frame that loaded sprites: those are the busy ones.
		if ((g_TestMode || s_Frozen || g_Shell == SH_PLAYING) && !s_Loaded)
			Panel_Update(g_PageShown);
		if (game && g_Shell != SH_ATTRACT)
			Border();
		else if (s_Border != BORDER_IDLE)
		{
			s_Border = BORDER_IDLE;
			VDP_SetColor(BORDER_IDLE);
		}
		Test_FrameEnd();

		if (g_BenchGo)
		{
			Bench();
			g_BenchGo = 0;
			last = g_JIFFY;
		}
		if (g_TestStart)
		{
			if (!g_TestMode)
			{
				// A test takes over: the game as the shell left it, gone; the jungle's palette.
				Data_Palette(FALSE);
				Sound_Tune(0xFF);
			}
			g_TestMode = 1;
			g_Shell = SH_PLAYING;
			g_ValeOpen = g_TestOpen;
			g_JungleOn = g_TestJungle;
			g_QuestOn = g_TestJungle && g_TestQuest;
			g_GameOn = g_QuestOn && g_TestGame;
			g_Easy = g_GameOn && g_TestEasy;
			g_LivesStart = g_Easy ? EASY_LIVES : PLAYER_LIVES_START;
			g_ValeEndless = g_TestEndless;
			g_EffectKind = EFFECT_NONE;   // nothing left over from the last test's game
			g_EffectScale = 256;
			g_EffectUntouchable = 0;
			g_OrchidHere = 0;
			if (g_JungleOn)
			{
				Rng_Seed(g_TestSeed);
				JUNGLE(Jungle_NewGame);
				if (g_QuestOn)
					QUEST(Quest_NewGame);
				if (g_GameOn)
					GAME(Game_NewGame);
			}
			EnterRoom(g_TestX, g_TestY);
			g_TestStart = 0;
			g_LateFrames = 0;
			last = g_JIFFY;
		}
		else if (g_TestSound)
		{
			u8 id = g_TestSound - 1;
			g_TestSound = 0;
			if (id == 0xFE)
				Sound_Tune(0xFF);
			else if (id < SFX_COUNT)
				Sound_Play(id);
			else
				Sound_Tune(id);
		}
		else if (Pressed(KEY_F4, &f4))
			g_PanelDebug ^= 1;
		else if (Pressed(KEY_F5, &f5) && (g_TestMode || g_Shell == SH_PLAYING))
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
