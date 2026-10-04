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

/*
** <d3d9types.h> off Windows (decision 7, phase A1): Platform/D3D9Posix.h, the Direct3D 9 names the renderer
** speaks, declared for the POSIX device.  This directory is on the include path of the POSIX renderer
** targets only, so WW3D2's and W3DDevice's #include <d3d9types.h> lines stay as they are; on Windows they
** reach the SDK and never see this file.
*/

#pragma once

#if defined(_WIN32)
#error "Platform/PosixD3D9 is the POSIX build's <d3d9types.h>; a Windows build must reach the SDK's"
#endif

#include "Platform/RenderTypes.h"
#include "Platform/D3D9Posix.h"
