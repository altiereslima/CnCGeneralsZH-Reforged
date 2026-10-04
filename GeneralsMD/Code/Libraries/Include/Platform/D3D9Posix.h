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
** The Direct3D 9 the renderer speaks, off Windows (decision 7, phase A1).
**
** WW3D2 and W3DDevice are written in Direct3D 9: 545 of its names appear in the code the POSIX build
** compiles (measured when the route was chosen; PORTING.md, decision 7).  Off Windows they talk to a D3D9-shaped device
** of our own, so this header declares those names - the types, the constants and the interfaces the
** renderer uses - and nothing else.  It is a deliberate exception to the port's engine-own names:
** here the D3D9 names ARE the interface.
**
**   - Written from the values Microsoft publishes for Direct3D 9, not copied from any header.  Every
**     enumerator, flag, error code and structure layout is checked against MinGW-w64's d3d9.h by
**     Tests/d3d9posix_check.cpp, which MinGW compiles; the structure sizes are asserted below too.
**   - No Win32 scalar types: the Win32 types the interface crosses with are Platform/RenderTypes.h's
**     (RenderUInt32 where D3D9 says DWORD, RenderResult for HRESULT, RenderRect, RenderPoint,
**     RenderWindow), and D3D9's UINT, INT, BOOL, WORD and BYTE are unsigned int, int, int,
**     unsigned short and unsigned char.
**   - The interfaces are abstract classes with the methods the renderer calls.  They are not COM:
**     there is no vtable layout to match, since the only implementation is ours (PosixDevice/Render).
**
** POSIX only: on Windows the SDK's own d3d9.h is used, and this header refuses to compile.
*/

#ifndef PLATFORM_D3D9POSIX_H
#define PLATFORM_D3D9POSIX_H

#if defined(_WIN32) && !defined(D3D9POSIX_CHECKER)
#error "Platform/D3D9Posix.h is the POSIX side of Direct3D 9; Windows uses the SDK's d3d9.h"
#endif

#if !defined(D3D9POSIX_CHECKER)
#include <stddef.h>
#include <stdint.h>
#include "Platform/RenderTypes.h"
#endif

//-------------------------------------------------------------------------------------------------
// Scalars
//-------------------------------------------------------------------------------------------------

typedef RenderUInt32 D3DCOLOR;

// A shader's version token: pixel shaders are 0xFFFF, vertex shaders 0xFFFE, in the top half.
#define D3DPS_VERSION(major, minor)	(0xFFFF0000u | ((major) << 8) | (minor))
#define D3DVS_VERSION(major, minor)	(0xFFFE0000u | ((major) << 8) | (minor))

#define D3D_SDK_VERSION			32
#define D3DADAPTER_DEFAULT		0
#define D3DDP_MAXTEXCOORD		8

#define D3DCOLOR_ARGB(a,r,g,b)	((D3DCOLOR)((((a)&0xff)<<24)|(((r)&0xff)<<16)|(((g)&0xff)<<8)|((b)&0xff)))
#define D3DCOLOR_RGBA(r,g,b,a)	D3DCOLOR_ARGB(a,r,g,b)
#define D3DCOLOR_XRGB(r,g,b)	D3DCOLOR_ARGB(0xff,r,g,b)

//-------------------------------------------------------------------------------------------------
// Results.  D3D9's errors are MAKE_HRESULT(1, 0x876, code).
//-------------------------------------------------------------------------------------------------

#define D3D9POSIX_ERROR(code)				((RenderResult)(0x88760000u | (code)))

#define D3D_OK									((RenderResult)0)
#define D3DERR_WRONGTEXTUREFORMAT				D3D9POSIX_ERROR(2072)
#define D3DERR_UNSUPPORTEDCOLOROPERATION		D3D9POSIX_ERROR(2073)
#define D3DERR_UNSUPPORTEDCOLORARG				D3D9POSIX_ERROR(2074)
#define D3DERR_UNSUPPORTEDALPHAOPERATION		D3D9POSIX_ERROR(2075)
#define D3DERR_UNSUPPORTEDALPHAARG				D3D9POSIX_ERROR(2076)
#define D3DERR_TOOMANYOPERATIONS				D3D9POSIX_ERROR(2077)
#define D3DERR_CONFLICTINGTEXTUREFILTER		D3D9POSIX_ERROR(2078)
#define D3DERR_UNSUPPORTEDFACTORVALUE			D3D9POSIX_ERROR(2079)
#define D3DERR_CONFLICTINGRENDERSTATE			D3D9POSIX_ERROR(2081)
#define D3DERR_UNSUPPORTEDTEXTUREFILTER		D3D9POSIX_ERROR(2082)
#define D3DERR_CONFLICTINGTEXTUREPALETTE		D3D9POSIX_ERROR(2086)
#define D3DERR_DRIVERINTERNALERROR				D3D9POSIX_ERROR(2087)
#define D3DERR_NOTFOUND							D3D9POSIX_ERROR(2150)
#define D3DERR_MOREDATA							D3D9POSIX_ERROR(2151)
#define D3DERR_DEVICELOST						D3D9POSIX_ERROR(2152)
#define D3DERR_DEVICENOTRESET					D3D9POSIX_ERROR(2153)
#define D3DERR_NOTAVAILABLE						D3D9POSIX_ERROR(2154)
#define D3DERR_OUTOFVIDEOMEMORY					D3D9POSIX_ERROR(380)
#define D3DERR_INVALIDDEVICE					D3D9POSIX_ERROR(2155)
#define D3DERR_INVALIDCALL						D3D9POSIX_ERROR(2156)
#define D3DERR_DRIVERINVALIDCALL				D3D9POSIX_ERROR(2157)
#define D3DERR_WASSTILLDRAWING					D3D9POSIX_ERROR(540)

//-------------------------------------------------------------------------------------------------
// Enumerations.  Each is four bytes, as D3D9's _FORCE_DWORD member makes them.
//-------------------------------------------------------------------------------------------------

#define D3D9POSIX_FOURCC(a,b,c,d)	((uint32_t)(uint8_t)(a) | ((uint32_t)(uint8_t)(b) << 8) | ((uint32_t)(uint8_t)(c) << 16) | ((uint32_t)(uint8_t)(d) << 24))

enum D3DFORMAT
{
	D3DFMT_UNKNOWN				= 0,
	D3DFMT_R8G8B8				= 20,
	D3DFMT_A8R8G8B8				= 21,
	D3DFMT_X8R8G8B8				= 22,
	D3DFMT_R5G6B5				= 23,
	D3DFMT_X1R5G5B5				= 24,
	D3DFMT_A1R5G5B5				= 25,
	D3DFMT_A4R4G4B4				= 26,
	D3DFMT_R3G3B2				= 27,
	D3DFMT_A8					= 28,
	D3DFMT_A8R3G3B2				= 29,
	D3DFMT_X4R4G4B4				= 30,
	D3DFMT_A2B10G10R10			= 31,
	D3DFMT_A8B8G8R8				= 32,
	D3DFMT_X8B8G8R8				= 33,
	D3DFMT_G16R16				= 34,
	D3DFMT_A2R10G10B10			= 35,
	D3DFMT_A16B16G16R16			= 36,
	D3DFMT_A8P8					= 40,
	D3DFMT_P8					= 41,
	D3DFMT_L8					= 50,
	D3DFMT_A8L8					= 51,
	D3DFMT_A4L4					= 52,
	D3DFMT_V8U8					= 60,
	D3DFMT_L6V5U5				= 61,
	D3DFMT_X8L8V8U8				= 62,
	D3DFMT_Q8W8V8U8				= 63,
	D3DFMT_V16U16				= 64,
	D3DFMT_A2W10V10U10			= 67,
	D3DFMT_UYVY					= D3D9POSIX_FOURCC('U', 'Y', 'V', 'Y'),
	D3DFMT_R8G8_B8G8			= D3D9POSIX_FOURCC('R', 'G', 'B', 'G'),
	D3DFMT_YUY2					= D3D9POSIX_FOURCC('Y', 'U', 'Y', '2'),
	D3DFMT_G8R8_G8B8			= D3D9POSIX_FOURCC('G', 'R', 'G', 'B'),
	D3DFMT_DXT1					= D3D9POSIX_FOURCC('D', 'X', 'T', '1'),
	D3DFMT_DXT2					= D3D9POSIX_FOURCC('D', 'X', 'T', '2'),
	D3DFMT_DXT3					= D3D9POSIX_FOURCC('D', 'X', 'T', '3'),
	D3DFMT_DXT4					= D3D9POSIX_FOURCC('D', 'X', 'T', '4'),
	D3DFMT_DXT5					= D3D9POSIX_FOURCC('D', 'X', 'T', '5'),
	D3DFMT_D16_LOCKABLE			= 70,
	D3DFMT_D32					= 71,
	D3DFMT_D15S1				= 73,
	D3DFMT_D24S8				= 75,
	D3DFMT_D24X8				= 77,
	D3DFMT_D24X4S4				= 79,
	D3DFMT_D16					= 80,
	D3DFMT_L16					= 81,
	D3DFMT_D32F_LOCKABLE		= 82,
	D3DFMT_D24FS8				= 83,
	D3DFMT_VERTEXDATA			= 100,
	D3DFMT_INDEX16				= 101,
	D3DFMT_INDEX32				= 102,
	D3DFMT_Q16W16V16U16			= 110,
	D3DFMT_R16F					= 111,
	D3DFMT_G16R16F				= 112,
	D3DFMT_A16B16G16R16F		= 113,
	D3DFMT_R32F					= 114,
	D3DFMT_G32R32F				= 115,
	D3DFMT_A32B32G32R32F		= 116,
	D3DFMT_CxV8U8				= 117,
	D3DFMT_FORCE_DWORD			= 0x7fffffff
};

