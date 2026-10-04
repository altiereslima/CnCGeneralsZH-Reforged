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
 * FFReference's programmable stages (A3e): vs_1_1 and ps_1_1 programs, run from their ORIGINAL tokens.
 *
 * The same rule as FFReference: written from Microsoft's documentation alone - the shader reference pages
 * for what each instruction, register and modifier does (one page cited per instruction in ffprogram.cpp),
 * the driver pages "Instruction Token", "Destination Parameter Token" and "Source Parameter Token" for
 * the bit layouts.  It shares nothing with the game's own shader translation, which it exists to check.
 * The numbers the pages leave out (opcodes, register types, modifiers, version tokens) are values from
 * Microsoft's d3d8types.h, the header the reference pages name; they are written here as this file's own
 * constants, and Tools/ffprogram-values-check.sh static_asserts them against MinGW-w64's d3d8types.h.
 *
 * SCOPE: exactly what the game ships (the census, 2026-09-26: 16 programs in ShadersZH.big and
 * shaders.big, 4 ps.1.1 text programs in W3DWater.cpp).
 *   ps_1_1: tex, texbem, mov, mul, mad, add, dp3, lrp, def; registers r, v, c, t; the complement source
 *           modifier; the _x2 result shift; write masks; the alpha-replicate source swizzle; co-issue.
 *   vs_1_1: mov, add, mad, mul, dp4, m4x4, rcp; registers r, v, c (relative through a0.x), a0; the
 *           negate source modifier; any swizzle and write mask; outputs oPos, oD0, oD1, oT0-oT7.
 * Everything else is REFUSED by name when the program is decoded (Program::refusals), as FFReference
 * refuses the fixed-function states it does not model.
 */

