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
** The fixed-function state the shader generators read, in a form that compiles without the
** DirectX SDK.
**
** On Windows this is <d3d9.h>, as it always was for ffshader.h and ffvertex.h, so everything that
** included one of them for the SDK still gets it.  Beside it, ffstate_values.h names the values the
** generators switch on, and each one is asserted equal to the SDK's here: the literals are checked
** by every Windows build that compiles a generator.
**
** Off Windows it is the table alone, and FixedFunctionValue is uint32_t: the width DWORD has on
** Windows, not unsigned long, which is 64 bits on an LP64 platform.  At 64 bits a ~ or a shift on a
** state value would differ from Windows in its upper bits, and so would anything that hashes or
** compares a description's raw bytes.  The type differs between the two (DWORD is unsigned long),
** so whatever prints a value casts it: the generators' keys print (unsigned long) with %lu.
*/

#ifndef FFSTATE_H
#define FFSTATE_H

#if defined(_WIN32)
#include <d3d9.h>
typedef DWORD FixedFunctionValue;
#else
#include <stdint.h>
typedef uint32_t FixedFunctionValue;
#endif

#include "ffstate_values.h"

#if defined(_WIN32)
static_assert(FF_TOP_DISABLE == D3DTOP_DISABLE, "D3DTOP_DISABLE");
static_assert(FF_TOP_SELECTARG1 == D3DTOP_SELECTARG1, "D3DTOP_SELECTARG1");
static_assert(FF_TOP_SELECTARG2 == D3DTOP_SELECTARG2, "D3DTOP_SELECTARG2");
static_assert(FF_TOP_MODULATE == D3DTOP_MODULATE, "D3DTOP_MODULATE");
static_assert(FF_TOP_MODULATE2X == D3DTOP_MODULATE2X, "D3DTOP_MODULATE2X");
static_assert(FF_TOP_MODULATE4X == D3DTOP_MODULATE4X, "D3DTOP_MODULATE4X");
static_assert(FF_TOP_ADD == D3DTOP_ADD, "D3DTOP_ADD");
static_assert(FF_TOP_ADDSIGNED == D3DTOP_ADDSIGNED, "D3DTOP_ADDSIGNED");
static_assert(FF_TOP_ADDSIGNED2X == D3DTOP_ADDSIGNED2X, "D3DTOP_ADDSIGNED2X");
static_assert(FF_TOP_SUBTRACT == D3DTOP_SUBTRACT, "D3DTOP_SUBTRACT");
static_assert(FF_TOP_ADDSMOOTH == D3DTOP_ADDSMOOTH, "D3DTOP_ADDSMOOTH");
static_assert(FF_TOP_BLENDDIFFUSEALPHA == D3DTOP_BLENDDIFFUSEALPHA, "D3DTOP_BLENDDIFFUSEALPHA");
static_assert(FF_TOP_BLENDTEXTUREALPHA == D3DTOP_BLENDTEXTUREALPHA, "D3DTOP_BLENDTEXTUREALPHA");
static_assert(FF_TOP_BLENDFACTORALPHA == D3DTOP_BLENDFACTORALPHA, "D3DTOP_BLENDFACTORALPHA");
static_assert(FF_TOP_BLENDTEXTUREALPHAPM == D3DTOP_BLENDTEXTUREALPHAPM, "D3DTOP_BLENDTEXTUREALPHAPM");
static_assert(FF_TOP_BLENDCURRENTALPHA == D3DTOP_BLENDCURRENTALPHA, "D3DTOP_BLENDCURRENTALPHA");
static_assert(FF_TOP_PREMODULATE == D3DTOP_PREMODULATE, "D3DTOP_PREMODULATE");
static_assert(FF_TOP_MODULATEALPHA_ADDCOLOR == D3DTOP_MODULATEALPHA_ADDCOLOR, "D3DTOP_MODULATEALPHA_ADDCOLOR");
static_assert(FF_TOP_MODULATECOLOR_ADDALPHA == D3DTOP_MODULATECOLOR_ADDALPHA, "D3DTOP_MODULATECOLOR_ADDALPHA");
static_assert(FF_TOP_MODULATEINVALPHA_ADDCOLOR == D3DTOP_MODULATEINVALPHA_ADDCOLOR, "D3DTOP_MODULATEINVALPHA_ADDCOLOR");
static_assert(FF_TOP_MODULATEINVCOLOR_ADDALPHA == D3DTOP_MODULATEINVCOLOR_ADDALPHA, "D3DTOP_MODULATEINVCOLOR_ADDALPHA");
static_assert(FF_TOP_BUMPENVMAP == D3DTOP_BUMPENVMAP, "D3DTOP_BUMPENVMAP");
static_assert(FF_TOP_BUMPENVMAPLUMINANCE == D3DTOP_BUMPENVMAPLUMINANCE, "D3DTOP_BUMPENVMAPLUMINANCE");
static_assert(FF_TOP_DOTPRODUCT3 == D3DTOP_DOTPRODUCT3, "D3DTOP_DOTPRODUCT3");
static_assert(FF_TOP_MULTIPLYADD == D3DTOP_MULTIPLYADD, "D3DTOP_MULTIPLYADD");
static_assert(FF_TOP_LERP == D3DTOP_LERP, "D3DTOP_LERP");
static_assert(FF_TA_SELECTMASK == D3DTA_SELECTMASK, "D3DTA_SELECTMASK");
static_assert(FF_TA_DIFFUSE == D3DTA_DIFFUSE, "D3DTA_DIFFUSE");
static_assert(FF_TA_CURRENT == D3DTA_CURRENT, "D3DTA_CURRENT");
static_assert(FF_TA_TEXTURE == D3DTA_TEXTURE, "D3DTA_TEXTURE");
static_assert(FF_TA_TFACTOR == D3DTA_TFACTOR, "D3DTA_TFACTOR");
static_assert(FF_TA_SPECULAR == D3DTA_SPECULAR, "D3DTA_SPECULAR");
static_assert(FF_TA_TEMP == D3DTA_TEMP, "D3DTA_TEMP");
static_assert(FF_TA_CONSTANT == D3DTA_CONSTANT, "D3DTA_CONSTANT");
static_assert(FF_TA_COMPLEMENT == D3DTA_COMPLEMENT, "D3DTA_COMPLEMENT");
static_assert(FF_TA_ALPHAREPLICATE == D3DTA_ALPHAREPLICATE, "D3DTA_ALPHAREPLICATE");
static_assert(FF_CMP_NEVER == D3DCMP_NEVER, "D3DCMP_NEVER");
static_assert(FF_CMP_LESS == D3DCMP_LESS, "D3DCMP_LESS");
static_assert(FF_CMP_EQUAL == D3DCMP_EQUAL, "D3DCMP_EQUAL");
static_assert(FF_CMP_LESSEQUAL == D3DCMP_LESSEQUAL, "D3DCMP_LESSEQUAL");
static_assert(FF_CMP_GREATER == D3DCMP_GREATER, "D3DCMP_GREATER");
static_assert(FF_CMP_NOTEQUAL == D3DCMP_NOTEQUAL, "D3DCMP_NOTEQUAL");
static_assert(FF_CMP_GREATEREQUAL == D3DCMP_GREATEREQUAL, "D3DCMP_GREATEREQUAL");
static_assert(FF_CMP_ALWAYS == D3DCMP_ALWAYS, "D3DCMP_ALWAYS");
static_assert(FF_MCS_MATERIAL == D3DMCS_MATERIAL, "D3DMCS_MATERIAL");
static_assert(FF_MCS_COLOR1 == D3DMCS_COLOR1, "D3DMCS_COLOR1");
static_assert(FF_MCS_COLOR2 == D3DMCS_COLOR2, "D3DMCS_COLOR2");
static_assert(FF_LIGHT_POINT == D3DLIGHT_POINT, "D3DLIGHT_POINT");
static_assert(FF_LIGHT_SPOT == D3DLIGHT_SPOT, "D3DLIGHT_SPOT");
static_assert(FF_LIGHT_DIRECTIONAL == D3DLIGHT_DIRECTIONAL, "D3DLIGHT_DIRECTIONAL");
static_assert(FF_FVF_XYZ == D3DFVF_XYZ, "D3DFVF_XYZ");
static_assert(FF_FVF_XYZRHW == D3DFVF_XYZRHW, "D3DFVF_XYZRHW");
static_assert(FF_FVF_POSITION_MASK == D3DFVF_POSITION_MASK, "D3DFVF_POSITION_MASK");
static_assert(FF_FVF_NORMAL == D3DFVF_NORMAL, "D3DFVF_NORMAL");
static_assert(FF_FVF_DIFFUSE == D3DFVF_DIFFUSE, "D3DFVF_DIFFUSE");
static_assert(FF_FVF_SPECULAR == D3DFVF_SPECULAR, "D3DFVF_SPECULAR");
static_assert(FF_FVF_TEXCOUNT_MASK == D3DFVF_TEXCOUNT_MASK, "D3DFVF_TEXCOUNT_MASK");
static_assert(FF_FVF_TEXCOUNT_SHIFT == D3DFVF_TEXCOUNT_SHIFT, "D3DFVF_TEXCOUNT_SHIFT");
static_assert(FF_FVF_TEX0 == D3DFVF_TEX0, "D3DFVF_TEX0");
static_assert(FF_FVF_TEX1 == D3DFVF_TEX1, "D3DFVF_TEX1");
static_assert(FF_FVF_TEX2 == D3DFVF_TEX2, "D3DFVF_TEX2");
static_assert(FF_FVF_TEX3 == D3DFVF_TEX3, "D3DFVF_TEX3");
static_assert(FF_FVF_TEX4 == D3DFVF_TEX4, "D3DFVF_TEX4");
static_assert(FF_TSS_TCI_PASSTHRU == D3DTSS_TCI_PASSTHRU, "D3DTSS_TCI_PASSTHRU");
static_assert(FF_TSS_TCI_CAMERASPACENORMAL == D3DTSS_TCI_CAMERASPACENORMAL, "D3DTSS_TCI_CAMERASPACENORMAL");
static_assert(FF_TSS_TCI_CAMERASPACEPOSITION == D3DTSS_TCI_CAMERASPACEPOSITION, "D3DTSS_TCI_CAMERASPACEPOSITION");
static_assert(FF_TSS_TCI_CAMERASPACEREFLECTIONVECTOR == D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR, "D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR");
static_assert(FF_TSS_TCI_SPHEREMAP == D3DTSS_TCI_SPHEREMAP, "D3DTSS_TCI_SPHEREMAP");
static_assert(FF_TTFF_DISABLE == D3DTTFF_DISABLE, "D3DTTFF_DISABLE");
static_assert(FF_TTFF_COUNT1 == D3DTTFF_COUNT1, "D3DTTFF_COUNT1");
static_assert(FF_TTFF_COUNT2 == D3DTTFF_COUNT2, "D3DTTFF_COUNT2");
static_assert(FF_TTFF_COUNT3 == D3DTTFF_COUNT3, "D3DTTFF_COUNT3");
static_assert(FF_TTFF_COUNT4 == D3DTTFF_COUNT4, "D3DTTFF_COUNT4");
static_assert(FF_TTFF_PROJECTED == D3DTTFF_PROJECTED, "D3DTTFF_PROJECTED");
static_assert(FF_FOG_NONE == D3DFOG_NONE, "D3DFOG_NONE");
static_assert(FF_FOG_EXP == D3DFOG_EXP, "D3DFOG_EXP");
static_assert(FF_FOG_EXP2 == D3DFOG_EXP2, "D3DFOG_EXP2");
static_assert(FF_FOG_LINEAR == D3DFOG_LINEAR, "D3DFOG_LINEAR");
#endif

#endif
