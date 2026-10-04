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
 * FFReference's programmable stages: see ffprogram.h for the rules, the sources and the scope.
 * Token layouts: "Instruction Token", "Destination Parameter Token", "Source Parameter Token"
 * (learn.microsoft.com, windows-hardware/drivers/display).
 */

#include "ffreference/ffprogram.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <string.h>

namespace FFRef {

namespace {

using namespace Token;

std::string at( size_t i, const char *what )
{
	char buffer[ 160 ];
	snprintf( buffer, sizeof( buffer ), "token %zu: %s", i, what );
	return buffer;
}

Operand destination( uint32_t t )
{
	Operand o;
	memset( &o, 0, sizeof( o ) );
	o.type = (t & REGTYPE_MASK) >> REGTYPE_SHIFT;
	o.index = (int)(t & REGNUM_MASK);
	o.mask = (t & WRITEMASK_MASK) >> WRITEMASK_SHIFT;
	for (int i = 0; i < 4; ++i)
		o.swizzle[i] = i;
	return o;
}

Operand source( uint32_t t )
{
	Operand o;
	memset( &o, 0, sizeof( o ) );
	o.type = (t & REGTYPE_MASK) >> REGTYPE_SHIFT;
	o.index = (int)(t & REGNUM_MASK);
	o.mask = 0xF;
	const uint32_t swizzle = (t & SWIZZLE_MASK) >> SWIZZLE_SHIFT;
	for (int i = 0; i < 4; ++i)
		o.swizzle[i] = (int)((swizzle >> (2 * i)) & 3);
	o.modifier = (t & SRCMOD_MASK) >> SRCMOD_SHIFT;
	o.relative = (t & ADDRESS_RELATIVE) != 0;
	return o;
}

/// The census: the operations each kind may use, and how many sources each takes
int censusSources( ProgramKind kind, uint32_t op )
{
	if (kind == PROGRAM_PIXEL)
		switch (op)
		{
			case OP_TEX: return 0;
			case OP_TEXBEM: case OP_MOV: return 1;
			case OP_MUL: case OP_ADD: case OP_DP3: return 2;
			case OP_MAD: case OP_LRP: return 3;
		}
	else
		switch (op)
		{
			case OP_MOV: case OP_RCP: return 1;
			case OP_ADD: case OP_MUL: case OP_DP4: case OP_M4x4: return 2;
			case OP_MAD: return 3;
		}
	return -1;
}

bool identitySwizzle( const Operand &o )
{
	return o.swizzle[0] == 0 && o.swizzle[1] == 1 && o.swizzle[2] == 2 && o.swizzle[3] == 3;
}

bool alphaReplicate( const Operand &o )
{
	return o.swizzle[0] == 3 && o.swizzle[1] == 3 && o.swizzle[2] == 3 && o.swizzle[3] == 3;
}

/// Everything about one instruction the census does not hold, into `refusals`
void checkCensus( const Program &p, const Instruction &in, size_t i, std::vector<std::string> &refusals )
{
	const bool ps = p.kind == PROGRAM_PIXEL;
	if (in.saturate)
		refusals.push_back( at( i, "the _sat result modifier (not in the census)" ) );
	if (in.shift != 0 && !(ps && in.shift == 1))
		refusals.push_back( at( i, "a result shift other than _x2" ) );
	if (in.coissue && !ps)
		refusals.push_back( at( i, "co-issue in a vertex shader" ) );
	const Operand &d = in.dst;
	if (ps)
	{
		const bool texture = in.opcode == OP_TEX || in.opcode == OP_TEXBEM;
		if (texture ? d.type != REG_TEXTURE : d.type != REG_TEMP)
			refusals.push_back( at( i, texture ? "a texture instruction writing other than t" : "an arithmetic instruction writing other than r" ) );
		if (d.mask != 0xF && d.mask != 0x7 && d.mask != 0x8)
			refusals.push_back( at( i, "a write mask other than .rgba, .rgb or .a" ) );
		if (d.index > 3 || (d.type == REG_TEMP && d.index > 1))
			refusals.push_back( at( i, "a register past ps_1_1's (r0-r1, t0-t3)" ) );
	}
	else
	{
		const bool ok = d.type == REG_TEMP || d.type == REG_ATTROUT || d.type == REG_TEXCRDOUT
			|| (d.type == REG_RASTOUT && d.index == RASTOUT_POSITION) || (d.type == REG_ADDR && in.opcode == OP_MOV);
		if (!ok)
			refusals.push_back( at( i, "a vertex destination outside the census (oFog, oPts, or a0 other than by mov)" ) );
		if (d.type == REG_ADDR && (d.index != 0 || d.mask != 0x1))
			refusals.push_back( at( i, "an address register other than a0.x" ) );
		if (d.type == REG_ATTROUT && d.index > 1)
			refusals.push_back( at( i, "a colour output past oD1" ) );
		if (d.type == REG_TEXCRDOUT && d.index > 7)
			refusals.push_back( at( i, "a texture coordinate output past oT7" ) );
		if (d.type == REG_TEMP && d.index > 11)
			refusals.push_back( at( i, "a temporary past r11" ) );
	}
	for (int s = 0; s < in.sources; ++s)
	{
		const Operand &o = in.src[s];
		if (ps)
		{
			if (o.type != REG_TEMP && o.type != REG_INPUT && o.type != REG_CONST && o.type != REG_TEXTURE)
				refusals.push_back( at( i, "a pixel source outside r, v, c, t" ) );
			if (o.modifier != SRC_NONE && o.modifier != SRC_COMPLEMENT)
				refusals.push_back( at( i, "a pixel source modifier other than complement (not in the census)" ) );
			if (!identitySwizzle( o ) && !alphaReplicate( o ))
				refusals.push_back( at( i, "a pixel source swizzle other than none or .a" ) );
			if (o.relative)
				refusals.push_back( at( i, "relative addressing in a pixel shader" ) );
			if ((o.type == REG_TEMP && o.index > 1) || (o.type == REG_INPUT && o.index > 1)
					|| (o.type == REG_CONST && o.index > 7) || (o.type == REG_TEXTURE && o.index > 3))
				refusals.push_back( at( i, "a register past ps_1_1's" ) );
		}
		else
		{
			if (o.type != REG_TEMP && o.type != REG_INPUT && o.type != REG_CONST)
				refusals.push_back( at( i, "a vertex source outside r, v, c" ) );
			if (o.modifier != SRC_NONE && o.modifier != SRC_NEGATE)
				refusals.push_back( at( i, "a vertex source modifier other than negate" ) );
			if (o.relative && o.type != REG_CONST)
				refusals.push_back( at( i, "relative addressing of other than a constant" ) );
			if ((o.type == REG_TEMP && o.index > 11) || (o.type == REG_INPUT && o.index > 15) || (o.type == REG_CONST && o.index > 95))
				refusals.push_back( at( i, "a register past vs_1_1's (r0-r11, v0-v15, c0-c95)" ) );
		}
	}
}

/// The channels of source `s` an instruction reads, as a mask of components (after the swizzle)
unsigned readMask( const Instruction &in, int s )
{
	const Operand &o = in.src[s];
	unsigned channels = 0;
	switch (in.opcode)
	{
		case OP_DP3: channels = 0x7; break;
		case OP_DP4: case OP_M4x4: channels = 0xF; break;
		case OP_RCP: channels = 0x1; break;
		case OP_TEXBEM: channels = 0x3; break;		// the red and green: du, dv
		default: channels = in.dst.mask; break;
	}
	unsigned mask = 0;
	for (int c = 0; c < 4; ++c)
		if (channels & (1u << c))
			mask |= 1u << o.swizzle[c];
	return mask;
}

/// A register the program can write, as one number: type * 16 + index (temps, t, a0, the outputs)
int slot( const Operand &o ) { return (int)o.type * 16 + o.index; }

/// What the pages require beyond the census: registers read only after they are written, and each
/// instruction's own restrictions
void checkPages( const Program &p, std::vector<std::string> &refusals )
{
	const bool ps = p.kind == PROGRAM_PIXEL;
	unsigned written[ 8 * 16 ] = {};
	bool bumpRead[ 4 ] = {};
	for (size_t i = 0; i < p.code.size(); ++i)
	{
		const Instruction &in = p.code[i];
		if (in.opcode == OP_DEF)
			continue;
		for (int s = 0; s < in.sources; ++s)
		{
			const Operand &o = in.src[s];
			// "Registers - ps_1_X" and "Temporary Register - vs": a temporary is read only after a write;
			// a ps_1_1 texture register only after tex or texbem loaded it
			if (o.type == REG_TEMP || (ps && o.type == REG_TEXTURE))
				if ((readMask( in, s ) & ~written[slot( o )]) != 0)
					refusals.push_back( at( i, "reads a register channel no earlier instruction wrote" ) );
			if (!ps && o.relative && written[slot( Operand{ REG_ADDR, 0, 0, { 0, 1, 2, 3 }, 0, false } )] == 0)
				refusals.push_back( at( i, "relative addressing before a0 is loaded (\"Address Register - vs\")" ) );
			// texbem - ps: "Register data that has been read by a texbem ... cannot be read later, except by
			// another texbem"
			if (ps && o.type == REG_TEXTURE && bumpRead[o.index] && in.opcode != OP_TEXBEM)
				refusals.push_back( at( i, "reads a texture register a texbem has read (texbem - ps)" ) );
		}
		if (ps && in.opcode == OP_TEXBEM)
		{
			if (in.src[0].type != REG_TEXTURE || in.src[0].index >= in.dst.index)
				refusals.push_back( at( i, "texbem t(m), t(n) needs m > n (texbem - ps)" ) );
			else
				bumpRead[in.src[0].index] = true;
		}
		if (!ps && in.opcode == OP_RCP && !(in.src[0].swizzle[0] == in.src[0].swizzle[1]
				&& in.src[0].swizzle[1] == in.src[0].swizzle[2] && in.src[0].swizzle[2] == in.src[0].swizzle[3]))
			refusals.push_back( at( i, "rcp without a replicate swizzle (rcp - vs)" ) );
		if (!ps && in.opcode == OP_M4x4)
		{
			// m4x4 - vs: the xyzw mask (its masking page also allows .xyz; the stricter holds here), no
			// modifier or swizzle on either source (the page contradicts itself on src0), dest not src0
			if (in.dst.mask != 0xF)
				refusals.push_back( at( i, "m4x4 without the xyzw mask" ) );
			for (int s = 0; s < 2; ++s)
				if (in.src[s].modifier != SRC_NONE || !identitySwizzle( in.src[s] ))
					refusals.push_back( at( i, "m4x4 with a source modifier or swizzle" ) );
			if (in.dst.type == in.src[0].type && in.dst.index == in.src[0].index)
				refusals.push_back( at( i, "m4x4 whose dest is its src0" ) );
		}
		// P7: a co-issued pair where either reads a channel the other writes
		if (in.coissue && i > 0)
		{
			const Instruction &a = p.code[i - 1];
			for (int k = 0; k < 2; ++k)
			{
				const Instruction &reader = k == 0 ? in : a, &writer = k == 0 ? a : in;
				for (int s = 0; s < reader.sources; ++s)
					if (slot( reader.src[s] ) == slot( writer.dst ) && (readMask( reader, s ) & writer.dst.mask) != 0)
						refusals.push_back( at( i, "a co-issued pair reads a channel its partner writes (no page defines it: P7)" ) );
			}
		}
		const unsigned mask = (in.opcode == OP_TEX || in.opcode == OP_TEXBEM) ? 0xFu : in.dst.mask;
		written[slot( in.dst )] |= mask;
	}
	// "r0 ... The value in r0 at the end of the shader is the pixel color": all of it, then
	if (ps && written[REG_TEMP * 16 + 0] != 0xF)
		refusals.push_back( "r0 is not fully written by the end (its unwritten channels have no documented value)" );
}

}	// namespace

bool decodeProgram( const uint32_t *tokens, size_t count, Program &out )
{
	out = Program();
	std::vector<std::string> &refusals = out.refusals;
	if (count == 0)
	{
		refusals.push_back( "an empty program" );
		return false;
	}
	const uint32_t version = tokens[0];
	if ((version & 0xFFFF0000u) == VERSION_PIXEL)
		out.kind = PROGRAM_PIXEL;
	else if ((version & 0xFFFF0000u) == VERSION_VERTEX)
		out.kind = PROGRAM_VERTEX;
	else
	{
		refusals.push_back( "the first token is not a version token" );
		return false;
	}
	out.major = (int)((version >> 8) & 0xFF);
	out.minor = (int)(version & 0xFF);
	if (out.major != 1 || out.minor != 1)
		refusals.push_back( out.kind == PROGRAM_PIXEL ? "a pixel shader version other than 1_1" : "a vertex shader version other than 1_1" );

	bool ended = false;
	size_t i = 1;
	while (i < count && !ended)
	{
		const uint32_t t = tokens[i];
		const uint32_t op = t & 0xFFFFu;
		if (t == END)
		{
			ended = true;
			++i;
			break;
		}
		if (t & PARAMETER)
		{
			refusals.push_back( at( i, "a parameter token where an instruction was due" ) );
			return false;
		}
		if (op == COMMENT)
		{
			i += 1 + ((t & COMMENTSIZE_MASK) >> COMMENTSIZE_SHIFT);
			continue;
		}
		Instruction in;
		memset( &in, 0, sizeof( in ) );
		in.opcode = op;
		in.coissue = (t & COISSUE) != 0;
		const size_t start = i++;
		if (op == OP_DEF)
		{
			if (out.kind != PROGRAM_PIXEL || i + 5 > count || !(tokens[i] & PARAMETER))
			{
				refusals.push_back( at( start, "a def outside a pixel shader, or cut short" ) );
				return false;
			}
			in.dst = destination( tokens[i] );
			for (int k = 0; k < 4; ++k)
			{
				float f;
				memcpy( &f, &tokens[i + 1 + k], 4 );
				in.value[k] = f;
			}
			i += 5;
			if (in.dst.type != REG_CONST || in.dst.index > 7)
				refusals.push_back( at( start, "a def of other than c0-c7" ) );
			out.code.push_back( in );
			continue;
		}
		const int wanted = censusSources( out.kind, op );
		if (wanted < 0)
		{
			char what[ 96 ];
			snprintf( what, sizeof( what ), "opcode %u, not in the census", (unsigned)op );
			refusals.push_back( at( start, what ) );
		}
		std::vector<uint32_t> params;
		while (i < count && (tokens[i] & PARAMETER))
			params.push_back( tokens[i++] );
		if (params.empty())
		{
			refusals.push_back( at( start, "an instruction with no destination" ) );
			return false;
		}
		in.dst = destination( params[0] );
		const uint32_t resultMod = (params[0] & RESULTMOD_MASK) >> RESULTMOD_SHIFT;
		in.saturate = (resultMod & RESULTMOD_SATURATE) != 0;
		if (resultMod & ~RESULTMOD_SATURATE)
			refusals.push_back( at( start, "a result modifier other than _sat" ) );
		const int shift = (int)((params[0] & RESULTSHIFT_MASK) >> RESULTSHIFT_SHIFT);
		in.shift = shift >= 8 ? shift - 16 : shift;		// a signed four-bit shift
		in.sources = (int)params.size() - 1;
		if (in.sources > 3)
		{
			refusals.push_back( at( start, "more than three sources" ) );
			return false;
		}
		for (int s = 0; s < in.sources; ++s)
			in.src[s] = source( params[1 + s] );
		if (wanted >= 0 && in.sources != wanted)
			refusals.push_back( at( start, "the wrong number of sources for its operation" ) );
		if (wanted >= 0)
			checkCensus( out, in, start, refusals );
		out.code.push_back( in );
	}
	if (!ended)
		refusals.push_back( "no end token" );
	if (!out.code.empty() && out.code[0].coissue)
		refusals.push_back( "the first instruction is co-issued" );
	if (refusals.empty())
		checkPages( out, refusals );
	return refusals.empty();
}

// ---- the ps.1.1 assembler -------------------------------------------------------------------------
/*
 * The syntax as the ps_1_x pages write it: "instruction[_modifier] dst[.mask], src, ..." with '+' for a
 * co-issued instruction, registers r#, v#, c#, t#, masks .rgba/.rgb/.a (or .xyzw/.xyz/.w), the alpha
 * replicate .a/.w, the complement prefix "1-", and "def c#, f, f, f, f".
 *   P9  The version line: the pages write ps_1_1; the game writes ps.1.1, which the runtime accepted.
 *       Both are taken.  Comments run from ';' or "//" to the end of the line.
 */
namespace {

std::string lower( std::string s )
{
	for (size_t i = 0; i < s.size(); ++i)
		if (s[i] >= 'A' && s[i] <= 'Z')
			s[i] = (char)(s[i] + 32);
	return s;
}

std::string trim( const std::string &s )
{
	size_t a = 0, b = s.size();
	while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r'))
		++a;
	while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r'))
		--b;
	return s.substr( a, b - a );
}

std::vector<std::string> splitOperands( const std::string &s )
{
	std::vector<std::string> out;
	size_t start = 0;
	for (size_t i = 0; i <= s.size(); ++i)
		if (i == s.size() || s[i] == ',')
		{
			out.push_back( trim( s.substr( start, i - start ) ) );
			start = i + 1;
		}
	return out;
}

bool registerOf( const std::string &name, uint32_t &type, int &index )
{
	if (name.size() < 2)
		return false;
	switch (name[0])
	{
		case 'r': type = REG_TEMP; break;
		case 'v': type = REG_INPUT; break;
		case 'c': type = REG_CONST; break;
		case 't': type = REG_TEXTURE; break;
		default: return false;
	}
	for (size_t i = 1; i < name.size(); ++i)
		if (name[i] < '0' || name[i] > '9')
			return false;
	index = atoi( name.c_str() + 1 );
	return true;
}

bool maskOf( const std::string &m, unsigned &mask )
{
	if (m.empty() || m == "rgba" || m == "xyzw") mask = 0xF;
	else if (m == "rgb" || m == "xyz") mask = 0x7;
	else if (m == "a" || m == "w") mask = 0x8;
	else return false;
	return true;
}

bool destinationToken( const std::string &text, int shift, uint32_t &token, std::string &error )
{
	const size_t dot = text.find( '.' );
	uint32_t type;
	int index;
	unsigned mask;
	if (!registerOf( text.substr( 0, dot ), type, index ) || !maskOf( dot == std::string::npos ? "" : text.substr( dot + 1 ), mask ))
	{
		error = "a destination the census does not hold: " + text;
		return false;
	}
	token = PARAMETER | (type << REGTYPE_SHIFT) | (uint32_t)index | (mask << WRITEMASK_SHIFT)
		| ((uint32_t)(shift & 0xF) << RESULTSHIFT_SHIFT);
	return true;
}

bool sourceToken( std::string text, uint32_t &token, std::string &error )
{
	uint32_t modifier = SRC_NONE;
	if (text.compare( 0, 2, "1-" ) == 0)
	{
		modifier = SRC_COMPLEMENT;
		text = trim( text.substr( 2 ) );
	}
	const size_t dot = text.find( '.' );
	uint32_t type;
	int index;
	uint32_t swizzle = 0xE4;		// .xyzw: none
	if (!registerOf( text.substr( 0, dot ), type, index ))
	{
		error = "a source the census does not hold: " + text;
		return false;
	}
	if (dot != std::string::npos)
	{
		const std::string s = text.substr( dot + 1 );
		if (s == "a" || s == "w")
			swizzle = 0xFF;
		else if (s != "rgba" && s != "xyzw")
		{
			error = "a source swizzle the census does not hold: " + text;
			return false;
		}
	}
	token = PARAMETER | (type << REGTYPE_SHIFT) | (uint32_t)index | (swizzle << SWIZZLE_SHIFT) | (modifier << SRCMOD_SHIFT);
	return true;
}

}	// namespace

bool assemblePixelProgram( const std::string &text, std::vector<uint32_t> &tokens, std::string &error )
{
	tokens.clear();
	bool versioned = false;
	size_t start = 0;
	while (start <= text.size())
	{
		size_t end = text.find( '\n', start );
		if (end == std::string::npos)
			end = text.size();
		std::string line = lower( text.substr( start, end - start ) );
		start = end + 1;
		const size_t semicolon = line.find( ';' ), slashes = line.find( "//" );
		line = trim( line.substr( 0, std::min( semicolon, slashes ) ) );
		if (line.empty())
			continue;
		if (!versioned)
		{
			if (line != "ps.1.1" && line != "ps_1_1")		// P9
			{
				error = "the first statement is not ps.1.1: " + line;
				return false;
			}
			tokens.push_back( VERSION_PIXEL | 0x0101 );
			versioned = true;
			continue;
		}
		bool coissue = false;
		if (line[0] == '+')
		{
			coissue = true;
			line = trim( line.substr( 1 ) );
		}
		const size_t space = line.find_first_of( " \t" );
		std::string mnemonic = line.substr( 0, space );
		const std::vector<std::string> operands = splitOperands( space == std::string::npos ? "" : trim( line.substr( space ) ) );
		int shift = 0;
		const size_t underscore = mnemonic.find( '_' );
		if (underscore != std::string::npos)
		{
			const std::string modifier = mnemonic.substr( underscore + 1 );
			mnemonic = mnemonic.substr( 0, underscore );
			if (modifier == "x2") shift = 1;
			else { error = "an instruction modifier the census does not hold: _" + modifier; return false; }
		}
		struct Op { const char *name; uint32_t code; int sources; };
		static const Op ops[] = { { "tex", OP_TEX, 0 }, { "texbem", OP_TEXBEM, 1 }, { "mov", OP_MOV, 1 }, { "mul", OP_MUL, 2 },
			{ "mad", OP_MAD, 3 }, { "add", OP_ADD, 2 }, { "dp3", OP_DP3, 2 }, { "lrp", OP_LRP, 3 }, { "def", OP_DEF, 4 } };
		const Op *op = NULL;
		for (size_t k = 0; k < sizeof( ops ) / sizeof( ops[0] ); ++k)
			if (mnemonic == ops[k].name)
				op = &ops[k];
		if (op == NULL)
		{
			error = "an instruction the census does not hold: " + mnemonic;
			return false;
		}
		if ((int)operands.size() != 1 + op->sources || operands[0].empty())
		{
			error = "the wrong number of operands: " + line;
			return false;
		}
		tokens.push_back( op->code | (coissue ? COISSUE : 0u) );
		uint32_t token;
		if (!destinationToken( operands[0], shift, token, error ))
			return false;
		tokens.push_back( token );
		for (int k = 0; k < op->sources; ++k)
		{
			if (op->code == OP_DEF)
			{
				const float f = (float)atof( operands[1 + k].c_str() );
				uint32_t bits;
				memcpy( &bits, &f, 4 );
				tokens.push_back( bits );
			}
			else
			{
				if (!sourceToken( operands[1 + k], token, error ))
					return false;
				tokens.push_back( token );
			}
		}
	}
	if (!versioned)
	{
		error = "no version statement";
		return false;
	}
	tokens.push_back( END );
	return true;
}

// ---- a vertex declaration --------------------------------------------------------------------------
namespace {

bool byRegister( const RegisterBinding &a, const RegisterBinding &b )
{
	return a.reg < b.reg;
}

}	// namespace

bool bindingsFromElements( const DeclarationElement *elements, int count, std::vector<RegisterBinding> &out, std::string &error )
{
	out.clear();
	unsigned bound = 0;
	for (int i = 0; i < count; ++i)
	{
		const DeclarationElement &e = elements[i];
		int reg = -1;
		switch (e.usage)
		{
			case Declaration::POSITION: reg = e.usageIndex == 0 ? D3D8::POSITION : (e.usageIndex == 1 ? D3D8::POSITION2 : -1); break;
			case Declaration::BLENDWEIGHT: reg = e.usageIndex == 0 ? D3D8::BLENDWEIGHT : -1; break;
			case Declaration::BLENDINDICES: reg = e.usageIndex == 0 ? D3D8::BLENDINDICES : -1; break;
			case Declaration::NORMAL: reg = e.usageIndex == 0 ? D3D8::NORMAL : -1; break;		// NORMAL1 is v16
			case Declaration::PSIZE: reg = e.usageIndex == 0 ? D3D8::PSIZE : -1; break;
			case Declaration::COLOR: reg = e.usageIndex <= 1 ? D3D8::DIFFUSE + e.usageIndex : -1; break;
			case Declaration::TEXCOORD: reg = e.usageIndex <= 7 ? D3D8::TEXCOORD0 + e.usageIndex : -1; break;
		}
		if (reg < 0 || e.method != 0 || (bound & (1u << reg)) != 0)
		{
			char what[ 96 ];
			snprintf( what, sizeof( what ), "element %d: usage %u.%u, method %u %s", i, (unsigned)e.usage,
				(unsigned)e.usageIndex, (unsigned)e.method, reg < 0 || e.method != 0 ? "outside the D3D8 registers" : "on a register bound twice" );
			error = what;
			return false;
		}
		bound |= 1u << reg;
		const RegisterBinding b = { e.stream, e.offset, (uint8_t)reg, e.type };
		out.push_back( b );
	}
	std::stable_sort( out.begin(), out.end(), byRegister );
	return true;
}

bool registerInputs( const uint8_t *vertex, const RegisterBinding *bindings, int count,
	double inputs[16][4], unsigned &present, std::string &error )
{
	present = 0;
	for (int k = 0; k < 16; ++k)
	{
		inputs[k][0] = inputs[k][1] = inputs[k][2] = 0.0;
		inputs[k][3] = 1.0;
	}
	for (int i = 0; i < count; ++i)
	{
		const RegisterBinding &b = bindings[i];
		char what[ 96 ];
		if (b.reg > 15 || b.stream != 0)
		{
			snprintf( what, sizeof( what ), "binding %d: v%u, stream %u: not a stream-0 vs_1_1 input", i, (unsigned)b.reg,
				(unsigned)b.stream );
			error = what;
			return false;
		}
		const uint8_t *p = vertex + b.offset;
		double *v = inputs[b.reg];
		switch (b.type)
		{
			case Declaration::FLOAT1: case Declaration::FLOAT2: case Declaration::FLOAT3: case Declaration::FLOAT4:
				// "One/Two/Three/Four-component float expanded to (float, 0, 0, 1)" ... "(float, float, float, float)"
				for (int c = 0; c <= b.type - Declaration::FLOAT1; ++c)
				{
					float f;
					memcpy( &f, p + 4 * c, 4 );
					v[c] = f;
				}
				break;
			case Declaration::D3DCOLOR:
				// "packed, unsigned bytes mapped to 0 to 1 range.  Input is a D3DCOLOR and is expanded to RGBA
				// order": the DWORD 0xAARRGGBB, stored B, G, R, A
				v[0] = p[2] / 255.0; v[1] = p[1] / 255.0; v[2] = p[0] / 255.0; v[3] = p[3] / 255.0;
				break;
			case Declaration::UBYTE4:		// "Four-component, unsigned byte"
				for (int c = 0; c < 4; ++c)
					v[c] = p[c];
				break;
			case Declaration::SHORT2: case Declaration::SHORT4:		// "signed short expanded to (value, value, 0, 1)"
				for (int c = 0; c < (b.type == Declaration::SHORT2 ? 2 : 4); ++c)
				{
					int16_t h;
					memcpy( &h, p + 2 * c, 2 );
					v[c] = h;
				}
				break;
			default:
				snprintf( what, sizeof( what ), "binding %d: type %u, not one the game declares", i, (unsigned)b.type );
				error = what;
				return false;
		}
		present |= 1u << b.reg;
	}
	return true;
}

bool declarationInputs( const uint8_t *vertex, const DeclarationElement *elements, int count,
	double inputs[16][4], unsigned &present, std::string &error )
{
	std::vector<RegisterBinding> bindings;
	if (!bindingsFromElements( elements, count, bindings, error ))
		return false;
	return registerInputs( vertex, bindings.empty() ? NULL : &bindings[0], (int)bindings.size(), inputs, present, error );
}

bool decodeD3D8Declaration( const uint32_t *tokens, size_t count, std::vector<RegisterBinding> &out, std::string &error )
{
	using namespace D3D8;
	out.clear();
	int stream = -1;
	unsigned offset[ 16 ] = { 0 };		// D1: per stream, the next byte
	unsigned bound = 0;
	char what[ 96 ];
	for (size_t i = 0; i < count; ++i)
	{
		const uint32_t t = tokens[i];
		if (t == DECLARATION_END)
		{
			std::stable_sort( out.begin(), out.end(), byRegister );
			return true;
		}
		switch (t >> TOKEN_TYPE_SHIFT)
		{
			case TOKEN_NOP:
				continue;		// D2
			case TOKEN_STREAM:
				if (t & STREAM_TESS_BIT)
				{
					error = "D3DVSD_STREAM_TESS: the tessellator's stream";
					return false;
				}
				stream = (int)(t & STREAM_NUMBER_MASK);
				continue;
			case TOKEN_STREAMDATA:
			{
				if (stream < 0)
				{
					error = "stream data before any D3DVSD_STREAM";
					return false;
				}
				if (t & DATA_LOAD_SKIP_BIT)
				{
					offset[stream] += 4 * ((t & SKIP_COUNT_MASK) >> SKIP_COUNT_SHIFT);		// "Skip _DWORDCount DWORDs"
					continue;
				}
				const unsigned reg = t & VERTEX_REG_MASK, type = (t & DATA_TYPE_MASK) >> DATA_TYPE_SHIFT;
				static const unsigned width[ 8 ] = { 4, 8, 12, 16, 4, 4, 4, 8 };		// D1, by D3DVSDT_ order
				if (reg > 15 || type > Declaration::SHORT4 || (bound & (1u << reg)) != 0)
				{
					snprintf( what, sizeof( what ), "token %u (0x%08X): v%u, type %u: %s", (unsigned)i, t, reg, type,
						reg > 15 ? "past v15" : (type > Declaration::SHORT4 ? "no D3DVSDT_ type" : "a register bound twice") );
					error = what;
					return false;
				}
				bound |= 1u << reg;
				const RegisterBinding b = { (uint16_t)stream, (uint16_t)offset[stream], (uint8_t)reg, (uint8_t)type };
				out.push_back( b );
				offset[stream] += width[type];
				continue;
			}
			default:
			{
				static const char *const names[ 8 ] = { "", "", "", "D3DVSD_TOKEN_TESSELLATOR", "D3DVSD_TOKEN_CONSTMEM",
					"D3DVSD_TOKEN_EXT", "token type 6", "D3DVSD_TOKEN_END without every bit set" };
				snprintf( what, sizeof( what ), "token %u (0x%08X): %s", (unsigned)i, t, names[t >> TOKEN_TYPE_SHIFT] );
				error = what;
				return false;
			}
		}
	}
	error = "no D3DVSD_END";
	return false;
}

std::string compareBindings( const std::vector<RegisterBinding> &a, const std::vector<RegisterBinding> &b )
{
	std::string differences;
	char line[ 128 ];
	for (unsigned reg = 0; reg < 32; ++reg)
	{
		const RegisterBinding *x = NULL, *y = NULL;
		for (size_t i = 0; i < a.size(); ++i)
			if (a[i].reg == reg)
				x = &a[i];
		for (size_t i = 0; i < b.size(); ++i)
			if (b[i].reg == reg)
				y = &b[i];
		if (x == NULL && y == NULL)
			continue;
		if (x != NULL && y != NULL && x->stream == y->stream && x->offset == y->offset && x->type == y->type)
			continue;
		if (x == NULL || y == NULL)
			snprintf( line, sizeof( line ), "v%u: only the %s binds it\n", reg, x != NULL ? "first" : "second" );
		else
			snprintf( line, sizeof( line ), "v%u: stream %u offset %u type %u against stream %u offset %u type %u\n", reg,
				(unsigned)x->stream, (unsigned)x->offset, (unsigned)x->type, (unsigned)y->stream, (unsigned)y->offset, (unsigned)y->type );
		differences += line;
	}
	return differences;
}

// ---- the vertex program ---------------------------------------------------------------------------
namespace {

/// A source's value: the register, swizzled, with its modifier (vs: negate only, by the census)
void readVertexSource( const Operand &o, const double r[12][4], const double inputs[16][4], const double (*c)[4], int cn,
	int a0, int offset, double v[4] )
{
	static const double zero[ 4 ] = { 0, 0, 0, 0 };
	const double *base = zero;
	if (o.type == REG_TEMP)
		base = r[o.index + offset];
	else if (o.type == REG_INPUT)
		base = inputs[o.index + offset];
	else
	{
		// "Constant Float Register": reads from out-of-range registers return (0.0, 0.0, 0.0, 0.0)
		const int k = o.index + offset + (o.relative ? a0 : 0);
		base = k >= 0 && k < cn ? c[k] : zero;
	}
	for (int i = 0; i < 4; ++i)
		v[i] = o.modifier == SRC_NEGATE ? -base[o.swizzle[i]] : base[o.swizzle[i]];
}

}	// namespace

void runVertexProgram( const Program &program, const double (*constants)[4], int constantCount,
	const double inputs[16][4], VertexRun &out )
{
	memset( &out, 0, sizeof( out ) );
	double r[ 12 ][ 4 ];
	memset( r, 0, sizeof( r ) );
	int a0 = 0;
	for (size_t i = 0; i < program.code.size(); ++i)
	{
		const Instruction &in = program.code[i];
		double s[ 3 ][ 4 ] = {};
		for (int k = 0; k < in.sources; ++k)
			readVertexSource( in.src[k], r, inputs, constants, constantCount, a0, 0, s[k] );
		double result[ 4 ] = { 0, 0, 0, 0 };
		switch (in.opcode)
		{
			case OP_MOV:		// mov - vs: dest = src
				for (int c = 0; c < 4; ++c) result[c] = s[0][c];
				break;
			case OP_ADD:		// add - vs: dest = src0 + src1
				for (int c = 0; c < 4; ++c) result[c] = s[0][c] + s[1][c];
				break;
			case OP_MUL:		// mul - vs: dest = src0 * src1
				for (int c = 0; c < 4; ++c) result[c] = s[0][c] * s[1][c];
				break;
			case OP_MAD:		// mad - vs: dest = src0 * src1 + src2
				for (int c = 0; c < 4; ++c) result[c] = s[0][c] * s[1][c] + s[2][c];
				break;
			case OP_DP4:		// dp4 - vs: the four-component dot product, to every component
			{
				const double d = s[0][0] * s[1][0] + s[0][1] * s[1][1] + s[0][2] * s[1][2] + s[0][3] * s[1][3];
				for (int c = 0; c < 4; ++c) result[c] = d;
				break;
			}
			case OP_M4x4:		// m4x4 - vs: dest.j = src0 . (src1 + j), the matrix in src1 and the next three
				for (int j = 0; j < 4; ++j)
				{
					double row[ 4 ];
					readVertexSource( in.src[1], r, inputs, constants, constantCount, a0, j, row );
					result[j] = s[0][0] * row[0] + s[0][1] * row[1] + s[0][2] * row[2] + s[0][3] * row[3];
				}
				break;
			case OP_RCP:		// rcp - vs, P2
			{
				double f = s[0][0];
				if (f == 0.0)
				{
					f = FLT_MAX;
					out.reciprocalOfZero = true;
				}
				else if (f != 1.0)
					f = 1.0 / f;
				for (int c = 0; c < 4; ++c) result[c] = f;
				break;
			}
		}
		const Operand &d = in.dst;
		if (d.type == REG_ADDR)
		{
			// P1: rounding to nearest, ties away from zero; the page's src.w against the swizzle's x
			if (s[0][0] != s[0][3])
				out.addressAmbiguous = true;
			a0 = (int)(s[0][0] < 0.0 ? -floor( -s[0][0] + 0.5 ) : floor( s[0][0] + 0.5 ));
			continue;
		}
		double *target = NULL;
		if (d.type == REG_TEMP) target = r[d.index];
		else if (d.type == REG_RASTOUT) target = out.position;
		else if (d.type == REG_ATTROUT) { target = out.colour[d.index]; out.wroteColour |= 1u << d.index; }
		else if (d.type == REG_TEXCRDOUT) { target = out.texture[d.index]; out.wroteTexture |= 1u << d.index; }
		for (int c = 0; c < 4; ++c)
			if (target != NULL && (d.mask & (1u << c)))
				target[c] = result[c];
	}
}

// ---- the pixel program ----------------------------------------------------------------------------
namespace {

const double PRECISION = 1.0 / 256.0;		// P5

struct Value { double n[4], lo[4], hi[4]; };

Value exact( const double v[4] )
{
	Value x;
	for (int c = 0; c < 4; ++c)
		x.n[c] = x.lo[c] = x.hi[c] = v[c];
	return x;
}

double clamp1( double v ) { return v < -1.0 ? -1.0 : (v > 1.0 ? 1.0 : v); }

/// P4: a register whose range is -cap..+cap with cap >= 1 - nominally 1, the interval every cap allows
void rangeOf( Value &x )
{
	for (int c = 0; c < 4; ++c)
	{
		x.lo[c] = fmin( x.lo[c], clamp1( x.lo[c] ) );
		x.hi[c] = fmax( x.hi[c], clamp1( x.hi[c] ) );
		x.n[c] = clamp1( x.n[c] );
	}
}

Value swizzled( const Value &v, const Operand &o )
{
	Value x;
	for (int c = 0; c < 4; ++c)
	{
		x.n[c] = v.n[o.swizzle[c]];
		x.lo[c] = v.lo[o.swizzle[c]];
		x.hi[c] = v.hi[o.swizzle[c]];
		if (o.modifier == SRC_COMPLEMENT)		// "Source Register Invert": 1 - value
		{
			const double lo = x.lo[c];
			x.n[c] = 1.0 - x.n[c];
			x.lo[c] = 1.0 - x.hi[c];
			x.hi[c] = 1.0 - lo;
		}
	}
	return x;
}

void product( double alo, double ahi, double blo, double bhi, double &lo, double &hi )
{
	const double p[ 4 ] = { alo * blo, alo * bhi, ahi * blo, ahi * bhi };
	lo = fmin( fmin( p[0], p[1] ), fmin( p[2], p[3] ) );
	hi = fmax( fmax( p[0], p[1] ), fmax( p[2], p[3] ) );
}

}	// namespace

void runPixelProgram( const Program &program, const PixelInputs &in, Interval4 &out )
{
	Value r[ 2 ], t[ 4 ], v[ 2 ], c[ 8 ];
	memset( r, 0, sizeof( r ) );
	memset( t, 0, sizeof( t ) );
	for (int k = 0; k < 2; ++k)
	{
		// "Input color data values are clamped (saturated) to the range 0 through 1"; P5 for the iteration
		double sat[ 4 ];
		for (int ch = 0; ch < 4; ++ch)
			sat[ch] = in.colour[k][ch] < 0.0 ? 0.0 : (in.colour[k][ch] > 1.0 ? 1.0 : in.colour[k][ch]);
		v[k] = exact( sat );
		for (int ch = 0; ch < 4; ++ch)
		{
			v[k].lo[ch] = fmax( 0.0, v[k].lo[ch] - PRECISION );
			v[k].hi[ch] = fmin( 1.0, v[k].hi[ch] + PRECISION );
		}
	}
	for (int k = 0; k < 8; ++k)
	{
		c[k] = exact( in.constants[k] );
		rangeOf( c[k] );	// P4: c# is -1..+1
	}
	for (size_t i = 0; i < program.code.size(); ++i)
	{
		const Instruction &ins = program.code[i];
		if (ins.opcode == OP_DEF)		// def - ps: the constant, for the rest of the program
		{
			c[ins.dst.index] = exact( ins.value );
			rangeOf( c[ins.dst.index] );
			continue;
		}
		if (ins.opcode == OP_TEX || ins.opcode == OP_TEXBEM)
		{
			double du = 0, dv = 0;
			if (ins.opcode == OP_TEXBEM)
			{
				// texbem - ps: u' = u + MAT00 * t(n)R + MAT10 * t(n)G, v' = v + MAT01 * t(n)R + MAT11 * t(n)G,
				// the matrix of the stage sampled (m); P8: the red and green are signed as they come
				const Value &bump = t[ins.src[0].index];
				const double *m = in.bumpMatrix[ins.dst.index];
				du = m[0] * bump.n[0] + m[2] * bump.n[1];
				dv = m[1] * bump.n[0] + m[3] * bump.n[1];
			}
			double rgba[ 4 ];
			in.sample( in.context, ins.dst.index, du, dv, rgba );		// tex - ps: stage n, its coordinates
			t[ins.dst.index] = exact( rgba );
			continue;
		}
		Value s[ 3 ];
		for (int k = 0; k < ins.sources; ++k)
		{
			const Operand &o = ins.src[k];
			const Value &base = o.type == REG_TEMP ? r[o.index] : o.type == REG_TEXTURE ? t[o.index]
				: o.type == REG_INPUT ? v[o.index] : c[o.index];
			s[k] = swizzled( base, o );
		}
		Value x;
		for (int ch = 0; ch < 4; ++ch)
		{
			double lo = 0, hi = 0, plo, phi;
			switch (ins.opcode)
			{
				case OP_MOV:		// mov - ps: dest = src
					x.n[ch] = s[0].n[ch]; lo = s[0].lo[ch]; hi = s[0].hi[ch];
					break;
				case OP_ADD:		// add - ps: dest = src0 + src1
					x.n[ch] = s[0].n[ch] + s[1].n[ch]; lo = s[0].lo[ch] + s[1].lo[ch]; hi = s[0].hi[ch] + s[1].hi[ch];
					break;
				case OP_MUL:		// mul - ps: dest = src0 * src1
					x.n[ch] = s[0].n[ch] * s[1].n[ch];
					product( s[0].lo[ch], s[0].hi[ch], s[1].lo[ch], s[1].hi[ch], lo, hi );
					break;
				case OP_MAD:		// mad - ps: dest = src0 * src1 + src2
					x.n[ch] = s[0].n[ch] * s[1].n[ch] + s[2].n[ch];
					product( s[0].lo[ch], s[0].hi[ch], s[1].lo[ch], s[1].hi[ch], plo, phi );
					lo = plo + s[2].lo[ch]; hi = phi + s[2].hi[ch];
					break;
				case OP_LRP:		// lrp - ps: dest = src0 * src1 + (1 - src0) * src2 = src2 + src0 * (src1 - src2)
				{
					x.n[ch] = s[0].n[ch] * s[1].n[ch] + (1.0 - s[0].n[ch]) * s[2].n[ch];
					double alo, ahi, blo, bhi;		// both forms enclose it; keep their intersection
					product( s[0].lo[ch], s[0].hi[ch], s[1].lo[ch], s[1].hi[ch], alo, ahi );
					product( 1.0 - s[0].hi[ch], 1.0 - s[0].lo[ch], s[2].lo[ch], s[2].hi[ch], blo, bhi );
					double dlo, dhi;
					product( s[0].lo[ch], s[0].hi[ch], s[1].lo[ch] - s[2].hi[ch], s[1].hi[ch] - s[2].lo[ch], dlo, dhi );
					lo = fmax( alo + blo, s[2].lo[ch] + dlo );
					hi = fmin( ahi + bhi, s[2].hi[ch] + dhi );
					break;
				}
				case OP_DP3:		// dp3 - ps, P6: the sum to every channel
				{
					x.n[ch] = s[0].n[0] * s[1].n[0] + s[0].n[1] * s[1].n[1] + s[0].n[2] * s[1].n[2];
					for (int k = 0; k < 3; ++k)
					{
						product( s[0].lo[k], s[0].hi[k], s[1].lo[k], s[1].hi[k], plo, phi );
						lo += plo; hi += phi;
					}
					break;
				}
			}
			// "Modifiers for ps_1_X": _x2 multiplies the result before it is written
			const double scale = ldexp( 1.0, ins.shift );
			x.n[ch] *= scale; x.lo[ch] = lo * scale; x.hi[ch] = hi * scale;
			if (ins.opcode != OP_MOV)		// P5: a new value is stored at the hardware's precision
			{
				x.lo[ch] -= PRECISION;
				x.hi[ch] += PRECISION;
			}
		}
		rangeOf( x );		// P4 for r#
		Value &target = r[ins.dst.index];
		for (int ch = 0; ch < 4; ++ch)
			if (ins.dst.mask & (1u << ch))
			{
				target.n[ch] = x.n[ch];
				target.lo[ch] = x.lo[ch];
				target.hi[ch] = x.hi[ch];
			}
	}
	for (int ch = 0; ch < 4; ++ch)
	{
		out.nominal[ch] = r[0].n[ch];
		out.lo[ch] = r[0].lo[ch];
		out.hi[ch] = r[0].hi[ch];
	}
}

}	// namespace FFRef