enum D3DDEVTYPE			{ D3DDEVTYPE_HAL = 1, D3DDEVTYPE_REF = 2, D3DDEVTYPE_SW = 3, D3DDEVTYPE_NULLREF = 4, D3DDEVTYPE_FORCE_DWORD = 0x7fffffff };
enum D3DRESOURCETYPE	{ D3DRTYPE_SURFACE = 1, D3DRTYPE_VOLUME = 2, D3DRTYPE_TEXTURE = 3, D3DRTYPE_VOLUMETEXTURE = 4, D3DRTYPE_CUBETEXTURE = 5,
						  D3DRTYPE_VERTEXBUFFER = 6, D3DRTYPE_INDEXBUFFER = 7, D3DRTYPE_FORCE_DWORD = 0x7fffffff };
enum D3DPOOL			{ D3DPOOL_DEFAULT = 0, D3DPOOL_MANAGED = 1, D3DPOOL_SYSTEMMEM = 2, D3DPOOL_SCRATCH = 3, D3DPOOL_FORCE_DWORD = 0x7fffffff };
enum D3DSWAPEFFECT		{ D3DSWAPEFFECT_DISCARD = 1, D3DSWAPEFFECT_FLIP = 2, D3DSWAPEFFECT_COPY = 3, D3DSWAPEFFECT_FORCE_DWORD = 0x7fffffff };
enum D3DBACKBUFFER_TYPE	{ D3DBACKBUFFER_TYPE_MONO = 0, D3DBACKBUFFER_TYPE_LEFT = 1, D3DBACKBUFFER_TYPE_RIGHT = 2, D3DBACKBUFFER_TYPE_FORCE_DWORD = 0x7fffffff };
enum D3DCUBEMAP_FACES	{ D3DCUBEMAP_FACE_POSITIVE_X = 0, D3DCUBEMAP_FACE_NEGATIVE_X = 1, D3DCUBEMAP_FACE_POSITIVE_Y = 2, D3DCUBEMAP_FACE_NEGATIVE_Y = 3,
						  D3DCUBEMAP_FACE_POSITIVE_Z = 4, D3DCUBEMAP_FACE_NEGATIVE_Z = 5, D3DCUBEMAP_FACE_FORCE_DWORD = 0x7fffffff };

enum D3DMULTISAMPLE_TYPE
{
	D3DMULTISAMPLE_NONE = 0, D3DMULTISAMPLE_NONMASKABLE = 1, D3DMULTISAMPLE_2_SAMPLES = 2, D3DMULTISAMPLE_3_SAMPLES = 3,
	D3DMULTISAMPLE_4_SAMPLES = 4, D3DMULTISAMPLE_5_SAMPLES = 5, D3DMULTISAMPLE_6_SAMPLES = 6, D3DMULTISAMPLE_7_SAMPLES = 7,
	D3DMULTISAMPLE_8_SAMPLES = 8, D3DMULTISAMPLE_9_SAMPLES = 9, D3DMULTISAMPLE_10_SAMPLES = 10, D3DMULTISAMPLE_11_SAMPLES = 11,
	D3DMULTISAMPLE_12_SAMPLES = 12, D3DMULTISAMPLE_13_SAMPLES = 13, D3DMULTISAMPLE_14_SAMPLES = 14, D3DMULTISAMPLE_15_SAMPLES = 15,
	D3DMULTISAMPLE_16_SAMPLES = 16, D3DMULTISAMPLE_FORCE_DWORD = 0x7fffffff
};

enum D3DPRIMITIVETYPE
{
	D3DPT_POINTLIST = 1, D3DPT_LINELIST = 2, D3DPT_LINESTRIP = 3, D3DPT_TRIANGLELIST = 4, D3DPT_TRIANGLESTRIP = 5,
	D3DPT_TRIANGLEFAN = 6, D3DPT_FORCE_DWORD = 0x7fffffff
};

enum D3DTRANSFORMSTATETYPE
{
	D3DTS_VIEW = 2, D3DTS_PROJECTION = 3,
	D3DTS_TEXTURE0 = 16, D3DTS_TEXTURE1 = 17, D3DTS_TEXTURE2 = 18, D3DTS_TEXTURE3 = 19,
	D3DTS_TEXTURE4 = 20, D3DTS_TEXTURE5 = 21, D3DTS_TEXTURE6 = 22, D3DTS_TEXTURE7 = 23,
	D3DTS_FORCE_DWORD = 0x7fffffff
};
#define D3DTS_WORLDMATRIX(index)	((D3DTRANSFORMSTATETYPE)((index) + 256))
#define D3DTS_WORLD					D3DTS_WORLDMATRIX(0)
#define D3DTS_WORLD1				D3DTS_WORLDMATRIX(1)
#define D3DTS_WORLD2				D3DTS_WORLDMATRIX(2)
#define D3DTS_WORLD3				D3DTS_WORLDMATRIX(3)

enum D3DRENDERSTATETYPE
{
	D3DRS_ZENABLE = 7, D3DRS_FILLMODE = 8, D3DRS_SHADEMODE = 9, D3DRS_ZWRITEENABLE = 14, D3DRS_ALPHATESTENABLE = 15,
	D3DRS_LASTPIXEL = 16, D3DRS_SRCBLEND = 19, D3DRS_DESTBLEND = 20, D3DRS_CULLMODE = 22, D3DRS_ZFUNC = 23,
	D3DRS_ALPHAREF = 24, D3DRS_ALPHAFUNC = 25, D3DRS_DITHERENABLE = 26, D3DRS_ALPHABLENDENABLE = 27, D3DRS_FOGENABLE = 28,
	D3DRS_SPECULARENABLE = 29, D3DRS_FOGCOLOR = 34, D3DRS_FOGTABLEMODE = 35, D3DRS_FOGSTART = 36, D3DRS_FOGEND = 37,
	D3DRS_FOGDENSITY = 38, D3DRS_RANGEFOGENABLE = 48, D3DRS_STENCILENABLE = 52, D3DRS_STENCILFAIL = 53,
	D3DRS_STENCILZFAIL = 54, D3DRS_STENCILPASS = 55, D3DRS_STENCILFUNC = 56, D3DRS_STENCILREF = 57, D3DRS_STENCILMASK = 58,
	D3DRS_STENCILWRITEMASK = 59, D3DRS_TEXTUREFACTOR = 60,
	D3DRS_WRAP0 = 128, D3DRS_WRAP1 = 129, D3DRS_WRAP2 = 130, D3DRS_WRAP3 = 131, D3DRS_WRAP4 = 132, D3DRS_WRAP5 = 133,
	D3DRS_WRAP6 = 134, D3DRS_WRAP7 = 135, D3DRS_CLIPPING = 136, D3DRS_LIGHTING = 137, D3DRS_AMBIENT = 139,
	D3DRS_FOGVERTEXMODE = 140, D3DRS_COLORVERTEX = 141, D3DRS_LOCALVIEWER = 142, D3DRS_NORMALIZENORMALS = 143,
	D3DRS_DIFFUSEMATERIALSOURCE = 145, D3DRS_SPECULARMATERIALSOURCE = 146, D3DRS_AMBIENTMATERIALSOURCE = 147,
	D3DRS_EMISSIVEMATERIALSOURCE = 148, D3DRS_VERTEXBLEND = 151, D3DRS_CLIPPLANEENABLE = 152, D3DRS_POINTSIZE = 154,
	D3DRS_POINTSIZE_MIN = 155, D3DRS_POINTSPRITEENABLE = 156, D3DRS_POINTSCALEENABLE = 157, D3DRS_POINTSCALE_A = 158,
	D3DRS_POINTSCALE_B = 159, D3DRS_POINTSCALE_C = 160, D3DRS_MULTISAMPLEANTIALIAS = 161, D3DRS_MULTISAMPLEMASK = 162,
	D3DRS_PATCHEDGESTYLE = 163, D3DRS_DEBUGMONITORTOKEN = 165, D3DRS_POINTSIZE_MAX = 166,
	D3DRS_INDEXEDVERTEXBLENDENABLE = 167, D3DRS_COLORWRITEENABLE = 168, D3DRS_TWEENFACTOR = 170, D3DRS_BLENDOP = 171,
	D3DRS_POSITIONDEGREE = 172, D3DRS_NORMALDEGREE = 173, D3DRS_SCISSORTESTENABLE = 174, D3DRS_SLOPESCALEDEPTHBIAS = 175,
	D3DRS_ANTIALIASEDLINEENABLE = 176, D3DRS_MINTESSELLATIONLEVEL = 178, D3DRS_MAXTESSELLATIONLEVEL = 179,
	D3DRS_ADAPTIVETESS_X = 180, D3DRS_ADAPTIVETESS_Y = 181, D3DRS_ADAPTIVETESS_Z = 182, D3DRS_ADAPTIVETESS_W = 183,
	D3DRS_ENABLEADAPTIVETESSELLATION = 184, D3DRS_TWOSIDEDSTENCILMODE = 185, D3DRS_CCW_STENCILFAIL = 186,
	D3DRS_CCW_STENCILZFAIL = 187, D3DRS_CCW_STENCILPASS = 188, D3DRS_CCW_STENCILFUNC = 189,
	D3DRS_COLORWRITEENABLE1 = 190, D3DRS_COLORWRITEENABLE2 = 191, D3DRS_COLORWRITEENABLE3 = 192, D3DRS_BLENDFACTOR = 193,
	D3DRS_SRGBWRITEENABLE = 194, D3DRS_DEPTHBIAS = 195,
	D3DRS_WRAP8 = 198, D3DRS_WRAP9 = 199, D3DRS_WRAP10 = 200, D3DRS_WRAP11 = 201, D3DRS_WRAP12 = 202, D3DRS_WRAP13 = 203,
	D3DRS_WRAP14 = 204, D3DRS_WRAP15 = 205, D3DRS_SEPARATEALPHABLENDENABLE = 206, D3DRS_SRCBLENDALPHA = 207,
	D3DRS_DESTBLENDALPHA = 208, D3DRS_BLENDOPALPHA = 209,
	D3DRS_FORCE_DWORD = 0x7fffffff
};

