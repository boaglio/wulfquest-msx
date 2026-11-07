// Wulf Quest for MSX2: the sound on the PSG (AGENTS.md §10, M9).
//
// The Java game's Mixer, on the PSG's three channels: two voices for the
// effects (channels A and B), each taken by an effect as the Mixer takes one
// (a free voice, else the least important busy one if the new is more
// important), and the tune on channel C, which keeps time but is silent
// while voice A plays, as the Mixer's tune is while its channel zero is busy.
// A sound is the converter's entries (Sound.java), a frame of the interrupt
// each: a period, noise or tone, a volume, for so many frames.
//
// Segment 0: Sound_Frame runs in the interrupt, which may come with any
// segment in bank 1; it maps the sounds' blob and puts back the one MSXgl
// records (g_Bank1Segment, which it sets before the mapper).

#include "sound.h"
#include "data.h"
#include "rom.h"

__sfr __at(0xA0) s_PsgAddr;
__sfr __at(0xA1) s_PsgData;

#define VOICES		3            // the effects' two, the tune's
#define NONE		0xFF
#define PENDING		8

u8 g_SoundOn = 1;

typedef struct
{
	u8 id;
	u8 priority;
	u16 at;        // the next entry, in the blob
	u16 left;      // entries left
	u8 frames;     // frames left of the current one
	u8 lo;
	u8 hi;         // bit 7: noise
	u8 vol;
} Voice;

static Voice s_Voice[VOICES];
static u8 s_Pending[PENDING];
static volatile u8 s_PendHead = 0;
static volatile u8 s_PendTail = 0;
static u8 s_LoopOn[SOUNDS];
static volatile u8 s_TuneWanted = NONE;
static u8 s_Seg = SOUND50_BIN_SEG;
static u16 s_Rel = SOUND50_BIN_REL;
static const u8 c_Priority[SFX_COUNT] = { SFX_PRIORITIES };
static const u8 c_Loop[SOUNDS] = { SOUND_LOOPS };

#if SOUND_VOICES != 2
#error "the PSG gives the effects two channels and the tune the third"
#endif

#define Psg(reg, v)	do { s_PsgAddr = (reg); s_PsgData = (v); } while (0)

// The voice being stepped, copied out of s_Voice (SDCC is slow through a
// struct's pointer): Load, Step, Store.
static Voice s_V;

// A voice's bytes (no library: SDCC copies a struct with memcpy, in segment 1).
#define VOICE_BYTES	10
typedef char c_VoiceBytes[sizeof(Voice) == VOICE_BYTES ? 1 : -1];

static void Copy(Voice* d, const Voice* src) __naked
{
	d;     // hl
	src;   // de
	__asm
		ex	de, hl
		ld	bc, #VOICE_BYTES
		ldir
		ret
	__endasm;
}


void Sound_Init(u8 hz60)
{
	s_Seg = hz60 ? SOUND60_BIN_SEG : SOUND50_BIN_SEG;
	s_Rel = hz60 ? SOUND60_BIN_REL : SOUND50_BIN_REL;
	for (u8 v = 0; v < VOICES; v++)
		s_Voice[v].id = NONE;
	for (u8 i = 0; i < SOUNDS; i++)
		s_LoopOn[i] = 0;
	s_PendHead = s_PendTail = 0;
	s_TuneWanted = NONE;
	for (u8 r = 8; r <= 10; r++)
		Psg(r, 0);
	Psg(7, 0xBF);   // all off; port B an output, port A an input (bits 7, 6)
}

void Sound_Play(u8 id)
{
	if (!g_SoundOn)
		return;
	s_Pending[s_PendTail & (PENDING - 1)] = id;
	s_PendTail++;   // after the id: the interrupt reads up to the tail
}

void Sound_Loop(u8 id, u8 on)
{
	if (on)
	{
		if (!s_LoopOn[id] && g_SoundOn)
		{
			s_LoopOn[id] = 1;
			Sound_Play(id);
		}
	}
	else
		s_LoopOn[id] = 0;
}

