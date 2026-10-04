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

// Decision 7, phase A2: the caps, the Check* answers and the adapter identifier - what this device
// says it can do.  A3 draws exactly what is claimed here, so nothing is claimed that A3 will not honour
// (the profile agreed with the draw side's author, 2026-09-26):
//
//   - The identifier names no vendor (VendorId and DeviceId 0), so neither DX8Caps' vendor quirks nor
//     W3DShaderManager::getChipset's vendor tables fire.  getChipset classifies such a device by its
//     caps, as it does an AMD or Intel card on Windows.
//   - Shader versions 0.0: POSIX has no D3D9 shader assembler, and A3's first draw is the fixed-function
//     generator's.  With PS 0.0 getChipset answers DC_UNKNOWN, and every path the engine gates on a
//     pixel shader chipset takes its fixed-function branch.  A3 raises these in the commit that makes
//     the engine's own shaders run.
//   - TextureOpCaps: exactly the stage operations ffshader.cpp's generator writes (test_posix_resources
//     asks the generator op by op and compares).  No BUMPENVMAP, no PREMODULATE.
//   - Formats: see posixFormatSupported below.  No bump formats (so the bump paths stay off), no P8, no
//     YUV.

#include "PosixDevice9.h"
#include "PosixD3D9Caps.h"
#include "PosixResources9.h"

#include <string.h>

//-------------------------------------------------------------------------------------------------
// Formats
//-------------------------------------------------------------------------------------------------

namespace {

bool isTextureFormat( D3DFORMAT format )
{
	switch (format)
	{
		case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: case D3DFMT_R5G6B5: case D3DFMT_A1R5G5B5: case D3DFMT_A4R4G4B4:
		case D3DFMT_L8: case D3DFMT_A8: case D3DFMT_A8L8:
		case D3DFMT_DXT1: case D3DFMT_DXT2: case D3DFMT_DXT3: case D3DFMT_DXT4: case D3DFMT_DXT5:
			return true;
		default:
			return false;
	}
}

bool isRenderTargetFormat( D3DFORMAT format )
{
	return format == D3DFMT_A8R8G8B8 || format == D3DFMT_X8R8G8B8 || format == D3DFMT_R5G6B5;
}

bool isDepthFormat( D3DFORMAT format )
{
	return format == D3DFMT_D16 || format == D3DFMT_D24S8 || format == D3DFMT_D24X8;
}

bool isCompressed( D3DFORMAT format )
{
	return format == D3DFMT_DXT1 || format == D3DFMT_DXT2 || format == D3DFMT_DXT3 || format == D3DFMT_DXT4
		|| format == D3DFMT_DXT5;
}

/** The stage operations ffshader.cpp's generator writes, as D3DTEXOPCAPS bits: D3DTOP_x is bit x - 1. */
const RenderUInt32 TEXTURE_OP_CAPS =
	D3DTEXOPCAPS_DISABLE | D3DTEXOPCAPS_SELECTARG1 | D3DTEXOPCAPS_SELECTARG2 | D3DTEXOPCAPS_MODULATE
	| D3DTEXOPCAPS_MODULATE2X | D3DTEXOPCAPS_MODULATE4X | D3DTEXOPCAPS_ADD | D3DTEXOPCAPS_ADDSIGNED
	| D3DTEXOPCAPS_ADDSIGNED2X | D3DTEXOPCAPS_SUBTRACT | D3DTEXOPCAPS_ADDSMOOTH | D3DTEXOPCAPS_BLENDDIFFUSEALPHA
	| D3DTEXOPCAPS_BLENDTEXTUREALPHA | D3DTEXOPCAPS_BLENDFACTORALPHA | D3DTEXOPCAPS_BLENDTEXTUREALPHAPM
	| D3DTEXOPCAPS_BLENDCURRENTALPHA | D3DTEXOPCAPS_MODULATEALPHA_ADDCOLOR | D3DTEXOPCAPS_MODULATECOLOR_ADDALPHA
	| D3DTEXOPCAPS_MODULATEINVALPHA_ADDCOLOR | D3DTEXOPCAPS_MODULATEINVCOLOR_ADDALPHA | D3DTEXOPCAPS_DOTPRODUCT3
	| D3DTEXOPCAPS_MULTIPLYADD | D3DTEXOPCAPS_LERP;

} // namespace

