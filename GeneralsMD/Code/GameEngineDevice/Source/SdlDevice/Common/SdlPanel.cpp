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

// SdlPanel.cpp: see SdlPanel.h.  No PreRTS.h: CoreGraphics' headers and the engine's do not mix.

#include "SdlDevice/Common/SdlPanel.h"

#include <math.h>

#if defined(__APPLE__)
#include <CoreGraphics/CoreGraphics.h>
#include <IOKit/graphics/IOGraphicsTypes.h>

bool SdlPanel_nativePixels( int x, int y, int width, int height, int *pixelWidth, int *pixelHeight )
{
	CGDirectDisplayID displays[16];
	uint32_t count = 0;
	if (CGGetActiveDisplayList( 16, displays, &count ) != kCGErrorSuccess)
		return false;
	for (uint32_t index = 0; index < count; ++index)
	{
		// CoreGraphics and SDL place displays in the same global points, the primary's corner at 0,0.
		const CGRect place = CGDisplayBounds( displays[index] );
		if (lround( place.origin.x ) != x || lround( place.origin.y ) != y
				|| lround( place.size.width ) != width || lround( place.size.height ) != height)
			continue;
		// With the low-resolution duplicates: without them a panel whose native mode is offered only at 2x
		// has none flagged.
		const void *keys[] = { kCGDisplayShowDuplicateLowResolutionModes };
		const void *values[] = { kCFBooleanTrue };
		CFDictionaryRef options = CFDictionaryCreate( NULL, keys, values, 1, &kCFTypeDictionaryKeyCallBacks,
			&kCFTypeDictionaryValueCallBacks );
		CFArrayRef modes = CGDisplayCopyAllDisplayModes( displays[index], options );
		if (options != NULL)
			CFRelease( options );
		bool found = false;
		for (CFIndex at = 0; modes != NULL && at < CFArrayGetCount( modes ) && !found; ++at)
		{
			CGDisplayModeRef mode = (CGDisplayModeRef)CFArrayGetValueAtIndex( modes, at );
			if ((CGDisplayModeGetIOFlags( mode ) & kDisplayModeNativeFlag) == 0)
				continue;
			*pixelWidth = (int)CGDisplayModeGetPixelWidth( mode );
			*pixelHeight = (int)CGDisplayModeGetPixelHeight( mode );
			found = true;
		}
		if (modes != NULL)
			CFRelease( modes );
		return found;
	}
	return false;
}
#else
bool SdlPanel_nativePixels( int, int, int, int, int *, int * )
{
	return false;
}
#endif
