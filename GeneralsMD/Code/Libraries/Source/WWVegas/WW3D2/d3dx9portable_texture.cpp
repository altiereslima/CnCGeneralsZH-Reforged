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

// d3dx9portable_texture.cpp: see d3dx9portable_texture.h.
//
// A surface's pixels are copied into a PosixImage, worked on there by the same posixCopyImage that does
// D3DX's surface work on macOS and Linux (d3dx9posix_texture.cpp), and the rectangle written is copied
// back.  The copy in and out is what it costs to share that code; it is load-time work, and the terrain's
// mip rebuilds, which D3DX's own copies cost about the same.

#include "d3dx9portable_texture.h"

#include "PosixImageOps.h"
#include "d3dx9texturerules.h"

#include <string.h>

using namespace D3DXTextureRules;

namespace {

/// Holds one COM reference and drops it on every way out.
template <class Interface>
struct Held
{
	Interface * Pointer = NULL;
	~Held() { if (Pointer != NULL) Pointer->Release(); }
};

/// Rows of whole blocks between a locked surface and an image of the same format and size.  `region` limits
/// the copy to the blocks it covers; a block format only ever gets a block-aligned region (posixCopyImage).
void copy_rows(uint8_t * to, int to_pitch, const uint8_t * from, int from_pitch, const PosixImage & image,
	const PosixRegion & region)
{
	const PosixFormatLayout & layout = image.layout();
	const unsigned int first_row = region.top / layout.blockHeight;
	const unsigned int end_row = (region.bottom + layout.blockHeight - 1) / layout.blockHeight;
	const size_t offset = (size_t)(region.left / layout.blockWidth) * layout.bytesPerBlock;
	const size_t bytes = (size_t)((region.right + layout.blockWidth - 1) / layout.blockWidth) * layout.bytesPerBlock
		- offset;
	for (unsigned int row = first_row; row < end_row; ++row) {
		memcpy(to + (size_t)row * to_pitch + offset, from + (size_t)row * from_pitch + offset, bytes);
	}
}

/// The whole surface as an image.  A surface that will not lock (a render target in the default pool) is
/// read through GetRenderTargetData, as D3DX reads one.
HRESULT read_surface(LPDIRECT3DSURFACE9 surface, PosixImage & image)
{
	D3DSURFACE_DESC desc;
	HRESULT result = surface->GetDesc(&desc);
	if (FAILED(result)) {
		return result;
	}
	if (!image.create(desc.Format, desc.Width, desc.Height, 1)) {
		return D3DERR_INVALIDCALL;
	}
	PosixRegion whole;
	posixRegionOf(image, NULL, &whole);

	D3DLOCKED_RECT locked;
	if (SUCCEEDED(surface->LockRect(&locked, NULL, D3DLOCK_READONLY))) {
		copy_rows(image.bytes(), (int)image.rowPitch(), (const uint8_t *)locked.pBits, locked.Pitch, image, whole);
		return surface->UnlockRect();
	}

	Held<IDirect3DDevice9> device;
	Held<IDirect3DSurface9> copy;
	result = surface->GetDevice(&device.Pointer);
	if (SUCCEEDED(result)) {
		result = device.Pointer->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format,
			D3DPOOL_SYSTEMMEM, &copy.Pointer, NULL);
	}
	if (SUCCEEDED(result)) {
		result = device.Pointer->GetRenderTargetData(surface, copy.Pointer);
	}
	if (SUCCEEDED(result)) {
		result = copy.Pointer->LockRect(&locked, NULL, D3DLOCK_READONLY);
	}
	if (FAILED(result)) {
		return result;
	}
	copy_rows(image.bytes(), (int)image.rowPitch(), (const uint8_t *)locked.pBits, locked.Pitch, image, whole);
	return copy.Pointer->UnlockRect();
}