bool posixFormatSupported( RenderUInt32 usage, D3DRESOURCETYPE resource_type, D3DFORMAT format )
{
	if (usage & D3DUSAGE_DEPTHSTENCIL)
		return resource_type == D3DRTYPE_SURFACE && isDepthFormat( format );		// no depth textures: nothing samples depth
	if (usage & D3DUSAGE_RENDERTARGET)
		return (resource_type == D3DRTYPE_SURFACE || resource_type == D3DRTYPE_TEXTURE
			|| resource_type == D3DRTYPE_CUBETEXTURE) && isRenderTargetFormat( format );
	switch (resource_type)
	{
		case D3DRTYPE_TEXTURE:
		case D3DRTYPE_CUBETEXTURE:
			// A compressed texture cannot have its chain made for it: it has to arrive with one.
			return isTextureFormat( format ) && !((usage & D3DUSAGE_AUTOGENMIPMAP) && isCompressed( format ));
		case D3DRTYPE_VOLUMETEXTURE:
			return isTextureFormat( format ) && !isCompressed( format );
		case D3DRTYPE_SURFACE:
			return isTextureFormat( format ) || isRenderTargetFormat( format );
		default:
			return false;
	}
}

//-------------------------------------------------------------------------------------------------
// The adapter's answers
//-------------------------------------------------------------------------------------------------

RenderResult PosixDirect3D9::GetAdapterIdentifier( unsigned int adapter, RenderUInt32, D3DADAPTER_IDENTIFIER9 *identifier )
{
	if (adapter != 0 || identifier == NULL)
		return D3DERR_INVALIDCALL;
	memset( identifier, 0, sizeof( *identifier ) );
	strncpy( identifier->Driver, "posixd3d9", sizeof( identifier->Driver ) - 1 );
	strncpy( identifier->Description, "Zero Hour Reforged D3D9 (SDL3 GPU)", sizeof( identifier->Description ) - 1 );
	strncpy( identifier->DeviceName, "posixd3d9", sizeof( identifier->DeviceName ) - 1 );
	identifier->DriverVersion = 1;		// nothing reads it but getChipset, which only stores it
	identifier->VendorId = 0;					// no vendor: see the file's head
	identifier->DeviceId = 0;
	return D3D_OK;
}

RenderResult PosixDirect3D9::CheckDeviceType( unsigned int adapter, D3DDEVTYPE type, D3DFORMAT display_format,
	D3DFORMAT back_buffer_format, int )
{
	if (adapter != 0 || type != D3DDEVTYPE_HAL)
		return D3DERR_INVALIDCALL;
	if (display_format != D3DFMT_X8R8G8B8 && display_format != D3DFMT_R5G6B5)
		return D3DERR_NOTAVAILABLE;
	const D3DFORMAT back = (back_buffer_format == D3DFMT_UNKNOWN) ? display_format : back_buffer_format;
	return isRenderTargetFormat( back ) ? D3D_OK : D3DERR_NOTAVAILABLE;
}

RenderResult PosixDirect3D9::CheckDeviceFormat( unsigned int adapter, D3DDEVTYPE type, D3DFORMAT, RenderUInt32 usage,
	D3DRESOURCETYPE resource_type, D3DFORMAT check_format )
{
	if (adapter != 0 || type != D3DDEVTYPE_HAL)
		return D3DERR_INVALIDCALL;
	return posixFormatSupported( usage, resource_type, check_format ) ? D3D_OK : D3DERR_NOTAVAILABLE;
}

RenderResult PosixDirect3D9::CheckDeviceMultiSampleType( unsigned int adapter, D3DDEVTYPE type, D3DFORMAT surface_format,
	int, D3DMULTISAMPLE_TYPE multisample, RenderUInt32 *quality_levels )
{
	if (quality_levels != NULL)
		*quality_levels = 0;
	if (adapter != 0 || type != D3DDEVTYPE_HAL)
		return D3DERR_INVALIDCALL;
	if (!isRenderTargetFormat( surface_format ) && !isDepthFormat( surface_format ))
		return D3DERR_NOTAVAILABLE;
	// Only single-sampled until A3 resolves multisampled targets.
	if (multisample != D3DMULTISAMPLE_NONE)
		return D3DERR_NOTAVAILABLE;
	if (quality_levels != NULL)
		*quality_levels = 1;
	return D3D_OK;
}

