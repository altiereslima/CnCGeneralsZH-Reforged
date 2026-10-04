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
// d3dx9portable.cpp against d3dx9_43.dll itself (W-ARM64).  Windows on ARM64 has no d3dx9_43.dll and binds
// the port's own arithmetic in its place (d3dx9runtime.cpp); macOS and Linux use the same bodies as D3DX.
// Until this test they had been checked against independent answers only (test_d3dx9posix.cpp: "the DLL was
// not run").  This runs where the DLL binds, Windows x64, and compares every entry point the port replaces:
//
//   - the FVF vertex size, over every position type, the normal, point size, the two colours, every texture
//     set count and a spread of per-set formats, with the last-beta flags too: equal, every one;
//   - transpose, scaling and translation, which move numbers and compute none: bit for bit;
//   - multiply, rotation about z, Vec3Transform and the inverse, which compute: within 1e-5 of the operands'
//     scale, with how many came out bit-identical reported.  The DLL picks its bodies by CPU (dx9_smoke's
//     D3DXVec4Transform note), so bit-identity is not the claim; none of these reaches the simulation;
//   - an exactly singular matrix: both refuse it and leave the output alone;
//   - on a real Direct3D 9 device, the same calls bound both ways in one process: first to the DLL, then, with
//     ZH_D3DX_PORTABLE=1, to what ARM64 binds (d3dx9portable_texture.cpp and d3dcompiler_47.dll).  Format
//     conversions, scaled and partial copies, mip chains, DXT copies and decodes, a render target read back
//     and one written through UpdateSurface, D3DXCreateTexture's sizes and format substitution, and the
//     assembler and the D3D8 shader translation: textures within one step of the narrowest channel, sizes,
//     formats and shader tokens equal.  No device here (no GPU, no WARP) skips this part and says so.
//
// Where the DLL does not bind (ARM64, or a machine without the redistributable) there is nothing to compare
// against, and the test says so and exits 77, which CMake records as skipped rather than passed.

#include "d3dx9runtime.h"
#include "d3dx9math.h"
#include "d3dx9portable.h"
#include "d3d8shadertranslate.h"

#include "PosixImage.h"
#include "PosixPixelCodec.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

static int failures = 0;

static uint32_t random_state = 0x2545F491u;

static uint32_t next_random()
{
	// xorshift32: the same sequence on every run, so a failure names the same input again
	random_state ^= random_state << 13;
	random_state ^= random_state >> 17;
	random_state ^= random_state << 5;
	return random_state;
}

static float random_float(float range)
{
	return ((float)(next_random() & 0xFFFFFF) / (float)0xFFFFFF * 2.0f - 1.0f) * range;
}

static void random_matrix(D3DXMATRIX & m, float range)
{
	for (int row = 0; row < 4; ++row) {
		for (int column = 0; column < 4; ++column) {
			m.m[row][column] = random_float(range);
		}
	}
}

static bool same_bits(const void * a, const void * b, size_t size)
{
	return memcmp(a, b, size) == 0;
}

struct Closeness
{
	const char * Name;
	unsigned Compared;
	unsigned Identical;
	double WorstRatio;		// the largest |port - dll| / scale seen
};

static void compare(Closeness & c, const float * port, const float * dll, const double * scale, int count)
{
	++c.Compared;
	if (same_bits(port, dll, sizeof(float) * count)) {
		++c.Identical;
		return;
	}
	for (int i = 0; i < count; ++i) {
		const double s = scale[i] > 1.0 ? scale[i] : 1.0;
		const double ratio = fabs((double)port[i] - (double)dll[i]) / s;
		if (ratio > c.WorstRatio) {
			c.WorstRatio = ratio;
		}
	}
}

static void report(const Closeness & c, double tolerance)
{
	printf("%-16s %6u compared, %6u bit-identical, worst difference %.3g of the operands' scale\n", c.Name,
		c.Compared, c.Identical, c.WorstRatio);
	if (c.WorstRatio > tolerance) {
		++failures;
		printf("FAIL: %s differs from d3dx9_43.dll by more than %.0e of the operands' scale\n", c.Name, tolerance);
	}
}

static void check_fvf_sizes()
{
	static const DWORD POSITIONS[] = { 0, D3DFVF_XYZ, D3DFVF_XYZRHW, D3DFVF_XYZW, D3DFVF_XYZB1, D3DFVF_XYZB2,
		D3DFVF_XYZB3, D3DFVF_XYZB4, D3DFVF_XYZB5 };
	static const DWORD LAST_BETA[] = { 0, D3DFVF_LASTBETA_UBYTE4, D3DFVF_LASTBETA_D3DCOLOR };
	unsigned compared = 0;
	for (size_t p = 0; p < sizeof(POSITIONS) / sizeof(POSITIONS[0]); ++p) {
		for (DWORD parts = 0; parts < 16; ++parts) {
			for (DWORD sets = 0; sets <= 8; ++sets) {
				for (int pattern = 0; pattern < 12; ++pattern) {
					DWORD fvf = POSITIONS[p] | (sets << D3DFVF_TEXCOUNT_SHIFT);
					if (parts & 1) fvf |= D3DFVF_NORMAL;
					if (parts & 2) fvf |= D3DFVF_PSIZE;
					if (parts & 4) fvf |= D3DFVF_DIFFUSE;
					if (parts & 8) fvf |= D3DFVF_SPECULAR;
					// patterns 0-3: every set the same format; 4-11: a random format per set
					for (DWORD set = 0; set < 8; ++set) {
						const DWORD format = pattern < 4 ? (DWORD)pattern : (next_random() & 3);
						fvf |= format << (16 + set * 2);
					}
					if (POSITIONS[p] >= D3DFVF_XYZB1 && POSITIONS[p] <= D3DFVF_XYZB5) {
						fvf |= LAST_BETA[pattern % 3];
					}
					const UINT dll = D3DXGetFVFVertexSize(fvf);
					const unsigned port = D3DXPortable_FVF_Vertex_Size(fvf);
					++compared;
					if (dll != port) {
						++failures;
						if (failures < 20) {
							printf("FAIL: FVF 0x%08lx: the DLL says %u bytes, the port %u\n", (unsigned long)fvf, dll, port);
						}
					}
				}
			}
		}
	}
	printf("FVF sizes        %6u compared against the DLL\n", compared);
}