enum D3DTEXTURESTAGESTATETYPE
{
	D3DTSS_COLOROP = 1, D3DTSS_COLORARG1 = 2, D3DTSS_COLORARG2 = 3, D3DTSS_ALPHAOP = 4, D3DTSS_ALPHAARG1 = 5,
	D3DTSS_ALPHAARG2 = 6, D3DTSS_BUMPENVMAT00 = 7, D3DTSS_BUMPENVMAT01 = 8, D3DTSS_BUMPENVMAT10 = 9, D3DTSS_BUMPENVMAT11 = 10,
	D3DTSS_TEXCOORDINDEX = 11, D3DTSS_BUMPENVLSCALE = 22, D3DTSS_BUMPENVLOFFSET = 23, D3DTSS_TEXTURETRANSFORMFLAGS = 24,
	D3DTSS_COLORARG0 = 26, D3DTSS_ALPHAARG0 = 27, D3DTSS_RESULTARG = 28, D3DTSS_CONSTANT = 32,
	D3DTSS_FORCE_DWORD = 0x7fffffff
};
#define D3DTSS_TCI_PASSTHRU							0x00000000
#define D3DTSS_TCI_CAMERASPACENORMAL				0x00010000
#define D3DTSS_TCI_CAMERASPACEPOSITION				0x00020000
#define D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR		0x00030000
#define D3DTSS_TCI_SPHEREMAP						0x00040000

enum D3DSAMPLERSTATETYPE
{
	D3DSAMP_ADDRESSU = 1, D3DSAMP_ADDRESSV = 2, D3DSAMP_ADDRESSW = 3, D3DSAMP_BORDERCOLOR = 4, D3DSAMP_MAGFILTER = 5,
	D3DSAMP_MINFILTER = 6, D3DSAMP_MIPFILTER = 7, D3DSAMP_MIPMAPLODBIAS = 8, D3DSAMP_MAXMIPLEVEL = 9, D3DSAMP_MAXANISOTROPY = 10,
	D3DSAMP_SRGBTEXTURE = 11, D3DSAMP_ELEMENTINDEX = 12, D3DSAMP_DMAPOFFSET = 13, D3DSAMP_FORCE_DWORD = 0x7fffffff
};

enum D3DTEXTUREOP
{
	D3DTOP_DISABLE = 1, D3DTOP_SELECTARG1 = 2, D3DTOP_SELECTARG2 = 3, D3DTOP_MODULATE = 4, D3DTOP_MODULATE2X = 5,
	D3DTOP_MODULATE4X = 6, D3DTOP_ADD = 7, D3DTOP_ADDSIGNED = 8, D3DTOP_ADDSIGNED2X = 9, D3DTOP_SUBTRACT = 10,
	D3DTOP_ADDSMOOTH = 11, D3DTOP_BLENDDIFFUSEALPHA = 12, D3DTOP_BLENDTEXTUREALPHA = 13, D3DTOP_BLENDFACTORALPHA = 14,
	D3DTOP_BLENDTEXTUREALPHAPM = 15, D3DTOP_BLENDCURRENTALPHA = 16, D3DTOP_PREMODULATE = 17,
	D3DTOP_MODULATEALPHA_ADDCOLOR = 18, D3DTOP_MODULATECOLOR_ADDALPHA = 19, D3DTOP_MODULATEINVALPHA_ADDCOLOR = 20,
	D3DTOP_MODULATEINVCOLOR_ADDALPHA = 21, D3DTOP_BUMPENVMAP = 22, D3DTOP_BUMPENVMAPLUMINANCE = 23, D3DTOP_DOTPRODUCT3 = 24,
	D3DTOP_MULTIPLYADD = 25, D3DTOP_LERP = 26, D3DTOP_FORCE_DWORD = 0x7fffffff
};

#define D3DTA_SELECTMASK		0x0000000f
#define D3DTA_DIFFUSE			0x00000000
#define D3DTA_CURRENT			0x00000001
#define D3DTA_TEXTURE			0x00000002
#define D3DTA_TFACTOR			0x00000003
#define D3DTA_SPECULAR			0x00000004
#define D3DTA_TEMP				0x00000005
#define D3DTA_CONSTANT			0x00000006
#define D3DTA_COMPLEMENT		0x00000010
#define D3DTA_ALPHAREPLICATE	0x00000020

enum D3DTEXTUREFILTERTYPE
{
	D3DTEXF_NONE = 0, D3DTEXF_POINT = 1, D3DTEXF_LINEAR = 2, D3DTEXF_ANISOTROPIC = 3, D3DTEXF_PYRAMIDALQUAD = 6,
	D3DTEXF_GAUSSIANQUAD = 7, D3DTEXF_CONVOLUTIONMONO = 8, D3DTEXF_FORCE_DWORD = 0x7fffffff
};

enum D3DTEXTUREADDRESS
{
	D3DTADDRESS_WRAP = 1, D3DTADDRESS_MIRROR = 2, D3DTADDRESS_CLAMP = 3, D3DTADDRESS_BORDER = 4, D3DTADDRESS_MIRRORONCE = 5,
	D3DTADDRESS_FORCE_DWORD = 0x7fffffff
};

enum D3DBLEND
{
	D3DBLEND_ZERO = 1, D3DBLEND_ONE = 2, D3DBLEND_SRCCOLOR = 3, D3DBLEND_INVSRCCOLOR = 4, D3DBLEND_SRCALPHA = 5,
	D3DBLEND_INVSRCALPHA = 6, D3DBLEND_DESTALPHA = 7, D3DBLEND_INVDESTALPHA = 8, D3DBLEND_DESTCOLOR = 9,
	D3DBLEND_INVDESTCOLOR = 10, D3DBLEND_SRCALPHASAT = 11, D3DBLEND_BOTHSRCALPHA = 12, D3DBLEND_BOTHINVSRCALPHA = 13,
	D3DBLEND_BLENDFACTOR = 14, D3DBLEND_INVBLENDFACTOR = 15, D3DBLEND_FORCE_DWORD = 0x7fffffff
};

enum D3DBLENDOP			{ D3DBLENDOP_ADD = 1, D3DBLENDOP_SUBTRACT = 2, D3DBLENDOP_REVSUBTRACT = 3, D3DBLENDOP_MIN = 4, D3DBLENDOP_MAX = 5,
						  D3DBLENDOP_FORCE_DWORD = 0x7fffffff };
