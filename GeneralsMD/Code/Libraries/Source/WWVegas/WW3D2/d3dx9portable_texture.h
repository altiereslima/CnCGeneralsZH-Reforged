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

// D3DX9's texture helpers on Windows without d3dx9_43.dll: the port's own (PosixImageOps, the code macOS
// and Linux run) over the real Direct3D 9 device.  Windows on ARM64 binds these, since Microsoft never
// shipped D3DX9 for ARM64; ZH_D3DX_PORTABLE=1 binds them on x64 too, so the same code can be run and
// compared there (d3dx9runtime.cpp).  Each has the signature of the D3DX9 function of the same name.

#pragma once

#ifndef D3DX9PORTABLE_TEXTURE_H
#define D3DX9PORTABLE_TEXTURE_H

#include "d3dx9runtime.h"

// The device's own Create* call with D3DX's sizes and its substitution of a format the device refuses.
HRESULT WINAPI D3DXPortable_Create_Texture(LPDIRECT3DDEVICE9 device, UINT width, UINT height, UINT mip_levels,
	DWORD usage, D3DFORMAT format, D3DPOOL pool, LPDIRECT3DTEXTURE9 * texture);
HRESULT WINAPI D3DXPortable_Create_Cube_Texture(LPDIRECT3DDEVICE9 device, UINT edge_length, UINT mip_levels,
	DWORD usage, D3DFORMAT format, D3DPOOL pool, LPDIRECT3DCUBETEXTURE9 * texture);
HRESULT WINAPI D3DXPortable_Create_Volume_Texture(LPDIRECT3DDEVICE9 device, UINT width, UINT height, UINT depth,
	UINT mip_levels, DWORD usage, D3DFORMAT format, D3DPOOL pool, LPDIRECT3DVOLUMETEXTURE9 * texture);

// A rectangle of one surface into a rectangle of another, converted and scaled on the CPU.  A surface the CPU
// cannot lock is read through GetRenderTargetData and written through UpdateSurface.  Writing DXT from
// another format (compressing), palettes and colour keys fail: nothing in the game asks for them.
HRESULT WINAPI D3DXPortable_Load_Surface_From_Surface(LPDIRECT3DSURFACE9 destination,
	const PALETTEENTRY * destination_palette, const RECT * destination_rect, LPDIRECT3DSURFACE9 source,
	const PALETTEENTRY * source_palette, const RECT * source_rect, DWORD filter, D3DCOLOR colour_key);

// Each level after `source_level` made from the one above it, for 2D and cube textures.
HRESULT WINAPI D3DXPortable_Filter_Texture(LPDIRECT3DBASETEXTURE9 texture, const PALETTEENTRY * palette,
	UINT source_level, DWORD filter);

#endif // D3DX9PORTABLE_TEXTURE_H
