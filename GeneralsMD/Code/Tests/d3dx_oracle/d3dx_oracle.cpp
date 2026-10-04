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

// Runs Microsoft's own D3DXVec4Transform machine code on a Mac, and compares d3dxportable.h
// against it.  See run_d3dx_oracle.sh for what this proves and what it does not.
//
// d3dx9_43.dll's D3DXVec4Transform export is a thunk through a slot the DLL fills by CPU.  The
// three bodies it can land on are leaf functions: no calls, no imports and no rip-relative data,
// only the three pointer arguments and the stack.  So their bytes can be copied out of the file
// into an executable page and called directly, with the Windows x64 calling convention, from an
// x86_64 process under Rosetta.  Nothing else of the DLL is loaded or run.
//
// The RVAs below were read out of the one build of the DLL that run_d3dx_oracle.sh accepts, by
// sha256.  Another build would put different code at these addresses, which is why the script
// refuses anything else rather than trying.

#if !defined(__x86_64__)
#error "d3dx_oracle runs Windows x64 machine code, so it has to be built for x86_64."
#endif

#include "d3dxportable.h"
#include "d3dx_golden.h"
#include "d3dx_sweep.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

typedef float * (__attribute__((ms_abi)) * DllTransform)(float * out, const float * vector,
	const float * matrix);

struct DllBody
{
	const char * name;
	unsigned int rva;
};

// Read out of the dispatch at RVA 0x5e198: the slot table starts as a copy of a default table
// (the scalar body), is left alone if HKLM\Software\Microsoft\Direct3D DisablePSGP or
// DisableD3DXPSGP is 1 or IsProcessorFeaturePresent(PF_XMMI_INSTRUCTIONS_AVAILABLE) is false,
// and is otherwise overwritten by the Intel table when CPUID leaf 0 says "GenuineIntel" and by
// the other one when it does not.
static const DllBody BODIES[3] = {
	{ "scalar (default; SSE absent or PSGP disabled)", 0x3efa0 },
	{ "SSE, any vendor but GenuineIntel",              0x2112c0 },
	{ "SSE, GenuineIntel",                             0x211e60 },
};
enum { SCALAR = 0, OTHER = 1, INTEL = 2 };

// .text's virtual address and file offset in this build.
static const unsigned int TEXT_RVA = 0x1000;
static const unsigned int TEXT_FILE_OFFSET = 0x400;
// More than any of the three needs.  The longest, the scalar body, is 0x103 bytes.
static const unsigned int BODY_BYTES = 0x200;

