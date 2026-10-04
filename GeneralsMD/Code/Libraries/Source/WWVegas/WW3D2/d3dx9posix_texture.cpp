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

// Decision 7, phase A2: D3DX's texture helpers off Windows, which d3dx9posix.cpp's
// Bind_D3DX9_Runtime points D3DXCreateTexture and the rest at (d3dx9posix.h names them).
//
// They do what D3DX does, on the device's own resources (posixd3d9): creating textures through the
// device, with D3DX's substitution of a supported format for one the device refuses; making mip levels
// from a level above them (FilterTexture); and copying one surface into another with conversion and
// scaling (LoadSurfaceFromSurface).  The pixel work is PosixImageOps', so a copy of the same format at
// the same size moves the bytes, DXT included, and a conversion that would have to compress into DXT
// fails rather than write something else.  With a null device each fails with D3DERR_INVALIDCALL and a
// null texture, which is what -nodevice's sized textures need (TerrainTex.cpp skips a null one).
//
// Not done, because nothing calls them: loading an image file (every texture of the game comes through
// WW3D2's own loaders, and no _Create_DX8_Texture call passes a file name), filtering a volume
// texture's levels, palettes, and colour keys.  Each fails and says so.

#include "d3dx9runtime.h"
#include "d3dx9posix.h"

#include "PosixImageOps.h"
#include "PosixResources9.h"
#include "d3dx9texturerules.h"

#include <stdio.h>

using namespace D3DXTextureRules;

namespace {

void say_once(bool & said, const char * what)
{
	if (!said) {
		said = true;
		fprintf(stderr, "D3DX: %s is not done off Windows; the call fails\n", what);
	}
}

PosixImage & image_of(LPDIRECT3DSURFACE9 surface)
{
	return static_cast<PosixSurface9 *>(surface)->image();
}

/// Makes each level after `first` from the one above it.
RenderResult filter_chain(PosixImage * (*level_of)(void *, unsigned int), void * texture, unsigned int count,
	unsigned int first, PosixFilter filter)
{
	for (unsigned int level = first + 1; level < count; ++level) {
		PosixImage & above = *level_of(texture, level - 1);
		PosixImage & below = *level_of(texture, level);
		PosixRegion from, to;
		posixRegionOf(above, NULL, &from);
		posixRegionOf(below, NULL, &to);
		const RenderResult result = posixCopyImage(below, to, above, from, filter);
		if (result != D3D_OK) {
			return result;
		}
	}
	return D3D_OK;
}

PosixImage * texture_level(void * texture, unsigned int level)
{
	return &static_cast<PosixTexture9 *>(texture)->level(level);
}

} // namespace

RenderResult D3DX9Posix_Create_Texture(LPDIRECT3DDEVICE9 device, unsigned int width, unsigned int height,
	unsigned int mip_levels, RenderUInt32 usage, D3DFORMAT format, D3DPOOL pool, LPDIRECT3DTEXTURE9 * texture)
{
	if (texture == NULL) {
		return D3DERR_INVALIDCALL;
	}
	*texture = NULL;
	if (device == NULL) {
		return D3DERR_INVALIDCALL;
	}
	return create_with_substitution(format, usage, [&](D3DFORMAT candidate, RenderUInt32 candidate_usage) {
		return device->CreateTexture(dimension(width), dimension(height), levels(mip_levels), candidate_usage,
			candidate, pool, texture, NULL);
	});
}

RenderResult D3DX9Posix_Create_Cube_Texture(LPDIRECT3DDEVICE9 device, unsigned int edge_length,
	unsigned int mip_levels, RenderUInt32 usage, D3DFORMAT format, D3DPOOL pool, LPDIRECT3DCUBETEXTURE9 * texture)
{
	if (texture == NULL) {
		return D3DERR_INVALIDCALL;
	}
	*texture = NULL;
	if (device == NULL) {
		return D3DERR_INVALIDCALL;
	}
	return create_with_substitution(format, usage, [&](D3DFORMAT candidate, RenderUInt32 candidate_usage) {
		return device->CreateCubeTexture(dimension(edge_length), levels(mip_levels), candidate_usage, candidate,
			pool, texture, NULL);
	});
}

