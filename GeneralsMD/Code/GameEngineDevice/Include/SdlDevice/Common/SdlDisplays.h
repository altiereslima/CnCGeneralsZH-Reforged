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

// SdlDisplays: SDL3's display list as Monitors.h's PlatformDisplays table (C2), which the options menu,
// borderless mode and the window placement read.  SdlGameEngine installs it once SDL's video is up and
// takes it down before SDL's video stops; with no table Monitors.h keeps its one 800x600 monitor.
//
// Sizes are in pixels, as on Windows, where the game is DPI-aware: SDL reports a display in points
// with a pixel density (2 on a Retina panel), and each size here is the points times that density.  A
// monitor's rect is its SDL bounds scaled by its own density, origin included, so the renderer (D4)
// divides by the same density to place a window in SDL's points.
//
// Monitors are numbered in SDL's order and named "\\.\DISPLAY<n>", the shape Windows gives the device
// Options.ini stores; the name is SDL's (the panel's, "Built-in Retina Display").

#pragma once

#ifndef __SDLDISPLAYS_H
#define __SDLDISPLAYS_H

#include "Common/Monitors.h"

struct SDL_Window;

/** Monitors.h's table over SDL3's displays.  Valid only while SDL's video subsystem is up. */
extern const PlatformDisplays TheSdlDisplays;

/** The pixel density of the display whose rect, in the table's pixels, is this one; with none, that of the
	* display the window is on, and 1 without either.  A size or place in the table's pixels divided by it is
	* the same in SDL's points, which is what SDL's window calls take. */
float SdlDisplays_densityOf( const MonitorRect &rect, SDL_Window *window );

#endif // __SDLDISPLAYS_H
