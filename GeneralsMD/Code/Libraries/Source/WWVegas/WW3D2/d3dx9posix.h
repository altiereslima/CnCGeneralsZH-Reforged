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

// The D3DX texture helpers off Windows (decision 7): defined in d3dx9posix_texture.cpp (phase A2),
// bound by Bind_D3DX9_Runtime in d3dx9posix.cpp.  Their signatures are d3dx9runtime.h's function
// pointer types'.  Not for the renderer to include: it calls the D3DX names.

#pragma once

#ifndef D3DX9POSIX_H
#define D3DX9POSIX_H

#include "d3dx9runtime.h"

#include <string>

RenderResult D3DX9Posix_Create_Texture(LPDIRECT3DDEVICE9 device, unsigned int width, unsigned int height,
	unsigned int mip_levels, RenderUInt32 usage, D3DFORMAT format, D3DPOOL pool, LPDIRECT3DTEXTURE9 * texture);
RenderResult D3DX9Posix_Create_Cube_Texture(LPDIRECT3DDEVICE9 device, unsigned int edge_length,
	unsigned int mip_levels, RenderUInt32 usage, D3DFORMAT format, D3DPOOL pool, LPDIRECT3DCUBETEXTURE9 * texture);
RenderResult D3DX9Posix_Create_Volume_Texture(LPDIRECT3DDEVICE9 device, unsigned int width, unsigned int height,
	unsigned int depth, unsigned int mip_levels, RenderUInt32 usage, D3DFORMAT format, D3DPOOL pool,
	LPDIRECT3DVOLUMETEXTURE9 * texture);
RenderResult D3DX9Posix_Create_Texture_From_File(LPDIRECT3DDEVICE9 device, const char * file_name,
	unsigned int width, unsigned int height, unsigned int mip_levels, RenderUInt32 usage, D3DFORMAT format,
	D3DPOOL pool, RenderUInt32 filter, RenderUInt32 mip_filter, D3DCOLOR colour_key, D3DXIMAGE_INFO * info,
	void * palette, LPDIRECT3DTEXTURE9 * texture);
RenderResult D3DX9Posix_Filter_Texture(LPDIRECT3DBASETEXTURE9 texture, const void * palette,
	unsigned int source_level, RenderUInt32 filter);
RenderResult D3DX9Posix_Load_Surface_From_Surface(LPDIRECT3DSURFACE9 destination, const void * destination_palette,
	const RenderRect * destination_rect, LPDIRECT3DSURFACE9 source, const void * source_palette,
	const RenderRect * source_rect, RenderUInt32 filter, D3DCOLOR colour_key);

/// The source text a POSIX D3DXAssembleShader wrapped in its stub token stream (d3dx9posix.cpp says
/// what the stub is and is not); false for a stream that is not one.
bool D3DX9Posix_Stub_Shader_Source(const RenderUInt32 * tokens, std::string & source);

#endif // D3DX9POSIX_H
