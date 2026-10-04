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

// Platform/MsvcFloatCasts.h against the bytes MSVC's cvttss2si-then-low-byte gives, worked by hand:
// truncation toward zero, the wrap modulo 256 both ways, and 0x80000000's low byte (0) for NaN and
// for values past the int range.  Then the particle case that crashed ARM64: an angle of -0.28 rad
// through W3DParticleSys's orientation expression is table index 245, inside the table's 256.

#include "Platform/MsvcFloatCasts.h"
#include "Platform/StrdupAsWindows.h"

#include <limits>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK_BYTE(value, expected) \
	do { const unsigned got_ = floatToByteAsMsvc(value); if (got_ != (unsigned)(expected)) { ++failures; \
		printf("FAIL %s:%d: floatToByteAsMsvc(%s) = %u, expected %u\n", __FILE__, __LINE__, #value, got_, (unsigned)(expected)); } } while (0)

int main()
{
	CHECK_BYTE(0.0f, 0);
	CHECK_BYTE(0.99f, 0);				// toward zero
	CHECK_BYTE(-0.99f, 0);				// toward zero from below, not -1
	CHECK_BYTE(255.7f, 255);
	CHECK_BYTE(256.0f, 0);				// the wrap
	CHECK_BYTE(300.2f, 44);
	CHECK_BYTE(-1.0f, 255);
	CHECK_BYTE(-11.3f, 245);
	CHECK_BYTE(-256.5f, 0);
	CHECK_BYTE(65535.9f, 255);
	CHECK_BYTE(2147483520.0f, 128);		// the largest float below 2^31: 0x7FFFFF80
	CHECK_BYTE(2147483648.0f, 0);		// out of range: 0x80000000
	CHECK_BYTE(-2147483648.0f, 0);		// INT_MIN exactly
	CHECK_BYTE(1.0e20f, 0);
	CHECK_BYTE(-1.0e20f, 0);
	CHECK_BYTE(std::numeric_limits<float>::quiet_NaN(), 0);
	CHECK_BYTE(std::numeric_limits<float>::infinity(), 0);

	// The crash: orientation -11, a particle between -0.2956 and -0.2710 rad; -0.28 rad here.
	const float PI_AS_BASETYPE = 3.14159265359f;
	const unsigned orientation = floatToByteAsMsvc(-0.28f * 255.0f / (2.0f * PI_AS_BASETYPE));
	if (orientation != 245) {
		++failures;
		printf("FAIL: the particle at -0.28 rad has orientation %u, expected 245\n", orientation);
	}

// MSVC for x86 and x64 only: the claim is cvttss2si's.  MSVC for ARM64 converts with fcvtzs/fcvtzu and
// saturates, as clang on ARM64 does, which is why the tree converts through these helpers there too.
#if defined(_M_X64) || defined(_M_IX86)
	// Where the claim can be measured: MSVC's own cast against the helper, over the wrap in range and
	// then past the int range, NaN and the infinities, where the claim is INT_MIN's low byte.  Each value
	// goes through a volatile so the compiler converts it at run time, as the game's code does, rather
	// than folding a constant (a second review's finding).
	for (float step = -1000.0f; step <= 1000.0f; step += 0.37f) {
		volatile float value = step;
		const unsigned char raw = (unsigned char)value;
		if (raw != floatToByteAsMsvc(value)) {
			++failures;
			printf("FAIL: MSVC casts %f to %u, the helper says %u\n", (double)value, (unsigned)raw, (unsigned)floatToByteAsMsvc(value));
			break;
		}
	}
	const float OUTSIDE[] = { 3.0e9f, -3.0e9f, 1.0e20f, -1.0e20f, std::numeric_limits<float>::infinity(),
		-std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() };
	for (size_t i = 0; i < sizeof(OUTSIDE) / sizeof(OUTSIDE[0]); ++i) {
		volatile float value = OUTSIDE[i];
		const unsigned char raw = (unsigned char)value;
		if (raw != floatToByteAsMsvc(value)) {
			++failures;
			printf("FAIL: MSVC casts %f to %u, the helper says %u\n", (double)value, (unsigned)raw, (unsigned)floatToByteAsMsvc(value));
		}
	}
#endif

	// strdup as the Windows build has it (Platform/StrdupAsWindows.h): a copy of any string, and NULL for
	// NULL, the UCRT's _strdup; Darwin's and glibc's strdup read through a NULL.  A cloned particle emitter's
	// NULL user string crashed the fog of war off Windows (port defect 31).
	// uint16 and uint32 (S8): the table MSVC 19.44 x64 printed on Windows 11 (W2), value by value.
	// (unsigned short) is the low 16 bits of the 32-bit conversion; (unsigned) the low 32 bits of the
	// 64-bit one, which is not the int path: 3e9 survives it, and 2^32, NaN and the infinities are 0.
	{
		struct Row { float value; unsigned short u16; unsigned int u32; };
		const float inf = std::numeric_limits<float>::infinity();
		const Row rows[] = {
			{ -1.5f, 65535, 4294967295u }, { -70000.5f, 61072, 4294897296u }, { 70000.5f, 4464, 70000u },
			{ 65535.9f, 65535, 65535u }, { 65536.0f, 0, 65536u }, { 131071.0f, 65535, 131071u },
			{ 2147483520.0f, 65408, 2147483520u }, { 2147483648.0f, 0, 2147483648u }, { 3.0e9f, 0, 3000000000u },
			{ 4294967040.0f, 0, 4294967040u }, { 4294967296.0f, 0, 0u }, { 5.0e9f, 0, 705032704u },
			{ -3.0e9f, 0, 1294967296u }, { 1.0e20f, 0, 0u }, { -1.0e20f, 0, 0u }, { inf, 0, 0u }, { -inf, 0, 0u },
			{ std::numeric_limits<float>::quiet_NaN(), 0, 0u } };
		for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
			const unsigned short u16 = floatToUnsignedShortAsMsvc(rows[i].value);
			const unsigned int u32 = floatToUnsignedAsMsvc(rows[i].value);
			if (u16 != rows[i].u16 || u32 != rows[i].u32) {
				++failures;
				printf("FAIL: %g converts to %u/%u, Windows gave %u/%u\n", (double)rows[i].value, (unsigned)u16, u32,
					(unsigned)rows[i].u16, rows[i].u32);
			}
#if defined(_M_X64) || defined(_M_IX86)
			volatile float value = rows[i].value;		// and the same against MSVC's own casts, at run time
			if ((unsigned short)value != u16 || (unsigned int)value != u32) {
				++failures;
				printf("FAIL: MSVC casts %g to %u/%u, the helpers say %u/%u\n", (double)rows[i].value,
					(unsigned)(unsigned short)value, (unsigned int)value, (unsigned)u16, u32);
			}
#endif
		}
	}

	if (strdupAsWindows(NULL) != NULL) {
		++failures;
		printf("FAIL: strdupAsWindows(NULL) is not NULL\n");
	}
	char *copy = strdupAsWindows("ParticleEmitter");
	if (copy == NULL || strcmp(copy, "ParticleEmitter") != 0) {
		++failures;
		printf("FAIL: strdupAsWindows did not copy its string\n");
	}
	free(copy);

	if (failures != 0) {
		printf("test_msvc_float_casts: %d FAILED\n", failures);
		return 1;
	}
	printf("test_msvc_float_casts: every conversion is MSVC's byte, uint16 and uint32\n");
	return 0;
}