enum D3DCMPFUNC			{ D3DCMP_NEVER = 1, D3DCMP_LESS = 2, D3DCMP_EQUAL = 3, D3DCMP_LESSEQUAL = 4, D3DCMP_GREATER = 5,
						  D3DCMP_NOTEQUAL = 6, D3DCMP_GREATEREQUAL = 7, D3DCMP_ALWAYS = 8, D3DCMP_FORCE_DWORD = 0x7fffffff };
enum D3DCULL			{ D3DCULL_NONE = 1, D3DCULL_CW = 2, D3DCULL_CCW = 3, D3DCULL_FORCE_DWORD = 0x7fffffff };
enum D3DFILLMODE		{ D3DFILL_POINT = 1, D3DFILL_WIREFRAME = 2, D3DFILL_SOLID = 3, D3DFILL_FORCE_DWORD = 0x7fffffff };
enum D3DSHADEMODE		{ D3DSHADE_FLAT = 1, D3DSHADE_GOURAUD = 2, D3DSHADE_PHONG = 3, D3DSHADE_FORCE_DWORD = 0x7fffffff };
enum D3DFOGMODE			{ D3DFOG_NONE = 0, D3DFOG_EXP = 1, D3DFOG_EXP2 = 2, D3DFOG_LINEAR = 3, D3DFOG_FORCE_DWORD = 0x7fffffff };
enum D3DSTENCILOP		{ D3DSTENCILOP_KEEP = 1, D3DSTENCILOP_ZERO = 2, D3DSTENCILOP_REPLACE = 3, D3DSTENCILOP_INCRSAT = 4,
						  D3DSTENCILOP_DECRSAT = 5, D3DSTENCILOP_INVERT = 6, D3DSTENCILOP_INCR = 7, D3DSTENCILOP_DECR = 8,
						  D3DSTENCILOP_FORCE_DWORD = 0x7fffffff };
enum D3DZBUFFERTYPE		{ D3DZB_FALSE = 0, D3DZB_TRUE = 1, D3DZB_USEW = 2, D3DZB_FORCE_DWORD = 0x7fffffff };
enum D3DMATERIALCOLORSOURCE { D3DMCS_MATERIAL = 0, D3DMCS_COLOR1 = 1, D3DMCS_COLOR2 = 2, D3DMCS_FORCE_DWORD = 0x7fffffff };
enum D3DVERTEXBLENDFLAGS { D3DVBF_DISABLE = 0, D3DVBF_1WEIGHTS = 1, D3DVBF_2WEIGHTS = 2, D3DVBF_3WEIGHTS = 3, D3DVBF_TWEENING = 255,
						  D3DVBF_0WEIGHTS = 256, D3DVBF_FORCE_DWORD = 0x7fffffff };
enum D3DTEXTURETRANSFORMFLAGS { D3DTTFF_DISABLE = 0, D3DTTFF_COUNT1 = 1, D3DTTFF_COUNT2 = 2, D3DTTFF_COUNT3 = 3, D3DTTFF_COUNT4 = 4,
						  D3DTTFF_PROJECTED = 256, D3DTTFF_FORCE_DWORD = 0x7fffffff };
enum D3DPATCHEDGESTYLE	{ D3DPATCHEDGE_DISCRETE = 0, D3DPATCHEDGE_CONTINUOUS = 1, D3DPATCHEDGE_FORCE_DWORD = 0x7fffffff };
enum D3DDEBUGMONITORTOKENS { D3DDMT_ENABLE = 0, D3DDMT_DISABLE = 1, D3DDMT_FORCE_DWORD = 0x7fffffff };
enum D3DLIGHTTYPE		{ D3DLIGHT_POINT = 1, D3DLIGHT_SPOT = 2, D3DLIGHT_DIRECTIONAL = 3, D3DLIGHT_FORCE_DWORD = 0x7fffffff };

//-------------------------------------------------------------------------------------------------
// Flags
//-------------------------------------------------------------------------------------------------

#define D3DWRAP_U						0x00000001
#define D3DWRAP_V						0x00000002
#define D3DWRAP_W						0x00000004

#define D3DCOLORWRITEENABLE_RED			(1L << 0)
#define D3DCOLORWRITEENABLE_GREEN		(1L << 1)
#define D3DCOLORWRITEENABLE_BLUE		(1L << 2)
#define D3DCOLORWRITEENABLE_ALPHA		(1L << 3)

#define D3DCLEAR_TARGET					0x00000001
#define D3DCLEAR_ZBUFFER				0x00000002
#define D3DCLEAR_STENCIL				0x00000004

#define D3DUSAGE_RENDERTARGET			0x00000001
#define D3DUSAGE_DEPTHSTENCIL			0x00000002
#define D3DUSAGE_WRITEONLY				0x00000008
#define D3DUSAGE_SOFTWAREPROCESSING		0x00000010
#define D3DUSAGE_DONOTCLIP				0x00000020
#define D3DUSAGE_POINTS					0x00000040
#define D3DUSAGE_RTPATCHES				0x00000080
#define D3DUSAGE_NPATCHES				0x00000100
#define D3DUSAGE_DYNAMIC				0x00000200
#define D3DUSAGE_AUTOGENMIPMAP			0x00000400

#define D3DLOCK_READONLY				0x00000010
#define D3DLOCK_NOSYSLOCK				0x00000800
#define D3DLOCK_NOOVERWRITE				0x00001000
#define D3DLOCK_DISCARD					0x00002000
#define D3DLOCK_DONOTWAIT				0x00004000
#define D3DLOCK_NO_DIRTY_UPDATE			0x00008000

#define D3DCURSOR_IMMEDIATE_UPDATE		0x00000001	// SetCursorPosition's flag (W3DMouse's RM_DX8, C3b)

#define D3DCREATE_FPU_PRESERVE			0x00000002
#define D3DCREATE_MULTITHREADED			0x00000004
#define D3DCREATE_PUREDEVICE			0x00000010
#define D3DCREATE_SOFTWARE_VERTEXPROCESSING	0x00000020
#define D3DCREATE_HARDWARE_VERTEXPROCESSING	0x00000040
#define D3DCREATE_MIXED_VERTEXPROCESSING	0x00000080

#define D3DPRESENT_INTERVAL_DEFAULT		0x00000000
#define D3DPRESENT_INTERVAL_ONE			0x00000001
#define D3DPRESENT_INTERVAL_TWO			0x00000002
#define D3DPRESENT_INTERVAL_THREE		0x00000004
#define D3DPRESENT_INTERVAL_FOUR		0x00000008
#define D3DPRESENT_INTERVAL_IMMEDIATE	0x80000000
#define D3DPRESENT_RATE_DEFAULT			0x00000000
#define D3DPRESENTFLAG_LOCKABLE_BACKBUFFER	0x00000001

#define D3DSGR_NO_CALIBRATION			0x00000000
#define D3DSGR_CALIBRATE				0x00000001

#define D3DFVF_RESERVED0				0x001
#define D3DFVF_POSITION_MASK			0x400E
#define D3DFVF_XYZ						0x002
#define D3DFVF_XYZRHW					0x004
#define D3DFVF_XYZB1					0x006
#define D3DFVF_XYZB2					0x008
#define D3DFVF_XYZB3					0x00a
#define D3DFVF_XYZB4					0x00c
#define D3DFVF_XYZB5					0x00e
#define D3DFVF_XYZW						0x4002
#define D3DFVF_NORMAL					0x010
#define D3DFVF_PSIZE					0x020
#define D3DFVF_DIFFUSE					0x040
#define D3DFVF_SPECULAR					0x080
#define D3DFVF_TEXCOUNT_MASK			0xf00
#define D3DFVF_TEXCOUNT_SHIFT			8
#define D3DFVF_TEX0						0x000
#define D3DFVF_TEX1						0x100
#define D3DFVF_TEX2						0x200
#define D3DFVF_TEX3						0x300
#define D3DFVF_TEX4						0x400
#define D3DFVF_TEX5						0x500
#define D3DFVF_TEX6						0x600
#define D3DFVF_TEX7						0x700
#define D3DFVF_TEX8						0x800
#define D3DFVF_LASTBETA_UBYTE4			0x1000
#define D3DFVF_LASTBETA_D3DCOLOR		0x8000
// Unsigned: D3DFVF_TEXCOORDSIZE1(7) and SIZE4(7) shift into bit 31, which a signed int cannot hold.
#define D3DFVF_TEXTUREFORMAT2			0u
#define D3DFVF_TEXTUREFORMAT1			3u
#define D3DFVF_TEXTUREFORMAT3			1u
#define D3DFVF_TEXTUREFORMAT4			2u
#define D3DFVF_TEXCOORDSIZE3(index)		(D3DFVF_TEXTUREFORMAT3 << ((index) * 2 + 16))
#define D3DFVF_TEXCOORDSIZE2(index)		(D3DFVF_TEXTUREFORMAT2)
#define D3DFVF_TEXCOORDSIZE4(index)		(D3DFVF_TEXTUREFORMAT4 << ((index) * 2 + 16))
#define D3DFVF_TEXCOORDSIZE1(index)		(D3DFVF_TEXTUREFORMAT1 << ((index) * 2 + 16))