static void check_exact()
{
	unsigned compared = 0;
	for (int round = 0; round < 2000; ++round) {
		D3DXMATRIX m, port, dll;
		random_matrix(m, 1000.0f);
		D3DXMatrixTranspose(&dll, &m);
		D3DXPortable_Matrix_Transpose(&port, &m);
		if (!same_bits(&port, &dll, sizeof(port))) { ++failures; printf("FAIL: transpose, round %d\n", round); }

		const float x = random_float(1000.0f), y = random_float(1000.0f), z = random_float(1000.0f);
		D3DXMatrixScaling(&dll, x, y, z);
		D3DXPortable_Matrix_Scaling(&port, x, y, z);
		if (!same_bits(&port, &dll, sizeof(port))) { ++failures; printf("FAIL: scaling, round %d\n", round); }

		D3DXMatrixTranslation(&dll, x, y, z);
		D3DXPortable_Matrix_Translation(&port, x, y, z);
		if (!same_bits(&port, &dll, sizeof(port))) { ++failures; printf("FAIL: translation, round %d\n", round); }
		compared += 3;
	}
	printf("moves            %6u compared, all bit-identical unless a FAIL says otherwise\n", compared);
}

static void check_computed()
{
	Closeness multiply = { "multiply", 0, 0, 0.0 };
	Closeness rotation = { "rotation z", 0, 0, 0.0 };
	Closeness vec3 = { "Vec3Transform", 0, 0, 0.0 };
	Closeness inverse = { "inverse", 0, 0, 0.0 };

	for (int round = 0; round < 20000; ++round) {
		D3DXMATRIX a, b, port, dll;
		random_matrix(a, 100.0f);
		random_matrix(b, 100.0f);

		D3DXMatrixMultiply(&dll, &a, &b);
		D3DXPortable_Matrix_Multiply(&port, &a, &b);
		double scale[16];
		for (int row = 0; row < 4; ++row) {
			for (int column = 0; column < 4; ++column) {
				double s = 0.0;
				for (int k = 0; k < 4; ++k) {
					s += fabs((double)a.m[row][k] * (double)b.m[k][column]);
				}
				scale[row * 4 + column] = s;
			}
		}
		compare(multiply, &port._11, &dll._11, scale, 16);

		const float angle = random_float(20.0f);
		D3DXMatrixRotationZ(&dll, angle);
		D3DXPortable_Matrix_Rotation_Z(&port, angle);
		double unit[16];
		for (int i = 0; i < 16; ++i) unit[i] = 1.0;
		compare(rotation, &port._11, &dll._11, unit, 16);

		D3DXVECTOR3 v;
		v.x = random_float(100.0f);
		v.y = random_float(100.0f);
		v.z = random_float(100.0f);
		D3DXVECTOR4 vp, vd;
		D3DXVec3Transform(&vd, &v, &a);
		D3DXPortable_Vec3_Transform(&vp, &v, &a);
		double vscale[4];
		for (int column = 0; column < 4; ++column) {
			vscale[column] = fabs(v.x * a.m[0][column]) + fabs(v.y * a.m[1][column]) + fabs(v.z * a.m[2][column])
				+ fabs(a.m[3][column]);
		}
		compare(vec3, &vp.x, &vd.x, vscale, 4);

		// A well-conditioned matrix to invert: a rotation and scale with a translation, as the renderer's
		// view matrices are.  Its inverse's entries are all of order one.
		D3DXMATRIX r, s, t, m;
		D3DXPortable_Matrix_Rotation_Z(&r, random_float(3.0f));
		D3DXPortable_Matrix_Scaling(&s, 0.5f + random_float(0.4f) + 1.0f, 1.5f, 1.25f);
		D3DXPortable_Matrix_Translation(&t, random_float(500.0f), random_float(500.0f), random_float(500.0f));
		D3DXPortable_Matrix_Multiply(&m, &r, &s);
		D3DXPortable_Matrix_Multiply(&m, &m, &t);
		float det_dll = 0.0f, det_port = 0.0f;
		D3DXMatrixInverse(&dll, &det_dll, &m);
		D3DXPortable_Matrix_Inverse(&port, &det_port, &m);
		double iscale[16];
		for (int i = 0; i < 16; ++i) iscale[i] = 1000.0;	// the translation row carries the 500s
		compare(inverse, &port._11, &dll._11, iscale, 16);
	}

	report(multiply, 1e-5);
	report(rotation, 1e-5);
	report(vec3, 1e-5);
	report(inverse, 1e-5);
}

