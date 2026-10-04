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
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

#pragma once

// Monitors.h
//
// The monitors on the desktop and the sizes each one offers, asked of Windows directly.
//
// The resolution list used to be the first Direct3D 9 adapter's, whichever monitor that was, while
// the display mode was changed on the primary and every window was put at the desktop's origin.  So
// the game could only ever be on the primary, and the list described the primary only by luck.
// Everything that places the window or picks its size asks here now: WinMain before the engine
// exists, which is why this is header-only and speaks plain C strings, applyWindowMode for
// borderless, W3DDisplay for the window and the options menu for both of its lists.
//
// A monitor is named by its GDI device, "\\.\DISPLAY2".  That is what Options.ini stores under
// Monitor and what ChangeDisplaySettingsEx takes.  A name that is empty, or that no monitor answers
// to any more because it was unplugged, means the primary.

#if defined(_WIN32)
#include <windows.h>
#else
#include <strings.h>
#endif
#include <stdlib.h>
#include <string.h>

/* Where a monitor sits, and how long its device name can be: Windows' own RECT and CCHDEVICENAME
	 there, so WinMain can keep taking a RECT; the same shape and the same 32 elsewhere. */
#if defined(_WIN32)
typedef RECT MonitorRect;
enum { MONITOR_DEVICE_NAME_LENGTH = CCHDEVICENAME };
#else
struct MonitorRect
{
	long left;
	long top;
	long right;
	long bottom;
};
enum { MONITOR_DEVICE_NAME_LENGTH = 32 };
#endif

struct MonitorEntry
{
	char	device[MONITOR_DEVICE_NAME_LENGTH];	///< "\\.\DISPLAY2"
	char	name[128];							///< what the monitor calls itself, "Lenovo Y27-30"; can be empty
	int		number;									///< the 2 in DISPLAY2, and what the options menu shows first
	MonitorRect	rect;							///< where it sits on the desktop, in pixels
	bool	primary;
};

struct DisplayModeEntry
{
	int width;
	int height;
};

enum
{
	MAX_MONITOR_ENTRIES = 16,
	MAX_DISPLAY_MODE_ENTRIES = 128,
};

// The floor the game's layouts are drawn for, and the colour depth the renderer asks for.
enum
{
	MIN_DISPLAY_MODE_WIDTH = 800,
	MIN_DISPLAY_MODE_HEIGHT = 600,
	MIN_DISPLAY_MODE_BITS = 24,
};

#if defined(_WIN32)
struct MonitorCollection
{
	MonitorEntry	*entries;
	int						capacity;
	int						count;
};

inline BOOL CALLBACK collectMonitor( HMONITOR monitor, HDC, LPRECT, LPARAM collectionAddress )
{
	MonitorCollection *collection = (MonitorCollection *)collectionAddress;
	if (collection->count == collection->capacity)
		return FALSE;

	MONITORINFOEXA info;
	info.cbSize = sizeof( info );
	if (!::GetMonitorInfoA( monitor, &info ))
		return TRUE;

	MonitorEntry &entry = collection->entries[collection->count++];
	::strncpy( entry.device, info.szDevice, sizeof( entry.device ) - 1 );
	entry.device[sizeof( entry.device ) - 1] = 0;
	entry.number = ::atoi( entry.device + ::strcspn( entry.device, "0123456789" ) );
	entry.rect = info.rcMonitor;
	entry.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;

	// Device 0 under an adapter is the monitor plugged into it.  A monitor Windows has no driver
	// for calls itself "Generic PnP Monitor", which is why the number goes in front of the name.
	DISPLAY_DEVICEA attached;
	attached.cb = sizeof( attached );
	entry.name[0] = 0;
	if (::EnumDisplayDevicesA( entry.device, 0, &attached, 0 ))
	{
		::strncpy( entry.name, attached.DeviceString, sizeof( entry.name ) - 1 );
		entry.name[sizeof( entry.name ) - 1] = 0;
	}
	return TRUE;
}

