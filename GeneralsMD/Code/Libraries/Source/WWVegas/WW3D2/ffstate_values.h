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
** The Direct3D 9 fixed-function vocabulary the shader generators are described in, as the
** generators' own names with Direct3D 9's values.
**
** ffshader, ffvertex and engineshader turn a description of fixed-function state into HLSL.  They
** only read that state, and the state is numbers: a texture stage's operation, a vertex format's
** bits.  The numbers are Direct3D 9's, because that is what the engine and the D3D11 backend hand
** them.  Off Windows there is no <d3d9.h> to name them, and a header that defined D3DTOP_MODULATE or
** DWORD there would be defining names the Windows SDK owns (B5), so the generators
** name them FF_* instead and this file gives each one its Direct3D 9 value.
**
** It is the only table there is: Windows reads it too, and ffstate.h asserts every entry equal to
** the SDK's own at compile time, so a wrong value here is a build that stops, on the one platform
** where the answer is on disk.  The values were taken from <d3d9types.h> (mingw-w64 14.0.0), not
** typed.  FixedFunctionValue is what the fields hold; ffstate.h defines it.
**
** D3D9 froze in 2006.  Nothing here will change.
*/

#ifndef FFSTATE_VALUES_H
#define FFSTATE_VALUES_H

// D3DTEXTUREOP: a texture stage's colour or alpha operation (D3DTSS_COLOROP, D3DTSS_ALPHAOP).
const FixedFunctionValue FF_TOP_DISABLE = 0x00000001;
const FixedFunctionValue FF_TOP_SELECTARG1 = 0x00000002;
const FixedFunctionValue FF_TOP_SELECTARG2 = 0x00000003;
const FixedFunctionValue FF_TOP_MODULATE = 0x00000004;
const FixedFunctionValue FF_TOP_MODULATE2X = 0x00000005;
const FixedFunctionValue FF_TOP_MODULATE4X = 0x00000006;
const FixedFunctionValue FF_TOP_ADD = 0x00000007;
const FixedFunctionValue FF_TOP_ADDSIGNED = 0x00000008;
const FixedFunctionValue FF_TOP_ADDSIGNED2X = 0x00000009;
const FixedFunctionValue FF_TOP_SUBTRACT = 0x0000000a;
const FixedFunctionValue FF_TOP_ADDSMOOTH = 0x0000000b;
const FixedFunctionValue FF_TOP_BLENDDIFFUSEALPHA = 0x0000000c;
const FixedFunctionValue FF_TOP_BLENDTEXTUREALPHA = 0x0000000d;
const FixedFunctionValue FF_TOP_BLENDFACTORALPHA = 0x0000000e;
const FixedFunctionValue FF_TOP_BLENDTEXTUREALPHAPM = 0x0000000f;
const FixedFunctionValue FF_TOP_BLENDCURRENTALPHA = 0x00000010;
const FixedFunctionValue FF_TOP_PREMODULATE = 0x00000011;
const FixedFunctionValue FF_TOP_MODULATEALPHA_ADDCOLOR = 0x00000012;
const FixedFunctionValue FF_TOP_MODULATECOLOR_ADDALPHA = 0x00000013;
const FixedFunctionValue FF_TOP_MODULATEINVALPHA_ADDCOLOR = 0x00000014;
const FixedFunctionValue FF_TOP_MODULATEINVCOLOR_ADDALPHA = 0x00000015;
const FixedFunctionValue FF_TOP_BUMPENVMAP = 0x00000016;
const FixedFunctionValue FF_TOP_BUMPENVMAPLUMINANCE = 0x00000017;
const FixedFunctionValue FF_TOP_DOTPRODUCT3 = 0x00000018;
const FixedFunctionValue FF_TOP_MULTIPLYADD = 0x00000019;
const FixedFunctionValue FF_TOP_LERP = 0x0000001a;

// D3DTA_*: a texture stage argument, and the two modifier flags above the select mask.
const FixedFunctionValue FF_TA_SELECTMASK = 0x0000000f;
const FixedFunctionValue FF_TA_DIFFUSE = 0x00000000;
const FixedFunctionValue FF_TA_CURRENT = 0x00000001;
const FixedFunctionValue FF_TA_TEXTURE = 0x00000002;
const FixedFunctionValue FF_TA_TFACTOR = 0x00000003;
const FixedFunctionValue FF_TA_SPECULAR = 0x00000004;
const FixedFunctionValue FF_TA_TEMP = 0x00000005;
const FixedFunctionValue FF_TA_CONSTANT = 0x00000006;
const FixedFunctionValue FF_TA_COMPLEMENT = 0x00000010;
const FixedFunctionValue FF_TA_ALPHAREPLICATE = 0x00000020;