static void check_singular()
{
	D3DXMATRIX singular;
	memset(&singular, 0, sizeof(singular));
	singular._11 = 1.0f;
	singular._22 = 1.0f;
	singular._33 = 1.0f;	// _44 is zero: rank three

	D3DXMATRIX port, dll;
	memset(&port, 0x5A, sizeof(port));
	memset(&dll, 0x5A, sizeof(dll));
	float det = 0.0f;
	D3DXMatrixInverse(&dll, &det, &singular);
	const D3DXMATRIX * result = D3DXPortable_Matrix_Inverse(&port, &det, &singular);
	if (result != NULL) { ++failures; printf("FAIL: the port inverted a singular matrix\n"); }
	if (!same_bits(&port, &dll, sizeof(port))) {
		++failures;
		printf("FAIL: a singular matrix left different outputs behind (the DLL's and the port's)\n");
	}
}

// ---- on a real device ----------------------------------------------------------------------------------------

static const char PORTABLE_VARIABLE[] = "ZH_D3DX_PORTABLE";
static const char DLL_NAME[] = "d3dx9_43.dll";
static const char PORTABLE_NAME_PREFIX[] = "the port's own";
static const UINT SIDE = 64;
static const uint32_t DEVICE_SEED = 0x1234567u;

static const char RIVER_PIXEL_SHADER[] =
	"ps.1.1\n"
	"tex t0\n"
	"tex t1\n"
	"tex t2\n"
	"tex t3\n"
	"mul r0.rgb, v0, t0\n"
	"mov r0.a, t0\n"
	"mul r1, t1, t2\n"
	"add r1.rgb, r1, t3\n"
	"mul r1.rgb, r1, v0.a\n"
	"+mul r0.a, r0, t3\n"
	"add r0.rgb, r0, r1\n";

static const char REPAIRED_VERTEX_SHADER[] =
	"vs.1.1\n"
	"dcl_position v0\n"
	"dcl_color v5\n"
	"mov r0, c0\n"
	"mov r1, c0\n"
	"mov r1, r0\n"
	"m4x4 r0, r1, c0\n"
	"mov oPos, r0\n"
	"mov oD0, v5\n";

// What ffshader.cpp's generator writes, cut down: one texture modulated by the diffuse colour.
static const char COMBINER_PIXEL_SHADER[] =
	"sampler2D s0 : register(s0);\n"
	"float4 main(float4 diffuse : COLOR0, float2 uv : TEXCOORD0) : COLOR0\n"
	"{\n"
	"\treturn tex2D(s0, uv) * diffuse;\n"
	"}\n";

/// One result of one call: what it returned, and what it left behind (pixels in whole blocks, rows packed;
/// a description; or shader tokens without their comments).
struct Capture
{
	std::string Name;
	HRESULT Result;
	D3DFORMAT Format;
	UINT Width;
	UINT Height;
	std::vector<uint8_t> Bytes;
	double ToleranceSteps = 1.0;	// how far from the DLL, in steps of the narrowest channel
};

// A mip level is made from the level above it, as the DLL makes it, so a one-step rounding difference can be
// carried down a level and add one more.
static const double CHAINED_TOLERANCE_STEPS = 2.0;

template <class Interface>
struct Held
{
	Interface * Pointer = NULL;
	~Held() { if (Pointer != NULL) Pointer->Release(); }
};

static void fill_random(IDirect3DTexture9 * texture, UINT level)
{
	D3DSURFACE_DESC desc;
	texture->GetLevelDesc(level, &desc);
	PosixFormatLayout layout;
	posixFormatLayout(desc.Format, &layout);
	const UINT rows = (desc.Height + layout.blockHeight - 1) / layout.blockHeight;
	const UINT row_bytes = (desc.Width + layout.blockWidth - 1) / layout.blockWidth * layout.bytesPerBlock;
	D3DLOCKED_RECT locked;
	texture->LockRect(level, &locked, NULL, 0);
	for (UINT row = 0; row < rows; ++row) {
		uint8_t * bytes = (uint8_t *)locked.pBits + (size_t)row * locked.Pitch;
		for (UINT index = 0; index < row_bytes; ++index) {
			bytes[index] = (uint8_t)next_random();
		}
	}
	texture->UnlockRect(level);
}

/// The surface's pixels, through GetRenderTargetData when it will not lock.
static Capture capture_surface(IDirect3DDevice9 * device, const std::string & name, HRESULT result,
	IDirect3DSurface9 * surface)
{
	Capture capture = { name, result, D3DFMT_UNKNOWN, 0, 0, {} };
	D3DSURFACE_DESC desc;
	surface->GetDesc(&desc);
	capture.Format = desc.Format;
	capture.Width = desc.Width;
	capture.Height = desc.Height;
	Held<IDirect3DSurface9> copy;
	IDirect3DSurface9 * readable = surface;
	D3DLOCKED_RECT locked;
	if (FAILED(surface->LockRect(&locked, NULL, D3DLOCK_READONLY))) {
		device->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &copy.Pointer, NULL);
		device->GetRenderTargetData(surface, copy.Pointer);
		readable = copy.Pointer;
		readable->LockRect(&locked, NULL, D3DLOCK_READONLY);
	}
	PosixFormatLayout layout;
	posixFormatLayout(desc.Format, &layout);
	const UINT rows = (desc.Height + layout.blockHeight - 1) / layout.blockHeight;
	const UINT row_bytes = (desc.Width + layout.blockWidth - 1) / layout.blockWidth * layout.bytesPerBlock;
	for (UINT row = 0; row < rows; ++row) {
		const uint8_t * bytes = (const uint8_t *)locked.pBits + (size_t)row * locked.Pitch;
		capture.Bytes.insert(capture.Bytes.end(), bytes, bytes + row_bytes);
	}
	readable->UnlockRect();
	return capture;
}

