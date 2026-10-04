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

// The half of MSVC's C runtime that widechar_crc_gate_msvc_model needs, and nothing more.
//
// -fshort-wchar makes wchar_t two bytes, as MSVC's is, but the C library on this machine was built
// with four-byte wchar_t.  UnicodeString.cpp calls wcslen, wcscpy, wcscat, wcsspn and wcscspn
// directly, and handing a two-byte string to a four-byte wcscpy overruns the heap: that is how
// this file was found, as a crash inside malloc.  So the Windows-model build defines those five
// here, over two-byte units, which is what MSVC's CRT does with them.  The executable's own
// definitions are the ones its objects link against, ahead of the C library's.
//
// Built ONLY into widechar_crc_gate_msvc_model.  It is the emulation of the Windows CRT the gate's
// (A) column relies on, and it is listed in the gate's header as such.  The C library's other wide
// functions that these objects name - fwprintf, vswprintf, vswscanf, all in WideCharFns.cpp's
// format funnel - are left alone because the hashing path never calls them; if it ever did, (A)
// would be wrong, and the gate's golden lines would stop matching the independent table.

#include <stddef.h>
#include <wchar.h>

static_assert(sizeof(wchar_t) == 2, "this file emulates MSVC's CRT and is for -fshort-wchar builds only");

extern "C" {

size_t wcslen(const wchar_t *s)
{
	const wchar_t *p = s;
	while (*p)
		++p;
	return (size_t)(p - s);
}

wchar_t *wcscpy(wchar_t *dst, const wchar_t *src)
{
	wchar_t *d = dst;
	while ((*d++ = *src++) != 0) {
	}
	return dst;
}

wchar_t *wcscat(wchar_t *dst, const wchar_t *src)
{
	wcscpy(dst + wcslen(dst), src);
	return dst;
}

static int contains(const wchar_t *set, wchar_t c)
{
	for (; *set; ++set)
		if (*set == c)
			return 1;
	return 0;
}

size_t wcsspn(const wchar_t *s, const wchar_t *accept)
{
	size_t n = 0;
	while (s[n] && contains(accept, s[n]))
		++n;
	return n;
}

size_t wcscspn(const wchar_t *s, const wchar_t *reject)
{
	size_t n = 0;
	while (s[n] && !contains(reject, s[n]))
		++n;
	return n;
}

}  // extern "C"