RenderResult PosixDirect3D9::CheckDepthStencilMatch( unsigned int adapter, D3DDEVTYPE type, D3DFORMAT,
	D3DFORMAT render_target_format, D3DFORMAT depth_stencil_format )
{
	if (adapter != 0 || type != D3DDEVTYPE_HAL)
		return D3DERR_INVALIDCALL;
	return (isRenderTargetFormat( render_target_format ) && isDepthFormat( depth_stencil_format ))
		? D3D_OK : D3DERR_NOTAVAILABLE;
}

RenderResult PosixDirect3D9::GetDeviceCaps( unsigned int adapter, D3DDEVTYPE type, D3DCAPS9 *caps )
{
	if (adapter != 0 || type != D3DDEVTYPE_HAL || caps == NULL)
		return D3DERR_INVALIDCALL;
	memset( caps, 0, sizeof( *caps ) );
	caps->DeviceType = D3DDEVTYPE_HAL;
	caps->AdapterOrdinal = 0;
	caps->PresentationIntervals = D3DPRESENT_INTERVAL_ONE | D3DPRESENT_INTERVAL_IMMEDIATE;
	caps->DevCaps = D3DDEVCAPS_HWTRANSFORMANDLIGHT;			// no N-patches
	caps->PrimitiveMiscCaps = D3DPMISCCAPS_COLORWRITEENABLE;
	caps->RasterCaps = D3DPRASTERCAPS_DEPTHBIAS | D3DPRASTERCAPS_FOGRANGE;
	caps->Caps2 = 0;																		// no gamma ramp: headless has no display, SDL none to set
	caps->TextureCaps = D3DPTEXTURECAPS_CUBEMAP;					// no power-of-two restriction
	caps->TextureFilterCaps = D3DPTFILTERCAPS_MINFLINEAR | D3DPTFILTERCAPS_MINFANISOTROPIC | D3DPTFILTERCAPS_MIPFLINEAR
		| D3DPTFILTERCAPS_MAGFLINEAR | D3DPTFILTERCAPS_MAGFANISOTROPIC;
	caps->CubeTextureFilterCaps = caps->TextureFilterCaps;
	caps->StretchRectFilterCaps = D3DPTFILTERCAPS_MINFLINEAR | D3DPTFILTERCAPS_MAGFLINEAR;
	caps->MaxTextureWidth = 8192;
	caps->MaxTextureHeight = 8192;
	caps->MaxVolumeExtent = 2048;
	caps->MaxTextureRepeat = 8192;
	caps->MaxTextureAspectRatio = 0;										// none
	caps->MaxAnisotropy = 16;
	caps->TextureOpCaps = TEXTURE_OP_CAPS;
	caps->MaxTextureBlendStages = 8;
	caps->MaxSimultaneousTextures = 8;
	caps->MaxActiveLights = 8;
	caps->MaxUserClipPlanes = 6;
	caps->MaxPointSize = 256.0f;
	caps->MaxPrimitiveCount = 0xffffff;
	caps->MaxVertexIndex = 0xffffff;
	caps->MaxStreams = 16;
	caps->MaxStreamStride = 255;
	// 1.1 and no higher (A3e): the device draws the engine's shaders from D3's transcriptions by the name
	// each is registered under, and 1.1 is what the engine asks for (DC_GENERIC_PIXEL_SHADER_1_1, with the
	// 8 stages above).  96 constants is vs_1_1's bank, which the transcriptions read.
	caps->VertexShaderVersion = D3DVS_VERSION(1, 1);
	caps->PixelShaderVersion = D3DPS_VERSION(1, 1);
	caps->MaxVertexShaderConst = 96;
	// The documented minimum for ps 1.0 to 1.3 (a project decision, 2026-09-26): inside +-1 every conforming
	// device agrees, so this claims only what all of them guarantee.  Registers hold [-1, 1].
	caps->PixelShader1xMaxValue = 1.0f;
	caps->NumSimultaneousRTs = 1;
	caps->MasterAdapterOrdinal = 0;
	caps->AdapterOrdinalInGroup = 0;
	caps->NumberOfAdaptersInGroup = 1;
	return D3D_OK;
}

RenderResult PosixDevice9::GetDeviceCaps( D3DCAPS9 *caps )
{
	return Adapter->GetDeviceCaps( 0, D3DDEVTYPE_HAL, caps );
}