// Capability bits the renderer tests.
#define D3DCAPS2_FULLSCREENGAMMA		0x00020000
#define D3DDEVCAPS_HWTRANSFORMANDLIGHT	0x00010000
#define D3DDEVCAPS_NPATCHES				0x01000000
#define D3DPMISCCAPS_COLORWRITEENABLE	0x00000080
#define D3DPRASTERCAPS_FOGRANGE			0x00010000
#define D3DPRASTERCAPS_DEPTHBIAS		0x04000000
#define D3DPTEXTURECAPS_CUBEMAP			0x00000800
#define D3DPTFILTERCAPS_MINFLINEAR		0x00000200
#define D3DPTFILTERCAPS_MINFANISOTROPIC	0x00000400
#define D3DPTFILTERCAPS_MIPFLINEAR		0x00020000
#define D3DPTFILTERCAPS_MAGFLINEAR		0x02000000
#define D3DPTFILTERCAPS_MAGFANISOTROPIC	0x04000000
#define D3DTEXOPCAPS_DISABLE					0x00000001
#define D3DTEXOPCAPS_SELECTARG1					0x00000002
#define D3DTEXOPCAPS_SELECTARG2					0x00000004
#define D3DTEXOPCAPS_MODULATE					0x00000008
#define D3DTEXOPCAPS_MODULATE2X					0x00000010
#define D3DTEXOPCAPS_MODULATE4X					0x00000020
#define D3DTEXOPCAPS_ADD						0x00000040
#define D3DTEXOPCAPS_ADDSIGNED					0x00000080
#define D3DTEXOPCAPS_ADDSIGNED2X				0x00000100
#define D3DTEXOPCAPS_SUBTRACT					0x00000200
#define D3DTEXOPCAPS_ADDSMOOTH					0x00000400
#define D3DTEXOPCAPS_BLENDDIFFUSEALPHA			0x00000800
#define D3DTEXOPCAPS_BLENDTEXTUREALPHA			0x00001000
#define D3DTEXOPCAPS_BLENDFACTORALPHA			0x00002000
#define D3DTEXOPCAPS_BLENDTEXTUREALPHAPM		0x00004000
#define D3DTEXOPCAPS_BLENDCURRENTALPHA			0x00008000
#define D3DTEXOPCAPS_PREMODULATE				0x00010000
#define D3DTEXOPCAPS_MODULATEALPHA_ADDCOLOR		0x00020000
#define D3DTEXOPCAPS_MODULATECOLOR_ADDALPHA		0x00040000
#define D3DTEXOPCAPS_MODULATEINVALPHA_ADDCOLOR	0x00080000
#define D3DTEXOPCAPS_MODULATEINVCOLOR_ADDALPHA	0x00100000
#define D3DTEXOPCAPS_BUMPENVMAP					0x00200000
#define D3DTEXOPCAPS_BUMPENVMAPLUMINANCE		0x00400000
#define D3DTEXOPCAPS_DOTPRODUCT3				0x00800000
#define D3DTEXOPCAPS_MULTIPLYADD				0x01000000
#define D3DTEXOPCAPS_LERP						0x02000000

//-------------------------------------------------------------------------------------------------
// Structures, laid out as D3D9's: DWORD and UINT are four bytes, LONG is four, and pointers are the
// platform's.  Tests/d3d9posix_check.cpp compares every offset with MinGW-w64's; the sizes are
// asserted at the end of this header.
//-------------------------------------------------------------------------------------------------

/// A GUID: D3D9's interface identifiers, and the adapter's device identifier.
struct D3D9PosixGuid
{
	uint32_t Data1;
	uint16_t Data2;
	uint16_t Data3;
	uint8_t Data4[8];
};

#include "Platform/D3D9PosixMath.h"		// D3DVECTOR and D3DMATRIX, which D3DX (and so GameEngine) reaches
struct D3DCOLORVALUE	{ float r, g, b, a; };
struct D3DRECT			{ int32_t x1, y1, x2, y2; };

struct D3DVIEWPORT9
{
	RenderUInt32 X;
	RenderUInt32 Y;
	RenderUInt32 Width;
	RenderUInt32 Height;
	float MinZ;
	float MaxZ;
};

struct D3DMATERIAL9
{
	D3DCOLORVALUE Diffuse;
	D3DCOLORVALUE Ambient;
	D3DCOLORVALUE Specular;
	D3DCOLORVALUE Emissive;
	float Power;
};

struct D3DLIGHT9
{
	D3DLIGHTTYPE Type;
	D3DCOLORVALUE Diffuse;
	D3DCOLORVALUE Specular;
	D3DCOLORVALUE Ambient;
	D3DVECTOR Position;
	D3DVECTOR Direction;
	float Range;
	float Falloff;
	float Attenuation0;
	float Attenuation1;
	float Attenuation2;
	float Theta;
	float Phi;
};

struct D3DLOCKED_RECT
{
	int Pitch;
	void *pBits;
};

struct D3DBOX
{
	unsigned int Left, Top, Right, Bottom, Front, Back;
};

struct D3DLOCKED_BOX
{
	int RowPitch;
	int SlicePitch;
	void *pBits;
};

struct D3DSURFACE_DESC
{
	D3DFORMAT Format;
	D3DRESOURCETYPE Type;
	RenderUInt32 Usage;
	D3DPOOL Pool;
	D3DMULTISAMPLE_TYPE MultiSampleType;
	RenderUInt32 MultiSampleQuality;
	unsigned int Width;
	unsigned int Height;
};

struct D3DVOLUME_DESC
{
	D3DFORMAT Format;
	D3DRESOURCETYPE Type;
	RenderUInt32 Usage;
	D3DPOOL Pool;
	unsigned int Width;
	unsigned int Height;
	unsigned int Depth;
};

struct D3DVERTEXBUFFER_DESC
{
	D3DFORMAT Format;
	D3DRESOURCETYPE Type;
	RenderUInt32 Usage;
	D3DPOOL Pool;
	unsigned int Size;
	RenderUInt32 FVF;
};

struct D3DINDEXBUFFER_DESC
{
	D3DFORMAT Format;
	D3DRESOURCETYPE Type;
	RenderUInt32 Usage;
	D3DPOOL Pool;
	unsigned int Size;
};

struct D3DDISPLAYMODE
{
	unsigned int Width;
	unsigned int Height;
	unsigned int RefreshRate;
	D3DFORMAT Format;
};

struct D3DGAMMARAMP
{
	unsigned short red[256];
	unsigned short green[256];
	unsigned short blue[256];
};

struct D3DPRESENT_PARAMETERS
{
	unsigned int BackBufferWidth;
	unsigned int BackBufferHeight;
	D3DFORMAT BackBufferFormat;
	unsigned int BackBufferCount;
	D3DMULTISAMPLE_TYPE MultiSampleType;
	RenderUInt32 MultiSampleQuality;
	D3DSWAPEFFECT SwapEffect;
	RenderWindow hDeviceWindow;
	int Windowed;
	int EnableAutoDepthStencil;
	D3DFORMAT AutoDepthStencilFormat;
	RenderUInt32 Flags;
	unsigned int FullScreen_RefreshRateInHz;
	unsigned int PresentationInterval;
};

/// D3D9's DriverVersion is a LARGE_INTEGER, eight bytes and eight-aligned; here it is the integer.
struct D3DADAPTER_IDENTIFIER9
{
	char Driver[512];
	char Description[512];
	char DeviceName[32];
	int64_t DriverVersion;
	RenderUInt32 VendorId;
	RenderUInt32 DeviceId;
	RenderUInt32 SubSysId;
	RenderUInt32 Revision;
	D3D9PosixGuid DeviceIdentifier;
	RenderUInt32 WHQLLevel;
};

struct D3DVSHADERCAPS2_0
{
	RenderUInt32 Caps;
	int DynamicFlowControlDepth;
	int NumTemps;
	int StaticFlowControlDepth;
};

struct D3DPSHADERCAPS2_0
{
	RenderUInt32 Caps;
	int DynamicFlowControlDepth;
	int NumTemps;
	int StaticFlowControlDepth;
	int NumInstructionSlots;
};

