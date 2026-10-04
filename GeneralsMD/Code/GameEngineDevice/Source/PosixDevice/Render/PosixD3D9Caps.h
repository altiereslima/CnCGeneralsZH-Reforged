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

// FILE: PosixD3D9Caps.h //////////////////////////////////////////////////////////////////////////
// Desc:   Which formats the device offers for what (decision 7, phase A2).
///////////////////////////////////////////////////////////////////////////////////////////////////

/* One answer for both sides of the device: CheckDeviceFormat reports it, and the resource methods
	 refuse what it refuses, as a real driver's CreateTexture fails for a format its caps do not
	 offer.  So the renderer can never make something the caps said it could not. */

#pragma once

#ifndef POSIXD3D9CAPS_H
#define POSIXD3D9CAPS_H

#include "Platform/D3D9Posix.h"

/** Whether `format` can be made as `resource_type` with `usage` (RENDERTARGET, DEPTHSTENCIL,
	* AUTOGENMIPMAP, DYNAMIC; other usage bits do not restrict). */
bool posixFormatSupported( RenderUInt32 usage, D3DRESOURCETYPE resource_type, D3DFORMAT format );

#endif // POSIXD3D9CAPS_H
