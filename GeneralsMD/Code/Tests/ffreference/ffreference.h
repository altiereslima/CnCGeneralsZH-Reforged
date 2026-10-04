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
 * FFReference: Direct3D 9's fixed-function pipeline, computed in double precision on the CPU, from
 * Microsoft's documentation only (A3b).
 *
 * WHAT IT IS FOR.  An independent reading of the documented pipeline, so that A3's GPU fixed-function
 * programs can be compared against something that did not come from the same author or the same
 * code.  A3's harness hands it a draw's full state and vertices, it renders into a double-precision
 * target, and compare() classifies each GPU pixel against it.
 *
 * INDEPENDENCE RECORD.  Written from learn.microsoft.com's Direct3D 9 pages alone (each function in
 * ffreference.cpp names the page it follows).  Its author did not read ffshader.cpp, ffvertex.cpp,
 * the dx11 backend or A3's code while writing it, and did not work on them before.  Where the pages
 * are thin, the reading chosen is listed below under NAMED CHOICES, one line each, so that any
 * disagreement with the GPU side traces to one line: either a bug on one side, or a choice to revisit.
 *
 * CONVENTIONS
 *   - Matrices are D3D's: row vectors, v' = v * M, m[row][col], so _41.._43 hold a translation.
 *   - RASTER: D3D9's.  Pixel (x, y)'s centre is at integer screen coordinates (x, y), NOT D3D10's
 *     (x + 0.5, y + 0.5); the top-left fill rule decides centres exactly on an edge.  Texel (i, j)'s
 *     centre is at ((i + 0.5) / width, (j + 0.5) / height), row 0 at v = 0.
 *     ("Rasterization Rules", "Directly Mapping Texels to Pixels")
 *   - Screen mapping: X = vp.X + (1 + x/w) * vp.Width / 2, Y = vp.Y + (1 - y/w) * vp.Height / 2,
 *     Z = vp.MinZ + (z/w) * (vp.MaxZ - vp.MinZ).  ("Viewports and Clipping")
 *   - Clipping: homogeneous, before the divide, against 0 <= z <= w (and w > 0).  x and y are not
 *     clipped geometrically: pixels are limited to the viewport rectangle, which is the same set.
 *     ("Viewports and Clipping")
 *   - Colours are doubles 0..1.  A D3DCOLOR 0xAARRGGBB converts by / 255.
 *   - Textures arrive decoded (the harness decodes BC1-3 and the rest): level 0 is the largest, texels
 *     row-major from the top row.  A bump-map format (V8U8, X8L8V8U8) arrives SIGNED: du in r, dv in g,
 *     -1..1, and luminance in b, 0..1.  A format without alpha arrives with a = 1.
 *
 * WHAT IT DOES: transform; lighting (directional, point, spot; material sources; local viewer);
 * vertex fog (linear, exp, exp2; depth or range) and table fog (z or w); texture-coordinate index and
 * generation (normal, position, reflection); texture transforms (count 1-4, projected); flat and
 * Gouraud shading; perspective-correct interpolation; 2D sampling with point/linear mag and min
 * filters, point/linear mip filters, LOD bias and MAXMIPLEVEL, all five address modes and the border
 * colour; the whole texture-stage cascade (D3DTOP 2-26, every D3DTA source and modifier, TEMP and
 * RESULTARG, the per-stage constant, bump mapping, premodulate); specular add; fog blend; alpha test;
 * depth (with bias) and stencil (one- and two-sided); scissor; alpha blending (every D3DBLEND and
 * D3DBLENDOP, separate alpha); colour write mask; the target's 8-bit quantisation.
 *
 * REFUSED (draw() returns false, Report::refusals says why, nothing is written):
 *   vertex or pixel shader bound; primitives other than triangle list/strip/fan; FILLMODE other than
 *   solid; SHADEMODE phong; VERTEXBLEND; user clip planes (CLIPPLANEENABLE != 0); w-buffering
 *   (ZENABLE = USEW); cube or volume textures at a stage the cascade reads; TCI SPHEREMAP (no page
 *   defines it); texture generation or TTFF on pretransformed (XYZRHW) vertices; MAG/MIN filter NONE,
 *   PYRAMIDALQUAD, GAUSSIANQUAD, CONVOLUTIONMONO, and ANISOTROPIC with MAXANISOTROPY > 1; sRGB reads or
 *   writes; D3DRS_WRAPn cylindrical wrapping; a colour-only D3DTOP used as ALPHAOP; RESULTARG other than CURRENT or
 *   TEMP; a stage reading D3DTA_TEXTURE with no texture bound, except the documented case of COLORARG1.
 *
 * NAMED CHOICES (where the pages are silent, contradictory, or image-only)
 *   N1  "Camera-Space Transformations" writes the directional light's camera direction as
 *       -norm(Dir * World * View) and a point light's vector as norm(V * Lp); both are read as the
 *       evident intent: -norm(Dir * View) (lights are in world space) and norm(Lp - V).
 *   N2  The spotlight and fog formulas are images; they are rebuilt from the variables the text names:
 *       spot = 1 if rho > cos(theta/2), 0 if rho <= cos(phi/2), else
 *       ((rho - cos(phi/2)) / (cos(theta/2) - cos(phi/2)))^falloff, rho = norm(Ldir_light).norm(V - Lp);
 *       fog linear (end - d)/(end - start), exp e^-(d*density), exp2 e^-(d*density)^2, clamped 0..1.
 *   N3  N.Ldir and N.H are clamped at 0 before use (a negative base has no real power).  pow(0, 0) = 1.
 *       Specular is gated by N.Ldir: a light with N.Ldir <= 0 adds no specular - MEASURED, not read (the
 *       page's formula has no gate): WARP and REF give 0 where N.H is .531 and N.Ldir -.436 (knownprobe
 *       N3a, knownprobe_windows.cpp).
 *   N4  Per-light ambient (Atten * Spot * La) is included, as "Ambient Lighting" writes it (measured on
 *       WARP and REF: attenuated, and none beyond the range; knownprobe F1).
 *   N5  With LIGHTING, the lit diffuse's alpha is the diffuse source's alpha and the lit specular's is
 *       the specular source's alpha; lit colours are clamped to 0..1 after summing all lights.
 *   N6  Lit specular is computed only with SPECULARENABLE ("Specular Lighting": "The default lighting
 *       state does not calculate specular highlights"); without it the lit specular's RGB is 0.  With
 *       it, it is added after the cascade (D3DRENDERSTATETYPE: "added to the base color after the
 *       texture cascade but before alpha blending"), RGB only, saturated.
 *   N7  An absent vertex diffuse is 0xFFFFFFFF, as D3DTA writes; an absent vertex SPECULAR is 0x00000000,
 *       RGB and alpha - MEASURED, not read: D3DTA writes 0xffffffff for both, but Windows' own Direct3D 9
 *       (the Microsoft Basic Render Driver, WARP, d3d10warp.dll 10.0.26100.5074, as a HAL device and as
 *       REF, 2026-09-27; f7probe_windows.cpp) reads an absent specular as
 *       black with alpha 0 through D3DTA_SPECULAR, adds nothing with SPECULARENABLE, and reads an absent
 *       diffuse as white.  This was the device's reading (finding F7); the measurement is the authority.
 *       COLORVERTEX with a material source naming a colour the vertex lacks uses the material's.
 *   N8  Vertex fog distance is |z| in camera space, or the camera-space distance with RANGEFOGENABLE.
 *       Table fog wins over vertex fog when both are set.  Table fog uses w (eye distance) unless the
 *       projection's fourth column is (0,0,0,1), then z/w before the viewport's MinZ/MaxZ scale.
 *       FOGENABLE with both modes NONE (and for pretransformed vertices without table fog) takes the
 *       fog factor from the specular alpha.  Linear fog with end == start gives 1 at d <= start, else 0.
 *   N9  Fog blends RGB only: f * C + (1 - f) * FOGCOLOR.  Order: cascade, specular add, fog, alpha
 *       test, stencil/depth, blend, write mask.
 *   N10 Colours, texture coordinates and the fog factor are all interpolated perspective-correct (by
 *       1/w); depth (Z) is interpolated linearly in screen space.
 *   N11 Flat shading takes diffuse and specular from the first vertex of each triangle (list 3i,
 *       strip i, fan i+1, per D3DSHADEMODE); fog is still interpolated.  Measured on WARP and REF
 *       (knownprobe F8, N11f): every vertex order, the diffuse alpha flat too, fog interpolated.  The
 *       specular is flat on REF, but WARP interpolates it: this follows REF and the page.
 *   N12 Winding (for culling and two-sided stencil) is judged on screen, y down: clockwise means
 *       (x1-x0)(y2-y0) - (x2-x0)(y1-y0) > 0.  Odd triangles of a strip are taken as (i+1, i, i+2).
 *   N13 Texture-transform input padding: (u) -> (u, 1, 0, 0), (u, v) -> (u, v, 1, 0),
 *       (u, v, w) -> (u, v, w, 1); generated coordinates are (x, y, z, 1).  TTFF COUNTn keeps n
 *       elements; PROJECTED divides the first n-1 by the n-th.  Without TTFF the first two are used.
 *       The (u, v, 1, 0) padding is measured on WARP and REF: _31/_32 move a COUNT2 set, _41/_42 do not
 *       (knownprobe F4b/F4c).
 *   N14 Reflection vector ("Cubic Environment Mapping"): R = 2(E.N)N - E with E = norm(-Vcamera) under
 *       LOCALVIEWER, else E = (0,0,-1): R = -2 Nz N + (0,0,1) - MEASURED, not read: the page writes
 *       2 Nz N - (0,0,1), but WARP and REF give R = (0, .96, -.28) for N (0, .6, -.8), which is E =
 *       (0,0,-1) (knownprobe N14b/N14d), and match E = norm(-Vcamera) with LOCALVIEWER (N14a/N14c).  The
 *       normal is the one lighting uses.
 *   N15 LOD: lambda = log2(max(|d(uW,vH)/dx|, |d(uW,vH)/dy|)) + MIPMAPLODBIAS, from the exact screen
 *       derivatives.  The pages define no LOD at all ("Texture Filtering with Mipmaps": "Direct3D can
 *       assess which texture in a mipmap set is the closest resolution"), so this is the nominal only,
 *       and the envelope allows +-0.6 (Freedoms::lodDelta, F11, 2026-09-26): any footprint norm from
 *       L-infinity to L1 is within sqrt(2) of this L2 one, +-0.5 in log2, and 2x2 differencing adds
 *       ~0.1.  Apple's GPU measured +0.10 mean, +0.58 worst on x-stretched footprints (a probe on an M3 Pro).
 *       Microsoft's own two follow this nominal: WARP and REF read lambda within -0.05..+0.03 of it on
 *       isotropic, anisotropic and rotated footprints, where L-infinity or L1 would be 0.5 off (knownprobe
 *       F11).  The +-0.6 stays a ruling for hardware, which no Windows driver here could measure.
 *       lambda <= 0 magnifies.  MIPFILTER NONE uses level MAXMIPLEVEL; POINT the nearest level
 *       (round half up); LINEAR blends floor and floor+1; all clamped to [MAXMIPLEVEL, levels-1].
 *       ANISOTROPIC with MAXANISOTROPY <= 1 filters as LINEAR.  Bump offsets do not enter the LOD.
 *   N16 Every stage's result is saturated to 0..1.  Argument modifiers apply to all four components;
 *       ALPHAREPLICATE copies alpha into RGB, COMPLEMENT takes 1 - x (the two commute).
 *   N17 DOTPRODUCT3 reads its arguments as signed, 2x - 1, so the sum is 4 * sum((a-.5)(b-.5)), then
 *       saturates; as a COLOROP it also replaces the stage's alpha ("replicate the sum to all color
 *       channels, including alpha").  The page's DirectX 6/7 note (x - .5, RGB only) is not followed.
 *       Measured on WARP and REF: C0 80 80 . C0 80 80 gives 65 in all four channels (knownprobe F3b).
 *   N18 Triadic operations: MULTIPLYADD = ARG0 + ARG1 * ARG2, LERP = ARG0 * ARG1 + (1 - ARG0) * ARG2
 *       (the page's Arg1/Arg2/Arg3 read as COLORARG0/1/2).
 *   N19 Bump mapping: du' = du * M00 + dv * M10, dv' = du * M01 + dv * M11 (the formula is an image),
 *       added to the next stage's coordinates in normalised units; L' = clamp(L * scale + offset, 0, 1)
 *       multiplies the next stage's texture RGB.  The bump stage passes CURRENT through (D3DTA_CURRENT).
 *   N20 A stage whose COLORARG1 is D3DTA_TEXTURE with no texture bound ends the cascade there.  With
 *       stage 0 disabled the cascade's result is the diffuse colour.  The result is CURRENT after the
 *       last enabled stage, whatever RESULTARG the last stage used.
 *   N21 Alpha test compares the pixel's alpha against ALPHAREF / 255 (low 8 bits), unrounded.
 *   N22 Depth bias: Z += DEPTHBIAS + SLOPESCALEDEPTHBIAS * max(|dZ/dX|, |dZ/dY|), in the 0..1 depth
 *       units of the z-buffer, then clamped to 0..1.  ZENABLE FALSE disables the test and the write.
 *   N23 D3DBLEND_BOTHSRCALPHA / BOTHINVSRCALPHA as SRCBLEND override DESTBLEND (and likewise for the
 *       separate alpha factors).  MIN and MAX ignore the factors.  The blend result is clamped to 0..1.
 *       A target without alpha reads destination alpha as 1.
 *   N24 Writes to an 8-bit target round to the nearest level.  DITHERENABLE is ignored.
 *   N25 Pretransformed (XYZRHW) vertices are not clipped geometrically: pixels whose Z falls outside
 *       0..1 are discarded, and the pixels are limited to the viewport like any other.
 *   N26 D3DTOP_PREMODULATE: stage n outputs ARG1; if stage n+1 has a texture, every D3DTA_CURRENT that
 *       stage n+1 reads is first multiplied by stage n+1's texture colour (the page's wording).
 *   N27 The halfway vector without LOCALVIEWER is norm((0,0,-1) + Ldir): the viewer toward -z, where a
 *       camera looking down +z is - MEASURED, not read: "Specular Lighting" writes (0,0,1), but WARP and
 *       REF give N.H = .949 for Ldir (0,-.6,-.8) and N (0,0,-1), which (0,0,1) would make 0 (knownprobe
 *       F2a0/F2c0).  With LOCALVIEWER, norm(norm(-Vcamera) + Ldir), as written and as measured (F2a1).
 *   N28 A stage whose TEXCOORDINDEX names a coordinate set the vertices lack reads u, v = (0, 0)
 *       (D3DTSS_TEXCOORDINDEX: "the system defaults to the u and v coordinates (0,0)"; measured on WARP
 *       and REF, knownprobe F4f); the page names
 *       no third or fourth component, so the set is padded as any 2-component set is (N13).
 *   N29 ALPHAOP DISABLE under an enabled COLOROP is "undefined behavior" (D3DTEXTUREOP, D3DTOP_DISABLE),
 *       and the game does it.  It is drawn rather than refused: that stage's alpha is unconstrained, with
 *       CURRENT's alpha passed through as the nominal and 0 and 1 as the envelope's variants.  Every
 *       later use of alpha - an operation, the alpha test, the blend - is linear or a threshold in it,
 *       so the two extremes bound what any value could give.  The pixels it moves carry ZONE_UNDEFINED.
 *       Measured, it is indeed undefined: WARP passes CURRENT's alpha through and REF writes 1
 *       (knownprobe N29), both inside the envelope.
 *   N30 Where the alpha test is undecided (ZONE_ALPHA_TEST: some variant passes, some fails), the colour
 *       that may be written is bounded by every variant's colour, passing or not.  The variants are
 *       points in a continuous freedom, and colour and alpha move together between them, so the pass
 *       boundary can fall between a failing and a passing variant with a colour on the way from one to
 *       the other - beyond every passing variant's when the texture's colour falls as its alpha rises
 *       (foliage over a bright ground).  Found on the game's own alpha-tested draws (C2, 2026-09-26).
 */

#ifndef FFREFERENCE_H
#define FFREFERENCE_H

#include "ffreference/ffprogram.h"
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <vector>

namespace FFRef {

// ---- the documented enumerations this reads (values from each enumeration's page) ----------------
enum { PT_POINTLIST = 1, PT_LINELIST = 2, PT_LINESTRIP = 3, PT_TRIANGLELIST = 4, PT_TRIANGLESTRIP = 5, PT_TRIANGLEFAN = 6 };

enum RenderState {
	RS_ZENABLE = 7, RS_FILLMODE = 8, RS_SHADEMODE = 9, RS_ZWRITEENABLE = 14, RS_ALPHATESTENABLE = 15,
	RS_LASTPIXEL = 16, RS_SRCBLEND = 19, RS_DESTBLEND = 20, RS_CULLMODE = 22, RS_ZFUNC = 23,
	RS_ALPHAREF = 24, RS_ALPHAFUNC = 25, RS_DITHERENABLE = 26, RS_ALPHABLENDENABLE = 27,
	RS_FOGENABLE = 28, RS_SPECULARENABLE = 29, RS_FOGCOLOR = 34, RS_FOGTABLEMODE = 35,
	RS_FOGSTART = 36, RS_FOGEND = 37, RS_FOGDENSITY = 38, RS_RANGEFOGENABLE = 48,
	RS_STENCILENABLE = 52, RS_STENCILFAIL = 53, RS_STENCILZFAIL = 54, RS_STENCILPASS = 55,
	RS_STENCILFUNC = 56, RS_STENCILREF = 57, RS_STENCILMASK = 58, RS_STENCILWRITEMASK = 59,
	RS_TEXTUREFACTOR = 60, RS_WRAP0 = 128, RS_CLIPPING = 136, RS_LIGHTING = 137, RS_AMBIENT = 139,
	RS_FOGVERTEXMODE = 140, RS_COLORVERTEX = 141, RS_LOCALVIEWER = 142, RS_NORMALIZENORMALS = 143,
	RS_DIFFUSEMATERIALSOURCE = 145, RS_SPECULARMATERIALSOURCE = 146, RS_AMBIENTMATERIALSOURCE = 147,
	RS_EMISSIVEMATERIALSOURCE = 148, RS_VERTEXBLEND = 151, RS_CLIPPLANEENABLE = 152,
	RS_COLORWRITEENABLE = 168, RS_BLENDOP = 171, RS_SCISSORTESTENABLE = 174,
	RS_SLOPESCALEDEPTHBIAS = 175, RS_TWOSIDEDSTENCILMODE = 185, RS_CCW_STENCILFAIL = 186,
	RS_CCW_STENCILZFAIL = 187, RS_CCW_STENCILPASS = 188, RS_CCW_STENCILFUNC = 189,
	RS_BLENDFACTOR = 193, RS_SRGBWRITEENABLE = 194, RS_DEPTHBIAS = 195, RS_WRAP8 = 198,
	RS_SEPARATEALPHABLENDENABLE = 206, RS_SRCBLENDALPHA = 207, RS_DESTBLENDALPHA = 208,
	RS_BLENDOPALPHA = 209
};

enum StageState {
	TSS_COLOROP = 1, TSS_COLORARG1 = 2, TSS_COLORARG2 = 3, TSS_ALPHAOP = 4, TSS_ALPHAARG1 = 5,
	TSS_ALPHAARG2 = 6, TSS_BUMPENVMAT00 = 7, TSS_BUMPENVMAT01 = 8, TSS_BUMPENVMAT10 = 9,
	TSS_BUMPENVMAT11 = 10, TSS_TEXCOORDINDEX = 11, TSS_BUMPENVLSCALE = 22, TSS_BUMPENVLOFFSET = 23,
	TSS_TEXTURETRANSFORMFLAGS = 24, TSS_COLORARG0 = 26, TSS_ALPHAARG0 = 27, TSS_RESULTARG = 28,
	TSS_CONSTANT = 32
};

enum SamplerState {
	SAMP_ADDRESSU = 1, SAMP_ADDRESSV = 2, SAMP_ADDRESSW = 3, SAMP_BORDERCOLOR = 4, SAMP_MAGFILTER = 5,
	SAMP_MINFILTER = 6, SAMP_MIPFILTER = 7, SAMP_MIPMAPLODBIAS = 8, SAMP_MAXMIPLEVEL = 9,
	SAMP_MAXANISOTROPY = 10, SAMP_SRGBTEXTURE = 11, SAMP_ELEMENTINDEX = 12, SAMP_DMAPOFFSET = 13
};

enum {
	TOP_DISABLE = 1, TOP_SELECTARG1, TOP_SELECTARG2, TOP_MODULATE, TOP_MODULATE2X, TOP_MODULATE4X,
	TOP_ADD, TOP_ADDSIGNED, TOP_ADDSIGNED2X, TOP_SUBTRACT, TOP_ADDSMOOTH, TOP_BLENDDIFFUSEALPHA,
	TOP_BLENDTEXTUREALPHA, TOP_BLENDFACTORALPHA, TOP_BLENDTEXTUREALPHAPM, TOP_BLENDCURRENTALPHA,
	TOP_PREMODULATE, TOP_MODULATEALPHA_ADDCOLOR, TOP_MODULATECOLOR_ADDALPHA,
	TOP_MODULATEINVALPHA_ADDCOLOR, TOP_MODULATEINVCOLOR_ADDALPHA, TOP_BUMPENVMAP,
	TOP_BUMPENVMAPLUMINANCE, TOP_DOTPRODUCT3, TOP_MULTIPLYADD, TOP_LERP
};

enum {
	TA_DIFFUSE = 0, TA_CURRENT = 1, TA_TEXTURE = 2, TA_TFACTOR = 3, TA_SPECULAR = 4, TA_TEMP = 5,
	TA_CONSTANT = 6, TA_SELECTMASK = 0xF, TA_COMPLEMENT = 0x10, TA_ALPHAREPLICATE = 0x20
};

enum {
	TSS_TCI_PASSTHRU = 0x00000, TSS_TCI_CAMERASPACENORMAL = 0x10000,
	TSS_TCI_CAMERASPACEPOSITION = 0x20000, TSS_TCI_CAMERASPACEREFLECTIONVECTOR = 0x30000,
	TSS_TCI_SPHEREMAP = 0x40000
};
enum { TTFF_DISABLE = 0, TTFF_COUNT1 = 1, TTFF_COUNT2, TTFF_COUNT3, TTFF_COUNT4, TTFF_PROJECTED = 256 };

enum { TEXF_NONE = 0, TEXF_POINT = 1, TEXF_LINEAR = 2, TEXF_ANISOTROPIC = 3 };
enum { TADDRESS_WRAP = 1, TADDRESS_MIRROR, TADDRESS_CLAMP, TADDRESS_BORDER, TADDRESS_MIRRORONCE };

enum { BLEND_ZERO = 1, BLEND_ONE, BLEND_SRCCOLOR, BLEND_INVSRCCOLOR, BLEND_SRCALPHA, BLEND_INVSRCALPHA,
	BLEND_DESTALPHA, BLEND_INVDESTALPHA, BLEND_DESTCOLOR, BLEND_INVDESTCOLOR, BLEND_SRCALPHASAT,
	BLEND_BOTHSRCALPHA, BLEND_BOTHINVSRCALPHA, BLEND_BLENDFACTOR, BLEND_INVBLENDFACTOR };
enum { BLENDOP_ADD = 1, BLENDOP_SUBTRACT, BLENDOP_REVSUBTRACT, BLENDOP_MIN, BLENDOP_MAX };
enum { CMP_NEVER = 1, CMP_LESS, CMP_EQUAL, CMP_LESSEQUAL, CMP_GREATER, CMP_NOTEQUAL, CMP_GREATEREQUAL, CMP_ALWAYS };
enum { STENCILOP_KEEP = 1, STENCILOP_ZERO, STENCILOP_REPLACE, STENCILOP_INCRSAT, STENCILOP_DECRSAT,
	STENCILOP_INVERT, STENCILOP_INCR, STENCILOP_DECR };
enum { CULL_NONE = 1, CULL_CW = 2, CULL_CCW = 3 };
enum { FILL_POINT = 1, FILL_WIREFRAME = 2, FILL_SOLID = 3 };
enum { SHADE_FLAT = 1, SHADE_GOURAUD = 2, SHADE_PHONG = 3 };
enum { FOG_NONE = 0, FOG_EXP = 1, FOG_EXP2 = 2, FOG_LINEAR = 3 };
enum { ZB_FALSE = 0, ZB_TRUE = 1, ZB_USEW = 2 };
enum { MCS_MATERIAL = 0, MCS_COLOR1 = 1, MCS_COLOR2 = 2 };
enum { LIGHT_POINT = 1, LIGHT_SPOT = 2, LIGHT_DIRECTIONAL = 3 };
enum { TEXTURE_2D = 0, TEXTURE_CUBE = 1, TEXTURE_VOLUME = 2 };

// ---- a draw's inputs ------------------------------------------------------------------------------
struct Color { double r, g, b, a; };
Color colorFromD3D( uint32_t argb );
uint32_t floatBits( float f );		///< for the float-valued states, as SetRenderState takes them

struct Matrix
{
	double m[4][4];		///< m[row][col]; row vectors: v' = v * M
	static Matrix identity();
};

struct Light			///< D3DLIGHT9, in world space
{
	bool enabled;
	int type;			///< LIGHT_*
	Color diffuse, specular, ambient;
	double position[3], direction[3];
	double range, falloff, attenuation0, attenuation1, attenuation2, theta, phi;
};

struct Material { Color diffuse, ambient, specular, emissive; double power; };	///< D3DMATERIAL9

struct Viewport { double x, y, width, height, minZ, maxZ; };	///< D3DVIEWPORT9
struct Rect { int left, top, right, bottom; };						///< the scissor rectangle, right/bottom exclusive

struct TextureLevel { int width, height; std::vector<Color> texels; };
struct Texture
{
	int type;							///< TEXTURE_2D; the others are refused where read
	std::vector<TextureLevel> levels;	///< decoded; see CONVENTIONS for bump formats
};

enum { MAX_STAGES = 8, MAX_LIGHTS = 8 };

struct DrawState
{
	uint32_t renderState[256];			///< raw D3DRS values, indexed by D3DRENDERSTATETYPE
	uint32_t stageState[MAX_STAGES][33];	///< raw D3DTSS values
	uint32_t samplerState[MAX_STAGES][14];	///< raw D3DSAMP values
	Matrix world, view, projection;
	Matrix textureTransform[MAX_STAGES];
	Light lights[MAX_LIGHTS];
	Material material;
	Viewport viewport;
	Rect scissor;
	const Texture *textures[MAX_STAGES];	///< NULL where no texture is bound

	bool vertexShaderBound, pixelShaderBound;	///< refused when set and no program below is given

	// A3e: the programmable stages (ffprogram.h), run from the programs' own tokens.  NULL: fixed function.
	// A vertex program comes with a pixel program (every shipped one does); fog and texture transforms
	// with a vertex program are refused (not in the census).
	const Program *vertexProgram, *pixelProgram;
	double vertexConstants[96][4];		///< c0-c95
	double pixelConstants[8][4];		///< c0-c7 (a program's def overrides, as it runs)
	int vertexInput[16];				///< the declaration: the Vertex element each vN reads (VertexInput)
	int vertexInputSize[16];			///< how many components it supplies; the rest are (0, 0, 0, 1)'s
	/// Or, the declaration decoded from each vertex's own bytes (declarationInputs): Vertex::programInput
	/// holds v0-v15, and bit n here says an element fed vN
	bool programInputsGiven;
	unsigned programInputPresent;

	// The vertex format (what the FVF or declaration supplies)
	bool pretransformed;				///< D3DFVF_XYZRHW: position is (X, Y, Z, RHW) on screen
	bool hasNormal, hasDiffuse, hasSpecular;
	int texCoordSets;					///< 0..8
	int texCoordSize[MAX_STAGES];		///< 1..4 elements per set

	/// Every state at its documented default (D3DRENDERSTATETYPE, D3DTEXTURESTAGESTATETYPE,
	/// D3DSAMPLERSTATETYPE); identity matrices; no lights; a white material; ZENABLE TRUE, as with an
	/// automatic depth-stencil.  The viewport and scissor cover width x height.
	void setDefaults( int width, int height );
};

/// What a vertex program's vN reads (the declaration's element), by Vertex member
enum VertexInput { INPUT_NONE, INPUT_POSITION, INPUT_NORMAL, INPUT_DIFFUSE, INPUT_SPECULAR, INPUT_TEXCOORD0 };	///< + set

struct Vertex
{
	double position[4];			///< x, y, z (and RHW when pretransformed)
	double normal[3];
	Color diffuse, specular;
	double tex[MAX_STAGES][4];	///< by coordinate set
	double programInput[16][4];	///< v0-v15, when DrawState::programInputsGiven (declarationInputs)
};

// ---- the target -----------------------------------------------------------------------------------
enum Zone			///< why a pixel's envelope is wider than its nominal value (the documented freedoms)
{
	ZONE_EDGE = 1,			///< its centre is within Freedoms::edgePixels of a triangle edge
	ZONE_TEXEL = 2,			///< a texel-coordinate shift changes it (point ties, bilinear weight precision)
	ZONE_LOD = 4,			///< an LOD shift changes it (mip transitions, mag/min crossover)
	ZONE_ALPHA_TEST = 8,	///< its alpha is within Freedoms::alphaRef of ALPHAREF
	ZONE_DEPTH = 16,		///< its depth test is a tie, or the depth under it was ambiguous
	ZONE_STENCIL = 32,		///< the stencil under it was ambiguous
	ZONE_UNDEFINED = 64,	///< a state the pages call undefined changes it (N29): allowed, never a clean pass
	ZONE_PROGRAM = 128		///< a pixel program's range cap or precision changes it (ffprogram.h P4, P5)
};

/// What the last triangle that nominally wrote a pixel did there: for reading a disagreement (C5).
struct PixelDetail
{
	int primitive;			///< its index among the draw's primitives; -1 when none wrote the pixel
	int layers;				///< how many of the draw's triangles nominally wrote the pixel
	int writers[8];			///< the first eight of them, by primitive index, in the order they wrote
	Color source;			///< its colour before the blend; its alpha is the one the alpha test read
	bool alphaPassed;
	double uv[2][2];		///< stages 0 and 1: the coordinates sampled
	double lod[2];			///< and the LOD they were sampled at (bias included); -1e9 when not sampled
	double axes[2][2];		///< the footprint's two axes there, level-0 texels per pixel along x and y
	double screen[3][2];	///< the triangle's vertices on screen
};

struct Target
{
	int width, height;
	bool hasAlpha;				///< false for X8R8G8B8: destination alpha reads 1
	int colorBits;				///< 8 quantises each write (N24); 0 keeps doubles
	int stencilBits;
	std::vector<Color> color;	///< nominal
	std::vector<Color> lo, hi;	///< per channel, the envelope every documented freedom allows
	std::vector<double> depth;
	std::vector<uint32_t> stencil;
	std::vector<uint32_t> zones;		///< Zone bits, accumulated over the draws that touched the pixel
	std::vector<uint8_t> depthAmbiguous, stencilAmbiguous;
	bool recordDetail;					///< fill `detail` as draws write (off unless asked for)
	std::vector<PixelDetail> detail;	///< per pixel, when recordDetail; sized by the first draw that records

	void create( int w, int h, bool alpha = true, int stencilBits = 8 );
	void clear( Color c, double z = 1.0, uint32_t s = 0 );
};

// ---- the tolerance policy's parameters (agreed with A3, see compare()) ----------------------------
struct Freedoms
{
	double edgePixels;			///< 1/256: a centre this close to an edge may go either way
	double pointTieTexels;		///< 1/512: point sampling within this of a texel boundary may go either way
	double bilinearTexels;		///< 1/128: bilinear weight precision, as a coordinate shift
	double lodDelta;			///< 0.6: the undocumented LOD, footprint norm and differencing (N15)
	double alphaRef;			///< 1.5/255: alpha within this of ALPHAREF may go either way
	double depthTie;			///< 1e-6: depth within this of the buffer may go either way
	Freedoms();
};

// ---- armed controls: deliberate departures from the documented pipeline ---------------------------
enum Mutation
{
	MUTATE_RASTER_HALF_PIXEL = 1 << 0,		///< sample at (x + .5, y + .5): D3D10's centres
	MUTATE_SCREEN_LINEAR = 1 << 1,			///< no perspective correction
	MUTATE_TEXEL_CORNER = 1 << 2,			///< texel centres at i, not i + .5
	MUTATE_NO_SPECULAR_ADD = 1 << 3,
	MUTATE_FOG_REVERSED = 1 << 4,			///< f and 1 - f swapped
	MUTATE_BLEND_SWAPPED = 1 << 5,			///< source and destination factors swapped
	MUTATE_LERP_SWAPPED = 1 << 6,			///< LERP's ARG1 and ARG2 swapped
	MUTATE_NO_ATTENUATION = 1 << 7,			///< point and spot lights unattenuated
	MUTATE_TOP_LEFT_OFF = 1 << 8			///< centres on any edge are inside
};

struct Report
{
	std::vector<std::string> refusals;	///< non-empty: the draw was refused and drew nothing
	long trianglesIn, trianglesCulled, trianglesClippedAway;
	long pixelsCovered, pixelsAmbiguous, pixelsWritten;
	unsigned zonesSeen;
	double minLod, maxLod;				///< over sampled pixels; minLod > maxLod when none
	long programVerticesReported;		///< vertices a vertex program ran through ffprogram.h's P1 or P2
	Report();
};

/// Draws.  vertices[] holds the vertex buffer; indices (or NULL for a non-indexed draw) hold indices
/// into it, already offset by BaseVertexIndex.  count is the index count (or vertex count).
bool draw( const DrawState &state, int primitiveType, const Vertex *vertices, int vertexCount,
		const uint32_t *indices, int count, Target &target, Report *report = 0,
		unsigned mutations = 0, const Freedoms &freedoms = Freedoms() );

// ---- the pieces, for the unit tests and for reading a disagreement ---------------------------------
struct LitVertex { Color diffuse, specular; };
/// Lighting at one camera-space position and normal ("Mathematics of Lighting" and its subpages)
LitVertex light( const DrawState &state, const double posCamera[3], const double normalCamera[3],
		bool hasNormal, const Color &vertexDiffuse, const Color &vertexSpecular, unsigned mutations = 0 );
/// The fog factor for a distance ("Fog Formulas"); 1 is no fog
double fogFactor( int mode, double d, double start, double end, double density );
/// One colour or alpha operation on its arguments (D3DTEXTUREOP), unsaturated
double textureOp( int op, int component, const Color &arg0, const Color &arg1, const Color &arg2,
		double blendAlpha, unsigned mutations = 0 );
/// D3DCMPFUNC
bool compareFunc( int func, double incoming, double reference );
/// Alpha blending of one pixel (D3DBLEND, D3DBLENDOP), result clamped
Color blend( const DrawState &state, const Color &src, const Color &dst, bool dstHasAlpha, unsigned mutations = 0 );
/// One texture sample at normalised (u, v) with the given LOD, before any freedom shift
Color sample( const Texture &texture, const uint32_t sampler[14], double u, double v, double lambda,
		unsigned mutations = 0 );

// ---- comparison against a GPU read-back -----------------------------------------------------------
struct Comparison
{
	long pixels;
	long exact;			///< within base of the nominal value, every channel
	long inFreedom;		///< not exact, but inside the envelope widened by base: a documented freedom
	long outside;		///< neither: a failure
	long zoneCounts[8];	///< for inFreedom pixels, how many carried each Zone bit (bit order)
	long histogram[256];///< max channel |gpu - nominal| in 1/255 steps, all pixels
	int worstX, worstY;	///< the worst outside pixel, or -1
	double worst;		///< its distance outside the envelope, 0..1
	bool passed() const { return outside == 0; }
};
/// gpu: RGBA8 bytes (R, G, B, A), rowBytes apart.  base: the arithmetic tolerance (2/255 agreed).
Comparison compare( const Target &reference, const uint8_t *gpu, int rowBytes, double base = 2.0 / 255.0 );
void print( const Comparison &c, FILE *out, const char *label );

}  // namespace FFRef

#endif
