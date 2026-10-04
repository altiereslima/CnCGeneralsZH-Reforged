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

// Float-to-unsigned conversions spelled out as the Windows build computes them.
//
// C++ leaves a float converted to an unsigned type undefined when the value does not fit, and this
// tree was written where that did not matter: MSVC for x86 and x64 lowers (unsigned char)f to
// cvttss2si, a truncation toward zero to a 32-bit int that answers 0x80000000 for anything out of
// int range or NaN, and then keeps the low byte.  So -11.3 became 245 and 300.2 became 44, and code
// came to depend on the wrap.  ARM64's clang lowers the same cast to fcvtzs or fcvtzu and, since the
// result "cannot" be out of range, uses it unmasked: a particle at about -0.28 rad (orientation -11)
// indexed its orientation table 190 GB past the end (PORTING.md, "Defects found while
// porting").
//
// The lowering assumed is MSVC's long-standing one.  Visual Studio 2022 has an option for it,
// /fpcvt:BC (that one) against /fpcvt:IA (saturating); this is from memory, and the current
// documentation should be checked.  A Windows build that changed it would stop matching this header
// while the other platforms still passed, which is what test_msvc_float_casts' MSVC-side comparison
// is there to catch.
//
// Each function here is that MSVC result, computed with defined operations only, so every platform
// gets the Windows answer and Windows gets the answer it always had.  Measured on Windows 11 with
// MSVC 19.44 x64 (W2): test_msvc_float_casts compares MSVC's
// own casts with these on Windows and passes, and a probe over sixteen to twenty edge values (the
// int, uint16 and uint32 edges, +-1e20, the infinities and NaN) matched every one.  One spelling for
// the whole tree: a float-to-unsigned site that the ARM64 sweep finds uses these, not a local cast.

#pragma once

#ifndef MSVCFLOATCASTS_H
#define MSVCFLOATCASTS_H

#include <type_traits>

/// cvttss2si: toward zero into a 32-bit int, and INT_MIN (0x80000000) for NaN or anything outside
/// the int range.
inline int floatToIntAsMsvc(float value)
{
	return (value > -2147483648.0f && value < 2147483648.0f) ? (int)value : (int)(-2147483647 - 1);
}

/// cvttsd2si, the same for a double: toward zero, and INT_MIN for NaN or outside the int range.  An
/// overload rather than a narrowing to float, which would round a double (or a large integer) first.
inline int floatToIntAsMsvc(double value)
{
	return (value > -2147483649.0 && value < 2147483648.0) ? (int)value : (int)(-2147483647 - 1);
}

/// An integer (or enum) handed to one of BaseType.h's REAL_TO_ macros, as a few callers do: the plain
/// conversion those macros always made, since no float is involved.  The float and double overloads
/// above are exact matches for their types and win over this template.
template <typename T, typename std::enable_if<std::is_integral<T>::value || std::is_enum<T>::value, int>::type = 0>
inline int floatToIntAsMsvc(T value)
{
	return (int)value;
}

/// (unsigned char)value as MSVC computes it: the low byte of floatToIntAsMsvc.
inline unsigned char floatToByteAsMsvc(float value)
{
	return (unsigned char)((unsigned int)floatToIntAsMsvc(value) & 0xFFu);
}

/// (unsigned short)value as MSVC computes it: the low 16 bits of floatToIntAsMsvc, as the byte is
/// (measured: 70000.5 is 4464, -1.5 is 65535, and 3e9, 2^31, NaN and the infinities are 0).
inline unsigned short floatToUnsignedShortAsMsvc(float value)
{
	return (unsigned short)((unsigned int)floatToIntAsMsvc(value) & 0xFFFFu);
}

/// (unsigned int)value as MSVC x64 computes it, which is not the int path: cvttss2si into a 64-bit
/// register, then the low 32 bits.  So -1 is 0xFFFFFFFF, 3e9 is 3000000000, 2^32 is 0 and 5e9 is
/// 705032704; NaN, the infinities and anything outside int64's range give INT64_MIN, whose low 32 bits
/// are 0 (measured, W2).  ARM64 saturates instead (a negative to 0, an infinity to 0xFFFFFFFF).
inline unsigned int floatToUnsignedAsMsvc(float value)
{
	if (!(value >= -9223372036854775808.0f && value < 9223372036854775808.0f))
		return 0u;
	return (unsigned int)(unsigned long long)(long long)value;
}

#endif // MSVCFLOATCASTS_H
