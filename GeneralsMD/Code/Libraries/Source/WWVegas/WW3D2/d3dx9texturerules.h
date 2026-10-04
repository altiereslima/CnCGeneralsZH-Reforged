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

// D3DX's rules for its texture helpers, shared by the two places the port does without d3dx9_43.dll:
// d3dx9posix_texture.cpp over the POSIX device's images, and d3dx9portable_texture.cpp over the real
// Direct3D 9 device on Windows on ARM64.  Include after d3dx9runtime.h and PosixImageOps.h.

#pragma once

#ifndef D3DX9TEXTURERULES_H
#define D3DX9TEXTURERULES_H

namespace D3DXTextureRules {

/// D3DX's filter value as the operation it names: the low byte, D3DX_DEFAULT meaning `otherwise`.  The
/// high bits (mirroring, dithering, sRGB) change nothing a copy here does.  False for no such filter.
inline bool filter_of(RenderUInt32 filter, PosixFilter otherwise, PosixFilter & out)
{
	if (filter == D3DX_DEFAULT) {
		out = otherwise;
		return true;
	}
	switch (filter & 0xff) {
	case D3DX_FILTER_NONE:		out = POSIX_FILTER_NONE; return true;
	case D3DX_FILTER_POINT:		out = POSIX_FILTER_POINT; return true;
	case D3DX_FILTER_LINEAR:	out = POSIX_FILTER_LINEAR; return true;
	case D3DX_FILTER_TRIANGLE:	// the same average as BOX for the 2:1 steps mip levels are
	case D3DX_FILTER_BOX:		out = POSIX_FILTER_BOX; return true;
	default:					return false;
	}
}

inline bool has_alpha(D3DFORMAT format)
{
	switch (format) {
	case D3DFMT_A8R8G8B8: case D3DFMT_A1R5G5B5: case D3DFMT_A4R4G4B4: case D3DFMT_A8: case D3DFMT_A8R3G3B2:
	case D3DFMT_A2B10G10R10: case D3DFMT_A8B8G8R8: case D3DFMT_A2R10G10B10: case D3DFMT_A16B16G16R16:
	case D3DFMT_A8P8: case D3DFMT_A8L8: case D3DFMT_A4L4:
	case D3DFMT_DXT1: case D3DFMT_DXT2: case D3DFMT_DXT3: case D3DFMT_DXT4: case D3DFMT_DXT5:
		return true;
	default:
		return false;
	}
}

/// D3DX's sizes: 0 is 1 and D3DX_DEFAULT is 256 for a dimension (what d3dx9_43.dll makes,
/// test_d3dx9portable_oracle); for levels, 0 or D3DX_DEFAULT is the full chain.
inline unsigned int dimension(unsigned int size)
{
	const unsigned int DEFAULT_DIMENSION = 256;
	if (size == D3DX_DEFAULT) {
		return DEFAULT_DIMENSION;
	}
	return size == 0 ? 1 : size;
}

inline unsigned int levels(unsigned int mip_levels)
{
	return (mip_levels == D3DX_DEFAULT) ? 0 : mip_levels;
}

/// D3DX's format rule: the format asked for if the device takes it, else the nearest it does - here the
/// 32-bit ARGB or XRGB by whether the asked format has alpha.  `create` makes one resource in a format.
template <class Create>
RenderResult create_with_substitution(D3DFORMAT format, RenderUInt32 usage, Create create)
{
	if (format == D3DFMT_UNKNOWN || (unsigned int)format == D3DX_DEFAULT) {
		format = D3DFMT_A8R8G8B8;
	}
	// The format first: when the device takes it but will not make its chain, D3DX keeps the format and
	// makes the chain itself; only a format the device refuses outright is replaced.
	const RenderUInt32 plain = usage & ~(RenderUInt32)D3DUSAGE_AUTOGENMIPMAP;
	RenderResult result = create(format, usage);
	if (result == D3DERR_INVALIDCALL && plain != usage) {
		result = create(format, plain);
	}
	if (result == D3DERR_INVALIDCALL) {
		const D3DFORMAT substitute = has_alpha(format) ? D3DFMT_A8R8G8B8 : D3DFMT_X8R8G8B8;
		if (substitute != format) {
			result = create(substitute, usage);
			if (result == D3DERR_INVALIDCALL && plain != usage) {
				result = create(substitute, plain);
			}
		}
	}
	return result;
}

} // namespace D3DXTextureRules

#endif // D3DX9TEXTURERULES_H