static Capture capture_level(IDirect3DDevice9 * device, const std::string & name, HRESULT result,
	IDirect3DTexture9 * texture, UINT level)
{
	Held<IDirect3DSurface9> surface;
	texture->GetSurfaceLevel(level, &surface.Pointer);
	return capture_surface(device, name, result, surface.Pointer);
}

static Capture capture_result(const std::string & name, HRESULT result)
{
	Capture capture = { name, result, D3DFMT_UNKNOWN, 0, 0, {} };
	return capture;
}

/// A shader's tokens without its comments: the assembler's and the compiler's own notes differ by version.
static Capture capture_tokens(const std::string & name, HRESULT result, const DWORD * tokens)
{
	Capture capture = capture_result(name, result);
	if (tokens == NULL) {
		return capture;
	}
	const DWORD * token = tokens;
	for (;;) {
		if ((*token & D3DSI_OPCODE_MASK) == D3DSIO_COMMENT) {
			token += 1 + ((*token & D3DSI_COMMENTSIZE_MASK) >> D3DSI_COMMENTSIZE_SHIFT);
			continue;
		}
		const uint8_t * bytes = (const uint8_t *)token;
		capture.Bytes.insert(capture.Bytes.end(), bytes, bytes + sizeof(DWORD));
		if (*token == D3DSIO_END) {
			return capture;
		}
		++token;
	}
}

static Capture capture_description(const std::string & name, HRESULT result, IDirect3DTexture9 * texture)
{
	Capture capture = capture_result(name, result);
	if (texture != NULL) {
		D3DSURFACE_DESC desc;
		texture->GetLevelDesc(0, &desc);
		capture.Format = desc.Format;
		capture.Width = desc.Width;
		capture.Height = desc.Height;
		const DWORD level_count = texture->GetLevelCount();
		const uint8_t * bytes = (const uint8_t *)&level_count;
		capture.Bytes.assign(bytes, bytes + sizeof(level_count));
	}
	return capture;
}

static std::string format_name(D3DFORMAT format)
{
	struct Named { D3DFORMAT Format; const char * Name; };
	static const Named NAMES[] = {
		{ D3DFMT_UNKNOWN, "UNKNOWN" }, { D3DFMT_R8G8B8, "R8G8B8" }, { D3DFMT_A8R8G8B8, "A8R8G8B8" },
		{ D3DFMT_X8R8G8B8, "X8R8G8B8" }, { D3DFMT_R5G6B5, "R5G6B5" }, { D3DFMT_X1R5G5B5, "X1R5G5B5" },
		{ D3DFMT_A1R5G5B5, "A1R5G5B5" }, { D3DFMT_A4R4G4B4, "A4R4G4B4" }, { D3DFMT_A8, "A8" },
		{ D3DFMT_A8R3G3B2, "A8R3G3B2" }, { D3DFMT_X4R4G4B4, "X4R4G4B4" },
	};
	for (size_t index = 0; index < sizeof(NAMES) / sizeof(NAMES[0]); ++index) {
		if (NAMES[index].Format == format) {
			return NAMES[index].Name;
		}
	}
	char name[16];
	if (format > 0xFF) {
		snprintf(name, sizeof(name), "%c%c%c%c", (char)(format & 0xFF), (char)((format >> 8) & 0xFF),
			(char)((format >> 16) & 0xFF), (char)((format >> 24) & 0xFF));
	}
	else {
		snprintf(name, sizeof(name), "%u", (unsigned)format);
	}
	return name;
}

static HRESULT managed_texture(IDirect3DDevice9 * device, UINT width, UINT height, UINT levels, D3DFORMAT format,
	IDirect3DTexture9 ** texture)
{
	return device->CreateTexture(width, height, levels, 0, format, D3DPOOL_MANAGED, texture, NULL);
}

/// DXT2-5 blocks with their colour end points in the order the format defines (c0 > c1).  With c0 <= c1 the
/// DLL's software decode and the port's part ways, by up to 250 of 255 (measured here): the port decodes such a
/// block as the GPU does, four colours, which is how every DXT texture of the game is drawn.
static void order_end_points(IDirect3DTexture9 * texture)
{
	const UINT BLOCK_BYTES = 16;
	const UINT COLOUR_OFFSET = 8;
	D3DSURFACE_DESC desc;
	texture->GetLevelDesc(0, &desc);
	D3DLOCKED_RECT locked;
	texture->LockRect(0, &locked, NULL, 0);
	for (UINT row = 0; row < desc.Height / 4; ++row) {
		uint8_t * blocks = (uint8_t *)locked.pBits + (size_t)row * locked.Pitch;
		for (UINT column = 0; column < desc.Width / 4; ++column) {
			uint8_t * colour = blocks + column * BLOCK_BYTES + COLOUR_OFFSET;
			uint16_t c0, c1;
			memcpy(&c0, colour, sizeof(c0));
			memcpy(&c1, colour + sizeof(c0), sizeof(c1));
			if (c0 == c1) {
				c1 = (uint16_t)(c1 & ~1u);
				c0 = (uint16_t)(c1 | 1u);
			}
			if (c0 < c1) {
				const uint16_t swapped = c0;
				c0 = c1;
				c1 = swapped;
			}
			memcpy(colour, &c0, sizeof(c0));
			memcpy(colour + sizeof(c0), &c1, sizeof(c1));
		}
	}
	texture->UnlockRect(0);
}

