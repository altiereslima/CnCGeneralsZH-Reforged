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

#if _MSC_VER >= 1000
#pragma once
#endif

#ifndef WWENDIAN_H
#define WWENDIAN_H

/*
**	WW_BIG_ENDIAN is 1 on a big-endian target and 0 everywhere else.  Test it with #if, never #ifdef.
**
**	This replaces `#ifdef BIG_ENDIAN`, which was right when it was written and is wrong on every POSIX
**	system now: BIG_ENDIAN is not a flag there but the *name of a byte order*, a constant defined
**	unconditionally beside LITTLE_ENDIAN and BYTE_ORDER - Darwin's <machine/endian.h> always, glibc's
**	<endian.h> under _DEFAULT_SOURCE.  So `#ifdef BIG_ENDIAN` was true on a little-endian Mac or Linux
**	machine and took the big-endian layout.  Measured in a wwlib translation unit on arm64 macOS:
**	BIG_ENDIAN is 4321, BYTE_ORDER is 1234.
**
**	__BYTE_ORDER__ is the compiler's own answer (clang and GCC).  MSVC does not define it, so this is
**	0 there by construction - correct, since every MSVC target is little-endian.
*/
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define WW_BIG_ENDIAN 1
#else
#define WW_BIG_ENDIAN 0
#endif

#endif // WWENDIAN_H
