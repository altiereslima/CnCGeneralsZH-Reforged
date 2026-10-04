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
// Portions adapted from GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/d3dx9runtime.cpp by Olcay Seygan (upstream CnCGeneralsZH-Reforged), GPL-3.0-or-later.

// D3DX9 off Windows (decision 7, phase A1): what d3dx9runtime.h and d3dx9math.h declare, with bodies
// of our own where Windows binds d3dx9_43.dll.  The headers say what each group does and why.

#include "d3dx9runtime.h"
#include "d3dx9math.h"
#include "d3dx9posix.h"
#include "d3dx9portable.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#if defined(_WIN32)
#error "d3dx9posix.cpp is the POSIX D3DX; Windows binds d3dx9_43.dll in d3dx9runtime.cpp"
#endif

//-------------------------------------------------------------------------------------------------
// The matrix functions and the FVF size are d3dx9portable.cpp's, which Windows on ARM64 shares.
//-------------------------------------------------------------------------------------------------

D3DXMATRIX * D3DXMatrixInverse(D3DXMATRIX * out, float * determinant, const D3DXMATRIX * matrix)
{
	return D3DXPortable_Matrix_Inverse(out, determinant, matrix);
}

D3DXMATRIX * D3DXMatrixMultiply(D3DXMATRIX * out, const D3DXMATRIX * left, const D3DXMATRIX * right)
{
	return D3DXPortable_Matrix_Multiply(out, left, right);
}

D3DXMATRIX * D3DXMatrixTranspose(D3DXMATRIX * out, const D3DXMATRIX * matrix)
{
	return D3DXPortable_Matrix_Transpose(out, matrix);
}

D3DXMATRIX * D3DXMatrixScaling(D3DXMATRIX * out, float x, float y, float z)
{
	return D3DXPortable_Matrix_Scaling(out, x, y, z);
}

D3DXMATRIX * D3DXMatrixTranslation(D3DXMATRIX * out, float x, float y, float z)
{
	return D3DXPortable_Matrix_Translation(out, x, y, z);
}

D3DXMATRIX * D3DXMatrixRotationZ(D3DXMATRIX * out, float angle)
{
	return D3DXPortable_Matrix_Rotation_Z(out, angle);
}

D3DXVECTOR4 * D3DXVec3Transform(D3DXVECTOR4 * out, const D3DXVECTOR3 * vector, const D3DXMATRIX * matrix)
{
	return D3DXPortable_Vec3_Transform(out, vector, matrix);
}

//-------------------------------------------------------------------------------------------------
// The shader functions: failures, each said once.  The texture functions are in
// d3dx9posix_texture.cpp.
//-------------------------------------------------------------------------------------------------

static void say_once(bool & said, const char * what)
{
	if (!said) {
		said = true;
		fprintf(stderr, "D3DX: %s is not available off Windows yet; the call fails\n", what);
	}
}

/* D3DXAssembleShader off Windows is NOT an assembler.  It wraps the source text, whole and unchanged,
	 in a comment token between the version token its first line names and the end token:

		 version (0xFFFF0101 for ps.1.1, 0xFFFE0101 for vs.1.1)
		 0xFFFE | (n << 16), then n DWORDs: STUB_SOURCE_TAG, the text's length in bytes, the text, zero-padded
		 0x0000FFFF

	 That is a well-formed D3D9 token stream with no instructions in it.  It exists so the water's
	 run-time `ps.1.1` programs reach CreatePixelShader and their registration, the one thing the POSIX
	 device reads: the device never runs D3D bytecode, it draws the program registered under that name
	 from D3's transcription (A3e, as the Direct3D 11 backend does), and a shader it cannot name is
	 refused at the draw, by name.  Text that does not start with a version it knows still fails.
	 D3DX9Posix_Stub_Shader_Source reads the text back out. */
static const RenderUInt32 STUB_SOURCE_TAG = 0x5253485A;	// "ZHSR" in memory: the stub's source