/// `region` of the image into the surface.  A surface that will not lock (the default pool) is written
/// through a system-memory copy and UpdateSurface.
HRESULT write_surface(LPDIRECT3DSURFACE9 surface, const PosixImage & image, const PosixRegion & region)
{
	D3DLOCKED_RECT locked;
	if (SUCCEEDED(surface->LockRect(&locked, NULL, 0))) {
		copy_rows((uint8_t *)locked.pBits, locked.Pitch, image.bytes(), (int)image.rowPitch(), image, region);
		return surface->UnlockRect();
	}

	Held<IDirect3DDevice9> device;
	Held<IDirect3DSurface9> copy;
	HRESULT result = surface->GetDevice(&device.Pointer);
	if (SUCCEEDED(result)) {
		result = device.Pointer->CreateOffscreenPlainSurface(image.width(), image.height(), image.format(),
			D3DPOOL_SYSTEMMEM, &copy.Pointer, NULL);
	}
	if (SUCCEEDED(result)) {
		result = copy.Pointer->LockRect(&locked, NULL, 0);
	}
	if (FAILED(result)) {
		return result;
	}
	copy_rows((uint8_t *)locked.pBits, locked.Pitch, image.bytes(), (int)image.rowPitch(), image, region);
	result = copy.Pointer->UnlockRect();
	if (FAILED(result)) {
		return result;
	}
	RECT rect = { (LONG)region.left, (LONG)region.top, (LONG)region.right, (LONG)region.bottom };
	POINT point = { (LONG)region.left, (LONG)region.top };
	return device.Pointer->UpdateSurface(copy.Pointer, &rect, surface, &point);
}

HRESULT copy_surface(LPDIRECT3DSURFACE9 destination, const RECT * destination_rect, LPDIRECT3DSURFACE9 source,
	const RECT * source_rect, PosixFilter filter)
{
	PosixImage from;
	HRESULT result = read_surface(source, from);
	if (FAILED(result)) {
		return result;
	}
	D3DSURFACE_DESC desc;
	result = destination->GetDesc(&desc);
	if (FAILED(result)) {
		return result;
	}
	PosixImage to;
	if (!to.create(desc.Format, desc.Width, desc.Height, 1)) {
		return D3DERR_INVALIDCALL;
	}
	PosixRegion to_region, from_region;
	if (!posixRegionOf(to, destination_rect, &to_region) || !posixRegionOf(from, source_rect, &from_region)) {
		return D3DERR_INVALIDCALL;
	}
	result = posixCopyImage(to, to_region, from, from_region, filter);
	if (FAILED(result)) {
		return result;
	}
	return write_surface(destination, to, to_region);
}

} // namespace

HRESULT WINAPI D3DXPortable_Create_Texture(LPDIRECT3DDEVICE9 device, UINT width, UINT height, UINT mip_levels,
	DWORD usage, D3DFORMAT format, D3DPOOL pool, LPDIRECT3DTEXTURE9 * texture)
{
	if (device == NULL || texture == NULL) {
		return D3DERR_INVALIDCALL;
	}
	*texture = NULL;
	return create_with_substitution(format, usage, [&](D3DFORMAT candidate, DWORD candidate_usage) {
		return device->CreateTexture(dimension(width), dimension(height), levels(mip_levels), candidate_usage,
			candidate, pool, texture, NULL);
	});
}

HRESULT WINAPI D3DXPortable_Create_Cube_Texture(LPDIRECT3DDEVICE9 device, UINT edge_length, UINT mip_levels,
	DWORD usage, D3DFORMAT format, D3DPOOL pool, LPDIRECT3DCUBETEXTURE9 * texture)
{
	if (device == NULL || texture == NULL) {
		return D3DERR_INVALIDCALL;
	}
	*texture = NULL;
	return create_with_substitution(format, usage, [&](D3DFORMAT candidate, DWORD candidate_usage) {
		return device->CreateCubeTexture(dimension(edge_length), levels(mip_levels), candidate_usage, candidate,
			pool, texture, NULL);
	});
}