/** Every monitor on the desktop, in the order of their numbers. */
inline int listMonitors( MonitorEntry *entries, int capacity )
{
	MonitorCollection collection = { entries, capacity, 0 };
	::EnumDisplayMonitors( NULL, NULL, collectMonitor, (LPARAM)&collection );

	for (int sorted = 1; sorted < collection.count; ++sorted)
	{
		const MonitorEntry moving = entries[sorted];
		int at = sorted;
		for (; at > 0 && entries[at - 1].number > moving.number; --at)
			entries[at] = entries[at - 1];
		entries[at] = moving;
	}
	return collection.count;
}

/** The monitor with this device name, or the primary when none has it. */
inline MonitorEntry findMonitor( const char *device )
{
	MonitorEntry monitors[MAX_MONITOR_ENTRIES];
	const int count = listMonitors( monitors, MAX_MONITOR_ENTRIES );

	int primary = -1;
	for (int index = 0; index < count; ++index)
	{
		if (::strcasecmp( monitors[index].device, device ) == 0)
			return monitors[index];
		if (monitors[index].primary)
			primary = index;
	}
	if (primary >= 0)
		return monitors[primary];

	// A session with no desktop to enumerate: the screen as GetSystemMetrics sees it, which is
	// what every caller of this used before there was a choice.
	MonitorEntry screen;
	::ZeroMemory( &screen, sizeof( screen ) );
	screen.rect.right = ::GetSystemMetrics( SM_CXSCREEN );
	screen.rect.bottom = ::GetSystemMetrics( SM_CYSCREEN );
	screen.primary = true;
	return screen;
}

/** The sizes this monitor can be set to, smallest first and each once, whatever refresh rates and
	* colour depths Windows lists it under.  Only the modes the monitor itself accepts: without
	* EDS_RAWMODE Windows leaves out what the monitor's own description rules out. */
inline int listDisplayModes( const char *device, DisplayModeEntry *entries, int capacity )
{
	int count = 0;
	DEVMODEA mode;
	::ZeroMemory( &mode, sizeof( mode ) );
	mode.dmSize = sizeof( mode );

	for (DWORD modeIndex = 0; ::EnumDisplaySettingsA( device, modeIndex, &mode ); ++modeIndex)
	{
		const int width = (int)mode.dmPelsWidth;
		const int height = (int)mode.dmPelsHeight;
		if (mode.dmBitsPerPel < MIN_DISPLAY_MODE_BITS
				|| width < MIN_DISPLAY_MODE_WIDTH || height < MIN_DISPLAY_MODE_HEIGHT)
			continue;

		int at = 0;
		while (at < count && (entries[at].width < width
													|| (entries[at].width == width && entries[at].height < height)))
			++at;
		if (at < count && entries[at].width == width && entries[at].height == height)
			continue;
		if (count == capacity)
			break;

		::memmove( entries + at + 1, entries + at, (count - at) * sizeof( entries[0] ) );
		entries[at].width = width;
		entries[at].height = height;
		++count;
	}
	return count;
}
#else
/* Off Windows the displays belong to the platform layer that owns the window: C2's SdlGameEngine
	 installs ThePlatformDisplays, over SDL3's display list, once its video is up.  This header cannot
	 call SDL itself (gameengine does not link it), so it asks through that table.

	 With no table there is one monitor, the primary, the size of the game's floor (800x600), offering
	 that one mode: a headless run, which starts no video, and the tests.  That is Windows' own answer
	 above when it has no desktop to enumerate, with the floor for a size: an empty rect would give
	 borderless mode a 0x0 resolution, and empty lists would give the options menu nothing to select. */
struct PlatformDisplays
{
	/** Every monitor, in the order of their numbers; as listMonitors. */
	int (*listMonitors)( MonitorEntry *entries, int capacity );
	/** The sizes the named monitor can be set to, smallest first and each once; as listDisplayModes. */
	int (*listDisplayModes)( const char *device, DisplayModeEntry *entries, int capacity );
	/** The named monitor's panel, in its own pixels, where the desktop is drawn at another size and scaled
		* to it (macOS's scaled modes); false where the platform cannot say.  May be NULL. */
	bool (*panelSize)( const char *device, int *width, int *height );
};
inline const PlatformDisplays *ThePlatformDisplays = NULL;

inline MonitorEntry fallbackMonitorEntry( void )
{
	MonitorEntry screen;
	::memset( &screen, 0, sizeof( screen ) );
	screen.number = 1;
	screen.rect.right = MIN_DISPLAY_MODE_WIDTH;
	screen.rect.bottom = MIN_DISPLAY_MODE_HEIGHT;
	screen.primary = true;
	return screen;
}