namespace {

class PosixD3DXBuffer : public ID3DXBuffer
{
public:
	explicit PosixD3DXBuffer(size_t size) : References(1), Bytes(size, 0) {}
	uint32_t AddRef() override { return ++References; }
	uint32_t Release() override
	{
		const uint32_t left = --References;
		if (left == 0) {
			delete this;
		}
		return left;
	}
	void * GetBufferPointer() override { return Bytes.empty() ? NULL : &Bytes[0]; }
	RenderUInt32 GetBufferSize() override { return (RenderUInt32)Bytes.size(); }

private:
	uint32_t References;
	std::vector<uint8_t> Bytes;
};

}  // namespace

/// The version token the text's first word names ("ps.1.1", "vs_1_1", ...), after any leading blanks
/// and comment lines; 0 when it names none.
static RenderUInt32 stub_version_of(const char * source, unsigned int length)
{
	unsigned int at = 0;
	for (;;) {
		while (at < length && (source[at] == ' ' || source[at] == '\t' || source[at] == '\r' || source[at] == '\n')) {
			++at;
		}
		if (at + 1 < length && ((source[at] == '/' && source[at + 1] == '/') || source[at] == ';')) {
			while (at < length && source[at] != '\n') {
				++at;
			}
			continue;
		}
		break;
	}
	if (at + 6 > length) {
		return 0;
	}
	const char * word = source + at;
	const bool pixel = word[0] == 'p' && word[1] == 's';
	const bool vertex = word[0] == 'v' && word[1] == 's';
	const bool separators = (word[2] == '.' || word[2] == '_') && (word[4] == '.' || word[4] == '_');
	if (!(pixel || vertex) || !separators || word[3] < '1' || word[3] > '3' || word[5] < '0' || word[5] > '4') {
		return 0;
	}
	const RenderUInt32 major = (RenderUInt32)(word[3] - '0'), minor = (RenderUInt32)(word[5] - '0');
	return (pixel ? 0xFFFF0000u : 0xFFFE0000u) | (major << 8) | minor;
}

static RenderResult posix_assemble_shader(const char * source, unsigned int source_length, const D3DXMACRO *,
	LPD3DXINCLUDE, RenderUInt32, LPD3DXBUFFER * shader, LPD3DXBUFFER * errors)
{
	if (errors != NULL) {
		*errors = NULL;
	}
	if (shader == NULL) {
		return D3DERR_INVALIDCALL;
	}
	*shader = NULL;
	const RenderUInt32 version = source != NULL ? stub_version_of(source, source_length) : 0;
	if (version == 0) {
		static bool said = false;
		if (!said) {
			said = true;
			fprintf(stderr, "D3DX: D3DXAssembleShader off Windows only wraps text that starts with a ps/vs version;"
				" this text names none, and the call fails\n");
		}
		return D3DERR_NOTAVAILABLE;
	}
	const size_t text_words = (source_length + 3) / 4;
	const size_t comment_words = 2 + text_words;		// the tag, the length, the text
	if (comment_words > 0x7FFF) {
		return D3DERR_INVALIDCALL;					// the comment token's length has 15 bits
	}
	PosixD3DXBuffer * buffer = new PosixD3DXBuffer((1 + 1 + comment_words + 1) * sizeof(RenderUInt32));
	RenderUInt32 * tokens = (RenderUInt32 *)buffer->GetBufferPointer();
	tokens[0] = version;
	tokens[1] = 0xFFFEu | ((RenderUInt32)comment_words << 16);
	tokens[2] = STUB_SOURCE_TAG;
	tokens[3] = source_length;
	memcpy(&tokens[4], source, source_length);
	tokens[4 + text_words] = 0x0000FFFFu;
	*shader = buffer;
	return D3D_OK;
}

bool D3DX9Posix_Stub_Shader_Source(const RenderUInt32 * tokens, std::string & source)
{
	if (tokens == NULL || (tokens[1] & 0xFFFFu) != 0xFFFEu || tokens[2] != STUB_SOURCE_TAG) {
		return false;
	}
	const RenderUInt32 comment_words = (tokens[1] >> 16) & 0x7FFF;
	const RenderUInt32 length = tokens[3];
	if (comment_words < 2 || (size_t)length > (size_t)(comment_words - 2) * 4) {
		return false;
	}
	source.assign((const char *)&tokens[4], length);
	return true;
}