struct D3DCAPS9
{
	D3DDEVTYPE DeviceType;
	unsigned int AdapterOrdinal;
	RenderUInt32 Caps;
	RenderUInt32 Caps2;
	RenderUInt32 Caps3;
	RenderUInt32 PresentationIntervals;
	RenderUInt32 CursorCaps;
	RenderUInt32 DevCaps;
	RenderUInt32 PrimitiveMiscCaps;
	RenderUInt32 RasterCaps;
	RenderUInt32 ZCmpCaps;
	RenderUInt32 SrcBlendCaps;
	RenderUInt32 DestBlendCaps;
	RenderUInt32 AlphaCmpCaps;
	RenderUInt32 ShadeCaps;
	RenderUInt32 TextureCaps;
	RenderUInt32 TextureFilterCaps;
	RenderUInt32 CubeTextureFilterCaps;
	RenderUInt32 VolumeTextureFilterCaps;
	RenderUInt32 TextureAddressCaps;
	RenderUInt32 VolumeTextureAddressCaps;
	RenderUInt32 LineCaps;
	RenderUInt32 MaxTextureWidth;
	RenderUInt32 MaxTextureHeight;
	RenderUInt32 MaxVolumeExtent;
	RenderUInt32 MaxTextureRepeat;
	RenderUInt32 MaxTextureAspectRatio;
	RenderUInt32 MaxAnisotropy;
	float MaxVertexW;
	float GuardBandLeft;
	float GuardBandTop;
	float GuardBandRight;
	float GuardBandBottom;
	float ExtentsAdjust;
	RenderUInt32 StencilCaps;
	RenderUInt32 FVFCaps;
	RenderUInt32 TextureOpCaps;
	RenderUInt32 MaxTextureBlendStages;
	RenderUInt32 MaxSimultaneousTextures;
	RenderUInt32 VertexProcessingCaps;
	RenderUInt32 MaxActiveLights;
	RenderUInt32 MaxUserClipPlanes;
	RenderUInt32 MaxVertexBlendMatrices;
	RenderUInt32 MaxVertexBlendMatrixIndex;
	float MaxPointSize;
	RenderUInt32 MaxPrimitiveCount;
	RenderUInt32 MaxVertexIndex;
	RenderUInt32 MaxStreams;
	RenderUInt32 MaxStreamStride;
	RenderUInt32 VertexShaderVersion;
	RenderUInt32 MaxVertexShaderConst;
	RenderUInt32 PixelShaderVersion;
	float PixelShader1xMaxValue;
	RenderUInt32 DevCaps2;
	float MaxNpatchTessellationLevel;
	RenderUInt32 Reserved5;
	unsigned int MasterAdapterOrdinal;
	unsigned int AdapterOrdinalInGroup;
	unsigned int NumberOfAdaptersInGroup;
	RenderUInt32 DeclTypes;
	RenderUInt32 NumSimultaneousRTs;
	RenderUInt32 StretchRectFilterCaps;
	D3DVSHADERCAPS2_0 VS20Caps;
	D3DPSHADERCAPS2_0 PS20Caps;
	RenderUInt32 VertexTextureFilterCaps;
	RenderUInt32 MaxVShaderInstructionsExecuted;
	RenderUInt32 MaxPShaderInstructionsExecuted;
	RenderUInt32 MaxVertexShader30InstructionSlots;
	RenderUInt32 MaxPixelShader30InstructionSlots;
};

// A vertex declaration's element types, methods and usages.  Unlike the other enumerations these have
// no _FORCE_DWORD: D3DVERTEXELEMENT9 stores each in a byte.
enum D3DDECLTYPE
{
	D3DDECLTYPE_FLOAT1		= 0,
	D3DDECLTYPE_FLOAT2		= 1,
	D3DDECLTYPE_FLOAT3		= 2,
	D3DDECLTYPE_FLOAT4		= 3,
	D3DDECLTYPE_D3DCOLOR	= 4,
	D3DDECLTYPE_UBYTE4		= 5,
	D3DDECLTYPE_SHORT2		= 6,
	D3DDECLTYPE_SHORT4		= 7,
	D3DDECLTYPE_UBYTE4N		= 8,
	D3DDECLTYPE_SHORT2N		= 9,
	D3DDECLTYPE_SHORT4N		= 10,
	D3DDECLTYPE_USHORT2N	= 11,
	D3DDECLTYPE_USHORT4N	= 12,
	D3DDECLTYPE_UDEC3		= 13,
	D3DDECLTYPE_DEC3N		= 14,
	D3DDECLTYPE_FLOAT16_2	= 15,
	D3DDECLTYPE_FLOAT16_4	= 16,
	D3DDECLTYPE_UNUSED		= 17
};

enum D3DDECLMETHOD
{
	D3DDECLMETHOD_DEFAULT			= 0,
	D3DDECLMETHOD_PARTIALU			= 1,
	D3DDECLMETHOD_PARTIALV			= 2,
	D3DDECLMETHOD_CROSSUV			= 3,
	D3DDECLMETHOD_UV				= 4,
	D3DDECLMETHOD_LOOKUP			= 5,
	D3DDECLMETHOD_LOOKUPPRESAMPLED	= 6
};

enum D3DDECLUSAGE
{
	D3DDECLUSAGE_POSITION		= 0,
	D3DDECLUSAGE_BLENDWEIGHT	= 1,
	D3DDECLUSAGE_BLENDINDICES	= 2,
	D3DDECLUSAGE_NORMAL			= 3,
	D3DDECLUSAGE_PSIZE			= 4,
	D3DDECLUSAGE_TEXCOORD		= 5,
	D3DDECLUSAGE_TANGENT		= 6,
	D3DDECLUSAGE_BINORMAL		= 7,
	D3DDECLUSAGE_TESSFACTOR		= 8,
	D3DDECLUSAGE_POSITIONT		= 9,
	D3DDECLUSAGE_COLOR			= 10,
	D3DDECLUSAGE_FOG			= 11,
	D3DDECLUSAGE_DEPTH			= 12,
	D3DDECLUSAGE_SAMPLE			= 13
};

struct D3DVERTEXELEMENT9
{
	unsigned short Stream;
	unsigned short Offset;
	unsigned char Type;
	unsigned char Method;
	unsigned char Usage;
	unsigned char UsageIndex;
};

/// The element that ends a declaration: stream 0xFF.
#define D3DDECL_END()	{ 0xFF, 0, D3DDECLTYPE_UNUSED, 0, 0, 0 }

//-------------------------------------------------------------------------------------------------
// Interfaces: the methods the renderer calls, with D3D9's parameters in D3D9's order.  BOOL is int,
// UINT unsigned int, HANDLE a void *; a shared handle is never asked for, so its pointer is always
// NULL.  Release returns the count left, as COM's does.
//-------------------------------------------------------------------------------------------------

// D3D9's interface identifiers, as GetContainer takes them, with D3D9's published values.  COM spells
// their type IID, which is guiddef.h's name and not D3D9's, so it is not defined here.  Defined in
// the header, where the SDK only declares them, so that the checker can read the values.
inline constexpr D3D9PosixGuid IID_IDirect3DTexture9 =
	{ 0x85c31227, 0x3de5, 0x4f00, { 0x9b, 0x3a, 0xf1, 0x1a, 0xc3, 0x8c, 0x18, 0xb5 } };
inline constexpr D3D9PosixGuid IID_IDirect3DCubeTexture9 =
	{ 0xfff32f81, 0xd953, 0x473a, { 0x92, 0x23, 0x93, 0xd6, 0x52, 0xab, 0xa9, 0x3f } };

class IDirect3DDevice9;
class IDirect3DSurface9;
class IDirect3DVolume9;

class D3D9PosixUnknown
{
public:
	virtual uint32_t AddRef() = 0;
	virtual uint32_t Release() = 0;
protected:
	virtual ~D3D9PosixUnknown() {}
};

class IDirect3DResource9 : public D3D9PosixUnknown
{
public:
	virtual RenderUInt32 SetPriority(RenderUInt32 priority) = 0;
	virtual RenderUInt32 GetPriority() = 0;
	virtual D3DRESOURCETYPE GetType() = 0;
};

class IDirect3DBaseTexture9 : public IDirect3DResource9
{
public:
	virtual RenderUInt32 SetLOD(RenderUInt32 lod) = 0;
	virtual RenderUInt32 GetLOD() = 0;
	virtual RenderUInt32 GetLevelCount() = 0;
};

class IDirect3DTexture9 : public IDirect3DBaseTexture9
{
public:
	virtual RenderResult GetLevelDesc(unsigned int level, D3DSURFACE_DESC *desc) = 0;
	virtual RenderResult GetSurfaceLevel(unsigned int level, IDirect3DSurface9 **surface) = 0;
	virtual RenderResult LockRect(unsigned int level, D3DLOCKED_RECT *locked, const RenderRect *rect, RenderUInt32 flags) = 0;
	virtual RenderResult UnlockRect(unsigned int level) = 0;
	virtual RenderResult AddDirtyRect(const RenderRect *dirty) = 0;
};