// The conversions and filters the game asks for.  Left out, with what the port does there: a luminance
// destination (the port weighs Rec. 601, the DLL otherwise, 31 of 255 apart; no texture of the game is
// converted into one), D3DX_FILTER_TRIANGLE and its default (the port averages a box, the DLL a wider tent;
// only SurfaceClass::Stretch_Copy asks for it, and nothing calls that), and a box over more than 2:1 (the
// port averages every source pixel, the DLL does not; mip levels are 2:1).
static void copy_cases(IDirect3DDevice9 * device, std::vector<Capture> & out)
{
	static const D3DFORMAT CONVERSIONS[] = { D3DFMT_A8R8G8B8, D3DFMT_X8R8G8B8, D3DFMT_R5G6B5, D3DFMT_X1R5G5B5,
		D3DFMT_A1R5G5B5, D3DFMT_A4R4G4B4, D3DFMT_X4R4G4B4, D3DFMT_A8 };
	Held<IDirect3DTexture9> source;
	managed_texture(device, SIDE, SIDE, 1, D3DFMT_A8R8G8B8, &source.Pointer);
	fill_random(source.Pointer, 0);
	Held<IDirect3DSurface9> source_surface;
	source.Pointer->GetSurfaceLevel(0, &source_surface.Pointer);

	for (size_t index = 0; index < sizeof(CONVERSIONS) / sizeof(CONVERSIONS[0]); ++index) {
		const std::string name = "A8R8G8B8 to " + format_name(CONVERSIONS[index]);
		Held<IDirect3DTexture9> destination;
		const HRESULT made = managed_texture(device, SIDE, SIDE, 1, CONVERSIONS[index], &destination.Pointer);
		if (FAILED(made)) {
			out.push_back(capture_result(name + " (no such texture here)", made));
			continue;
		}
		Held<IDirect3DSurface9> surface;
		destination.Pointer->GetSurfaceLevel(0, &surface.Pointer);
		const HRESULT result = D3DXLoadSurfaceFromSurface(surface.Pointer, NULL, NULL, source_surface.Pointer, NULL,
			NULL, D3DX_FILTER_NONE, 0);
		out.push_back(capture_surface(device, name, result, surface.Pointer));
	}

	struct Scaled { UINT Width; UINT Height; DWORD Filter; const char * Name; };
	static const Scaled SCALED[] = {
		{ SIDE / 2, SIDE / 2, D3DX_FILTER_BOX, "halved, box" },
	};
	for (size_t index = 0; index < sizeof(SCALED) / sizeof(SCALED[0]); ++index) {
		Held<IDirect3DTexture9> destination;
		managed_texture(device, SCALED[index].Width, SCALED[index].Height, 1, D3DFMT_A8R8G8B8, &destination.Pointer);
		Held<IDirect3DSurface9> surface;
		destination.Pointer->GetSurfaceLevel(0, &surface.Pointer);
		const HRESULT result = D3DXLoadSurfaceFromSurface(surface.Pointer, NULL, NULL, source_surface.Pointer, NULL,
			NULL, SCALED[index].Filter, 0);
		out.push_back(capture_surface(device, SCALED[index].Name, result, surface.Pointer));
	}

	// A rectangle into a rectangle, the rest of the destination left as it was (surfaceclass.cpp's Copy).
	{
		Held<IDirect3DTexture9> destination;
		managed_texture(device, SIDE, SIDE, 1, D3DFMT_R5G6B5, &destination.Pointer);
		fill_random(destination.Pointer, 0);
		Held<IDirect3DSurface9> surface;
		destination.Pointer->GetSurfaceLevel(0, &surface.Pointer);
		const RECT from = { 8, 8, 40, 40 };
		const RECT to = { 16, 20, 48, 52 };
		const HRESULT result = D3DXLoadSurfaceFromSurface(surface.Pointer, NULL, &to, source_surface.Pointer, NULL,
			&from, D3DX_FILTER_NONE, 0);
		out.push_back(capture_surface(device, "a rectangle into R5G6B5", result, surface.Pointer));
	}
}

static void mip_cases(IDirect3DDevice9 * device, std::vector<Capture> & out)
{
	// The last, narrow, chain's tail halves one side only (4x1 to 2x1 to 1x1).
	struct Chain { D3DFORMAT Format; UINT Width; UINT Height; };
	static const Chain CHAINS[] = { { D3DFMT_A8R8G8B8, SIDE, SIDE }, { D3DFMT_R5G6B5, SIDE, SIDE },
		{ D3DFMT_A4R4G4B4, SIDE, SIDE }, { D3DFMT_A8R8G8B8, SIDE, SIDE / 4 } };
	for (size_t index = 0; index < sizeof(CHAINS) / sizeof(CHAINS[0]); ++index) {
		Held<IDirect3DTexture9> texture;
		managed_texture(device, CHAINS[index].Width, CHAINS[index].Height, 0, CHAINS[index].Format, &texture.Pointer);
		fill_random(texture.Pointer, 0);
		const HRESULT result = D3DXFilterTexture(texture.Pointer, NULL, 0, D3DX_FILTER_BOX);
		for (UINT level = 1; level < texture.Pointer->GetLevelCount(); ++level) {
			char name[64];
			snprintf(name, sizeof(name), "%s %ux%u mip level %u", format_name(CHAINS[index].Format).c_str(),
				CHAINS[index].Width, CHAINS[index].Height, level);
			out.push_back(capture_level(device, name, result, texture.Pointer, level));
			out.back().ToleranceSteps = CHAINED_TOLERANCE_STEPS;
		}
	}
}

