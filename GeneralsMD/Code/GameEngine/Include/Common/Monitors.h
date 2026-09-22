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

#include <windows.h>
#include <stdlib.h>
#include <string.h>

struct MonitorEntry
{
	char	device[CCHDEVICENAME];	///< "\\.\DISPLAY2"
	char	name[128];							///< what the monitor calls itself, "Lenovo Y27-30"; can be empty
	int		number;									///< the 2 in DISPLAY2, and what the options menu shows first
	RECT	rect;										///< where it sits on the desktop, in pixels
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
		if (::_stricmp( monitors[index].device, device ) == 0)
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
