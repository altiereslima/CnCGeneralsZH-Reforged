/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
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

#pragma once

// WindowMode.h
//
// How the game owns the screen.  This is one header with one enum and no includes on purpose: it
// is named by GlobalData, by the command line parser, by WinMain - which picks the window style
// before the engine exists and so cannot have GameEngine's types - and by EarlyOptions.h, which
// pulls in <windows.h> and must never be dragged into GlobalData.h in return.  BaseType.h fights
// windef.h over the BitTest macro, and that fight is not worth restarting for three constants.

#ifndef __WINDOWMODE_H_
#define __WINDOWMODE_H_

enum WindowModeType
{
	WINDOW_MODE_FULLSCREEN	= 0,	///< a topmost window over the monitor at the chosen resolution, scaled to it; minimised while away
	WINDOW_MODE_BORDERLESS	= 1,	///< a windowed device covering the display, no caption, no frame
	WINDOW_MODE_WINDOWED		= 2,	///< an ordinary window at the chosen resolution

	WINDOW_MODE_COUNT				= 3,
};

// How a fullscreen picture whose shape is not the monitor's fills it.  The monitor keeps its own
// mode and the picture is scaled onto it either way.
enum FullscreenScalingType
{
	FULLSCREEN_SCALING_STRETCH			= 0,	///< all of the monitor, the picture stretched to it
	FULLSCREEN_SCALING_KEEP_ASPECT	= 1,	///< the picture's own shape, black bars beside it

	FULLSCREEN_SCALING_COUNT				= 2,
};

#endif // __WINDOWMODE_H_