static unsigned int bits_of(float value)
{
	unsigned int bits;
	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static float float_of(unsigned int bits)
{
	float value;
	memcpy(&value, &bits, sizeof(value));
	return value;
}

int main(int argc, char ** argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: d3dx_oracle <d3dx9_43.dll, x64, 9.29.952.3111>\n");
		return 2;
	}

	FILE * file = fopen(argv[1], "rb");
	if (file == NULL) {
		fprintf(stderr, "[d3dx-oracle] ERROR: cannot open %s\n", argv[1]);
		return 1;
	}
	fseek(file, 0, SEEK_END);
	const long size = ftell(file);
	fseek(file, 0, SEEK_SET);
	unsigned char * image = (unsigned char *)malloc((size_t)size);
	if (image == NULL || fread(image, 1, (size_t)size, file) != (size_t)size) {
		fprintf(stderr, "[d3dx-oracle] ERROR: cannot read %s\n", argv[1]);
		return 1;
	}
	fclose(file);

	unsigned char * page = (unsigned char *)mmap(NULL, 3 * BODY_BYTES, PROT_READ | PROT_WRITE,
		MAP_ANON | MAP_PRIVATE, -1, 0);
	if (page == MAP_FAILED) {
		perror("[d3dx-oracle] mmap");
		return 1;
	}
	DllTransform body[3];
	for (int i = 0; i < 3; ++i) {
		const long offset = (long)(BODIES[i].rva - TEXT_RVA + TEXT_FILE_OFFSET);
		if (offset + (long)BODY_BYTES > size) {
			fprintf(stderr, "[d3dx-oracle] ERROR: %s lies past the end of the file\n", BODIES[i].name);
			return 1;
		}
		memcpy(page + i * BODY_BYTES, image + offset, BODY_BYTES);
		body[i] = (DllTransform)(void *)(page + i * BODY_BYTES);
	}
	if (mprotect(page, 3 * BODY_BYTES, PROT_READ | PROT_EXEC) != 0) {
		perror("[d3dx-oracle] mprotect");
		return 1;
	}

	int failures = 0;

	// 1. The golden table, which test_d3dxportable.cpp asserts on every build.  Re-derived here
	//    from the DLL itself, so the table cannot drift from what it says it records.
	for (unsigned int row = 0; row < D3DX_GOLDEN_ROW_COUNT; ++row) {
		alignas(16) float in[4];
		alignas(16) float basis[16];
		alignas(16) float out[3][4];
		for (int lane = 0; lane < 4; ++lane) {
			in[lane] = float_of(D3DX_GOLDEN_ROWS[row].in[lane]);
		}
		memcpy(basis, D3DX_GOLDEN_BASIS, sizeof(basis));
		for (int b = 0; b < 3; ++b) {
			body[b](out[b], in, basis);
		}
		for (int lane = 0; lane < 4; ++lane) {
			const unsigned int want = D3DX_GOLDEN_ROWS[row].out[lane];
			const unsigned int intel_want = lane == 0 ? D3DX_GOLDEN_ROWS[row].intel_x : want;
			if (bits_of(out[SCALAR][lane]) != want || bits_of(out[OTHER][lane]) != want
				|| bits_of(out[INTEL][lane]) != intel_want) {
				printf("[d3dx-oracle] FAIL: d3dx_golden.h row %u lane %d does not match the DLL: "
					"table 0x%08x/0x%08x, DLL 0x%08x/0x%08x/0x%08x\n", row, lane, want, intel_want,
					bits_of(out[SCALAR][lane]), bits_of(out[OTHER][lane]), bits_of(out[INTEL][lane]));
				++failures;
			}
		}
	}
	printf("[d3dx-oracle] d3dx_golden.h: %u rows re-derived from the DLL%s\n", D3DX_GOLDEN_ROW_COUNT,
		failures == 0 ? ", all match" : "");

	// 2. The sweep.  The portable transform must match the scalar and non-Intel bodies on every
	//    lane of every input.  The Intel body is measured, not required, because it sums in a
	//    different order by design.  What IS required of it is the structural claim the rest of
	//    this work rests on: over the Bezier basis it can differ only in lane x.
	D3DXSweepState state = { D3DX_SWEEP_SEED };
	unsigned int hash = D3DX_SWEEP_HASH_BASIS;
	unsigned long portable_mismatch = 0;
	unsigned long other_vs_scalar = 0;
	unsigned long basis_rows = 0;
	unsigned long intel_x_differs = 0;
	// The basis rows split two ways, and only one of them is a shape the game makes: control
	// points, as BezFwdIterator::start passes them.  The t-vector rows use random t, where the
	// game only ever evaluates t = 0.5, which is exact.  So the combined rate is not the rate a
	// player's shells see; this one is.
	unsigned long control_rows = 0;
	unsigned long control_x_differs = 0;
	unsigned long intel_yzw_differs = 0;
	unsigned long general_rows = 0;
	unsigned long intel_general_differs = 0;
	for (unsigned int index = 0; index < D3DX_SWEEP_COUNT; ++index) {
		alignas(16) float vector[4];
		alignas(16) float matrix[16];
		alignas(16) float dll[3][4];
		float portable[4];
		const bool basis = d3dx_sweep_input(&state, index, vector, matrix);
		for (int b = 0; b < 3; ++b) {
			body[b](dll[b], vector, matrix);
		}
		D3DXPortable::Vec4Transform(portable, vector, matrix);
		hash = d3dx_sweep_mix(hash, portable);

		bool portable_ok = true;
		bool intel_differs = false;
		for (int lane = 0; lane < 4; ++lane) {
			if (bits_of(portable[lane]) != bits_of(dll[SCALAR][lane])) {
				portable_ok = false;
			}
			if (bits_of(dll[OTHER][lane]) != bits_of(dll[SCALAR][lane])) {
				++other_vs_scalar;
			}
			if (bits_of(dll[INTEL][lane]) != bits_of(dll[SCALAR][lane])) {
				intel_differs = true;
				if (basis && lane == 0) {
					++intel_x_differs;
					if (index % 4u < 2u) {
						++control_x_differs;
					}
				} else if (basis) {
					++intel_yzw_differs;
				}
			}
		}
		if (!portable_ok) {
			if (portable_mismatch < 10) {
				printf("[d3dx-oracle] FAIL: sweep %u: portable (%08x %08x %08x %08x) vs DLL scalar "
					"(%08x %08x %08x %08x)\n", index,
					bits_of(portable[0]), bits_of(portable[1]), bits_of(portable[2]), bits_of(portable[3]),
					bits_of(dll[SCALAR][0]), bits_of(dll[SCALAR][1]), bits_of(dll[SCALAR][2]),
					bits_of(dll[SCALAR][3]));
			}
			++portable_mismatch;
		}
		if (basis) {
			++basis_rows;
			if (index % 4u < 2u) {
				++control_rows;
			}
		} else {
			++general_rows;
			if (intel_differs) {
				++intel_general_differs;
			}
		}
	}

	printf("[d3dx-oracle] sweep: %u inputs (%lu over the Bezier basis, %lu over general matrices)\n",
		D3DX_SWEEP_COUNT, basis_rows, general_rows);
	printf("[d3dx-oracle]   portable vs DLL scalar body:        %lu inputs differ\n", portable_mismatch);
	printf("[d3dx-oracle]   DLL non-Intel SSE vs scalar body:   %lu lanes differ\n", other_vs_scalar);
	printf("[d3dx-oracle]   DLL GenuineIntel vs scalar, basis:  lane x differs on %lu of %lu (%.1f%%), "
		"lanes y/z/w on %lu\n", intel_x_differs, basis_rows, 100.0 * (double)intel_x_differs / (double)basis_rows,
		intel_yzw_differs);
	printf("[d3dx-oracle]     of which control-point inputs,     BezFwdIterator's shape: %lu of %lu (%.1f%%)\n",
		control_x_differs, control_rows, 100.0 * (double)control_x_differs / (double)control_rows);
	printf("[d3dx-oracle]   DLL GenuineIntel vs scalar, general: %lu of %lu inputs differ\n",
		intel_general_differs, general_rows);
	printf("[d3dx-oracle] sweep fingerprint (portable, x86_64): 0x%08x\n", hash);

	if (portable_mismatch != 0) {
		++failures;
	}
	if (other_vs_scalar != 0) {
		printf("[d3dx-oracle] FAIL: the DLL's two left-to-right bodies disagree with each other, "
			"so d3dxportable.h's premise is wrong\n");
		++failures;
	}
	if (intel_yzw_differs != 0) {
		printf("[d3dx-oracle] FAIL: the GenuineIntel body differs outside lane x over the Bezier "
			"basis, which contradicts what d3dxportable.h says about it\n");
		++failures;
	}
	if (intel_x_differs == 0) {
		printf("[d3dx-oracle] FAIL: the GenuineIntel body never differs; either the RVAs are wrong "
			"or this oracle has lost the ability to tell the orders apart\n");
		++failures;
	}

	if (failures != 0) {
		return 1;
	}
	printf("[d3dx-oracle] ok: d3dxportable.h matches the DLL's scalar and non-Intel bodies bit for bit\n");
	return 0;
}
