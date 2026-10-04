/*
**	Copyright 2026 İlyas Akın
**	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// SdlKeyTable.cpp: see SdlKeyTable.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/DIKeyCodes.h"
#include "SdlDevice/GameClient/SdlKeyTable.h"

// In DIK order, which is the PC keyboard's scan-code order.
const SdlKeyMapping SdlKeyTable[] =
{
	{ SDL_SCANCODE_ESCAPE,					DIK_ESCAPE },
	{ SDL_SCANCODE_1,								DIK_1 },
	{ SDL_SCANCODE_2,								DIK_2 },
	{ SDL_SCANCODE_3,								DIK_3 },
	{ SDL_SCANCODE_4,								DIK_4 },
	{ SDL_SCANCODE_5,								DIK_5 },
	{ SDL_SCANCODE_6,								DIK_6 },
	{ SDL_SCANCODE_7,								DIK_7 },
	{ SDL_SCANCODE_8,								DIK_8 },
	{ SDL_SCANCODE_9,								DIK_9 },
	{ SDL_SCANCODE_0,								DIK_0 },
	{ SDL_SCANCODE_MINUS,						DIK_MINUS },
	{ SDL_SCANCODE_EQUALS,					DIK_EQUALS },
	{ SDL_SCANCODE_BACKSPACE,				DIK_BACK },				// a Mac's "delete" key
	{ SDL_SCANCODE_TAB,							DIK_TAB },
	{ SDL_SCANCODE_Q,								DIK_Q },
	{ SDL_SCANCODE_W,								DIK_W },
	{ SDL_SCANCODE_E,								DIK_E },
	{ SDL_SCANCODE_R,								DIK_R },
	{ SDL_SCANCODE_T,								DIK_T },
	{ SDL_SCANCODE_Y,								DIK_Y },
	{ SDL_SCANCODE_U,								DIK_U },
	{ SDL_SCANCODE_I,								DIK_I },
	{ SDL_SCANCODE_O,								DIK_O },
	{ SDL_SCANCODE_P,								DIK_P },
	{ SDL_SCANCODE_LEFTBRACKET,			DIK_LBRACKET },
	{ SDL_SCANCODE_RIGHTBRACKET,		DIK_RBRACKET },
	{ SDL_SCANCODE_RETURN,					DIK_RETURN },
	{ SDL_SCANCODE_LCTRL,						DIK_LCONTROL },		// Control, on a Mac too: the game's Ctrl bindings stay there
	{ SDL_SCANCODE_A,								DIK_A },
	{ SDL_SCANCODE_S,								DIK_S },
	{ SDL_SCANCODE_D,								DIK_D },
	{ SDL_SCANCODE_F,								DIK_F },
	{ SDL_SCANCODE_G,								DIK_G },
	{ SDL_SCANCODE_H,								DIK_H },
	{ SDL_SCANCODE_J,								DIK_J },
	{ SDL_SCANCODE_K,								DIK_K },
	{ SDL_SCANCODE_L,								DIK_L },
	{ SDL_SCANCODE_SEMICOLON,				DIK_SEMICOLON },
	{ SDL_SCANCODE_APOSTROPHE,			DIK_APOSTROPHE },
	{ SDL_SCANCODE_GRAVE,						DIK_GRAVE },
	{ SDL_SCANCODE_LSHIFT,					DIK_LSHIFT },
	{ SDL_SCANCODE_BACKSLASH,				DIK_BACKSLASH },
	{ SDL_SCANCODE_Z,								DIK_Z },
	{ SDL_SCANCODE_X,								DIK_X },
	{ SDL_SCANCODE_C,								DIK_C },
	{ SDL_SCANCODE_V,								DIK_V },
	{ SDL_SCANCODE_B,								DIK_B },
	{ SDL_SCANCODE_N,								DIK_N },
	{ SDL_SCANCODE_M,								DIK_M },
	{ SDL_SCANCODE_COMMA,						DIK_COMMA },
	{ SDL_SCANCODE_PERIOD,					DIK_PERIOD },
	{ SDL_SCANCODE_SLASH,						DIK_SLASH },
	{ SDL_SCANCODE_RSHIFT,					DIK_RSHIFT },
	{ SDL_SCANCODE_KP_MULTIPLY,			DIK_NUMPADSTAR },
	{ SDL_SCANCODE_LALT,						DIK_LALT },				// Option on a Mac
	{ SDL_SCANCODE_SPACE,						DIK_SPACE },
	{ SDL_SCANCODE_CAPSLOCK,				DIK_CAPSLOCK },
	{ SDL_SCANCODE_F1,							DIK_F1 },
	{ SDL_SCANCODE_F2,							DIK_F2 },
	{ SDL_SCANCODE_F3,							DIK_F3 },
	{ SDL_SCANCODE_F4,							DIK_F4 },
	{ SDL_SCANCODE_F5,							DIK_F5 },
	{ SDL_SCANCODE_F6,							DIK_F6 },
	{ SDL_SCANCODE_F7,							DIK_F7 },
	{ SDL_SCANCODE_F8,							DIK_F8 },
	{ SDL_SCANCODE_F9,							DIK_F9 },
	{ SDL_SCANCODE_F10,							DIK_F10 },
	{ SDL_SCANCODE_NUMLOCKCLEAR,		DIK_NUMLOCK },		// a Mac keypad's Clear key
	{ SDL_SCANCODE_SCROLLLOCK,			DIK_SCROLL },
	{ SDL_SCANCODE_KP_7,						DIK_NUMPAD7 },
	{ SDL_SCANCODE_KP_8,						DIK_NUMPAD8 },
	{ SDL_SCANCODE_KP_9,						DIK_NUMPAD9 },
	{ SDL_SCANCODE_KP_MINUS,				DIK_NUMPADMINUS },
	{ SDL_SCANCODE_KP_4,						DIK_NUMPAD4 },
	{ SDL_SCANCODE_KP_5,						DIK_NUMPAD5 },
	{ SDL_SCANCODE_KP_6,						DIK_NUMPAD6 },
	{ SDL_SCANCODE_KP_PLUS,					DIK_NUMPADPLUS },
	{ SDL_SCANCODE_KP_1,						DIK_NUMPAD1 },
	{ SDL_SCANCODE_KP_2,						DIK_NUMPAD2 },
	{ SDL_SCANCODE_KP_3,						DIK_NUMPAD3 },
	{ SDL_SCANCODE_KP_0,						DIK_NUMPAD0 },
	{ SDL_SCANCODE_KP_PERIOD,				DIK_NUMPADPERIOD },
	{ SDL_SCANCODE_NONUSBACKSLASH,	DIK_OEM_102 },		// the ISO key beside the left shift
	{ SDL_SCANCODE_F11,							DIK_F11 },
	{ SDL_SCANCODE_F12,							DIK_F12 },
	{ SDL_SCANCODE_INTERNATIONAL2,	DIK_KANA },				// JIS katakana/hiragana
	{ SDL_SCANCODE_LANG1,						DIK_KANA },				// a Mac JIS keyboard's kana key
	{ SDL_SCANCODE_INTERNATIONAL4,	DIK_CONVERT },		// JIS henkan
	{ SDL_SCANCODE_INTERNATIONAL5,	DIK_NOCONVERT },	// JIS muhenkan
	{ SDL_SCANCODE_INTERNATIONAL3,	DIK_YEN },
	{ SDL_SCANCODE_KP_ENTER,				DIK_NUMPADENTER },	// fn+Return on a Mac laptop
	{ SDL_SCANCODE_RCTRL,						DIK_RCONTROL },
	{ SDL_SCANCODE_KP_DIVIDE,				DIK_NUMPADSLASH },
	{ SDL_SCANCODE_PRINTSCREEN,			DIK_SYSRQ },
	{ SDL_SCANCODE_RALT,						DIK_RALT },
	{ SDL_SCANCODE_HOME,						DIK_HOME },
	{ SDL_SCANCODE_UP,							DIK_UPARROW },
	{ SDL_SCANCODE_PAGEUP,					DIK_PGUP },
	{ SDL_SCANCODE_LEFT,						DIK_LEFTARROW },
	{ SDL_SCANCODE_RIGHT,						DIK_RIGHTARROW },
	{ SDL_SCANCODE_END,							DIK_END },
	{ SDL_SCANCODE_DOWN,						DIK_DOWNARROW },
	{ SDL_SCANCODE_PAGEDOWN,				DIK_PGDN },
	{ SDL_SCANCODE_INSERT,					DIK_INSERT },			// an old Apple extended keyboard's Help key
	{ SDL_SCANCODE_DELETE,					DIK_DELETE },			// forward delete; fn+delete on a Mac laptop
};
const Int SdlKeyTableSize = sizeof( SdlKeyTable ) / sizeof( SdlKeyTable[0] );

const SdlUnreachableKey SdlUnreachableKeys[] =
{
	{ DIK_CIRCUMFLEX,	"NEC PC-98's circumflex key: no USB HID usage" },
	{ DIK_KANJI,			"the Japanese AX keyboard's kanji key: no USB HID usage (a 106-key board's hankaku/zenkaku key is GRAVE's position)" },
};
const Int SdlUnreachableKeysSize = sizeof( SdlUnreachableKeys ) / sizeof( SdlUnreachableKeys[0] );

UnsignedByte SdlKeyTable_dikFor( SDL_Scancode scancode )
{
	static UnsignedByte byScancode[ SDL_SCANCODE_COUNT ];
	static Bool built = FALSE;
	if (!built)
	{
		for (Int i = 0; i < SdlKeyTableSize; ++i)
			byScancode[ SdlKeyTable[i].scancode ] = SdlKeyTable[i].dik;
		built = TRUE;
	}
	if ((Int)scancode <= 0 || (Int)scancode >= SDL_SCANCODE_COUNT)
		return 0;
	return byScancode[ scancode ];
}
