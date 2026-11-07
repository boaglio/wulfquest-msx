// Wulf Quest for MSX2: the original's Wulf (AGENTS.md §10, M7; the Java
// game's §13.8, Simulation.prowl): one, along its row of rooms only, round
// the map's ends; it turns towards Vale once as he comes onto its row, and a
// sabre held towards it as it comes makes it sit.
//
// Part of quest.c's banked unit (it includes this file): the same rules.

u8  g_WulfState = WULF_ABSENT;
u8  g_WulfStateTick = 0;
u8  g_WulfAt = 0;
i8  g_WulfHeading = -1;
i16 g_WulfSpeed = 0;
u8  g_WulfLatch = LATCH_FREE;
u8  g_WulfHere = 0;
i32 g_WulfX = 0;
i32 g_WulfY = 0;
u16 g_WulfMoveTicks = 0;

#if (GRID_W * PLAYFIELD_W) != 4096 || PLAYFIELD_W != 256
#error "the Wulf wraps round the map's width as 8.8 with a mask, its column x's third byte"
#endif

// head: its heading, and its face (Creature.direction).
static void Wulf_Head(i8 heading)
{
	g_WulfHeading = heading;
}

// placeProwler: where the original's actor table puts it.
static void Wulf_Place()
{
	g_WulfState = WULF_PROWLING;
	g_WulfStateTick = 0;
	g_WulfAt = WULF_ROOM;
	g_WulfSpeed = g_Easy ? EASY_WULF_SPEED_FP : WULF_SPEED_FP;   // Simulation.paced
	g_WulfLatch = LATCH_FREE;
	g_WulfHere = 0;
	g_WulfMoveTicks = 0;
	g_WulfX = (i32)((WULF_ROOM % GRID_W) * PLAYFIELD_W + WULF_X) << 8;
	g_WulfY = (i32)((WULF_ROOM / GRID_W) * PLAYFIELD_H + WULF_Y) << 8;
	Wulf_Head(WULF_HEADING);
}

static i16 WulfCentreX()
{
	return FP_PX(g_WulfX) + (WULF_BOX_X + WULF_BOX_W / 2);
}

// heldOff: he is fighting, it is coming at him, and he faces it.
static bool HeldOff()
{
	if (g_SabreW == 0 || FacingDX() != -g_WulfHeading)
		return FALSE;
	i16 gap = ValeCentreX() - WulfCentreX();
	return g_WulfHeading < 0 ? gap < 0 : gap >= 0;
}

// stepProwler: along the row; blocked, it turns; past an end of the map, it
// comes in at the other.
static void StepProwler()
{
	g_MoveWorld = MOVE_MAP;
	g_MovePos = g_WulfX;
	g_MoveDelta = g_WulfHeading < 0 ? -g_WulfSpeed : g_WulfSpeed;
	g_MoveOther = FP_PX(g_WulfY);
	g_MoveOff = WULF_BOX_X;
	g_MoveSize = WULF_BOX_W;
	g_MoveOffO = WULF_BOX_Y;
	g_MoveSizeO = WULF_BOX_H;
	g_MoveAlongX = 1;
	Move_Axis();
	if (g_MovePos == g_WulfX)
	{
		Wulf_Head(-g_WulfHeading);
		g_WulfMoveTicks = 0;
		return;
	}
	g_WulfX = g_MovePos & 0x000FFFFF;   // floorMod by the map's width, 2^20 in 8.8
	g_WulfMoveTicks++;
	g_WulfAt = (g_WulfAt & ~(GRID_W - 1)) | (((u8*)&g_WulfX)[2] & (GRID_W - 1));   // its column: x >> 16
}

void Wulf_Notice()
{
	g_WulfHere = g_WulfAt == g_ValeRoom;
}

void Wulf_Prowl()
{
	if (g_WulfState == WULF_PROWLING)
	{
		if (g_ValeRoom / GRID_W != g_WulfAt / GRID_W)
			g_WulfLatch = LATCH_FREE;
		else if (g_WulfLatch == LATCH_FREE)
		{
			// By room along the row, or within the room by which side he is on.
			g_WulfLatch = LATCH_STEERED;
			g_WulfSpeed = g_Easy ? EASY_WULF_SPEED_FP : WULF_SPEED_FP;
			u8 vc = g_ValeRoom % GRID_W;
			u8 wc = g_WulfAt % GRID_W;
			Wulf_Head(vc != wc ? (vc > wc ? 1 : -1) : ValeCentreX() < WulfCentreX() ? -1 : 1);
		}
		if (g_WulfHere && g_WulfLatch != LATCH_SPENT && HeldOff())
		{
			g_WulfState = WULF_SITTING;
			g_WulfStateTick = 0;
			g_WulfMoveTicks = 0;
		}
	}
	switch (g_WulfState)
	{
	case WULF_SITTING:
		// It sits for as long as the sabre is held towards it, then a while longer.
		if (g_WulfHere && g_SabreW != 0 && FacingDX() == -g_WulfHeading)
			g_WulfStateTick = 0;
		else if (++g_WulfStateTick >= WULF_SIT_RELEASE_TICKS)
		{
			g_WulfState = WULF_RISING;
			g_WulfStateTick = 0;
		}
		break;
	case WULF_RISING:
		StepProwler();
		if (++g_WulfStateTick >= WULF_RISE_TICKS)
		{
			// Up and away, faster, done with turning and sitting until he leaves the row.
			g_WulfState = WULF_PROWLING;
			g_WulfStateTick = 0;
			g_WulfLatch = LATCH_SPENT;
			g_WulfSpeed = g_Easy ? EASY_WULF_RISEN_SPEED_FP : WULF_RISEN_SPEED_FP;
		}
		break;
	default:
		StepProwler();
	}
	Wulf_Notice();
}

// getUp: a Wulf about is gone. The original's prowler too: it walks on, unseen
// and harmless, the Java game's way (Simulation.getUp).
void Wulf_GetUp()
{
	if (g_WulfState != WULF_ABSENT)
	{
		g_WulfState = WULF_ABSENT;
		g_WulfStateTick = 0;
	}
}