class IDirect3DCubeTexture9 : public IDirect3DBaseTexture9
{
public:
	virtual RenderResult GetLevelDesc(unsigned int level, D3DSURFACE_DESC *desc) = 0;
	virtual RenderResult GetCubeMapSurface(D3DCUBEMAP_FACES face, unsigned int level, IDirect3DSurface9 **surface) = 0;
	virtual RenderResult LockRect(D3DCUBEMAP_FACES face, unsigned int level, D3DLOCKED_RECT *locked, const RenderRect *rect, RenderUInt32 flags) = 0;
	virtual RenderResult UnlockRect(D3DCUBEMAP_FACES face, unsigned int level) = 0;
	virtual RenderResult AddDirtyRect(D3DCUBEMAP_FACES face, const RenderRect *dirty) = 0;
};

class IDirect3DVolumeTexture9 : public IDirect3DBaseTexture9
{
public:
	virtual RenderResult GetLevelDesc(unsigned int level, D3DVOLUME_DESC *desc) = 0;
	virtual RenderResult GetVolumeLevel(unsigned int level, IDirect3DVolume9 **volume) = 0;
	virtual RenderResult LockBox(unsigned int level, D3DLOCKED_BOX *locked, const D3DBOX *box, RenderUInt32 flags) = 0;
	virtual RenderResult UnlockBox(unsigned int level) = 0;
};

class IDirect3DSurface9 : public IDirect3DResource9
{
public:
	virtual RenderResult GetContainer(const D3D9PosixGuid &riid, void **container) = 0;
	virtual RenderResult GetDesc(D3DSURFACE_DESC *desc) = 0;
	virtual RenderResult LockRect(D3DLOCKED_RECT *locked, const RenderRect *rect, RenderUInt32 flags) = 0;
	virtual RenderResult UnlockRect() = 0;
};

class IDirect3DVolume9 : public D3D9PosixUnknown
{
public:
	virtual RenderResult GetDesc(D3DVOLUME_DESC *desc) = 0;
	virtual RenderResult LockBox(D3DLOCKED_BOX *locked, const D3DBOX *box, RenderUInt32 flags) = 0;
	virtual RenderResult UnlockBox() = 0;
};

class IDirect3DVertexBuffer9 : public IDirect3DResource9
{
public:
	virtual RenderResult Lock(unsigned int offset, unsigned int size, void **data, RenderUInt32 flags) = 0;
	virtual RenderResult Unlock() = 0;
	virtual RenderResult GetDesc(D3DVERTEXBUFFER_DESC *desc) = 0;
};

class IDirect3DIndexBuffer9 : public IDirect3DResource9
{
public:
	virtual RenderResult Lock(unsigned int offset, unsigned int size, void **data, RenderUInt32 flags) = 0;
	virtual RenderResult Unlock() = 0;
	virtual RenderResult GetDesc(D3DINDEXBUFFER_DESC *desc) = 0;
};

class IDirect3DVertexShader9 : public D3D9PosixUnknown {};
class IDirect3DPixelShader9 : public D3D9PosixUnknown {};
class IDirect3DVertexDeclaration9 : public D3D9PosixUnknown {};

class IDirect3DSwapChain9 : public D3D9PosixUnknown
{
public:
	virtual RenderResult Present(const RenderRect *source, const RenderRect *dest, RenderWindow override_window,
		const void *dirty_region, RenderUInt32 flags) = 0;
	virtual RenderResult GetFrontBufferData(IDirect3DSurface9 *dest) = 0;
	virtual RenderResult GetBackBuffer(unsigned int index, D3DBACKBUFFER_TYPE type, IDirect3DSurface9 **surface) = 0;
	virtual RenderResult GetDisplayMode(D3DDISPLAYMODE *mode) = 0;
};

class IDirect3DDevice9 : public D3D9PosixUnknown
{
public:
	virtual RenderResult TestCooperativeLevel() = 0;
	virtual unsigned int GetAvailableTextureMem() = 0;
	virtual RenderResult EvictManagedResources() = 0;
	virtual RenderResult GetDeviceCaps(D3DCAPS9 *caps) = 0;
	virtual RenderResult GetDisplayMode(unsigned int swap_chain, D3DDISPLAYMODE *mode) = 0;
	virtual RenderResult SetCursorProperties(unsigned int hotspot_x, unsigned int hotspot_y, IDirect3DSurface9 *bitmap) = 0;
	virtual void SetCursorPosition(int x, int y, RenderUInt32 flags) = 0;
	virtual int ShowCursor(int show) = 0;
	virtual RenderResult CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS *parameters, IDirect3DSwapChain9 **swap_chain) = 0;
	virtual RenderResult Reset(D3DPRESENT_PARAMETERS *parameters) = 0;
	virtual RenderResult Present(const RenderRect *source, const RenderRect *dest, RenderWindow override_window,
		const void *dirty_region) = 0;
	virtual RenderResult GetBackBuffer(unsigned int swap_chain, unsigned int index, D3DBACKBUFFER_TYPE type,
		IDirect3DSurface9 **surface) = 0;
	virtual void SetGammaRamp(unsigned int swap_chain, RenderUInt32 flags, const D3DGAMMARAMP *ramp) = 0;
	virtual RenderResult CreateTexture(unsigned int width, unsigned int height, unsigned int levels, RenderUInt32 usage,
		D3DFORMAT format, D3DPOOL pool, IDirect3DTexture9 **texture, void **shared) = 0;
	virtual RenderResult CreateVolumeTexture(unsigned int width, unsigned int height, unsigned int depth, unsigned int levels,
		RenderUInt32 usage, D3DFORMAT format, D3DPOOL pool, IDirect3DVolumeTexture9 **texture, void **shared) = 0;
	virtual RenderResult CreateCubeTexture(unsigned int edge, unsigned int levels, RenderUInt32 usage, D3DFORMAT format,
		D3DPOOL pool, IDirect3DCubeTexture9 **texture, void **shared) = 0;
	virtual RenderResult CreateVertexBuffer(unsigned int length, RenderUInt32 usage, RenderUInt32 fvf, D3DPOOL pool,
		IDirect3DVertexBuffer9 **buffer, void **shared) = 0;
	virtual RenderResult CreateIndexBuffer(unsigned int length, RenderUInt32 usage, D3DFORMAT format, D3DPOOL pool,
		IDirect3DIndexBuffer9 **buffer, void **shared) = 0;
	virtual RenderResult CreateRenderTarget(unsigned int width, unsigned int height, D3DFORMAT format,
		D3DMULTISAMPLE_TYPE multisample, RenderUInt32 quality, int lockable, IDirect3DSurface9 **surface, void **shared) = 0;
	virtual RenderResult CreateDepthStencilSurface(unsigned int width, unsigned int height, D3DFORMAT format,
		D3DMULTISAMPLE_TYPE multisample, RenderUInt32 quality, int discard, IDirect3DSurface9 **surface, void **shared) = 0;
	virtual RenderResult UpdateSurface(IDirect3DSurface9 *source, const RenderRect *source_rect, IDirect3DSurface9 *dest,
		const RenderPoint *dest_point) = 0;
	virtual RenderResult UpdateTexture(IDirect3DBaseTexture9 *source, IDirect3DBaseTexture9 *dest) = 0;
	virtual RenderResult GetRenderTargetData(IDirect3DSurface9 *render_target, IDirect3DSurface9 *dest) = 0;
	virtual RenderResult GetFrontBufferData(unsigned int swap_chain, IDirect3DSurface9 *dest) = 0;
	virtual RenderResult StretchRect(IDirect3DSurface9 *source, const RenderRect *source_rect, IDirect3DSurface9 *dest,
		const RenderRect *dest_rect, D3DTEXTUREFILTERTYPE filter) = 0;
	virtual RenderResult CreateOffscreenPlainSurface(unsigned int width, unsigned int height, D3DFORMAT format, D3DPOOL pool,
		IDirect3DSurface9 **surface, void **shared) = 0;
	virtual RenderResult SetRenderTarget(RenderUInt32 index, IDirect3DSurface9 *surface) = 0;
	virtual RenderResult GetRenderTarget(RenderUInt32 index, IDirect3DSurface9 **surface) = 0;
	virtual RenderResult SetDepthStencilSurface(IDirect3DSurface9 *surface) = 0;
	virtual RenderResult GetDepthStencilSurface(IDirect3DSurface9 **surface) = 0;
	virtual RenderResult BeginScene() = 0;
	virtual RenderResult EndScene() = 0;
	virtual RenderResult Clear(RenderUInt32 count, const D3DRECT *rects, RenderUInt32 flags, D3DCOLOR color, float z,
		RenderUInt32 stencil) = 0;
	virtual RenderResult SetTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix) = 0;
	virtual RenderResult GetTransform(D3DTRANSFORMSTATETYPE state, D3DMATRIX *matrix) = 0;
	virtual RenderResult SetViewport(const D3DVIEWPORT9 *viewport) = 0;
	virtual RenderResult GetViewport(D3DVIEWPORT9 *viewport) = 0;
	virtual RenderResult SetMaterial(const D3DMATERIAL9 *material) = 0;
	virtual RenderResult SetLight(RenderUInt32 index, const D3DLIGHT9 *light) = 0;
	virtual RenderResult LightEnable(RenderUInt32 index, int enable) = 0;
	virtual RenderResult SetClipPlane(RenderUInt32 index, const float *plane) = 0;
	virtual RenderResult SetRenderState(D3DRENDERSTATETYPE state, RenderUInt32 value) = 0;
	virtual RenderResult GetRenderState(D3DRENDERSTATETYPE state, RenderUInt32 *value) = 0;
	virtual RenderResult GetTexture(RenderUInt32 stage, IDirect3DBaseTexture9 **texture) = 0;
	virtual RenderResult SetTexture(RenderUInt32 stage, IDirect3DBaseTexture9 *texture) = 0;
	virtual RenderResult GetTextureStageState(RenderUInt32 stage, D3DTEXTURESTAGESTATETYPE type, RenderUInt32 *value) = 0;
	virtual RenderResult SetTextureStageState(RenderUInt32 stage, D3DTEXTURESTAGESTATETYPE type, RenderUInt32 value) = 0;
	virtual RenderResult SetSamplerState(RenderUInt32 sampler, D3DSAMPLERSTATETYPE type, RenderUInt32 value) = 0;
	virtual RenderResult ValidateDevice(RenderUInt32 *passes) = 0;
	virtual RenderResult SetSoftwareVertexProcessing(int software) = 0;
	virtual RenderResult DrawPrimitive(D3DPRIMITIVETYPE type, unsigned int start_vertex, unsigned int primitive_count) = 0;
	virtual RenderResult DrawIndexedPrimitive(D3DPRIMITIVETYPE type, int base_vertex, unsigned int min_vertex,
		unsigned int vertex_count, unsigned int start_index, unsigned int primitive_count) = 0;
	virtual RenderResult DrawPrimitiveUP(D3DPRIMITIVETYPE type, unsigned int primitive_count, const void *vertices,
		unsigned int stride) = 0;
	virtual RenderResult ProcessVertices(unsigned int source_start, unsigned int dest_index, unsigned int vertex_count,
		IDirect3DVertexBuffer9 *dest, IDirect3DVertexDeclaration9 *declaration, RenderUInt32 flags) = 0;
	virtual RenderResult CreateVertexDeclaration(const D3DVERTEXELEMENT9 *elements, IDirect3DVertexDeclaration9 **declaration) = 0;
	virtual RenderResult SetVertexDeclaration(IDirect3DVertexDeclaration9 *declaration) = 0;
	virtual RenderResult SetFVF(RenderUInt32 fvf) = 0;
	virtual RenderResult CreateVertexShader(const RenderUInt32 *function, IDirect3DVertexShader9 **shader) = 0;
	virtual RenderResult SetVertexShader(IDirect3DVertexShader9 *shader) = 0;
	virtual RenderResult GetVertexShader(IDirect3DVertexShader9 **shader) = 0;
	virtual RenderResult SetVertexShaderConstantF(unsigned int start, const float *data, unsigned int count) = 0;
	virtual RenderResult SetStreamSource(unsigned int stream, IDirect3DVertexBuffer9 *buffer, unsigned int offset,
		unsigned int stride) = 0;
	virtual RenderResult SetIndices(IDirect3DIndexBuffer9 *buffer) = 0;
	virtual RenderResult GetIndices(IDirect3DIndexBuffer9 **buffer) = 0;
	virtual RenderResult CreatePixelShader(const RenderUInt32 *function, IDirect3DPixelShader9 **shader) = 0;
	virtual RenderResult SetPixelShader(IDirect3DPixelShader9 *shader) = 0;
	virtual RenderResult GetPixelShader(IDirect3DPixelShader9 **shader) = 0;
	virtual RenderResult SetPixelShaderConstantF(unsigned int start, const float *data, unsigned int count) = 0;
};