static RenderResult posix_compile_shader(const char *, unsigned int, const D3DXMACRO *, LPD3DXINCLUDE,
	const char *, const char *, RenderUInt32, LPD3DXBUFFER * shader, LPD3DXBUFFER * errors,
	void ** constant_table)
{
	static bool said = false;
	say_once(said, "D3DXCompileShader (to D3D9 bytecode; the SDL3 GPU draw compiles its own)");
	*shader = NULL;
	if (errors != NULL) {
		*errors = NULL;
	}
	if (constant_table != NULL) {
		*constant_table = NULL;
	}
	return D3DERR_NOTAVAILABLE;
}

static RenderResult posix_disassemble_shader(const RenderUInt32 *, int, const char *,
	LPD3DXBUFFER * disassembly)
{
	static bool said = false;
	say_once(said, "D3DXDisassembleShader");
	*disassembly = NULL;
	return D3DERR_NOTAVAILABLE;
}

//-------------------------------------------------------------------------------------------------
// The pointers, and binding them.
//-------------------------------------------------------------------------------------------------

D3DXAssembleShaderFunction			D3DXAssembleShader = NULL;
D3DXCompileShaderFunction			D3DXCompileShader = NULL;
D3DXDisassembleShaderFunction		D3DXDisassembleShader = NULL;
D3DXCreateTextureFunction			D3DXCreateTexture = NULL;
D3DXCreateCubeTextureFunction		D3DXCreateCubeTexture = NULL;
D3DXCreateVolumeTextureFunction		D3DXCreateVolumeTexture = NULL;
D3DXCreateTextureFromFileExFunction	D3DXCreateTextureFromFileExA = NULL;
D3DXFilterTextureFunction			D3DXFilterTexture = NULL;
D3DXLoadSurfaceFromSurfaceFunction	D3DXLoadSurfaceFromSurface = NULL;

bool Bind_D3DX9_Runtime(void)
{
	D3DXAssembleShader = posix_assemble_shader;
	D3DXCompileShader = posix_compile_shader;
	D3DXDisassembleShader = posix_disassemble_shader;
	D3DXCreateTexture = D3DX9Posix_Create_Texture;
	D3DXCreateCubeTexture = D3DX9Posix_Create_Cube_Texture;
	D3DXCreateVolumeTexture = D3DX9Posix_Create_Volume_Texture;
	D3DXCreateTextureFromFileExA = D3DX9Posix_Create_Texture_From_File;
	D3DXFilterTexture = D3DX9Posix_Filter_Texture;
	D3DXLoadSurfaceFromSurface = D3DX9Posix_Load_Surface_From_Surface;
	return true;
}

void Unbind_D3DX9_Runtime(void)
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
}

//-------------------------------------------------------------------------------------------------
// The two helpers that need no binding.
//-------------------------------------------------------------------------------------------------

struct ErrorName
{
	RenderResult Result;
	const char * Name;
};

// d3dx9runtime.cpp's list, less the three COM codes (E_OUTOFMEMORY, E_INVALIDARG, E_FAIL): their
// names are winerror.h's, and RENDER_FAIL is E_FAIL's value, so that one is named by value.
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
	{ RENDER_FAIL,						"E_FAIL" }
};

static char UnknownErrorText[32] = "";

const char * Get_D3D_Error_String(RenderResult result)
{
	for (size_t index = 0; index < sizeof(ERROR_NAMES) / sizeof(ERROR_NAMES[0]); ++index) {
		if (ERROR_NAMES[index].Result == result) {
			return ERROR_NAMES[index].Name;
		}
	}
	// Through uint32_t: an unsigned long is eight bytes here and would print a negative result with
	// eight more digits than Windows does.
	snprintf(UnknownErrorText, sizeof(UnknownErrorText), "0x%08x", (unsigned int)(uint32_t)result);
	return UnknownErrorText;
}

unsigned int Get_FVF_Vertex_Size(RenderUInt32 fvf)
{
	return D3DXPortable_FVF_Vertex_Size(fvf);
}
