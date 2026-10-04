/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
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
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

#include "d3dx9runtime.h"
#include "d3dx9math.h"
#include "d3dx9portable.h"
#include "d3dx9portable_texture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

D3DXAssembleShaderFunction			D3DXAssembleShader = NULL;
D3DXCompileShaderFunction			D3DXCompileShader = NULL;
D3DXDisassembleShaderFunction		D3DXDisassembleShader = NULL;
D3DXCreateTextureFunction			D3DXCreateTexture = NULL;
D3DXCreateCubeTextureFunction		D3DXCreateCubeTexture = NULL;
D3DXCreateVolumeTextureFunction		D3DXCreateVolumeTexture = NULL;
D3DXCreateTextureFromFileExFunction	D3DXCreateTextureFromFileExA = NULL;
D3DXFilterTextureFunction			D3DXFilterTexture = NULL;
D3DXLoadSurfaceFromSurfaceFunction	D3DXLoadSurfaceFromSurface = NULL;
D3DXGetFVFVertexSizeFunction		D3DXGetFVFVertexSize = NULL;

D3DXMatrixInverseFunction	D3DXMatrixInverse = NULL;
D3DXMatrixBinaryFunction	D3DXMatrixMultiply = NULL;
D3DXMatrixUnaryFunction		D3DXMatrixTranspose = NULL;
D3DXMatrixTripleFunction	D3DXMatrixScaling = NULL;
D3DXMatrixTripleFunction	D3DXMatrixTranslation = NULL;
D3DXMatrixAngleFunction		D3DXMatrixRotationZ = NULL;
D3DXVec4TransformFunction	D3DXVec4TransformFromDLL = NULL;
D3DXVec3TransformFunction	D3DXVec3Transform = NULL;

// The last D3DX9 release, and the one d3d8to9 binds, so a machine that runs this fork
// today already has it.  There is no fallback to an earlier d3dx9_NN.dll on purpose:
// the earlier ones differ in behaviour, and a renderer that silently landed on one
// would be the hardest kind of bug to see.
static const char D3DX9_MODULE_NAME[] = "d3dx9_43.dll";

// Without D3DX9, its shader assembler, compiler and disassembler come from Microsoft's HLSL compiler,
// which ships with every Windows this fork runs on, ARM64's included.  It is the DLL the Direct3D 11
// backend compiles with (dx11backend.cpp).
static const char COMPILER_MODULE_NAME[] = "d3dcompiler_47.dll";

// Set to anything but "" or "0", it binds the port's own D3DX on x64 as well, which is how the code
// Windows on ARM64 runs is run and compared on a machine that has d3dx9_43.dll.
static const char PORTABLE_VARIABLE[] = "ZH_D3DX_PORTABLE";

static HMODULE D3DX9Module = NULL;
static HMODULE CompilerModule = NULL;
static bool BindAttempted = false;
static bool BindSucceeded = false;
static bool BoundPortable = false;

static void release_module(void);

/* The port's own D3DX, for Windows on Arm, which has no DLL: Microsoft shipped d3dx9_43.dll for x86 and
	 x64 only, and an ARM64 process cannot load either.  An x64 machine that lacks the DLL lands here too.
	 - The arithmetic is d3dx9portable.cpp, what macOS and Linux use, held against the DLL by
		 test_d3dx9portable_oracle on x64.  None of it reaches the simulation, whose D3DXVec4Transform is
		 d3dx9math.h's.
	 - The texture helpers are d3dx9portable_texture.cpp: PosixImageOps, the macOS and Linux surface code,
		 over the real device.
	 - The shader assembler, compiler and disassembler are d3dcompiler_47.dll's D3DAssemble, D3DCompile and
		 D3DDisassemble, which take the same arguments as D3DX's and hand back ID3DBlob, whose methods are
		 ID3DXBuffer's in the same order.  Without that DLL they refuse, and the water and the shipped
		 shaders keep their fixed-function paths.
	 - Loading an image file refuses: every texture of the game comes through WW3D2's own loaders. */
static bool portable_wanted(void)
{
#if defined(_M_ARM64)
	return true;
#else
	const char * value = getenv(PORTABLE_VARIABLE);
	return value != NULL && value[0] != '\0' && strcmp(value, "0") != 0;
#endif
}

