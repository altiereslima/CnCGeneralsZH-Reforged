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

// SdlPanel: a display's panel in its own pixels, which SDL does not report.  macOS draws a scaled desktop
// ("More Space") into a backing store larger than the panel and scales it down; SDL's display is that
// backing store.  Its own file because CoreGraphics' headers (MacTypes.h: Rect, Point) cannot share a
// translation unit with the engine's.

#pragma once

#ifndef __SDLPANEL_H
#define __SDLPANEL_H

/** The native pixels of the display at this place and size, in the desktop's points (SDL_GetDisplayBounds);
	* false where the platform cannot say (everywhere but macOS) or no display matches. */
bool SdlPanel_nativePixels( int x, int y, int width, int height, int *pixelWidth, int *pixelHeight );

#endif // __SDLPANEL_H