static void compressed_cases(IDirect3DDevice9 * device, std::vector<Capture> & out)
{
	static const D3DFORMAT COMPRESSED[] = { D3DFMT_DXT1, D3DFMT_DXT3, D3DFMT_DXT5 };
	for (size_t index = 0; index < sizeof(COMPRESSED) / sizeof(COMPRESSED[0]); ++index) {
		const std::string name = format_name(COMPRESSED[index]);
		Held<IDirect3DTexture9> source;
		const HRESULT made = managed_texture(device, SIDE, SIDE, 1, COMPRESSED[index], &source.Pointer);
		if (FAILED(made)) {
			out.push_back(capture_result(name + " (no such texture here)", made));
			continue;
		}
		fill_random(source.Pointer, 0);
		if (COMPRESSED[index] != D3DFMT_DXT1) {
			order_end_points(source.Pointer);
		}
		Held<IDirect3DSurface9> source_surface;
		source.Pointer->GetSurfaceLevel(0, &source_surface.Pointer);

		Held<IDirect3DTexture9> same;
		managed_texture(device, SIDE, SIDE, 1, COMPRESSED[index], &same.Pointer);
		Held<IDirect3DSurface9> same_surface;
		same.Pointer->GetSurfaceLevel(0, &same_surface.Pointer);
		HRESULT result = D3DXLoadSurfaceFromSurface(same_surface.Pointer, NULL, NULL, source_surface.Pointer, NULL,
			NULL, D3DX_FILTER_NONE, 0);
		out.push_back(capture_surface(device, name + " to itself", result, same_surface.Pointer));

		Held<IDirect3DTexture9> decoded;
		managed_texture(device, SIDE, SIDE, 1, D3DFMT_A8R8G8B8, &decoded.Pointer);
		Held<IDirect3DSurface9> decoded_surface;
		decoded.Pointer->GetSurfaceLevel(0, &decoded_surface.Pointer);
		result = D3DXLoadSurfaceFromSurface(decoded_surface.Pointer, NULL, NULL, source_surface.Pointer, NULL, NULL,
			D3DX_FILTER_NONE, 0);
		out.push_back(capture_surface(device, name + " decoded to A8R8G8B8", result, decoded_surface.Pointer));
	}
}

static void render_target_cases(IDirect3DDevice9 * device, std::vector<Capture> & out)
{
	// Read: a render target that will not lock, two colours, into a managed texture.
	Held<IDirect3DSurface9> target;
	device->CreateRenderTarget(SIDE, SIDE, D3DFMT_X8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &target.Pointer, NULL);
	const RECT left = { 0, 0, (LONG)SIDE / 2, (LONG)SIDE };
	const RECT right = { (LONG)SIDE / 2, 0, (LONG)SIDE, (LONG)SIDE };
	device->ColorFill(target.Pointer, &left, D3DCOLOR_ARGB(255, 200, 100, 50));
	device->ColorFill(target.Pointer, &right, D3DCOLOR_ARGB(255, 10, 220, 130));
	Held<IDirect3DTexture9> destination;
	managed_texture(device, SIDE, SIDE, 1, D3DFMT_A8R8G8B8, &destination.Pointer);
	Held<IDirect3DSurface9> surface;
	destination.Pointer->GetSurfaceLevel(0, &surface.Pointer);
	HRESULT result = D3DXLoadSurfaceFromSurface(surface.Pointer, NULL, NULL, target.Pointer, NULL, NULL,
		D3DX_FILTER_NONE, 0);
	out.push_back(capture_surface(device, "a render target read back", result, surface.Pointer));

	// Write: into a default-pool render target texture, which will not lock either.
	Held<IDirect3DTexture9> drawn;
	device->CreateTexture(SIDE, SIDE, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &drawn.Pointer, NULL);
	Held<IDirect3DSurface9> drawn_surface;
	drawn.Pointer->GetSurfaceLevel(0, &drawn_surface.Pointer);
	result = D3DXLoadSurfaceFromSurface(drawn_surface.Pointer, NULL, NULL, surface.Pointer, NULL, NULL,
		D3DX_FILTER_NONE, 0);
	out.push_back(capture_surface(device, "a render target written", result, drawn_surface.Pointer));
}