RenderResult D3DX9Posix_Create_Volume_Texture(LPDIRECT3DDEVICE9 device, unsigned int width, unsigned int height,
	unsigned int depth, unsigned int mip_levels, RenderUInt32 usage, D3DFORMAT format, D3DPOOL pool,
	LPDIRECT3DVOLUMETEXTURE9 * texture)
{
	if (texture == NULL) {
		return D3DERR_INVALIDCALL;
	}
	*texture = NULL;
	if (device == NULL) {
		return D3DERR_INVALIDCALL;
	}
	return create_with_substitution(format, usage, [&](D3DFORMAT candidate, RenderUInt32 candidate_usage) {
		return device->CreateVolumeTexture(dimension(width), dimension(height), dimension(depth),
			levels(mip_levels), candidate_usage, candidate, pool, texture, NULL);
	});
}

RenderResult D3DX9Posix_Create_Texture_From_File(LPDIRECT3DDEVICE9 device, const char *,
	unsigned int, unsigned int, unsigned int, RenderUInt32, D3DFORMAT, D3DPOOL, RenderUInt32,
	RenderUInt32, D3DCOLOR, D3DXIMAGE_INFO *, void *, LPDIRECT3DTEXTURE9 * texture)
{
	if (texture != NULL) {
		*texture = NULL;
	}
	if (device == NULL) {
		return D3DERR_INVALIDCALL;
	}
	static bool said = false;
	say_once(said, "D3DXCreateTextureFromFileExA (the game's textures load through WW3D2's own loaders)");
	return D3DERR_NOTAVAILABLE;
}

RenderResult D3DX9Posix_Filter_Texture(LPDIRECT3DBASETEXTURE9 texture, const void * palette,
	unsigned int source_level, RenderUInt32 filter)
{
	if (texture == NULL) {
		return D3DERR_INVALIDCALL;
	}
	PosixFilter how;
	if (palette != NULL || !filter_of(filter, POSIX_FILTER_BOX, how)) {
		return D3DERR_INVALIDCALL;
	}
	const unsigned int first = (source_level == D3DX_DEFAULT) ? 0 : source_level;
	const unsigned int count = texture->GetLevelCount();
	if (first >= count) {
		return D3DERR_INVALIDCALL;
	}
	switch (texture->GetType()) {
	case D3DRTYPE_TEXTURE:
		return filter_chain(texture_level, static_cast<PosixTexture9 *>(texture), count, first, how);
	case D3DRTYPE_CUBETEXTURE: {
		PosixCubeTexture9 * cube = static_cast<PosixCubeTexture9 *>(texture);
		for (unsigned int face = 0; face < 6; ++face) {
			for (unsigned int level = first + 1; level < count; ++level) {
				PosixImage & above = *cube->image(face, level - 1);
				PosixImage & below = *cube->image(face, level);
				PosixRegion from, to;
				posixRegionOf(above, NULL, &from);
				posixRegionOf(below, NULL, &to);
				const RenderResult result = posixCopyImage(below, to, above, from, how);
				if (result != D3D_OK) {
					return result;
				}
			}
		}
		return D3D_OK;
	}
	default: {
		static bool said = false;
		say_once(said, "D3DXFilterTexture on a volume texture");
		return D3DERR_NOTAVAILABLE;
	}
	}
}

RenderResult D3DX9Posix_Load_Surface_From_Surface(LPDIRECT3DSURFACE9 destination, const void * destination_palette,
	const RenderRect * destination_rect, LPDIRECT3DSURFACE9 source, const void * source_palette,
	const RenderRect * source_rect, RenderUInt32 filter, D3DCOLOR colour_key)
{
	if (destination == NULL || source == NULL) {
		return D3DERR_INVALIDCALL;
	}
	if (destination_palette != NULL || source_palette != NULL || colour_key != 0) {
		static bool said = false;
		say_once(said, "D3DXLoadSurfaceFromSurface with a palette or a colour key");
		return D3DERR_NOTAVAILABLE;
	}
	PosixFilter how;
	if (!filter_of(filter, POSIX_FILTER_BOX, how)) {		// D3DX's default is TRIANGLE, the same average
		return D3DERR_INVALIDCALL;
	}
	PosixImage & to = image_of(destination);
	PosixImage & from = image_of(source);
	PosixRegion to_region, from_region;
	if (!posixRegionOf(to, destination_rect, &to_region) || !posixRegionOf(from, source_rect, &from_region)) {
		return D3DERR_INVALIDCALL;
	}
	return posixCopyImage(to, to_region, from, from_region, how);
}