void Sound_Tune(u8 id)
{
	s_TuneWanted = g_SoundOn ? id : NONE;
}

// A sound's start, on s_V: its entries' place and count, from the blob's index.
static void Start(u8 id)
{
	const u8* ix = (const u8*)(ROM_DATA_ADDR + s_Rel) + ((u16)id << 2);
	s_V.id = id;
	s_V.at = *(const u16*)ix;
	s_V.left = *(const u16*)(ix + 2);
	s_V.frames = 0;
	s_V.vol = 0;
}

// The next frame: a new entry when the last is done; at the end, again (a
// loop still wanted) or free.
static void Step()
{
	if (s_V.id == NONE)
		return;
	if (s_V.frames == 0)
	{
		if (s_V.left == 0)
		{
			u8 id = s_V.id;
			bool again = c_Loop[id] && (id >= SFX_COUNT ? s_TuneWanted == id : s_LoopOn[id]);
			if (!again)
			{
				if (id >= SFX_COUNT && s_TuneWanted == id)
					s_TuneWanted = NONE;   // a tune that has finished must not start again
				s_V.id = NONE;
				s_V.vol = 0;
				return;
			}
			Start(id);
		}
		const u8* e = (const u8*)(ROM_DATA_ADDR + s_Rel) + s_V.at;
		s_V.lo = e[0];
		s_V.hi = e[1];
		s_V.vol = e[2];
		s_V.frames = e[3];
		s_V.at += SOUND_ENTRY_BYTES;
		s_V.left--;
	}
	s_V.frames--;
}

void Sound_Frame()
{
	u8 keep = g_Bank0Segment[1];
	*(volatile u8*)ADDR_BANK_1 = s_Seg;   // mapped for the reading; not recorded
	// Mixer.startPending: each effect asked for, onto a voice.
	while (s_PendHead != s_PendTail)
	{
		u8 id = s_Pending[s_PendHead & (PENDING - 1)];
		s_PendHead++;
		if (id >= SFX_COUNT)
			continue;
		Voice* pick = 0;
		for (u8 i = 0; i < 2 && !pick; i++)
			if (s_Voice[i].id == NONE)
				pick = &s_Voice[i];
		if (!pick)
		{
			Voice* weakest = &s_Voice[0];
			if (s_Voice[1].priority < weakest->priority)
				weakest = &s_Voice[1];
			if (c_Priority[id] > weakest->priority)
				pick = weakest;
		}
		if (pick)
		{
			Start(id);
			s_V.priority = c_Priority[id];
			Copy(pick, &s_V);
		}
	}
	// Mixer.followTune.
	if (s_TuneWanted != s_Voice[2].id)
	{
		if (s_TuneWanted == NONE)
		{
			s_Voice[2].id = NONE;
			s_Voice[2].vol = 0;
		}
		else
		{
			Start(s_TuneWanted);
			Copy(&s_Voice[2], &s_V);
		}
	}
	for (u8 i = 0; i < VOICES; i++)
	{
		if (s_Voice[i].id == NONE)
			continue;
		Copy(&s_V, &s_Voice[i]);
		Step();
		Copy(&s_Voice[i], &s_V);
	}
	*(volatile u8*)ADDR_BANK_1 = keep;
	// To the PSG: periods, volumes, the mixer (tone or noise each channel).
	u8 mixer = 0x80 | 0x3F;   // all off; port B an output
	for (u8 i = 0; i < VOICES; i++)
	{
		Voice* v = &s_Voice[i];
		u8 vol = v->id == NONE ? 0 : v->vol;
		if (i == 2 && s_Voice[0].id != NONE)
			vol = 0;   // a stolen channel silences the tune while it holds it
		if (vol)
		{
			if (v->hi & 0x80)
			{
				Psg(6, v->lo & 0x1F);
				mixer &= ~(8 << i);
			}
			else
			{
				Psg(i * 2, v->lo);
				Psg(i * 2 + 1, v->hi & 0x0F);
				mixer &= ~(1 << i);
			}
		}
		Psg(8 + i, vol);
	}
	Psg(7, mixer);
}