// D3DCMPFUNC: the alpha test's comparison.
const FixedFunctionValue FF_CMP_NEVER = 0x00000001;
const FixedFunctionValue FF_CMP_LESS = 0x00000002;
const FixedFunctionValue FF_CMP_EQUAL = 0x00000003;
const FixedFunctionValue FF_CMP_LESSEQUAL = 0x00000004;
const FixedFunctionValue FF_CMP_GREATER = 0x00000005;
const FixedFunctionValue FF_CMP_NOTEQUAL = 0x00000006;
const FixedFunctionValue FF_CMP_GREATEREQUAL = 0x00000007;
const FixedFunctionValue FF_CMP_ALWAYS = 0x00000008;

// D3DMATERIALCOLORSOURCE: where a lit colour's material term comes from.
const FixedFunctionValue FF_MCS_MATERIAL = 0x00000000;
const FixedFunctionValue FF_MCS_COLOR1 = 0x00000001;
const FixedFunctionValue FF_MCS_COLOR2 = 0x00000002;

// D3DLIGHTTYPE.
const FixedFunctionValue FF_LIGHT_POINT = 0x00000001;
const FixedFunctionValue FF_LIGHT_SPOT = 0x00000002;
const FixedFunctionValue FF_LIGHT_DIRECTIONAL = 0x00000003;

// D3DFVF_*: the flexible vertex format bits the vertex generator reads.
const FixedFunctionValue FF_FVF_XYZ = 0x00000002;
const FixedFunctionValue FF_FVF_XYZRHW = 0x00000004;
const FixedFunctionValue FF_FVF_POSITION_MASK = 0x0000400e;
const FixedFunctionValue FF_FVF_NORMAL = 0x00000010;
const FixedFunctionValue FF_FVF_DIFFUSE = 0x00000040;
const FixedFunctionValue FF_FVF_SPECULAR = 0x00000080;
const FixedFunctionValue FF_FVF_TEXCOUNT_MASK = 0x00000f00;
const FixedFunctionValue FF_FVF_TEXCOUNT_SHIFT = 0x00000008;
const FixedFunctionValue FF_FVF_TEX0 = 0x00000000;
const FixedFunctionValue FF_FVF_TEX1 = 0x00000100;
const FixedFunctionValue FF_FVF_TEX2 = 0x00000200;
const FixedFunctionValue FF_FVF_TEX3 = 0x00000300;
const FixedFunctionValue FF_FVF_TEX4 = 0x00000400;

// D3DTSS_TCI_*: the generation mode in the high half of D3DTSS_TEXCOORDINDEX.
const FixedFunctionValue FF_TSS_TCI_PASSTHRU = 0x00000000;
const FixedFunctionValue FF_TSS_TCI_CAMERASPACENORMAL = 0x00010000;
const FixedFunctionValue FF_TSS_TCI_CAMERASPACEPOSITION = 0x00020000;
const FixedFunctionValue FF_TSS_TCI_CAMERASPACEREFLECTIONVECTOR = 0x00030000;
const FixedFunctionValue FF_TSS_TCI_SPHEREMAP = 0x00040000;

// D3DTEXTURETRANSFORMFLAGS.
const FixedFunctionValue FF_TTFF_DISABLE = 0x00000000;
const FixedFunctionValue FF_TTFF_COUNT1 = 0x00000001;
const FixedFunctionValue FF_TTFF_COUNT2 = 0x00000002;
const FixedFunctionValue FF_TTFF_COUNT3 = 0x00000003;
const FixedFunctionValue FF_TTFF_COUNT4 = 0x00000004;
const FixedFunctionValue FF_TTFF_PROJECTED = 0x00000100;

// D3DFOGMODE.
const FixedFunctionValue FF_FOG_NONE = 0x00000000;
const FixedFunctionValue FF_FOG_EXP = 0x00000001;
const FixedFunctionValue FF_FOG_EXP2 = 0x00000002;
const FixedFunctionValue FF_FOG_LINEAR = 0x00000003;

#endif