static void creation_cases(IDirect3DDevice9 * device, std::vector<Capture> & out)
{
	struct Asked { UINT Width; UINT Height; UINT Levels; D3DFORMAT Format; };
	static const Asked ASKED[] = {
		{ SIDE, SIDE, 0, D3DFMT_A8R8G8B8 },
		{ SIDE, SIDE, D3DX_DEFAULT, D3DFMT_R5G6B5 },
		{ 0, 0, 1, D3DFMT_A4R4G4B4 },
		{ D3DX_DEFAULT, D3DX_DEFAULT, 1, D3DFMT_A8R8G8B8 },
		{ 100, 60, 0, D3DFMT_X8R8G8B8 },
		{ SIDE, SIDE, 1, D3DFMT_R8G8B8 },
		{ SIDE, SIDE, 1, D3DFMT_A8R3G3B2 },
		{ SIDE, SIDE, 1, D3DFMT_UNKNOWN },
		{ SIDE, SIDE, 0, D3DFMT_DXT1 },
	};
	for (size_t index = 0; index < sizeof(ASKED) / sizeof(ASKED[0]); ++index) {
		char name[96];
		snprintf(name, sizeof(name), "D3DXCreateTexture %dx%d, %d levels, %s", (int)ASKED[index].Width,
			(int)ASKED[index].Height, (int)ASKED[index].Levels, format_name(ASKED[index].Format).c_str());
		Held<IDirect3DTexture9> texture;
		const HRESULT result = D3DXCreateTexture(device, ASKED[index].Width, ASKED[index].Height, ASKED[index].Levels,
			0, ASKED[index].Format, D3DPOOL_MANAGED, &texture.Pointer);
		out.push_back(capture_description(name, result, texture.Pointer));
	}
}

static void shader_cases(IDirect3DDevice9 * device, std::vector<Capture> & out)
{
	Held<ID3DXBuffer> river;
	HRESULT result = D3DXAssembleShader(RIVER_PIXEL_SHADER, (UINT)strlen(RIVER_PIXEL_SHADER), NULL, NULL, 0,
		&river.Pointer, NULL);
	out.push_back(capture_tokens("the river ps.1.1 assembled", result,
		river.Pointer != NULL ? (const DWORD *)river.Pointer->GetBufferPointer() : NULL));

	Held<ID3DXBuffer> vertex;
	result = D3DXAssembleShader(REPAIRED_VERTEX_SHADER, (UINT)strlen(REPAIRED_VERTEX_SHADER), NULL, NULL, 0,
		&vertex.Pointer, NULL);
	out.push_back(capture_tokens("a vs.1.1 assembled", result,
		vertex.Pointer != NULL ? (const DWORD *)vertex.Pointer->GetBufferPointer() : NULL));

	// The shipped .pso files' path: disassembled, repaired and assembled again (d3d8shadertranslate.cpp).
	if (river.Pointer != NULL) {
		Held<IDirect3DPixelShader9> translated;
		result = Create_Translated_Pixel_Shader(device, (const RenderUInt32 *)river.Pointer->GetBufferPointer(),
			&translated.Pointer);
		std::vector<DWORD> function;
		if (translated.Pointer != NULL) {
			UINT size = 0;
			translated.Pointer->GetFunction(NULL, &size);
			function.resize(size / sizeof(DWORD));
			translated.Pointer->GetFunction(function.data(), &size);
		}
		out.push_back(capture_tokens("the river shader translated", result, function.empty() ? NULL : function.data()));
	}

	// The fixed-function combiners' compiler (ffshadercache.cpp).  The two compilers are years apart, so only
	// whether it compiles and the device takes it is compared, not the tokens.
	Held<ID3DXBuffer> compiled;
	result = D3DXCompileShader(COMBINER_PIXEL_SHADER, (UINT)strlen(COMBINER_PIXEL_SHADER), NULL, NULL, "main",
		"ps_2_0", 0, &compiled.Pointer, NULL, NULL);
	Held<IDirect3DPixelShader9> combiner;
	if (compiled.Pointer != NULL) {
		result = device->CreatePixelShader((const DWORD *)compiled.Pointer->GetBufferPointer(), &combiner.Pointer);
	}
	out.push_back(capture_result("a ps_2_0 combiner compiled and created", result));
}

static std::vector<Capture> run_cases(IDirect3DDevice9 * device)
{
	random_state = DEVICE_SEED;		// the same inputs for both bindings
	std::vector<Capture> out;
	copy_cases(device, out);
	mip_cases(device, out);
	compressed_cases(device, out);
	render_target_cases(device, out);
	creation_cases(device, out);
	shader_cases(device, out);
	return out;
}

/// The bits of the narrowest channel a format keeps, for the tolerance: one step of it.
static int narrowest_channel(D3DFORMAT format)
{
	switch (format) {
	case D3DFMT_R5G6B5: case D3DFMT_X1R5G5B5: case D3DFMT_DXT1: case D3DFMT_DXT3: case D3DFMT_DXT5:
		return 5;
	case D3DFMT_A1R5G5B5:
		return 1;
	case D3DFMT_A4R4G4B4: case D3DFMT_X4R4G4B4:
		return 4;
	default:
		return 8;
	}
}

/// The largest channel difference between two images of one format, in steps of its narrowest channel.
static double worst_steps(const Capture & dll, const Capture & port)
{
	PosixFormatLayout layout;
	posixFormatLayout(dll.Format, &layout);
	const double step = 1.0 / ((1 << narrowest_channel(dll.Format)) - 1);
	double worst = 0.0;
	for (size_t offset = 0; offset + layout.bytesPerBlock <= dll.Bytes.size(); offset += layout.bytesPerBlock) {
		PosixColor a[16], b[16];
		posixDecodeBlock(dll.Format, &dll.Bytes[offset], a);
		posixDecodeBlock(port.Format, &port.Bytes[offset], b);
		for (UINT pixel = 0; pixel < layout.blockWidth * layout.blockHeight; ++pixel) {
			const double d = fmax(fmax(fabs(a[pixel].r - b[pixel].r), fabs(a[pixel].g - b[pixel].g)),
				fmax(fabs(a[pixel].b - b[pixel].b), fabs(a[pixel].a - b[pixel].a)));
			worst = fmax(worst, d / step);
		}
	}
	return worst;
}