inline int listMonitors( MonitorEntry *entries, int capacity )
{
	if (capacity < 1)
		return 0;
	if (ThePlatformDisplays != NULL)
	{
		const int count = ThePlatformDisplays->listMonitors( entries, capacity );
		if (count > 0)
			return count;
	}
	entries[0] = fallbackMonitorEntry();
	return 1;
}

/** The monitor with this device name, or the primary when none has it; as on Windows. */
inline MonitorEntry findMonitor( const char *device )
{
	MonitorEntry monitors[MAX_MONITOR_ENTRIES];
	const int count = listMonitors( monitors, MAX_MONITOR_ENTRIES );

	int primary = -1;
	for (int index = 0; index < count; ++index)
	{
		if (device != NULL && ::strcasecmp( monitors[index].device, device ) == 0)
			return monitors[index];
		if (monitors[index].primary)
			primary = index;
	}
	return monitors[primary >= 0 ? primary : 0];
}

inline int listDisplayModes( const char *device, DisplayModeEntry *entries, int capacity )
{
	if (capacity < 1)
		return 0;
	if (ThePlatformDisplays != NULL)
	{
		const int count = ThePlatformDisplays->listDisplayModes( device, entries, capacity );
		if (count > 0)
			return count;
	}
	entries[0].width = MIN_DISPLAY_MODE_WIDTH;
	entries[0].height = MIN_DISPLAY_MODE_HEIGHT;
	return 1;
}

/** A monitor's size, brought down to its panel's where the panel has fewer pixels.  A Mac in a scaled
	* mode ("More Space") draws its desktop at 3420x2224 and shows it on a 2560x1664 panel: drawing the game
	* at the larger size costs 1.8 times the pixels, which the scaler then throws away.  A panel of the same
	* shape (within 1%) gives its own size, so the game's pixels are the panel's; another shape keeps the
	* monitor's, scaled to fit the panel.  A panel no smaller, or none known (0), leaves the size as it is. */
inline void fitToPanel( int monitorWidth, int monitorHeight, int panelWidth, int panelHeight, int *width, int *height )
{
	*width = monitorWidth;
	*height = monitorHeight;
	if (panelWidth <= 0 || panelHeight <= 0 || monitorWidth <= 0 || monitorHeight <= 0
			|| (panelWidth >= monitorWidth && panelHeight >= monitorHeight))
		return;
	const double monitorShape = (double)monitorWidth / monitorHeight;
	const double panelShape = (double)panelWidth / panelHeight;
	if (panelShape > monitorShape * 0.99 && panelShape < monitorShape * 1.01)
	{
		*width = panelWidth;
		*height = panelHeight;
		return;
	}
	const double scaleX = (double)panelWidth / monitorWidth, scaleY = (double)panelHeight / monitorHeight;
	const double scale = scaleX < scaleY ? scaleX : scaleY;
	*width = (int)(monitorWidth * scale + 0.5);
	*height = (int)(monitorHeight * scale + 0.5);
}

/** The resolution a first run starts at, when Options.ini names none: the monitor's own size, in pixels,
	* or its panel's where that is smaller (fitToPanel).
	* Windows starts at GameData's 800x600, and its fullscreen sets the monitor to that size.  Fullscreen
	* here is the desktop at its own size, so 800x600 would be stretched over it, and a window would be
	* a small box in the middle.  With no platform table (headless, the tests) this is the floor-sized
	* monitor's 800x600, as before. */
inline void firstRunResolution( const char *device, int *width, int *height )
{
	const MonitorEntry monitor = findMonitor( device );
	int panelWidth = 0, panelHeight = 0;
	if (ThePlatformDisplays != NULL && ThePlatformDisplays->panelSize != NULL
			&& !ThePlatformDisplays->panelSize( monitor.device, &panelWidth, &panelHeight ))
		panelWidth = panelHeight = 0;
	fitToPanel( (int)(monitor.rect.right - monitor.rect.left), (int)(monitor.rect.bottom - monitor.rect.top),
		panelWidth, panelHeight, width, height );
}
#endif