HRESULT WINAPI D3DXPortable_Create_Volume_Texture(LPDIRECT3DDEVICE9 device, UINT width, UINT height, UINT depth,
	UINT mip_levels, DWORD usage, D3DFORMAT format, D3DPOOL pool, LPDIRECT3DVOLUMETEXTURE9 * texture)
{
	if (device == NULL || texture == NULL) {
		return D3DERR_INVALIDCALL;
	}
	*texture = NULL;
	return create_with_substitution(format, usage, [&](D3DFORMAT candidate, DWORD candidate_usage) {
		return device->CreateVolumeTexture(dimension(width), dimension(height), dimension(depth),
			levels(mip_levels), candidate_usage, candidate, pool, texture, NULL);
	});
}

HRESULT WINAPI D3DXPortable_Load_Surface_From_Surface(LPDIRECT3DSURFACE9 destination,
	const PALETTEENTRY * destination_palette, const RECT * destination_rect, LPDIRECT3DSURFACE9 source,
	const PALETTEENTRY * source_palette, const RECT * source_rect, DWORD filter, D3DCOLOR colour_key)
{
	if (destination == NULL || source == NULL) {
		return D3DERR_INVALIDCALL;
	}
	if (destination_palette != NULL || source_palette != NULL || colour_key != 0) {
		return D3DERR_NOTAVAILABLE;
	}
	PosixFilter how;
	if (!filter_of(filter, POSIX_FILTER_BOX, how)) {		// D3DX's default is TRIANGLE, the same average
		return D3DERR_INVALIDCALL;
	}
	return copy_surface(destination, destination_rect, source, source_rect, how);
}

HRESULT WINAPI D3DXPortable_Filter_Texture(LPDIRECT3DBASETEXTURE9 texture, const PALETTEENTRY * palette,
	UINT source_level, DWORD filter)
{
	if (texture == NULL) {
		return D3DERR_INVALIDCALL;
	}
	PosixFilter how;
	if (palette != NULL || !filter_of(filter, POSIX_FILTER_BOX, how)) {
		return D3DERR_INVALIDCALL;
	}
	const UINT first = (source_level == D3DX_DEFAULT) ? 0 : source_level;
	const UINT count = texture->GetLevelCount();
	if (first >= count) {
		return D3DERR_INVALIDCALL;
	}
	const D3DRESOURCETYPE type = texture->GetType();
	if (type != D3DRTYPE_TEXTURE && type != D3DRTYPE_CUBETEXTURE) {
		return D3DERR_NOTAVAILABLE;		// a volume's levels: nothing in the game filters one
	}
	const UINT faces = (type == D3DRTYPE_CUBETEXTURE) ? 6 : 1;
	for (UINT face = 0; face < faces; ++face) {
		for (UINT level = first + 1; level < count; ++level) {
			Held<IDirect3DSurface9> above;
			Held<IDirect3DSurface9> below;
			HRESULT result;
			if (type == D3DRTYPE_TEXTURE) {
				IDirect3DTexture9 * plain = static_cast<IDirect3DTexture9 *>(texture);
				result = plain->GetSurfaceLevel(level - 1, &above.Pointer);
				if (SUCCEEDED(result)) {
					result = plain->GetSurfaceLevel(level, &below.Pointer);
				}
			}
			else {
				IDirect3DCubeTexture9 * cube = static_cast<IDirect3DCubeTexture9 *>(texture);
				result = cube->GetCubeMapSurface((D3DCUBEMAP_FACES)face, level - 1, &above.Pointer);
				if (SUCCEEDED(result)) {
					result = cube->GetCubeMapSurface((D3DCUBEMAP_FACES)face, level, &below.Pointer);
				}
			}
			if (SUCCEEDED(result)) {
				result = copy_surface(below.Pointer, NULL, above.Pointer, NULL, how);
			}
			if (FAILED(result)) {
				return result;
			}
		}
	}
	return D3D_OK;
}
