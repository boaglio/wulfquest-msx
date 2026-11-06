// Wulf Quest for MSX2. M5: Ranger Vale walks the whole map, in colour
// (AGENTS.md §10, M3 to M5).
//
// The game loop: 50 ticks a second, driven by the vertical interrupt; on a
// 60 Hz machine every sixth frame has no tick, so the game runs at the same
// speed (§6). A frame starts by showing Vale as the last tick left him, four
// layered hardware sprites and his blade (sprites.c), while the screen is
// blank; then its ticks run,
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
#include "player_hand.h"
#define BLADE_TABLES
#include "blade.h"

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
static u8 s_ValeFrame = 0xFF;

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
	Halt();
	VDP_SetPage(hidden);
	s_Shown = hidden;
	DrawSprites();
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
u8  g_TestFrame[SPR_SMALL] = { 0 };
i16 g_TestSpriteX[SPR_SMALL] = { 0 };
i16 g_TestSpriteY[SPR_SMALL] = { 0 };

static i8 s_BladeShape = -1;
static bool s_Loaded = FALSE;   // DrawSprites loaded a frame or a shape this frame
static u8 s_SmallFrame[SPR_SMALL] = { 0 };

// Vale (four layered sprites, PlayerPainter's frame), his blade out of his
// hand while it is out, and whatever else is to be shown, in a frame's blank.
static void DrawSprites()
{
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
	if (f != s_ValeFrame)
	{
		s_Loaded = TRUE;
		Data_LoadValeFrame(f, g_SpritePatternHigh, Sprites_PatternAddress(SLOT_VALE), Sprites_Colours(SLOT_VALE));
		Sprites_Changed(SLOT_VALE);
		s_ValeFrame = f;
	}
	Sprites_Begin();
	if (blade < 0 || g_PlayerHand[f * 2] < 0)
		Sprites_Put(SLOT_BLADE, -128, -128);   // off the screen: held below it
	else
	{
		if (blade != s_BladeShape)
		{
			s_Loaded = TRUE;
			Data_LoadBlade((u8)blade, g_SpritePatternHigh, Sprites_PatternAddress(SLOT_BLADE));
			s_BladeShape = blade;
		}
		const i8* at = g_BladeAt + ((u8)blade << 2);
		i8 hx = g_PlayerHand[f * 2];
		i8 hy = g_PlayerHand[f * 2 + 1];
		Sprites_SetOffset(SLOT_BLADE, 0, hx + at[0], hy + at[1]);
		Sprites_SetOffset(SLOT_BLADE, 1, hx + at[2], hy + at[3]);
		Sprites_Put(SLOT_BLADE, sx, sy);
	}
	Sprites_Put(SLOT_VALE, sx, sy);
	if (g_TestGallery)
	{
		for (u8 i = 0; i < g_TestCount; i++)
		{
			if (g_TestFrame[i] != s_SmallFrame[i])
			{
				Data_LoadSpriteFrame(g_TestFrame[i], g_SpritePatternHigh, Sprites_PatternAddress(SLOT_SMALL + i),
					Sprites_Colours(SLOT_SMALL + i));
				Sprites_Changed(SLOT_SMALL + i);
				s_SmallFrame[i] = g_TestFrame[i];
			}
			Sprites_Put(SLOT_SMALL + i, g_TestSpriteX[i], g_TestSpriteY[i]);
		}
	}
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
	for (u8 i = 0; i < SPR_SMALL; i++)
		s_SmallFrame[i] = 0xFF;
}

void main()
{
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

		// First what the last ticks did, while the screen is blank.
		DrawSprites();
		Test_Shown();

		u8 ticks = g_TestGallery ? 0 : TicksIn(frames);
		if (ticks > MAX_CATCHUP_TICKS)
			ticks = MAX_CATCHUP_TICKS;
		while (ticks--)
			if (Tick())
			{
				Flip();
				last = g_JIFFY;   // the frames the drawing took are not late
				break;
			}
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
