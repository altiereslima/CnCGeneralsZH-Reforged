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

// FILE: W3DWindowHooks.h /////////////////////////////////////////////////////////////////////////
// Desc:   Off Windows, how W3DDisplay dresses and sizes the game's window without knowing what it is.
///////////////////////////////////////////////////////////////////////////////////////////////////

/* On Windows W3DDisplay restyles and moves ApplicationHWnd itself when the window mode or the
	 resolution changes (applyWindowFrame, sizeWindowToClient), and dx8wrapper sizes it to the resolution
	 when the device is made.  Elsewhere the window belongs to the platform layer (C2's SdlGameEngine,
	 over SDL3), which sets these hooks; W3DDisplay calls them where Windows makes its own calls.  NULL
	 while there is no window: a headless run, or a test. */

#pragma once

#ifndef __W3DWINDOWHOOKS_H_
#define __W3DWINDOWHOOKS_H_

#if !defined(_WIN32)

#include "Lib/BaseType.h"
#include "Common/Monitors.h"

/** Dresses the window for a WindowModeType: a frame and a caption for a plain window, none for the
	* two that own the screen. */
typedef void (*W3DWindowFrameHook)( Int mode );

/** Gives the window a client area this big: a plain window in the middle of `screen` (the chosen
	* monitor), a borderless one covering it from its corner.  Never called for fullscreen. */
typedef void (*W3DWindowSizeHook)( Int mode, Int width, Int height, const MonitorRect &screen );

extern W3DWindowFrameHook TheW3DWindowFrameHook;
extern W3DWindowSizeHook TheW3DWindowSizeHook;

#endif

#endif // __W3DWINDOWHOOKS_H_