static HRESULT WINAPI portable_matrix_inverse(D3DXMATRIX * out, FLOAT * determinant, const D3DXMATRIX * matrix)
{
	// No caller reads the result; the DLL's is the matrix pointer, or null when it is singular.
	return D3DXPortable_Matrix_Inverse(out, determinant, matrix) != NULL ? S_OK : E_FAIL;
}

static UINT WINAPI portable_fvf_vertex_size(DWORD fvf)
{
	return D3DXPortable_FVF_Vertex_Size(fvf);
}

typedef HRESULT (WINAPI * D3DAssembleFunction)(LPCVOID data, SIZE_T size, LPCSTR file_name,
	const D3DXMACRO * defines, LPD3DXINCLUDE include, UINT flags, LPD3DXBUFFER * shader, LPD3DXBUFFER * errors);
typedef HRESULT (WINAPI * D3DCompileFunction)(LPCVOID data, SIZE_T size, LPCSTR file_name,
	const D3DXMACRO * defines, LPD3DXINCLUDE include, LPCSTR entry_point, LPCSTR target, UINT flags,
	UINT effect_flags, LPD3DXBUFFER * shader, LPD3DXBUFFER * errors);
typedef HRESULT (WINAPI * D3DDisassembleFunction)(LPCVOID data, SIZE_T size, UINT flags, LPCSTR comments,
	LPD3DXBUFFER * disassembly);

static D3DAssembleFunction CompilerAssemble = NULL;
static D3DCompileFunction CompilerCompile = NULL;
static D3DDisassembleFunction CompilerDisassemble = NULL;

static HRESULT WINAPI portable_assemble_shader(LPCSTR source, UINT source_length, const D3DXMACRO * defines,
	LPD3DXINCLUDE include, DWORD flags, LPD3DXBUFFER * shader, LPD3DXBUFFER * errors)
{
	return CompilerAssemble(source, source_length, NULL, defines, include, flags, shader, errors);
}

static HRESULT WINAPI portable_compile_shader(LPCSTR source, UINT source_length, const D3DXMACRO * defines,
	LPD3DXINCLUDE include, LPCSTR entry_point, LPCSTR profile, DWORD flags, LPD3DXBUFFER * shader,
	LPD3DXBUFFER * errors, void ** constant_table)
{
	// D3DCompile has no constant table to hand back; no caller asks for one.
	if (constant_table != NULL) {
		*constant_table = NULL;
	}
	return CompilerCompile(source, source_length, NULL, defines, include, entry_point, profile, flags, 0,
		shader, errors);
}

static HRESULT WINAPI portable_disassemble_shader(const DWORD * shader, BOOL colour_code, LPCSTR comments,
	LPD3DXBUFFER * disassembly)
{
	// D3DX finds the end itself; D3DDisassemble wants the size.  A token stream ends at D3DSIO_END, and a
	// comment is stepped over by its own length, so no text inside one can end the walk.  A def constant
	// whose bits are 0x0000FFFF (a denormal) would; no shader of the game's has one.
	const DWORD * token = shader + 1;
	while (*token != D3DSIO_END) {
		if ((*token & D3DSI_OPCODE_MASK) == D3DSIO_COMMENT) {
			token += (*token & D3DSI_COMMENTSIZE_MASK) >> D3DSI_COMMENTSIZE_SHIFT;
		}
		++token;
	}
	const SIZE_T size = (SIZE_T)(token + 1 - shader) * sizeof(DWORD);
	return CompilerDisassemble(shader, size, colour_code ? 1 : 0, comments, disassembly);
}

static HRESULT WINAPI unavailable_assemble_shader(LPCSTR, UINT, const D3DXMACRO *, LPD3DXINCLUDE, DWORD,
	LPD3DXBUFFER * shader, LPD3DXBUFFER * errors)
{
	if (shader != NULL) *shader = NULL;
	if (errors != NULL) *errors = NULL;
	return D3DERR_NOTAVAILABLE;
}

