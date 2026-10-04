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

// SdlKeyTable.h: SDL3 scancodes to the engine's DirectInput key codes, in one table (C3, decision 3).
//
// The engine, its bindings (CommandMap*.ini) and every key test in its code speak DirectInput's DIK
// codes: positions on a PC keyboard.  SDL's scancodes are positions too - USB HID usage IDs - and the
// same on every platform SDL runs on, so one table serves macOS and Linux alike.  Each of the 107 DIK
// codes the engine carries (GameClient/DIKeyCodes.h) is either reached from some scancode here or on
// the unreachable list with the reason; test_sdl_input fails on any that is neither.
//
// Keys that reach nothing, on purpose: the GUI keys (Command on a Mac, the Windows keys on a PC - the
// engine has no DIK code for either, and Command belongs to the system: Cmd+Q comes as SDL_EVENT_QUIT,
// the path Alt+F4 takes), Pause (not among the engine's codes), F13 and up, the keypad '=' of a Mac
// keyboard, and the application key.

#pragma once

#ifndef __SDLKEYTABLE_H
#define __SDLKEYTABLE_H

#include "Lib/BaseType.h"

#include <SDL3/SDL_scancode.h>

struct SdlKeyMapping
{
	SDL_Scancode scancode;
	UnsignedByte dik;						///< GameClient/DIKeyCodes.h
};

struct SdlUnreachableKey
{
	UnsignedByte dik;
	const char *why;
};

extern const SdlKeyMapping SdlKeyTable[];
extern const Int SdlKeyTableSize;
extern const SdlUnreachableKey SdlUnreachableKeys[];
extern const Int SdlUnreachableKeysSize;

/// The DIK code for a scancode; 0 (KEY_NONE) for a key the engine does not have
UnsignedByte SdlKeyTable_dikFor( SDL_Scancode scancode );

#endif // __SDLKEYTABLE_H