#ifndef FFPROGRAM_H
#define FFPROGRAM_H

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace FFRef {

// ---- token values (Microsoft's d3d8types.h, as this file's own names) ------------------------------
namespace Token {
	enum : uint32_t {
		VERSION_PIXEL = 0xFFFF0000u,		///< the high half of a pixel shader's version token
		VERSION_VERTEX = 0xFFFE0000u,		///< and of a vertex shader's; major in bits 15:8, minor in 7:0
		END = 0x0000FFFFu,
		COMMENT = 0xFFFEu,					///< opcode of a comment; its length in bits 30:16
		COISSUE = 0x40000000u,				///< instruction token bit 30 (ps_1_x)
		PARAMETER = 0x80000000u				///< bit 31, set on every parameter token
	};
	enum Opcode : uint32_t {
		OP_NOP = 0, OP_MOV = 1, OP_ADD = 2, OP_SUB = 3, OP_MAD = 4, OP_MUL = 5, OP_RCP = 6, OP_RSQ = 7,
		OP_DP3 = 8, OP_DP4 = 9, OP_MIN = 10, OP_MAX = 11, OP_SLT = 12, OP_SGE = 13, OP_EXP = 14, OP_LOG = 15,
		OP_LIT = 16, OP_DST = 17, OP_LRP = 18, OP_FRC = 19, OP_M4x4 = 20, OP_M4x3 = 21, OP_M3x4 = 22,
		OP_M3x3 = 23, OP_M3x2 = 24,
		OP_TEXCOORD = 64, OP_TEXKILL = 65, OP_TEX = 66, OP_TEXBEM = 67, OP_TEXBEML = 68,
		OP_TEXREG2AR = 69, OP_TEXREG2GB = 70, OP_TEXM3x2PAD = 71, OP_TEXM3x2TEX = 72, OP_TEXM3x3PAD = 73,
		OP_TEXM3x3TEX = 74, OP_TEXM3x3DIFF = 75, OP_TEXM3x3SPEC = 76, OP_TEXM3x3VSPEC = 77, OP_EXPP = 78,
		OP_LOGP = 79, OP_CND = 80, OP_DEF = 81, OP_TEXREG2RGB = 82, OP_TEXDP3TEX = 83, OP_TEXM3x2DEPTH = 84,
		OP_TEXDP3 = 85, OP_TEXM3x3 = 86, OP_TEXDEPTH = 87, OP_CMP = 88, OP_BEM = 89, OP_PHASE = 0xFFFD
	};
	/// Register types: bits 30:28 of a parameter token (the only three bits shader model 1 uses)
	enum Register : uint32_t {
		REG_TEMP = 0, REG_INPUT = 1, REG_CONST = 2, REG_ADDR = 3, REG_TEXTURE = 3,	///< a0 in a vertex shader, t in a pixel shader
		REG_RASTOUT = 4, REG_ATTROUT = 5, REG_TEXCRDOUT = 6
	};
	enum : uint32_t { RASTOUT_POSITION = 0, RASTOUT_FOG = 1, RASTOUT_POINTSIZE = 2 };
	/// Source modifiers, bits 27:24 of a source parameter token
	enum SourceModifier : uint32_t {
		SRC_NONE = 0, SRC_NEGATE = 1, SRC_BIAS = 2, SRC_BIASNEGATE = 3, SRC_SIGN = 4, SRC_SIGNNEGATE = 5,
		SRC_COMPLEMENT = 6, SRC_X2 = 7, SRC_X2NEGATE = 8, SRC_DZ = 9, SRC_DW = 10
	};
	enum : uint32_t {
		REGNUM_MASK = 0x7FFu, REGTYPE_SHIFT = 28, REGTYPE_MASK = 0x70000000u,
		WRITEMASK_SHIFT = 16, WRITEMASK_MASK = 0x000F0000u,
		RESULTMOD_SHIFT = 20, RESULTMOD_MASK = 0x00F00000u, RESULTMOD_SATURATE = 1,
		RESULTSHIFT_SHIFT = 24, RESULTSHIFT_MASK = 0x0F000000u,
		SWIZZLE_SHIFT = 16, SWIZZLE_MASK = 0x00FF0000u,
		SRCMOD_SHIFT = 24, SRCMOD_MASK = 0x0F000000u,
		ADDRESS_RELATIVE = 0x00002000u,		///< bit 13: relative addressing (vertex shader sources); not RELATIVE, a Windows macro
		COMMENTSIZE_SHIFT = 16, COMMENTSIZE_MASK = 0x7FFF0000u
	};
}

// ---- a decoded program ----------------------------------------------------------------------------
enum ProgramKind { PROGRAM_VERTEX, PROGRAM_PIXEL };

struct Operand
{
	uint32_t type;			///< Token::Register
	int index;				///< register number
	unsigned mask;			///< destinations: the write mask, bit 0 = x/r .. bit 3 = w/a
	int swizzle[4];			///< sources: the component each channel reads, 0..3
	uint32_t modifier;		///< sources: Token::SourceModifier
	bool relative;			///< sources: c[a0.x + index]
};

struct Instruction
{
	uint32_t opcode;		///< Token::Opcode
	bool coissue;			///< ps_1_x '+': runs with the instruction before it
	bool saturate;			///< the _sat result modifier
	int shift;				///< the result shift, as a power of two: 1 is _x2, -1 is _d2 (ps_1_x)
	Operand dst;
	Operand src[3];
	int sources;
	double value[4];		///< def's constant
};

struct Program
{
	ProgramKind kind;
	int major, minor;
	std::vector<Instruction> code;
	std::vector<std::string> refusals;	///< non-empty: outside the census; it must not be run
};

/// Decodes a token stream (version token first, END last) into `out`.  FALSE, with out.refusals saying
/// why, for anything malformed or outside the census.
bool decodeProgram( const uint32_t *tokens, size_t count, Program &out );

/// Assembles ps.1.1 text, as the water's programs are written, into tokens.  FALSE with `error` for
/// anything the census does not hold.  (A second reading of what D3DXAssembleShader produces.)
bool assemblePixelProgram( const std::string &text, std::vector<uint32_t> &tokens, std::string &error );

// ---- a vertex declaration --------------------------------------------------------------------------
/*
 * One D3DVERTEXELEMENT9, as capture v2 records the declaration bound at a draw.  Types and usages by the
 * values the D3DDECLTYPE and D3DDECLUSAGE pages give.  Each element's usage names the vN it feeds by
 * "Map between D3D9 and D3D8 declarations" and the D3DVSDE_ numbers (D3D8 below): POSITION0 v0,
 * BLENDWEIGHT0 v1, BLENDINDICES0 v2, NORMAL0 v3, PSIZE0 v4, COLOR0 v5, COLOR1 v6, TEXCOORD0-7 v7-v14,
 * POSITION1 v15 (NORMAL1 is v16, past vs_1_1's inputs).  The port's device converts the engine's D3D8
 * declarations with the same table (capture v2); capture v3 also holds the engine's own tokens,
 * which decodeD3D8Declaration reads without it.
 */
struct DeclarationElement
{
	uint16_t stream, offset;
	uint8_t type, method, usage, usageIndex;
};
namespace Declaration {
	enum Type : uint8_t { FLOAT1 = 0, FLOAT2 = 1, FLOAT3 = 2, FLOAT4 = 3, D3DCOLOR = 4, UBYTE4 = 5, SHORT2 = 6, SHORT4 = 7 };
	enum Usage : uint8_t { POSITION = 0, BLENDWEIGHT = 1, BLENDINDICES = 2, NORMAL = 3, PSIZE = 4, TEXCOORD = 5, COLOR = 10 };
}
/// v0-v15 of one vertex from its stream bytes.  `present`: bit n set for each vN an element fed.  A
/// register no element feeds, and the channels a type leaves out, are (0, 0, 0, 1)'s (D3DDECLTYPE's
/// expansions; "Input Register - vs": partial (0, 0, 0, 1)).  FALSE with `error` for a type, stream,
/// method or usage outside what the game declares.
bool declarationInputs( const uint8_t *vertex, const DeclarationElement *elements, int count,
	double inputs[16][4], unsigned &present, std::string &error );

// ---- the engine's own D3D8 declaration -------------------------------------------------------------
/*
 * The DWORD tokens the engine hands D3D8's CreateVertexShader (capture v3 records them).  No reference
 * page describes them: the D3D8 documentation is not on learn.microsoft.com.  The fields are as
 * Microsoft's d3d8types.h defines them and says in its comments, written here as own constants and
 * checked against MinGW-w64's d3d8types.h (Tools/ffprogram-values-check.sh):
 *   bits 31..29  the token type: NOP 0, STREAM 1, STREAMDATA 2, TESSELLATOR 3, CONSTMEM 4, EXT 5, END 7
 *                ("end-of-array (requires all DWORD bits to be 1)").
 *   STREAM       "Set current stream": the number in bits 3..0; bit 28 set is D3DVSD_STREAM_TESS.
 *   STREAMDATA   bit 28 clear, D3DVSD_REG: "bind single vertex register to vertex element from vertex
 *                stream", the register "[0..15]" in bits 4..0 and the D3DVSDT_ type in bits 19..16.
 *                Bit 28 set, D3DVSD_SKIP: "Skip _DWORDCount DWORDs in vertex", the count in bits 19..16.
 * The register a binding names IS the vN: no usage stands between.  Named choices (D-items):
 *   D1  A stream's bindings and skips lie one after another from byte 0, each as wide as its type says
 *       (FLOATn 4n bytes; D3DCOLOR "4D packed unsigned bytes", UBYTE4 "4D unsigned byte" and SHORT2
 *       "2D signed short" 4; SHORT4 8).  The header says so only through SKIP's "DWORDs in vertex".
 *   D2  NOP tokens are passed over.  TESSELLATOR, CONSTMEM, EXT, STREAM_TESS, a register over 15
 *       (D3DVSDE_NORMAL2 is 16, past the "[0..15]" range), a register bound twice, data before any
 *       STREAM, and a missing END are refused by name.
 * The D3DVSDT_ values are D3DDECLTYPE's ("Map between D3D9 and D3D8 declarations": same-named types),
 * so a binding's type is a Declaration::Type.
 */
struct RegisterBinding
{
	uint16_t stream, offset;
	uint8_t reg, type;
};
namespace D3D8 {
	enum : uint32_t {
		TOKEN_TYPE_SHIFT = 29, TOKEN_NOP = 0, TOKEN_STREAM = 1, TOKEN_STREAMDATA = 2, TOKEN_TESSELLATOR = 3,
		TOKEN_CONSTMEM = 4, TOKEN_EXT = 5, TOKEN_END = 7, DECLARATION_END = 0xFFFFFFFFu,
		STREAM_NUMBER_MASK = 0xF, STREAM_TESS_BIT = 1u << 28,
		DATA_LOAD_SKIP_BIT = 1u << 28, DATA_TYPE_SHIFT = 16, DATA_TYPE_MASK = 0xFu << 16,
		SKIP_COUNT_SHIFT = 16, SKIP_COUNT_MASK = 0xFu << 16, VERTEX_REG_MASK = 0x1F,
	};
	/// D3DVSDE_: the register each D3D9 usage became ("Map between D3D9 and D3D8 declarations")
	enum : uint8_t { POSITION = 0, BLENDWEIGHT = 1, BLENDINDICES = 2, NORMAL = 3, PSIZE = 4, DIFFUSE = 5, SPECULAR = 6,
		TEXCOORD0 = 7, TEXCOORD7 = 14, POSITION2 = 15, NORMAL2 = 16 };
}
/// The bindings, in register order.  FALSE with `error` for a token D2 refuses.
bool decodeD3D8Declaration( const uint32_t *tokens, size_t count, std::vector<RegisterBinding> &out, std::string &error );
/// The device's D3D9 elements read back to registers through the mapping page (usage and index to
/// D3DVSDE_), in register order.  FALSE with `error` for a usage the page does not map to v0-v15, or
/// two elements on one register.
bool bindingsFromElements( const DeclarationElement *elements, int count, std::vector<RegisterBinding> &out, std::string &error );
/// v0-v15 of one stream-0 vertex, as declarationInputs; a binding on another stream is refused.
bool registerInputs( const uint8_t *vertex, const RegisterBinding *bindings, int count,
	double inputs[16][4], unsigned &present, std::string &error );
/// "" when the two readings bind the same registers to the same stream, offset and type; else each
/// difference, one per line.
std::string compareBindings( const std::vector<RegisterBinding> &a, const std::vector<RegisterBinding> &b );

// ---- running a vertex program ---------------------------------------------------------------------
/*
 * Plain double precision, as FFReference's fixed-function vertex stage is; the GPU's single precision
 * is inside the rasterizer's edge freedom.  Named choices (P-items, ffprogram.cpp):
 *   P1  mov to a0: "rounding to nearest" (mov - vs); ties are not defined, and go away from zero.  The
 *       page's pseudocode rounds src.w where every other instruction works per component; a0.x is taken
 *       from the component the swizzle names for x, and a vertex whose .x and .w differ is REPORTED
 *       (VertexRun::addressAmbiguous), since no envelope can bound a different constant.
 *   P2  rcp of 0: the pseudocode gives FLT_MAX, the text "infinity"; FLT_MAX is used and the vertex is
 *       REPORTED (VertexRun::reciprocalOfZero).
 *   P3  An output the program does not write has no documented value (Registers - vs_1_1: "None");
 *       VertexRun says which were written, and the caller refuses a pixel stage that reads one.
 * Inputs are as the declaration made them: a channel the stream does not supply is (0, 0, 0, 1)'s.
 */
struct VertexRun
{
	double position[4];		///< oPos
	double colour[2][4];	///< oD0, oD1, as written (the pixel stage saturates, "Registers - ps_1_X")
	double texture[8][4];	///< oT0-oT7
	unsigned wroteColour;	///< bit n: oDn written
	unsigned wroteTexture;	///< bit n: oTn written
	bool addressAmbiguous;	///< P1: a0 was loaded from a source whose .x and .w differ
	bool reciprocalOfZero;	///< P2
};
/// `constants`: c0..c(count-1); reads outside them (relative or not) give (0, 0, 0, 0), as "Constant
/// Float Register" says.
void runVertexProgram( const Program &program, const double (*constants)[4], int constantCount,
	const double inputs[16][4], VertexRun &out );

// ---- running a pixel program ----------------------------------------------------------------------
/*
 * Each register holds a nominal value and an interval every documented freedom allows:
 *   P4  Range ("Registers - ps_1_X"): r# is -PixelShader1xMaxValue..+PixelShader1xMaxValue, a cap at
 *       least 1; c# is -1..+1.  The nominal clamps at 1; the interval holds every cap from 1 up.
 *   P5  Precision: "approximately eight bits for the fractional part" - every result written widens the
 *       interval by 1/256 either way.
 *   P6  dp3 writes its sum to x, y, z and w (the pseudocode); the remark that ps_1_1 dp3 writes "the
 *       color channels" is read as the pipe it runs in, since shipped programs (monochrome.pso) read the
 *       alpha of a full-mask dp3 and passed the runtime's validation.
 *   P7  Co-issue: no page says whether the '+' instruction sees its partner's result; both are run
 *       reading the registers as they were before the pair, and decoding refuses a pair where either
 *       reads a channel the other writes (none shipped does).
 *   P8  texbem's perturbation is read as signed (the page: "always interprets du and dv as signed");
 *       the page gives defined results only for signed data, which the caller's sampler must supply.
 * v# inputs are saturated to 0..1 on the way in, as the page says.  The result is r0.
 */
struct Interval4
{
	double nominal[4], lo[4], hi[4];
};
/// Samples stage `stage` at its interpolated coordinates moved by (du, dv) (texbem; 0 for tex): RGBA.
typedef void (*StageSampler)( void *context, int stage, double du, double dv, double rgba[4] );
struct PixelInputs
{
	double colour[2][4];		///< v0, v1: the iterated diffuse and specular
	double constants[8][4];		///< c0-c7 as set (def overrides, as a program runs)
	double bumpMatrix[4][4];	///< per stage: BUMPENVMAT00, 01, 10, 11
	StageSampler sample;
	void *context;
};
/// Runs `program` (a decoded ps_1_1); r0 in `out`.
void runPixelProgram( const Program &program, const PixelInputs &in, Interval4 &out );

}	// namespace FFRef

#endif