static HRESULT WINAPI unavailable_compile_shader(LPCSTR, UINT, const D3DXMACRO *, LPD3DXINCLUDE, LPCSTR,
	LPCSTR, DWORD, LPD3DXBUFFER * shader, LPD3DXBUFFER * errors, void ** constant_table)
{
	if (shader != NULL) *shader = NULL;
	if (errors != NULL) *errors = NULL;
	if (constant_table != NULL) *constant_table = NULL;
	return D3DERR_NOTAVAILABLE;
}

static HRESULT WINAPI unavailable_disassemble_shader(const DWORD *, BOOL, LPCSTR, LPD3DXBUFFER * disassembly)
{
	if (disassembly != NULL) *disassembly = NULL;
	return D3DERR_NOTAVAILABLE;
}

static HRESULT WINAPI unavailable_texture_from_file(LPDIRECT3DDEVICE9, LPCSTR, UINT, UINT, UINT, DWORD,
	D3DFORMAT, D3DPOOL, DWORD, DWORD, D3DCOLOR, D3DXIMAGE_INFO *, PALETTEENTRY *, LPDIRECT3DTEXTURE9 * texture)
{
	if (texture != NULL) *texture = NULL;
	return D3DERR_NOTAVAILABLE;
}

static void bind_compiler(void)
{
	CompilerModule = LoadLibraryA(COMPILER_MODULE_NAME);
	if (CompilerModule != NULL) {
		CompilerAssemble = (D3DAssembleFunction)GetProcAddress(CompilerModule, "D3DAssemble");
		CompilerCompile = (D3DCompileFunction)GetProcAddress(CompilerModule, "D3DCompile");
		CompilerDisassemble = (D3DDisassembleFunction)GetProcAddress(CompilerModule, "D3DDisassemble");
	}
	D3DXAssembleShader = CompilerAssemble != NULL ? portable_assemble_shader : unavailable_assemble_shader;
	D3DXCompileShader = CompilerCompile != NULL ? portable_compile_shader : unavailable_compile_shader;
	D3DXDisassembleShader = CompilerDisassemble != NULL ? portable_disassemble_shader : unavailable_disassemble_shader;
}

static void bind_portable_runtime(void)
{
	D3DXCreateTexture = D3DXPortable_Create_Texture;
	D3DXCreateCubeTexture = D3DXPortable_Create_Cube_Texture;
	D3DXCreateVolumeTexture = D3DXPortable_Create_Volume_Texture;
	D3DXCreateTextureFromFileExA = unavailable_texture_from_file;
	D3DXFilterTexture = D3DXPortable_Filter_Texture;
	D3DXLoadSurfaceFromSurface = D3DXPortable_Load_Surface_From_Surface;
	D3DXGetFVFVertexSize = portable_fvf_vertex_size;
	bind_compiler();

	D3DXMatrixInverse = portable_matrix_inverse;
	D3DXMatrixMultiply = (D3DXMatrixBinaryFunction)D3DXPortable_Matrix_Multiply;
	D3DXMatrixTranspose = (D3DXMatrixUnaryFunction)D3DXPortable_Matrix_Transpose;
	D3DXMatrixScaling = (D3DXMatrixTripleFunction)D3DXPortable_Matrix_Scaling;
	D3DXMatrixTranslation = (D3DXMatrixTripleFunction)D3DXPortable_Matrix_Translation;
	D3DXMatrixRotationZ = (D3DXMatrixAngleFunction)D3DXPortable_Matrix_Rotation_Z;
	D3DXVec3Transform = (D3DXVec3TransformFunction)D3DXPortable_Vec3_Transform;
}

static bool bind_portable_fallback(void)
{
	bind_portable_runtime();
	BoundPortable = true;
	BindSucceeded = true;
	return true;
}

struct ErrorName
{
	HRESULT Result;
	const char * Name;
};