static void compare_captures(const std::vector<Capture> & dll, const std::vector<Capture> & port)
{
	const double ROUNDING_SLACK = 1e-3;
	if (dll.size() != port.size()) {
		++failures;
		printf("FAIL: the DLL made %u results, the port %u\n", (unsigned)dll.size(), (unsigned)port.size());
		return;
	}
	unsigned identical = 0;
	for (size_t index = 0; index < dll.size(); ++index) {
		const Capture & a = dll[index];
		const Capture & b = port[index];
		const bool same_shape = a.Result == b.Result && a.Format == b.Format && a.Width == b.Width
			&& a.Height == b.Height && a.Bytes.size() == b.Bytes.size();
		if (!same_shape) {
			++failures;
			printf("FAIL: %s: the DLL gave 0x%08lx %s %ux%u (%u bytes), the port 0x%08lx %s %ux%u (%u bytes)\n",
				a.Name.c_str(), (unsigned long)a.Result, format_name(a.Format).c_str(), a.Width, a.Height,
				(unsigned)a.Bytes.size(), (unsigned long)b.Result, format_name(b.Format).c_str(), b.Width, b.Height,
				(unsigned)b.Bytes.size());
			continue;
		}
		if (a.Bytes == b.Bytes) {
			++identical;
			continue;
		}
		if (a.Format == D3DFMT_UNKNOWN || !posixCanDecode(a.Format)) {
			++failures;
			printf("FAIL: %s: the bytes differ\n", a.Name.c_str());
			continue;
		}
		const double steps = worst_steps(a, b);
		printf("%-44s differs by at most %.2f step(s) of its narrowest channel\n", a.Name.c_str(), steps);
		if (steps > a.ToleranceSteps + ROUNDING_SLACK) {
			++failures;
			printf("FAIL: %s: more than %.0f step(s) from the DLL\n", a.Name.c_str(), a.ToleranceSteps);
		}
	}
	printf("device           %6u results compared, %6u byte-identical to the DLL\n", (unsigned)dll.size(), identical);
}

static void check_on_device()
{
	WNDCLASSA window_class = {};
	window_class.lpfnWndProc = DefWindowProcA;
	window_class.hInstance = GetModuleHandleA(NULL);
	window_class.lpszClassName = "d3dx9portable_oracle";
	RegisterClassA(&window_class);
	const HWND window = CreateWindowA(window_class.lpszClassName, "d3dx9portable_oracle", WS_OVERLAPPEDWINDOW, 0, 0,
		SIDE, SIDE, NULL, NULL, window_class.hInstance, NULL);
	Held<IDirect3D9> direct3d;
	direct3d.Pointer = Direct3DCreate9(D3D_SDK_VERSION);
	D3DPRESENT_PARAMETERS present = {};
	present.Windowed = TRUE;
	present.SwapEffect = D3DSWAPEFFECT_DISCARD;
	present.BackBufferFormat = D3DFMT_X8R8G8B8;
	present.BackBufferWidth = SIDE;
	present.BackBufferHeight = SIDE;
	present.hDeviceWindow = window;
	Held<IDirect3DDevice9> device;
	const HRESULT created = direct3d.Pointer == NULL ? E_FAIL : direct3d.Pointer->CreateDevice(D3DADAPTER_DEFAULT,
		D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &present,
		&device.Pointer);
	if (FAILED(created)) {
		printf("device           skipped: no Direct3D 9 device here (0x%08lx)\n", (unsigned long)created);
		DestroyWindow(window);
		return;
	}

	const std::vector<Capture> dll = run_cases(device.Pointer);

	Unbind_D3DX9_Runtime();
	_putenv_s(PORTABLE_VARIABLE, "1");
	Bind_D3DX9_Runtime();
	if (strncmp(D3DX9_Runtime_Name(), PORTABLE_NAME_PREFIX, strlen(PORTABLE_NAME_PREFIX)) != 0) {
		++failures;
		printf("FAIL: %s=1 bound %s, not the port's own\n", PORTABLE_VARIABLE, D3DX9_Runtime_Name());
	}
	printf("device           the port bound as: %s\n", D3DX9_Runtime_Name());
	const std::vector<Capture> port = run_cases(device.Pointer);
	Unbind_D3DX9_Runtime();
	_putenv_s(PORTABLE_VARIABLE, "");

	compare_captures(dll, port);
	device.Pointer->Release();
	device.Pointer = NULL;
	DestroyWindow(window);
}

int main()
{
	if (!Bind_D3DX9_Runtime() || strcmp(D3DX9_Runtime_Name(), DLL_NAME) != 0) {
		printf("SKIP: d3dx9_43.dll did not bind here (ARM64, or no DirectX redistributable): nothing to compare against\n");
		return 77;
	}

	check_fvf_sizes();
	check_exact();
	check_computed();
	check_singular();
	check_on_device();

	if (failures != 0) {
		printf("test_d3dx9portable_oracle: %d FAILED\n", failures);
		return 1;
	}
	printf("test_d3dx9portable_oracle: the port agrees with d3dx9_43.dll\n");
	return 0;
}
