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

// The graphics backend drawing the picture off Windows, for the HUD corner that reads "DX9" or "DX11" on
// Windows (W3DDisplay::getRendererName).  Off Windows the device underneath is the SDL3 GPU one, so this
// names the SDL3 backend it runs on, with the architecture as Windows' names carry theirs: "Metal arm64"
// on an Apple silicon Mac, "Vulkan x64" on Linux, and "Headless" when there is no window and so no GPU.
//
// Implemented by the POSIX device (posixd3d9), which sets it when it makes its GPU frame.  The string is
// the device's, UTF-16, and lives as long as the program; asking for it never allocates.

#pragma once

#ifndef RENDERERNAME_H
#define RENDERERNAME_H

#if !defined(_WIN32)
const char16_t *PosixRenderer_Name(void);
#endif

#endif // RENDERERNAME_H
