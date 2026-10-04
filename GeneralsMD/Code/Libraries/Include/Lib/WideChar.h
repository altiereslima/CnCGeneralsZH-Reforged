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

// The engine's character type for text, and nothing else.
//
// Its own header, so that a library which needs the type but not the engine's preamble can have it.
// WW3D2's text renderer is the reason: it takes the engine's strings, and pulling Lib/BaseType.h into
// WW3D2 would bring `#define NULL 0` against four WWLib headers that already define NULL differently,
// which is ill-formed (measured during B1).  So this file includes nothing and defines one name.
// BaseType.h includes it, so nothing that already had WideChar loses it.

#pragma once

#ifndef LIB_WIDECHAR_H
#define LIB_WIDECHAR_H

// char16_t, not wchar_t: wchar_t is two bytes under MSVC and four everywhere else, and the width of
// this type is the width of every UTF-16 .csf string, map chunk, save game, LAN packet and - through
// Xfer::xferUnicodeString (Xfer.cpp:209), hashed by XferCRC - the replay and network checksum.  Windows
// always had two unsigned bytes here; char16_t is two unsigned bytes on every compiler, so every byte
// those formats and that checksum see is unchanged there, and now the same on every other platform.
// Tests/test_widechar_crc_gate.cpp is the proof, against a table computed independently.
typedef char16_t WideChar;	///< a UTF-16 code unit, on every platform

static_assert(sizeof(WideChar) == 2, "WideChar's width is the replay and network CRC's: see XferCRC.cpp");

#endif // LIB_WIDECHAR_H
