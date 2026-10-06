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

// fullscreenfit.h
//
// Where a fullscreen picture of one size goes on a monitor of another.  The monitor keeps its own
// mode and the picture is scaled onto it, so this is the whole of the decision: all of the monitor,
// or the largest rectangle of the picture's own shape centred on it, with black either side.

#pragma once

struct FullscreenFitRect
{
	int x, y, width, height;	// relative to the monitor's top left corner
};

inline FullscreenFitRect Fullscreen_Fit(int monitor_width, int monitor_height, int width, int height,
	bool keep_aspect)
{
	FullscreenFitRect fit = { 0, 0, monitor_width, monitor_height };
	if (!keep_aspect || width <= 0 || height <= 0 || monitor_width <= 0 || monitor_height <= 0) {
		return fit;
	}
	// Compared as products so 1920x1080 on 1920x1200 is decided without rounding.
	const long long wide = (long long)width * monitor_height;
	const long long tall = (long long)height * monitor_width;
	if (wide > tall) {
		fit.height = (int)(((long long)monitor_width * height + width / 2) / width);
	} else if (tall > wide) {
		fit.width = (int)(((long long)monitor_height * width + height / 2) / height);
	}
	fit.x = (monitor_width - fit.width) / 2;
	fit.y = (monitor_height - fit.height) / 2;
	return fit;
}
