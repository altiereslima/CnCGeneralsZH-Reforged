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

// SdlDisplays.cpp: see SdlDisplays.h.

#include "PreRTS.h"

#include "SdlDevice/Common/SdlDisplays.h"

#include "SdlDevice/Common/SdlPanel.h"

#include <SDL3/SDL.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

/** A size in SDL's points, in pixels. */
static long toPixels( int points, float density )
{
	return lround( points * (double)density );
}

static float densityOf( SDL_DisplayID display )
{
	const SDL_DisplayMode *desktop = SDL_GetDesktopDisplayMode( display );
	return (desktop != NULL && desktop->pixel_density > 0.0f) ? desktop->pixel_density : 1.0f;
}

float SdlDisplays_densityOf( const MonitorRect &rect, SDL_Window *window )
{
	int count = 0;
	SDL_DisplayID *displays = SDL_GetDisplays( &count );
	float found = 0.0f;
	for (int index = 0; displays != NULL && index < count && found == 0.0f; ++index)
	{
		SDL_Rect bounds;
		if (!SDL_GetDisplayBounds( displays[index], &bounds ))
			continue;
		const float density = densityOf( displays[index] );
		if (toPixels( bounds.x, density ) == rect.left && toPixels( bounds.y, density ) == rect.top
				&& toPixels( bounds.w, density ) == rect.right - rect.left && toPixels( bounds.h, density ) == rect.bottom - rect.top)
			found = density;
	}
	SDL_free( displays );
	if (found == 0.0f && window != NULL && SDL_GetDisplayForWindow( window ) != 0)
		found = densityOf( SDL_GetDisplayForWindow( window ) );
	return found > 0.0f ? found : 1.0f;
}

static int sdlListMonitors( MonitorEntry *entries, int capacity )
{
	int count = 0;
	SDL_DisplayID *displays = SDL_GetDisplays( &count );
	if (displays == NULL)
		return 0;

	const SDL_DisplayID primary = SDL_GetPrimaryDisplay();
	int listed = 0;
	for (int index = 0; index < count && listed < capacity; ++index)
	{
		SDL_Rect bounds;
		if (!SDL_GetDisplayBounds( displays[index], &bounds ))
			continue;
		const float density = densityOf( displays[index] );

		MonitorEntry &entry = entries[listed++];
		memset( &entry, 0, sizeof( entry ) );
		entry.number = index + 1;
		snprintf( entry.device, sizeof( entry.device ), "\\\\.\\DISPLAY%d", entry.number );
		const char *name = SDL_GetDisplayName( displays[index] );
		if (name != NULL)
		{
			strncpy( entry.name, name, sizeof( entry.name ) - 1 );
			entry.name[sizeof( entry.name ) - 1] = 0;
		}
		entry.rect.left = toPixels( bounds.x, density );
		entry.rect.top = toPixels( bounds.y, density );
		entry.rect.right = entry.rect.left + toPixels( bounds.w, density );
		entry.rect.bottom = entry.rect.top + toPixels( bounds.h, density );
		entry.primary = (displays[index] == primary);
	}
	SDL_free( displays );
	return listed;
}

/** The display a device name from sdlListMonitors names; the primary when none does, as findMonitor. */
static SDL_DisplayID displayNamed( const char *device )
{
	int count = 0;
	SDL_DisplayID *displays = SDL_GetDisplays( &count );
	SDL_DisplayID found = SDL_GetPrimaryDisplay();
	if (displays != NULL && device != NULL)
	{
		char name[MONITOR_DEVICE_NAME_LENGTH];
		for (int index = 0; index < count; ++index)
		{
			snprintf( name, sizeof( name ), "\\\\.\\DISPLAY%d", index + 1 );
			if (strcasecmp( name, device ) == 0)
				found = displays[index];
		}
	}
	SDL_free( displays );
	return found;
}

/** Adds one size to the sorted, duplicate-free list; the same rules as Windows' listDisplayModes. */
static void addMode( DisplayModeEntry *entries, int capacity, int &count, const SDL_DisplayMode *mode )
{
	if (mode == NULL || SDL_BITSPERPIXEL( mode->format ) < MIN_DISPLAY_MODE_BITS)
		return;
	const float density = mode->pixel_density > 0.0f ? mode->pixel_density : 1.0f;
	const int width = (int)toPixels( mode->w, density );
	const int height = (int)toPixels( mode->h, density );
	if (width < MIN_DISPLAY_MODE_WIDTH || height < MIN_DISPLAY_MODE_HEIGHT)
		return;

	int at = 0;
	while (at < count && (entries[at].width < width
												|| (entries[at].width == width && entries[at].height < height)))
		++at;
	if (at < count && entries[at].width == width && entries[at].height == height)
		return;
	if (count == capacity)
		return;
	memmove( entries + at + 1, entries + at, (count - at) * sizeof( entries[0] ) );
	entries[at].width = width;
	entries[at].height = height;
	++count;
}

static int sdlListDisplayModes( const char *device, DisplayModeEntry *entries, int capacity )
{
	const SDL_DisplayID display = displayNamed( device );
	if (display == 0)
		return 0;

	int count = 0;
	int modeCount = 0;
	SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes( display, &modeCount );
	if (modes != NULL)
	{
		for (int index = 0; index < modeCount; ++index)
			addMode( entries, capacity, count, modes[index] );
		SDL_free( modes );
	}
	addMode( entries, capacity, count, SDL_GetDesktopDisplayMode( display ) );	// the desktop's own size, always offered
	return count;
}

/** The panel's own pixels, where the platform knows them (SdlPanel.h): SDL reports the desktop's backing
	* store, 3420x2224 on a 2560x1664 panel in macOS's "More Space" mode, and has no word for the panel. */
static bool sdlPanelSize( const char *device, int *width, int *height )
{
	SDL_Rect bounds;
	return SDL_GetDisplayBounds( displayNamed( device ), &bounds )
		&& SdlPanel_nativePixels( bounds.x, bounds.y, bounds.w, bounds.h, width, height );
}

const PlatformDisplays TheSdlDisplays = { sdlListMonitors, sdlListDisplayModes, sdlPanelSize };
