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
** The Win32 types the renderer hands across the Direct3D 9 device interface, under the engine's own
** names (decision 7, phase A0).
**
** WW3D2 and W3DDevice speak Direct3D 9, and off Windows they will speak it to a D3D9-shaped device of
** our own (PORTING.md, decision 7).  That device's header declares the D3D9 names but
** none of Win32's: the port does not define DWORD, HRESULT or HWND off Windows.  So where the renderer
** holds a Win32 type, it holds one of two things instead:
**
**   - an engine scalar (UnsignedInt, UnsignedShort, UnsignedByte, Int, Real) where the value never
**     crosses the device interface by address - the usual case, B5's "scalar in disguise";
**   - one of the names below where it does: a variable the device writes through a pointer (a
**     render state read back, a locked rectangle), an array the device reads (shader tokens), or a
**     result the device returns.
**
** On Windows each name below IS the SDK type, so every call site means exactly what it meant.  Off
** Windows each is a fixed-width type with the same size, and the D3D9-shaped device's header is
** written against these.
*/

#pragma once

#ifndef PLATFORM_RENDERTYPES_H
#define PLATFORM_RENDERTYPES_H

#if defined(_WIN32)

// windows.h must already be in, and this header does not include it.  Its configuration
// (WIN32_LEAN_AND_MEAN, and winsock2.h before winsock.h) is each includer's to choose; a header that
// pulled it in first would choose for them without a word.  d3d9.h includes it, so after <d3d9.h> -
// as dx8wrapper.h has it - is always right.
#if !defined(_WINDOWS_) && !defined(_INC_WINDOWS)
#error "Platform/RenderTypes.h needs windows.h (or d3d9.h) included first; it will not choose windows.h's configuration for you"
#endif

typedef DWORD RenderUInt32;			///< a DWORD the device reads or writes through a pointer
typedef HRESULT RenderResult;		///< what a device call returns
typedef RECT RenderRect;
typedef POINT RenderPoint;
typedef HWND RenderWindow;			///< the window the device draws into

/// E_FAIL: the one generic failure the renderer returns of its own (D3D_OK, D3D9's name, is S_OK).
static const RenderResult RENDER_FAIL = E_FAIL;

/// FAILED and SUCCEEDED, as the SDK has them.
inline bool Render_Failed(RenderResult result) { return FAILED(result); }
inline bool Render_Succeeded(RenderResult result) { return SUCCEEDED(result); }

#else

#include <stdint.h>

typedef uint32_t RenderUInt32;
typedef int32_t RenderResult;			///< signed, as HRESULT is: a failure is negative
struct RenderRect { int32_t left, top, right, bottom; };
struct RenderPoint { int32_t x, y; };
typedef struct RenderWindowOpaque *RenderWindow;		///< C2's window; the device never looks inside

static const RenderResult RENDER_FAIL = (RenderResult)0x80004005u;		///< E_FAIL's value

inline bool Render_Failed(RenderResult result) { return result < 0; }
inline bool Render_Succeeded(RenderResult result) { return result >= 0; }

#endif

static_assert(sizeof(RenderUInt32) == 4, "RenderUInt32 is DWORD's four bytes");
static_assert(sizeof(RenderResult) == 4, "RenderResult is HRESULT's four bytes");
static_assert(sizeof(RenderRect) == 16, "RenderRect is RECT's four LONGs");
static_assert(sizeof(RenderPoint) == 8, "RenderPoint is POINT's two LONGs");
static_assert((RenderResult)-1 < 0, "RenderResult is signed, as HRESULT is");
static_assert(RENDER_FAIL < 0, "RENDER_FAIL is a failure");

#endif // PLATFORM_RENDERTYPES_H