// Every code the renderer's own paths return.  D3DERR values are D3D_OK plus the
// Direct3D facility, so they cannot be written as plain integers here.
static const ErrorName ERROR_NAMES[] =
{
	{ D3D_OK,							"D3D_OK" },
	{ D3DERR_DEVICELOST,				"D3DERR_DEVICELOST" },
	{ D3DERR_DEVICENOTRESET,			"D3DERR_DEVICENOTRESET" },
	{ D3DERR_DRIVERINTERNALERROR,		"D3DERR_DRIVERINTERNALERROR" },
	{ D3DERR_INVALIDCALL,				"D3DERR_INVALIDCALL" },
	{ D3DERR_INVALIDDEVICE,				"D3DERR_INVALIDDEVICE" },
	{ D3DERR_NOTAVAILABLE,				"D3DERR_NOTAVAILABLE" },
	{ D3DERR_NOTFOUND,					"D3DERR_NOTFOUND" },
	{ D3DERR_OUTOFVIDEOMEMORY,			"D3DERR_OUTOFVIDEOMEMORY" },
	{ D3DERR_TOOMANYOPERATIONS,			"D3DERR_TOOMANYOPERATIONS" },
	{ D3DERR_UNSUPPORTEDTEXTUREFILTER,	"D3DERR_UNSUPPORTEDTEXTUREFILTER" },
	{ D3DERR_WRONGTEXTUREFORMAT,		"D3DERR_WRONGTEXTUREFORMAT" },
	{ E_OUTOFMEMORY,					"E_OUTOFMEMORY" },
	{ E_INVALIDARG,						"E_INVALIDARG" },
	{ E_FAIL,							"E_FAIL" }
};

static const int ERROR_NAME_COUNT = sizeof(ERROR_NAMES) / sizeof(ERROR_NAMES[0]);
static const int ERROR_TEXT_SIZE = 32;

static char UnknownErrorText[ERROR_TEXT_SIZE] = "";

bool Bind_D3DX9_Runtime(void)
{
	if (BindAttempted) {
		return BindSucceeded;
	}
	BindAttempted = true;

	if (portable_wanted()) {
		return bind_portable_fallback();
	}

	// A repack that never ran the DirectX June 2010 redistributable has no d3dx9_43.dll.  It gets
	// the code Windows on ARM64 runs instead of a null D3DXCreateTexture at the first texture.
	D3DX9Module = LoadLibraryA(D3DX9_MODULE_NAME);
	if (D3DX9Module == NULL) {
		return bind_portable_fallback();
	}

	D3DXAssembleShader = (D3DXAssembleShaderFunction)
		GetProcAddress(D3DX9Module, "D3DXAssembleShader");
	D3DXCompileShader = (D3DXCompileShaderFunction)
		GetProcAddress(D3DX9Module, "D3DXCompileShader");
	D3DXDisassembleShader = (D3DXDisassembleShaderFunction)
		GetProcAddress(D3DX9Module, "D3DXDisassembleShader");
	D3DXCreateTexture = (D3DXCreateTextureFunction)
		GetProcAddress(D3DX9Module, "D3DXCreateTexture");
	D3DXCreateCubeTexture = (D3DXCreateCubeTextureFunction)
		GetProcAddress(D3DX9Module, "D3DXCreateCubeTexture");
	D3DXCreateVolumeTexture = (D3DXCreateVolumeTextureFunction)
		GetProcAddress(D3DX9Module, "D3DXCreateVolumeTexture");
	D3DXCreateTextureFromFileExA = (D3DXCreateTextureFromFileExFunction)
		GetProcAddress(D3DX9Module, "D3DXCreateTextureFromFileExA");
	D3DXFilterTexture = (D3DXFilterTextureFunction)
		GetProcAddress(D3DX9Module, "D3DXFilterTexture");
	D3DXLoadSurfaceFromSurface = (D3DXLoadSurfaceFromSurfaceFunction)
		GetProcAddress(D3DX9Module, "D3DXLoadSurfaceFromSurface");
	D3DXGetFVFVertexSize = (D3DXGetFVFVertexSizeFunction)
		GetProcAddress(D3DX9Module, "D3DXGetFVFVertexSize");

	D3DXMatrixInverse = (D3DXMatrixInverseFunction)
		GetProcAddress(D3DX9Module, "D3DXMatrixInverse");
	D3DXMatrixMultiply = (D3DXMatrixBinaryFunction)
		GetProcAddress(D3DX9Module, "D3DXMatrixMultiply");
	D3DXMatrixTranspose = (D3DXMatrixUnaryFunction)
		GetProcAddress(D3DX9Module, "D3DXMatrixTranspose");
	D3DXMatrixScaling = (D3DXMatrixTripleFunction)
		GetProcAddress(D3DX9Module, "D3DXMatrixScaling");
	D3DXMatrixTranslation = (D3DXMatrixTripleFunction)
		GetProcAddress(D3DX9Module, "D3DXMatrixTranslation");
	D3DXMatrixRotationZ = (D3DXMatrixAngleFunction)
		GetProcAddress(D3DX9Module, "D3DXMatrixRotationZ");
	D3DXVec4TransformFromDLL = (D3DXVec4TransformFunction)
		GetProcAddress(D3DX9Module, "D3DXVec4Transform");
	D3DXVec3Transform = (D3DXVec3TransformFunction)
		GetProcAddress(D3DX9Module, "D3DXVec3Transform");

	BindSucceeded = D3DXAssembleShader != NULL
		&& D3DXCompileShader != NULL
		&& D3DXDisassembleShader != NULL
		&& D3DXCreateTexture != NULL
		&& D3DXCreateCubeTexture != NULL
		&& D3DXCreateVolumeTexture != NULL
		&& D3DXCreateTextureFromFileExA != NULL
		&& D3DXFilterTexture != NULL
		&& D3DXLoadSurfaceFromSurface != NULL
		&& D3DXGetFVFVertexSize != NULL
		&& D3DXMatrixInverse != NULL
		&& D3DXMatrixMultiply != NULL
		&& D3DXMatrixTranspose != NULL
		&& D3DXMatrixScaling != NULL
		&& D3DXMatrixTranslation != NULL
		&& D3DXMatrixRotationZ != NULL
		&& D3DXVec4TransformFromDLL != NULL
		&& D3DXVec3Transform != NULL;

	if (!BindSucceeded) {
		release_module();
		return bind_portable_fallback();
	}
	return BindSucceeded;
}