class IDirect3D9 : public D3D9PosixUnknown
{
public:
	virtual unsigned int GetAdapterCount() = 0;
	virtual RenderResult GetAdapterIdentifier(unsigned int adapter, RenderUInt32 flags, D3DADAPTER_IDENTIFIER9 *identifier) = 0;
	virtual unsigned int GetAdapterModeCount(unsigned int adapter, D3DFORMAT format) = 0;
	virtual RenderResult EnumAdapterModes(unsigned int adapter, D3DFORMAT format, unsigned int mode, D3DDISPLAYMODE *display_mode) = 0;
	virtual RenderResult GetAdapterDisplayMode(unsigned int adapter, D3DDISPLAYMODE *mode) = 0;
	virtual RenderResult CheckDeviceType(unsigned int adapter, D3DDEVTYPE type, D3DFORMAT display_format,
		D3DFORMAT back_buffer_format, int windowed) = 0;
	virtual RenderResult CheckDeviceFormat(unsigned int adapter, D3DDEVTYPE type, D3DFORMAT adapter_format, RenderUInt32 usage,
		D3DRESOURCETYPE resource_type, D3DFORMAT check_format) = 0;
	virtual RenderResult CheckDeviceMultiSampleType(unsigned int adapter, D3DDEVTYPE type, D3DFORMAT surface_format,
		int windowed, D3DMULTISAMPLE_TYPE multisample, RenderUInt32 *quality_levels) = 0;
	virtual RenderResult CheckDepthStencilMatch(unsigned int adapter, D3DDEVTYPE type, D3DFORMAT adapter_format,
		D3DFORMAT render_target_format, D3DFORMAT depth_stencil_format) = 0;
	virtual RenderResult GetDeviceCaps(unsigned int adapter, D3DDEVTYPE type, D3DCAPS9 *caps) = 0;
	virtual RenderResult CreateDevice(unsigned int adapter, D3DDEVTYPE type, RenderWindow focus_window,
		RenderUInt32 behaviour_flags, D3DPRESENT_PARAMETERS *parameters, IDirect3DDevice9 **device) = 0;
};

typedef IDirect3D9 *LPDIRECT3D9;
typedef IDirect3DDevice9 *LPDIRECT3DDEVICE9;
typedef IDirect3DBaseTexture9 *LPDIRECT3DBASETEXTURE9;
typedef IDirect3DTexture9 *LPDIRECT3DTEXTURE9;
typedef IDirect3DCubeTexture9 *LPDIRECT3DCUBETEXTURE9;
typedef IDirect3DVolumeTexture9 *LPDIRECT3DVOLUMETEXTURE9;
typedef IDirect3DSurface9 *LPDIRECT3DSURFACE9;
typedef IDirect3DVertexBuffer9 *LPDIRECT3DVERTEXBUFFER9;
typedef IDirect3DIndexBuffer9 *LPDIRECT3DINDEXBUFFER9;
typedef IDirect3DSwapChain9 *LPDIRECT3DSWAPCHAIN9;

/// The one entry point: the POSIX device, or NULL - and a log line saying why - until A2/A3 make one.
IDirect3D9 *Direct3DCreate9(unsigned int sdk_version);

#if !defined(D3D9POSIX_CHECKER)
// The layouts, checked against MinGW-w64's by Tests/d3d9posix_check.cpp and fixed here for every
// POSIX compiler: the renderer copies these structures whole and reads their fields by name.
static_assert(sizeof(D3DVIEWPORT9) == 24, "D3DVIEWPORT9 layout");
static_assert(sizeof(D3DMATERIAL9) == 68, "D3DMATERIAL9 layout");
static_assert(sizeof(D3DLIGHT9) == 104, "D3DLIGHT9 layout");
static_assert(sizeof(D3DSURFACE_DESC) == 32, "D3DSURFACE_DESC layout");
static_assert(sizeof(D3DLOCKED_RECT) == 16 && offsetof(D3DLOCKED_RECT, pBits) == 8,
	"D3DLOCKED_RECT layout (INT Pitch, then an eight-aligned pointer)");
static_assert(sizeof(D3DLOCKED_BOX) == 16 && offsetof(D3DLOCKED_BOX, pBits) == 8,
	"D3DLOCKED_BOX layout (two INT pitches, then the pointer)");
static_assert(sizeof(D3DGAMMARAMP) == 1536, "D3DGAMMARAMP layout");
static_assert(sizeof(D3DCAPS9) == 304, "D3DCAPS9 layout");
static_assert(sizeof(D3DVERTEXELEMENT9) == 8, "D3DVERTEXELEMENT9 layout");
static_assert(sizeof(D3DPRESENT_PARAMETERS) == 64, "D3DPRESENT_PARAMETERS layout (the window handle is eight-aligned)");
static_assert(sizeof(D3DADAPTER_IDENTIFIER9) == 1104, "D3DADAPTER_IDENTIFIER9 layout");
static_assert(sizeof(D3DFORMAT) == 4 && sizeof(D3DRENDERSTATETYPE) == 4, "D3D9's enumerations are four bytes");
#endif


#endif // PLATFORM_D3D9POSIX_H
