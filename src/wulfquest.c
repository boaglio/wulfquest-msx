// Wulf Quest for MSX2. M0: the ROM boots, sets SCREEN 5 and the game's
// palette, says it is there, and waits for Space or Enter (AGENTS.md §10, M0).

#include "msxgl.h"
#include "palette.h"

#include "font/font_mgl_sample6.h"

void main()
{
	VDP_SetMode(VDP_MODE_SCREEN5);
	VDP_EnableVBlank(TRUE);
	VDP_ClearVRAM();
	VDP_SetPalette(g_Palette);
	VDP_SetColor(0);

	Print_SetBitmapFont(g_Font_MGL_Sample6);
	Print_SetColor(15, 0);
	Print_SetPosition(8, 8);
	Print_DrawText("WULF QUEST MSX2");
	Print_SetPosition(8, 24);
	Print_DrawText("PRESS SPACE");

	while (!Keyboard_IsKeyPressed(KEY_SPACE) && !Keyboard_IsKeyPressed(KEY_RET))
		Halt();

	Print_SetPosition(8, 40);
	Print_DrawText("OK");
	while (TRUE)
		Halt();
}