void Unbind_D3DX9_Runtime(void)
{
	release_module();
	BindAttempted = false;
	BindSucceeded = false;
	BoundPortable = false;
}

const char * D3DX9_Runtime_Name(void)
{
	if (!BindSucceeded) {
		return "none";
	}
	if (!BoundPortable) {
		return D3DX9_MODULE_NAME;
	}
	return CompilerModule != NULL ? "the port's own, shaders by d3dcompiler_47.dll" : "the port's own, no shaders";
}

static void release_module(void)
{
	D3DXAssembleShader = NULL;
	D3DXCompileShader = NULL;
	D3DXDisassembleShader = NULL;
	D3DXCreateTexture = NULL;
	D3DXCreateCubeTexture = NULL;
	D3DXCreateVolumeTexture = NULL;
	D3DXCreateTextureFromFileExA = NULL;
	D3DXFilterTexture = NULL;
	D3DXLoadSurfaceFromSurface = NULL;
	D3DXGetFVFVertexSize = NULL;

	D3DXMatrixInverse = NULL;
	D3DXMatrixMultiply = NULL;
	D3DXMatrixTranspose = NULL;
	D3DXMatrixScaling = NULL;
	D3DXMatrixTranslation = NULL;
	D3DXMatrixRotationZ = NULL;
	D3DXVec4TransformFromDLL = NULL;
	D3DXVec3Transform = NULL;

	CompilerAssemble = NULL;
	CompilerCompile = NULL;
	CompilerDisassemble = NULL;

	if (D3DX9Module != NULL) {
		FreeLibrary(D3DX9Module);
		D3DX9Module = NULL;
	}
	if (CompilerModule != NULL) {
		FreeLibrary(CompilerModule);
		CompilerModule = NULL;
	}
}

UINT Get_FVF_Vertex_Size(DWORD fvf)
{
	if (!Bind_D3DX9_Runtime()) {
		return 0;
	}
	return D3DXGetFVFVertexSize(fvf);
}

const char * Get_D3D_Error_String(HRESULT result)
{
	for (int index = 0; index < ERROR_NAME_COUNT; ++index) {
		if (ERROR_NAMES[index].Result == result) {
			return ERROR_NAMES[index].Name;
		}
	}
	snprintf(UnknownErrorText, ERROR_TEXT_SIZE, "0x%08lx", (unsigned long)result);
	UnknownErrorText[ERROR_TEXT_SIZE - 1] = '\0';
	return UnknownErrorText;
}
