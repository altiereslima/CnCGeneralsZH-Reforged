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
 * Decision 7's A2: the D3D9-shaped device's resources in memory (PosixDevice/Render/PosixResources9).
 * Formats' block layouts, mip chains, locks and their D3D9 rules, COM reference counting between a
 * texture and its surfaces, cube and volume textures, and buffers; and the pixel codec
 * (PosixPixelCodec) against hand-worked values of each format's bit layout and of DXT's blocks; and
 * the image operations (PosixImageOps): block copies, conversions, D3DX's filters, fills; and the device's
 * A2 half (PosixDevice9Resources, PosixD3D9Caps): a device made with no window, as -headless makes it,
 * its implicit surfaces, Clear read back, copies, and caps that claim exactly what the device and the
 * fixed-function generator do; and D3DX's texture helpers (WW3D2/d3dx9posix_texture.cpp).  Run under ASan where it can be,
 * since a reference count that is off shows up as a leak or a use after free, not as a wrong value.
 */
#include "test_harness.h"

#include "PosixD3D9Caps.h"
#include "PosixDevice9.h"
#include "PosixImageOps.h"
#include "d3dx9posix.h"
#include "ffshader.h"
#include "ffstate_values.h"
#include "PosixPixelCodec.h"
#include "PosixResources9.h"

#include <string.h>

namespace {

// A COM object's count, read without changing it.
template <class Object>
uint32_t references_of(Object *object)
{
	object->AddRef();
	return object->Release();
}

PosixFormatLayout layout_of(D3DFORMAT format)
{
	PosixFormatLayout layout = { 0, 0, 0 };
	posixFormatLayout(format, &layout);
	return layout;
}

} // namespace

TEST(posix_formats_have_d3d9_block_layouts)
{
	CHECK_EQ(layout_of(D3DFMT_A8R8G8B8).bytesPerBlock, 4u);
	CHECK_EQ(layout_of(D3DFMT_R8G8B8).bytesPerBlock, 3u);
	CHECK_EQ(layout_of(D3DFMT_R5G6B5).bytesPerBlock, 2u);
	CHECK_EQ(layout_of(D3DFMT_L8).bytesPerBlock, 1u);
	CHECK_EQ(layout_of(D3DFMT_DXT1).blockWidth, 4u);
	CHECK_EQ(layout_of(D3DFMT_DXT1).blockHeight, 4u);
	CHECK_EQ(layout_of(D3DFMT_DXT1).bytesPerBlock, 8u);
	CHECK_EQ(layout_of(D3DFMT_DXT3).bytesPerBlock, 16u);
	CHECK_EQ(layout_of(D3DFMT_DXT5).bytesPerBlock, 16u);
	CHECK_EQ(layout_of(D3DFMT_YUY2).blockWidth, 2u);
	CHECK_EQ(layout_of(D3DFMT_YUY2).bytesPerBlock, 4u);
	CHECK_EQ(layout_of(D3DFMT_D24S8).bytesPerBlock, 4u);
	CHECK_EQ(layout_of(D3DFMT_A32B32G32R32F).bytesPerBlock, 16u);
	CHECK(!posixFormatLayout(D3DFMT_UNKNOWN, NULL));
	CHECK(!posixFormatLayout(D3DFMT_VERTEXDATA, NULL));
}

TEST(posix_texture_full_chain_and_level_sizes)
{
	CHECK_EQ(posixFullChainLength(256, 64, 1), 9u);
	CHECK_EQ(posixFullChainLength(1, 1, 1), 1u);
	CHECK_EQ(posixFullChainLength(3, 1, 1), 2u);

	PosixTexture9 *texture = PosixTexture9::create(256, 64, 0, 0, D3DFMT_DXT1, D3DPOOL_MANAGED);
	CHECK(texture != NULL);
	if (texture == NULL) return;
	CHECK_EQ(texture->GetLevelCount(), 9u);
	D3DSURFACE_DESC desc;
	CHECK_EQ(texture->GetLevelDesc(2, &desc), D3D_OK);
	CHECK_EQ(desc.Width, 64u);
	CHECK_EQ(desc.Height, 16u);
	CHECK_EQ(desc.Format, D3DFMT_DXT1);
	CHECK_EQ(desc.Pool, D3DPOOL_MANAGED);
	CHECK_EQ(texture->GetLevelDesc(8, &desc), D3D_OK);
	CHECK_EQ(desc.Width, 1u);
	CHECK_EQ(desc.Height, 1u);
	CHECK_EQ(texture->GetLevelDesc(9, &desc), D3DERR_INVALIDCALL);
	// DXT pitch is one row of 4x4 blocks: 256 / 4 * 8, and a 1x1 level is still one whole block.
	CHECK_EQ(texture->level(0).rowPitch(), 512u);
	CHECK_EQ(texture->level(8).rowPitch(), 8u);
	CHECK_EQ(texture->level(8).size(), (size_t)8);
	CHECK_EQ(texture->Release(), 0u);

	PosixTexture9 *two = PosixTexture9::create(64, 64, 2, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED);
	CHECK_EQ(two->GetLevelCount(), 2u);
	two->Release();
	PosixTexture9 *autogen = PosixTexture9::create(64, 64, 0, D3DUSAGE_AUTOGENMIPMAP, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT);
	CHECK_EQ(autogen->GetLevelCount(), 1u);
	autogen->Release();
	CHECK(PosixTexture9::create(64, 64, 1, 0, D3DFMT_UNKNOWN, D3DPOOL_MANAGED) == NULL);
	CHECK(PosixTexture9::create(0, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED) == NULL);
}

TEST(posix_texture_locks_write_and_read_back)
{
	PosixTexture9 *texture = PosixTexture9::create(8, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED);
	D3DLOCKED_RECT locked;
	CHECK_EQ(texture->LockRect(0, &locked, NULL, 0), D3D_OK);
	CHECK_EQ(locked.Pitch, 32);
	for (int y = 0; y < 4; ++y)
		for (int x = 0; x < 8; ++x)
			((uint32_t *)((uint8_t *)locked.pBits + y * locked.Pitch))[x] = 0xff000000u | (uint32_t)(y * 8 + x);
	// locked twice, and unlocked twice: both are errors
	D3DLOCKED_RECT again;
	CHECK_EQ(texture->LockRect(0, &again, NULL, 0), D3DERR_INVALIDCALL);
	CHECK_EQ(texture->UnlockRect(0), D3D_OK);
	CHECK_EQ(texture->UnlockRect(0), D3DERR_INVALIDCALL);
	CHECK_EQ(texture->level(0).version(), 1u);

	// a sub-rectangle, read-only: the right address, and no write counted
	RenderRect rect = { 2, 1, 5, 3 };
	CHECK_EQ(texture->LockRect(0, &locked, &rect, D3DLOCK_READONLY), D3D_OK);
	CHECK_EQ(((uint32_t *)locked.pBits)[0], 0xff000000u | 10u);
	CHECK_EQ(((uint32_t *)((uint8_t *)locked.pBits + locked.Pitch))[2], 0xff000000u | 20u);
	CHECK_EQ(texture->UnlockRect(0), D3D_OK);
	CHECK_EQ(texture->level(0).version(), 1u);

	RenderRect outside = { 0, 0, 9, 4 };
	CHECK_EQ(texture->LockRect(0, &locked, &outside, 0), D3DERR_INVALIDCALL);
	CHECK(locked.pBits == NULL);
	RenderRect empty = { 3, 1, 3, 2 };
	CHECK_EQ(texture->LockRect(0, &locked, &empty, 0), D3DERR_INVALIDCALL);
	texture->Release();
}

TEST(posix_block_format_locks_are_on_block_boundaries)
{
	PosixTexture9 *texture = PosixTexture9::create(16, 16, 1, 0, D3DFMT_DXT5, D3DPOOL_MANAGED);
	D3DLOCKED_RECT locked;
	CHECK_EQ(texture->LockRect(0, &locked, NULL, 0), D3D_OK);
	uint8_t *base = (uint8_t *)locked.pBits;
	CHECK_EQ(locked.Pitch, 64);
	texture->UnlockRect(0);

	RenderRect aligned = { 4, 8, 12, 16 };
	CHECK_EQ(texture->LockRect(0, &locked, &aligned, 0), D3D_OK);
	CHECK(locked.pBits == base + 2 * 64 + 1 * 16);
	texture->UnlockRect(0);
	RenderRect misaligned = { 1, 0, 4, 4 };
	CHECK_EQ(texture->LockRect(0, &locked, &misaligned, 0), D3DERR_INVALIDCALL);
	texture->Release();

	// a 6x6 DXT1 image: blocks cover 8x8, and a rectangle may end at the image's edge
	PosixTexture9 *odd = PosixTexture9::create(6, 6, 1, 0, D3DFMT_DXT1, D3DPOOL_MANAGED);
	CHECK_EQ(odd->level(0).size(), (size_t)(2 * 2 * 8));
	RenderRect edge = { 4, 4, 6, 6 };
	CHECK_EQ(odd->LockRect(0, &locked, &edge, 0), D3D_OK);
	odd->UnlockRect(0);
	RenderRect short_of_edge = { 0, 0, 5, 4 };
	CHECK_EQ(odd->LockRect(0, &locked, &short_of_edge, 0), D3DERR_INVALIDCALL);
	odd->Release();
}

TEST(posix_texture_surfaces_count_on_the_texture)
{
	PosixTexture9 *texture = PosixTexture9::create(32, 32, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED);
	CHECK_EQ(references_of(texture), 1u);
	IDirect3DSurface9 *surface = NULL;
	CHECK_EQ(texture->GetSurfaceLevel(1, &surface), D3D_OK);
	CHECK(surface != NULL);
	CHECK_EQ(references_of(texture), 2u);
	IDirect3DSurface9 *same = NULL;
	texture->GetSurfaceLevel(1, &same);
	CHECK(same == surface);
	CHECK_EQ(references_of(texture), 3u);
	same->Release();

	D3DSURFACE_DESC desc;
	CHECK_EQ(surface->GetDesc(&desc), D3D_OK);
	CHECK_EQ(desc.Width, 16u);
	CHECK_EQ(desc.Type, D3DRTYPE_SURFACE);

	void *container = NULL;
	CHECK_EQ(surface->GetContainer(IID_IDirect3DTexture9, &container), D3D_OK);
	CHECK(container == (IDirect3DTexture9 *)texture);
	CHECK_EQ(references_of(texture), 3u);
	((IDirect3DTexture9 *)container)->Release();
	CHECK_EQ(surface->GetContainer(IID_IDirect3DCubeTexture9, &container), POSIX_D3D_NOINTERFACE);
	CHECK(container == NULL);

	// the surface keeps its texture alive: drop the texture's own reference, write through the surface
	CHECK_EQ(texture->Release(), 1u);
	D3DLOCKED_RECT locked;
	CHECK_EQ(surface->LockRect(&locked, NULL, 0), D3D_OK);
	memset(locked.pBits, 0xab, 16 * 4);
	CHECK_EQ(surface->UnlockRect(), D3D_OK);
	CHECK_EQ(surface->Release(), 0u);		// the texture goes with it (ASan says if it does not)
}

TEST(posix_standalone_surfaces_count_themselves)
{
	PosixSurface9 *surface = PosixSurface9::createStandalone(100, 100, D3DFMT_X8R8G8B8, D3DUSAGE_RENDERTARGET,
		D3DPOOL_DEFAULT, D3DMULTISAMPLE_NONE, 0);
	CHECK(surface != NULL);
	D3DSURFACE_DESC desc;
	surface->GetDesc(&desc);
	CHECK_EQ(desc.Usage, (RenderUInt32)D3DUSAGE_RENDERTARGET);
	CHECK_EQ(desc.Pool, D3DPOOL_DEFAULT);
	void *container = (void *)1;
	CHECK_EQ(surface->GetContainer(IID_IDirect3DTexture9, &container), POSIX_D3D_NOINTERFACE);
	CHECK(container == NULL);
	CHECK_EQ(surface->AddRef(), 2u);
	CHECK_EQ(surface->Release(), 1u);
	CHECK_EQ(surface->Release(), 0u);
	CHECK(PosixSurface9::createStandalone(16, 16, D3DFMT_UNKNOWN, 0, D3DPOOL_DEFAULT, D3DMULTISAMPLE_NONE, 0) == NULL);
}

TEST(posix_cube_textures_have_six_separate_faces)
{
	PosixCubeTexture9 *cube = PosixCubeTexture9::create(16, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED);
	CHECK(cube != NULL);
	CHECK_EQ(cube->GetLevelCount(), 5u);
	D3DLOCKED_RECT locked;
	for (int face = 0; face < 6; ++face)
	{
		CHECK_EQ(cube->LockRect((D3DCUBEMAP_FACES)face, 0, &locked, NULL, 0), D3D_OK);
		((uint32_t *)locked.pBits)[0] = 0x100u + (uint32_t)face;
		cube->UnlockRect((D3DCUBEMAP_FACES)face, 0);
	}
	for (int face = 0; face < 6; ++face)
		CHECK_EQ(((const uint32_t *)cube->image(face, 0)->bytes())[0], 0x100u + (uint32_t)face);
	CHECK_EQ(cube->LockRect((D3DCUBEMAP_FACES)6, 0, &locked, NULL, 0), D3DERR_INVALIDCALL);

	IDirect3DSurface9 *surface = NULL;
	CHECK_EQ(cube->GetCubeMapSurface(D3DCUBEMAP_FACE_NEGATIVE_Y, 2, &surface), D3D_OK);
	void *container = NULL;
	CHECK_EQ(surface->GetContainer(IID_IDirect3DCubeTexture9, &container), D3D_OK);
	CHECK(container == (IDirect3DCubeTexture9 *)cube);
	((IDirect3DCubeTexture9 *)container)->Release();
	CHECK_EQ(surface->GetContainer(IID_IDirect3DTexture9, &container), POSIX_D3D_NOINTERFACE);
	surface->Release();
	CHECK_EQ(cube->Release(), 0u);
}

TEST(posix_volume_textures_lock_boxes)
{
	PosixVolumeTexture9 *volume = PosixVolumeTexture9::create(32, 16, 8, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED);
	CHECK(volume != NULL);
	CHECK_EQ(volume->GetLevelCount(), 6u);
	D3DVOLUME_DESC desc;
	CHECK_EQ(volume->GetLevelDesc(1, &desc), D3D_OK);
	CHECK_EQ(desc.Width, 16u);
	CHECK_EQ(desc.Height, 8u);
	CHECK_EQ(desc.Depth, 4u);
	D3DLOCKED_BOX locked;
	D3DBOX box = { 1, 2, 3, 4, 5, 6 };		// Left, Top, Right, Bottom, Front, Back
	CHECK_EQ(volume->LockBox(0, &locked, &box, 0), D3D_OK);
	CHECK_EQ(locked.RowPitch, 128);
	CHECK_EQ(locked.SlicePitch, 128 * 16);
	CHECK(locked.pBits == volume->level(0).bytes() + 5 * 128 * 16 + 2 * 128 + 1 * 4);
	CHECK_EQ(volume->UnlockBox(0), D3D_OK);
	IDirect3DVolume9 *level = NULL;
	CHECK_EQ(volume->GetVolumeLevel(5, &level), D3D_OK);
	CHECK_EQ(level->GetDesc(&desc), D3D_OK);
	CHECK_EQ(desc.Width, 1u);
	level->Release();
	CHECK_EQ(volume->Release(), 0u);
}

TEST(posix_buffers_lock_ranges_as_d3d9_does)
{
	PosixVertexBuffer9 *vertices = PosixVertexBuffer9::create(1024, D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC, D3DFVF_XYZ, D3DPOOL_DEFAULT);
	CHECK(vertices != NULL);
	void *data = NULL;
	CHECK_EQ(vertices->Lock(0, 0, &data, D3DLOCK_DISCARD), D3D_OK);		// 0, 0: the whole buffer
	CHECK(data == vertices->storage().bytes());
	void *more = NULL;
	CHECK_EQ(vertices->Lock(1000, 24, &more, D3DLOCK_NOOVERWRITE), D3D_OK);	// a second lock is allowed
	CHECK(more == vertices->storage().bytes() + 1000);
	CHECK_EQ(vertices->Lock(1000, 25, &more, 0), D3DERR_INVALIDCALL);
	CHECK(more == NULL);
	CHECK_EQ(vertices->Unlock(), D3D_OK);
	CHECK_EQ(vertices->Unlock(), D3D_OK);
	CHECK_EQ(vertices->Unlock(), D3DERR_INVALIDCALL);
	D3DVERTEXBUFFER_DESC vdesc;
	vertices->GetDesc(&vdesc);
	CHECK_EQ(vdesc.Size, 1024u);
	CHECK_EQ(vdesc.FVF, (RenderUInt32)D3DFVF_XYZ);
	CHECK_EQ(vdesc.Format, D3DFMT_VERTEXDATA);
	CHECK_EQ(vertices->Release(), 0u);

	PosixIndexBuffer9 *indices = PosixIndexBuffer9::create(600, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED);
	D3DINDEXBUFFER_DESC idesc;
	indices->GetDesc(&idesc);
	CHECK_EQ(idesc.Format, D3DFMT_INDEX16);
	CHECK_EQ(idesc.Size, 600u);
	indices->Release();
	CHECK(PosixIndexBuffer9::create(600, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED) == NULL);
	CHECK(PosixVertexBuffer9::create(0, 0, 0, D3DPOOL_MANAGED) == NULL);
}

namespace {

bool near(float a, float b) { return a > b - 0.002f && a < b + 0.002f; }

PosixColor decode1(D3DFORMAT format, const uint8_t *bytes)
{
	PosixColor color = { -1, -1, -1, -1 };
	posixDecodeBlock(format, bytes, &color);
	return color;
}

} // namespace

TEST(posix_codec_reads_packed_formats_as_d3d9_lays_them_out)
{
	const uint8_t argb[4] = { 0x00, 0x40, 0xff, 0x80 };		// 0x80FF4000, little-endian
	PosixColor c = decode1(D3DFMT_A8R8G8B8, argb);
	CHECK(near(c.a, 128 / 255.0f) && near(c.r, 1) && near(c.g, 64 / 255.0f) && near(c.b, 0));
	c = decode1(D3DFMT_X8R8G8B8, argb);
	CHECK(near(c.a, 1));
	const uint8_t rgb[3] = { 0x10, 0x20, 0x30 };					// R8G8B8 is B, G, R in memory
	c = decode1(D3DFMT_R8G8B8, rgb);
	CHECK(near(c.b, 0x10 / 255.0f) && near(c.g, 0x20 / 255.0f) && near(c.r, 0x30 / 255.0f));
	const uint8_t red565[2] = { 0x00, 0xf8 }, green565[2] = { 0xe0, 0x07 };
	c = decode1(D3DFMT_R5G6B5, red565);
	CHECK(near(c.r, 1) && near(c.g, 0) && near(c.b, 0) && near(c.a, 1));
	c = decode1(D3DFMT_R5G6B5, green565);
	CHECK(near(c.r, 0) && near(c.g, 1) && near(c.b, 0));
	const uint8_t blueOpaque1555[2] = { 0x1f, 0x80 };
	c = decode1(D3DFMT_A1R5G5B5, blueOpaque1555);
	CHECK(near(c.a, 1) && near(c.b, 1) && near(c.r, 0));
	const uint8_t argb4444[2] = { 0x3c, 0xa5 };						// A=a R=5 G=3 B=c
	c = decode1(D3DFMT_A4R4G4B4, argb4444);
	CHECK(near(c.a, 10 / 15.0f) && near(c.r, 5 / 15.0f) && near(c.g, 3 / 15.0f) && near(c.b, 12 / 15.0f));
	const uint8_t l8 = 0x80;
	c = decode1(D3DFMT_L8, &l8);
	CHECK(near(c.r, 128 / 255.0f) && near(c.g, c.r) && near(c.b, c.r) && near(c.a, 1));
	const uint8_t a8l8[2] = { 0xc0, 0x40 };								// low byte L, high byte A
	c = decode1(D3DFMT_A8L8, a8l8);
	CHECK(near(c.r, 0xc0 / 255.0f) && near(c.a, 0x40 / 255.0f));
	const uint8_t a8 = 0x33;
	c = decode1(D3DFMT_A8, &a8);
	CHECK(near(c.a, 0x33 / 255.0f) && near(c.r, 0));
}

TEST(posix_codec_writes_what_it_reads)
{
	const D3DFORMAT formats[] = { D3DFMT_A8R8G8B8, D3DFMT_X8R8G8B8, D3DFMT_R8G8B8, D3DFMT_R5G6B5,
		D3DFMT_A1R5G5B5, D3DFMT_A4R4G4B4, D3DFMT_A8L8, D3DFMT_L8, D3DFMT_A8 };
	for (size_t f = 0; f < sizeof(formats) / sizeof(formats[0]); ++f)
	{
		PosixFormatLayout layout;
		posixFormatLayout(formats[f], &layout);
		for (unsigned int v = 0; v < 256; v += 17)
		{
			uint8_t in[4] = { (uint8_t)v, (uint8_t)(255 - v), (uint8_t)(v * 7), (uint8_t)(v ^ 0x5a) };
			if (formats[f] == D3DFMT_X8R8G8B8) in[3] = 0xff;
			PosixColor c;
			CHECK(posixDecodeBlock(formats[f], in, &c));
			uint8_t out[4] = { 0, 0, 0, 0 };
			CHECK(posixEncodePixel(formats[f], c, out));
			if (formats[f] == D3DFMT_A8L8 || formats[f] == D3DFMT_L8)
				CHECK_EQ(out[0], in[0]);		// L decodes to r = g = b, and their luma is L again
			else
				CHECK_MEM(out, in, layout.bytesPerBlock);
		}
	}
	CHECK(!posixCanEncode(D3DFMT_DXT1));
	CHECK(posixCanDecode(D3DFMT_DXT5));
	CHECK(!posixCanDecode(D3DFMT_P8));
}

TEST(posix_codec_decodes_dxt_blocks)
{
	// DXT1, four-colour mode (c0 > c1): red and blue end points, indices 0, 1, 2, 3 across row 0.
	uint8_t dxt1[8] = { 0x00, 0xf8, 0x1f, 0x00, 0xe4, 0x00, 0x00, 0x00 };	// indices row 0: 0,1,2,3
	PosixColor px[16];
	CHECK(posixDecodeBlock(D3DFMT_DXT1, dxt1, px));
	CHECK(near(px[0].r, 1) && near(px[0].b, 0) && near(px[0].a, 1));
	CHECK(near(px[1].r, 0) && near(px[1].b, 1));
	CHECK(near(px[2].r, 2 / 3.0f) && near(px[2].b, 1 / 3.0f));
	CHECK(near(px[3].r, 1 / 3.0f) && near(px[3].b, 2 / 3.0f));
	CHECK(near(px[4].r, 1));																		// row 1 is all index 0
	// DXT1, three-colour mode (c0 <= c1): index 2 the midpoint, index 3 transparent black.
	uint8_t dxt1t[8] = { 0x1f, 0x00, 0x00, 0xf8, 0xe4, 0x00, 0x00, 0x00 };
	CHECK(posixDecodeBlock(D3DFMT_DXT1, dxt1t, px));
	CHECK(near(px[2].r, 0.5f) && near(px[2].b, 0.5f) && near(px[2].a, 1));
	CHECK(near(px[3].r, 0) && near(px[3].a, 0));
	// DXT3: explicit 4-bit alpha, low nibble first; the colour block is always four-colour.
	uint8_t dxt3[16] = { 0xf0, 0x00, 0, 0, 0, 0, 0, 0,   0x1f, 0x00, 0x00, 0xf8, 0x00, 0x00, 0x00, 0x00 };
	CHECK(posixDecodeBlock(D3DFMT_DXT3, dxt3, px));
	CHECK(near(px[0].a, 0) && near(px[1].a, 1) && near(px[2].a, 0));
	CHECK(near(px[0].b, 1) && near(px[0].r, 0));		// four-colour even though c0 < c1
	// DXT5: a0 = 255, a1 = 0 (eight-alpha mode); pixel 0 index 0, pixel 1 index 1, pixel 2 index 2.
	uint8_t dxt5[16] = { 0xff, 0x00, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0xf8, 0x00, 0xf8, 0, 0, 0, 0 };
	CHECK(posixDecodeBlock(D3DFMT_DXT5, dxt5, px));
	CHECK(near(px[0].a, 1) && near(px[1].a, 0) && near(px[2].a, 6 / 7.0f));
	// DXT5 six-alpha mode (a0 <= a1): index 6 is 0, index 7 is 1.
	uint8_t dxt5b[16] = { 0x00, 0xff, 0xbe, 0x00, 0x00, 0x00, 0x00, 0x00,   0x00, 0xf8, 0x00, 0xf8, 0, 0, 0, 0 };
	CHECK(posixDecodeBlock(D3DFMT_DXT5, dxt5b, px));
	CHECK(near(px[0].a, 0) && near(px[1].a, 1));
}

TEST(posix_codec_writes_depth_and_stencil)
{
	uint8_t out[4];
	CHECK(posixEncodeDepth(D3DFMT_D24S8, 1.0f, 5, out));
	CHECK_EQ((uint32_t)out[0] | ((uint32_t)out[1] << 8) | ((uint32_t)out[2] << 16) | ((uint32_t)out[3] << 24), 0xffffff05u);
	CHECK(posixEncodeDepth(D3DFMT_D16, 0.0f, 0, out));
	CHECK(out[0] == 0 && out[1] == 0);
	CHECK(posixEncodeDepth(D3DFMT_D16, 1.0f, 0, out));
	CHECK(out[0] == 0xff && out[1] == 0xff);
	CHECK(!posixEncodeDepth(D3DFMT_A8R8G8B8, 1.0f, 0, out));
	const PosixColor c = posixColorFromD3DColor(0x80ff4000u);
	CHECK(near(c.a, 128 / 255.0f) && near(c.r, 1) && near(c.g, 64 / 255.0f) && near(c.b, 0));
}

namespace {

PosixImage image_of(D3DFORMAT format, unsigned int width, unsigned int height)
{
	PosixImage image;
	image.create(format, width, height, 1);
	return image;
}

uint32_t argb_at(const PosixImage &image, unsigned int x, unsigned int y)
{
	const uint8_t *p = image.bytes() + (size_t)y * image.rowPitch() + x * 4;
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void set_argb(PosixImage &image, unsigned int x, unsigned int y, uint32_t value)
{
	uint8_t *p = image.bytes() + (size_t)y * image.rowPitch() + x * 4;
	p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8); p[2] = (uint8_t)(value >> 16); p[3] = (uint8_t)(value >> 24);
}

} // namespace

TEST(posix_copy_moves_dxt_blocks_unchanged)
{
	PosixImage source = image_of(D3DFMT_DXT1, 16, 16), dest = image_of(D3DFMT_DXT1, 16, 16);
	for (size_t i = 0; i < source.size(); ++i) source.bytes()[i] = (uint8_t)(i * 31 + 7);
	const PosixRegion from = { 4, 4, 12, 12 }, to = { 0, 8, 8, 16 };
	CHECK_EQ(posixCopyImage(dest, to, source, from, POSIX_FILTER_NONE), D3D_OK);
	for (unsigned int row = 0; row < 2; ++row)		// two rows of two blocks, eight bytes each
		CHECK_MEM(dest.bytes() + (2 + row) * dest.rowPitch(), source.bytes() + (1 + row) * source.rowPitch() + 8, 16);
	CHECK_EQ(dest.bytes()[0], 0);									// what was not copied to is untouched
	CHECK_EQ(dest.version(), 1u);
	// a region DXT cannot address, and a conversion into DXT, are refused
	const PosixRegion off_block = { 1, 0, 9, 8 };
	CHECK_EQ(posixCopyImage(dest, to, source, off_block, POSIX_FILTER_NONE), D3DERR_INVALIDCALL);
	PosixImage argb = image_of(D3DFMT_A8R8G8B8, 8, 8);
	const PosixRegion whole8 = { 0, 0, 8, 8 };
	CHECK_EQ(posixCopyImage(dest, to, argb, whole8, POSIX_FILTER_NONE), D3DERR_INVALIDCALL);
}

TEST(posix_copy_converts_between_formats)
{
	// DXT1 decoded into A8R8G8B8: one red block
	PosixImage dxt = image_of(D3DFMT_DXT1, 4, 4), argb = image_of(D3DFMT_A8R8G8B8, 4, 4);
	const uint8_t red_block[8] = { 0x00, 0xf8, 0x00, 0xf8, 0, 0, 0, 0 };
	memcpy(dxt.bytes(), red_block, 8);
	const PosixRegion whole = { 0, 0, 4, 4 };
	CHECK_EQ(posixCopyImage(argb, whole, dxt, whole, POSIX_FILTER_NONE), D3D_OK);
	CHECK_EQ(argb_at(argb, 3, 3), 0xffff0000u);
	// A8R8G8B8 into R5G6B5: 0xff00ff00 is pure green, 0x07e0
	set_argb(argb, 0, 0, 0xff00ff00u);
	PosixImage rgb565 = image_of(D3DFMT_R5G6B5, 4, 4);
	CHECK_EQ(posixCopyImage(rgb565, whole, argb, whole, POSIX_FILTER_POINT), D3D_OK);
	CHECK_EQ((uint32_t)rgb565.bytes()[0] | ((uint32_t)rgb565.bytes()[1] << 8), 0x07e0u);
	CHECK_EQ((uint32_t)rgb565.bytes()[2] | ((uint32_t)rgb565.bytes()[3] << 8), 0xf800u);
}

TEST(posix_copy_scales_with_d3dx_filters)
{
	PosixImage two = image_of(D3DFMT_A8R8G8B8, 2, 2);
	set_argb(two, 0, 0, 0xff000000u); set_argb(two, 1, 0, 0xff00007fu);
	set_argb(two, 0, 1, 0xff0000ffu); set_argb(two, 1, 1, 0xff00003fu);
	PosixImage one = image_of(D3DFMT_A8R8G8B8, 1, 1);
	const PosixRegion two_whole = { 0, 0, 2, 2 }, one_whole = { 0, 0, 1, 1 };
	CHECK_EQ(posixCopyImage(one, one_whole, two, two_whole, POSIX_FILTER_BOX), D3D_OK);
	// blue channel: (0 + 127 + 255 + 63) / 4 = 111.25, written as 111 (POINT would give 63)
	CHECK_EQ(argb_at(one, 0, 0), 0xff00006fu);

	// NONE into a larger destination: what the source does not cover is transparent black
	PosixImage four = image_of(D3DFMT_A8R8G8B8, 4, 4);
	for (unsigned int y = 0; y < 4; ++y) for (unsigned int x = 0; x < 4; ++x) set_argb(four, x, y, 0x12345678u);
	const PosixRegion four_whole = { 0, 0, 4, 4 };
	CHECK_EQ(posixCopyImage(four, four_whole, two, two_whole, POSIX_FILTER_NONE), D3D_OK);
	CHECK_EQ(argb_at(four, 1, 1), 0xff00003fu);
	CHECK_EQ(argb_at(four, 3, 3), 0x00000000u);

	// POINT doubling: each source pixel becomes a 2x2 square
	CHECK_EQ(posixCopyImage(four, four_whole, two, two_whole, POSIX_FILTER_POINT), D3D_OK);
	CHECK_EQ(argb_at(four, 0, 3), 0xff0000ffu);
	CHECK_EQ(argb_at(four, 3, 0), 0xff00007fu);

	// LINEAR across a 2-pixel gradient into 4: the inner pixels blend
	PosixImage ramp = image_of(D3DFMT_A8R8G8B8, 2, 1), wide = image_of(D3DFMT_A8R8G8B8, 4, 1);
	set_argb(ramp, 0, 0, 0xff000000u); set_argb(ramp, 1, 0, 0xff0000ffu);
	const PosixRegion ramp_whole = { 0, 0, 2, 1 }, wide_whole = { 0, 0, 4, 1 };
	CHECK_EQ(posixCopyImage(wide, wide_whole, ramp, ramp_whole, POSIX_FILTER_LINEAR), D3D_OK);
	CHECK_EQ(argb_at(wide, 0, 0), 0xff000000u);
	CHECK_EQ(argb_at(wide, 1, 0) & 0xff, 0x40u);		// u = 0.25: a quarter of the way
	CHECK_EQ(argb_at(wide, 2, 0) & 0xff, 0xbfu);		// u = 0.75
	CHECK_EQ(argb_at(wide, 3, 0), 0xff0000ffu);
}

TEST(posix_fills_colour_and_depth)
{
	PosixImage target = image_of(D3DFMT_X8R8G8B8, 8, 8);
	const PosixRegion part = { 2, 2, 4, 4 };
	CHECK_EQ(posixFillImage(target, part, posixColorFromD3DColor(0x00336699u)), D3D_OK);
	CHECK_EQ(argb_at(target, 2, 2), 0xff336699u);		// X8: the unused byte written as 0xff
	CHECK_EQ(argb_at(target, 1, 1), 0u);
	PosixImage dxt = image_of(D3DFMT_DXT1, 8, 8);
	CHECK_EQ(posixFillImage(dxt, part, posixColorFromD3DColor(0)), D3DERR_INVALIDCALL);

	PosixImage depth = image_of(D3DFMT_D24S8, 4, 4);
	const PosixRegion all = { 0, 0, 4, 4 };
	CHECK_EQ(posixFillDepth(depth, all, D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 1.0f, 3), D3D_OK);
	CHECK_EQ(argb_at(depth, 0, 0), 0xffffff03u);
	CHECK_EQ(posixFillDepth(depth, all, D3DCLEAR_STENCIL, 0.0f, 9), D3D_OK);	// the depth stays
	CHECK_EQ(argb_at(depth, 3, 3), 0xffffff09u);
	CHECK_EQ(posixFillDepth(depth, all, D3DCLEAR_ZBUFFER, 0.0f, 0), D3D_OK);		// the stencil stays
	CHECK_EQ(argb_at(depth, 3, 3), 0x00000009u);
	CHECK_EQ(posixFillDepth(target, all, D3DCLEAR_ZBUFFER, 0.0f, 0), D3DERR_INVALIDCALL);
}

TEST(posix_gpu_read_backs_land_in_the_targets_format)
{
	// Three pixels by two rows of B8G8R8A8, as the GPU hands them back: rows packed, top first.
	const uint8_t bgra[3 * 2 * 4] = {
		0x00, 0x80, 0xff, 0x40,		0x11, 0x22, 0x33, 0x44,		0xff, 0xff, 0xff, 0x00,
		0x01, 0x02, 0x03, 0x04,		0x00, 0x00, 0x00, 0xff,		0x10, 0x20, 0x30, 0x80,
	};

	PosixImage argb = image_of(D3DFMT_A8R8G8B8, 3, 2);
	const uint32_t version = argb.version();
	CHECK(posixWriteFromBgra(argb, bgra, 3, 2));
	CHECK_EQ(argb_at(argb, 0, 0), 0x40ff8000u);
	CHECK_EQ(argb_at(argb, 2, 1), 0x80302010u);
	CHECK_EQ(argb.version(), version);		// the GPU's copy is the newer one: nothing to upload

	PosixImage xrgb = image_of(D3DFMT_X8R8G8B8, 3, 2);
	CHECK(posixWriteFromBgra(xrgb, bgra, 3, 2));
	CHECK_EQ(argb_at(xrgb, 2, 0), 0xffffffffu);
	CHECK_EQ(argb_at(xrgb, 0, 1), 0xff030201u);		// X8: the unused byte is 0xff whatever the GPU had

	PosixImage rgb565 = image_of(D3DFMT_R5G6B5, 3, 2);
	CHECK(posixWriteFromBgra(rgb565, bgra, 3, 2));
	const uint8_t *first = rgb565.bytes();
	CHECK_EQ((unsigned)(first[0] | (first[1] << 8)), 0xfc00u);		// red 255, green 128 (32 of 63), blue 0
	const uint8_t *last = rgb565.bytes() + rgb565.rowPitch() + 2 * 2;
	CHECK_EQ((unsigned)(last[0] | (last[1] << 8)), 0x3102u);		// 0x30, 0x20, 0x10 to 6, 8, 2

	CHECK(!posixWriteFromBgra(argb, bgra, 2, 3));		// another size
	PosixImage dxt = image_of(D3DFMT_DXT1, 4, 4);
	CHECK(!posixWriteFromBgra(dxt, bgra, 4, 4));
	PosixImage depth = image_of(D3DFMT_D24S8, 3, 2);
	CHECK(!posixWriteFromBgra(depth, bgra, 3, 2));
	PosixImage a1 = image_of(D3DFMT_A1R5G5B5, 3, 2);
	CHECK(!posixWriteFromBgra(a1, bgra, 3, 2));		// not a format the engine renders to
}

namespace {

D3DPRESENT_PARAMETERS headless_parameters()
{
	D3DPRESENT_PARAMETERS parameters;
	memset(&parameters, 0, sizeof(parameters));
	parameters.BackBufferWidth = 100;			// what -headless asks for (CommandLine.cpp's HEADLESS_RESOLUTION)
	parameters.BackBufferHeight = 100;
	parameters.BackBufferFormat = D3DFMT_X8R8G8B8;
	parameters.BackBufferCount = 1;
	parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
	parameters.Windowed = 1;
	parameters.EnableAutoDepthStencil = 1;
	parameters.AutoDepthStencilFormat = D3DFMT_D24S8;
	return parameters;
}

// A device with no window, as -headless makes it; NULL (and a failed check) if it cannot be made.
IDirect3DDevice9 *headless_device(PosixDirect3D9 **adapter_out)
{
	PosixDirect3D9 *adapter = new PosixDirect3D9;
	D3DPRESENT_PARAMETERS parameters = headless_parameters();
	IDirect3DDevice9 *device = NULL;
	const RenderResult result = adapter->CreateDevice(0, D3DDEVTYPE_HAL, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING,
		&parameters, &device);
	CHECK_EQ(result, D3D_OK);
	*adapter_out = adapter;
	return device;
}

uint32_t pixel_of(IDirect3DSurface9 *surface, unsigned int x, unsigned int y)
{
	D3DLOCKED_RECT locked;
	if (surface->LockRect(&locked, NULL, D3DLOCK_READONLY) != D3D_OK) return 0xdeadbeefu;
	const uint32_t value = ((const uint32_t *)((const uint8_t *)locked.pBits + y * locked.Pitch))[x];
	surface->UnlockRect();
	return value;
}

} // namespace

TEST(posix_device_without_a_window_has_its_implicit_surfaces)
{
	PosixDirect3D9 *adapter = NULL;
	IDirect3DDevice9 *device = headless_device(&adapter);
	if (device == NULL) { adapter->Release(); return; }

	IDirect3DSurface9 *back = NULL, *target = NULL, *depth = NULL;
	CHECK_EQ(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back), D3D_OK);
	CHECK_EQ(device->GetRenderTarget(0, &target), D3D_OK);
	CHECK(back == target);															// render target 0 is the back buffer
	CHECK_EQ(device->GetDepthStencilSurface(&depth), D3D_OK);
	D3DSURFACE_DESC desc;
	back->GetDesc(&desc);
	CHECK_EQ(desc.Width, 100u);
	CHECK_EQ(desc.Format, D3DFMT_X8R8G8B8);
	CHECK_EQ(desc.Usage, (RenderUInt32)D3DUSAGE_RENDERTARGET);
	depth->GetDesc(&desc);
	CHECK_EQ(desc.Format, D3DFMT_D24S8);
	IDirect3DSurface9 *none = (IDirect3DSurface9 *)1;
	CHECK_EQ(device->GetRenderTarget(1, &none), D3DERR_NOTFOUND);
	CHECK(none == NULL);

	// the renderer's headless init: W3DShaderManager::init asks for the render target and releases it
	back->Release(); target->Release(); depth->Release();

	// Reset makes them again, at the new size
	D3DPRESENT_PARAMETERS bigger = headless_parameters();
	bigger.BackBufferWidth = 320;
	bigger.BackBufferHeight = 200;
	CHECK_EQ(device->Reset(&bigger), D3D_OK);
	CHECK_EQ(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back), D3D_OK);
	back->GetDesc(&desc);
	CHECK_EQ(desc.Width, 320u);
	back->Release();
	CHECK_EQ(device->Release(), 0u);
	CHECK_EQ(adapter->Release(), 0u);
}

TEST(posix_device_clear_is_real_and_reads_back)
{
	PosixDirect3D9 *adapter = NULL;
	IDirect3DDevice9 *device = headless_device(&adapter);
	if (device == NULL) { adapter->Release(); return; }

	CHECK_EQ(device->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0x00336699u, 1.0f, 7), D3D_OK);
	// one rectangle, clipped by a viewport that starts at 10, 10
	D3DVIEWPORT9 viewport = { 10, 10, 50, 50, 0.0f, 1.0f };
	CHECK_EQ(device->SetViewport(&viewport), D3D_OK);
	D3DRECT rect = { 0, 0, 20, 20 };
	CHECK_EQ(device->Clear(1, &rect, D3DCLEAR_TARGET, 0x00ff0000u, 0.0f, 0), D3D_OK);

	IDirect3DSurface9 *target = NULL, *copy = NULL;
	device->GetRenderTarget(0, &target);
	CHECK_EQ(device->CreateOffscreenPlainSurface(100, 100, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &copy, NULL), D3D_OK);
	CHECK_EQ(device->GetRenderTargetData(target, copy), D3D_OK);
	CHECK_EQ(pixel_of(copy, 50, 50), 0xff336699u);
	CHECK_EQ(pixel_of(copy, 15, 15), 0xffff0000u);	// inside both the rectangle and the viewport
	CHECK_EQ(pixel_of(copy, 5, 5), 0xff336699u);		// inside the rectangle, outside the viewport
	CHECK_EQ(pixel_of(copy, 25, 15), 0xff336699u);	// inside the viewport, outside the rectangle

	IDirect3DSurface9 *depth = NULL;
	device->GetDepthStencilSurface(&depth);
	CHECK_EQ(pixel_of(depth, 0, 0), 0xffffff07u);		// depth 1, stencil 7

	// a stencil clear needs a stencil: with D16 bound it is refused
	IDirect3DSurface9 *d16 = NULL;
	CHECK_EQ(device->CreateDepthStencilSurface(100, 100, D3DFMT_D16, D3DMULTISAMPLE_NONE, 0, 0, &d16, NULL), D3D_OK);
	device->SetDepthStencilSurface(d16);
	CHECK_EQ(device->Clear(0, NULL, D3DCLEAR_STENCIL, 0, 1.0f, 0), D3DERR_INVALIDCALL);
	CHECK_EQ(device->Clear(0, NULL, D3DCLEAR_ZBUFFER, 0, 0.5f, 0), D3D_OK);

	// SetRenderTarget(0) resets the viewport to the whole target
	CHECK_EQ(device->SetRenderTarget(0, target), D3D_OK);
	D3DVIEWPORT9 after;
	device->GetViewport(&after);
	CHECK_EQ(after.X, 0u);
	CHECK_EQ(after.Width, 100u);
	CHECK_EQ(device->SetRenderTarget(0, NULL), D3DERR_INVALIDCALL);

	d16->Release(); depth->Release(); copy->Release(); target->Release();
	CHECK_EQ(device->Release(), 0u);
	adapter->Release();
}

TEST(posix_device_copies_textures_and_stretches_surfaces)
{
	PosixDirect3D9 *adapter = NULL;
	IDirect3DDevice9 *device = headless_device(&adapter);
	if (device == NULL) { adapter->Release(); return; }

	IDirect3DTexture9 *system = NULL, *video = NULL;
	CHECK_EQ(device->CreateTexture(8, 8, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &system, NULL), D3D_OK);
	CHECK_EQ(device->CreateTexture(8, 8, 2, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &video, NULL), D3D_OK);
	D3DLOCKED_RECT locked;
	system->LockRect(1, &locked, NULL, 0);
	((uint32_t *)locked.pBits)[0] = 0x11223344u;
	system->UnlockRect(1);
	CHECK_EQ(device->UpdateTexture(system, video), D3D_OK);		// the destination's two levels, from the top
	IDirect3DSurface9 *level1 = NULL;
	video->GetSurfaceLevel(1, &level1);
	CHECK_EQ(pixel_of(level1, 0, 0), 0x11223344u);
	CHECK_EQ(device->UpdateTexture(video, system), D3DERR_INVALIDCALL);	// more levels than the source has

	// StretchRect: level 1 (4x4) into a 2x2 render target, point-filtered
	IDirect3DSurface9 *small = NULL;
	CHECK_EQ(device->CreateRenderTarget(2, 2, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, 0, &small, NULL), D3D_OK);
	CHECK_EQ(device->StretchRect(level1, NULL, small, NULL, D3DTEXF_POINT), D3D_OK);
	D3DSURFACE_DESC desc;
	small->GetDesc(&desc);
	CHECK_EQ(desc.Width, 2u);
	level1->Release(); small->Release(); system->Release(); video->Release();
	CHECK_EQ(device->Release(), 0u);
	adapter->Release();
}

TEST(posix_device_creates_what_its_caps_offer_and_nothing_else)
{
	PosixDirect3D9 *adapter = NULL;
	IDirect3DDevice9 *device = headless_device(&adapter);
	if (device == NULL) { adapter->Release(); return; }

	const D3DFORMAT formats[] = { D3DFMT_A8R8G8B8, D3DFMT_X8R8G8B8, D3DFMT_R5G6B5, D3DFMT_A1R5G5B5, D3DFMT_A4R4G4B4,
		D3DFMT_R8G8B8, D3DFMT_X1R5G5B5, D3DFMT_L8, D3DFMT_A8, D3DFMT_A8L8, D3DFMT_P8, D3DFMT_V8U8, D3DFMT_X8L8V8U8,
		D3DFMT_Q8W8V8U8, D3DFMT_DXT1, D3DFMT_DXT2, D3DFMT_DXT3, D3DFMT_DXT4, D3DFMT_DXT5, D3DFMT_YUY2, D3DFMT_D24S8 };
	int offered = 0;
	for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i)
	{
		const bool check = adapter->CheckDeviceFormat(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, 0, D3DRTYPE_TEXTURE, formats[i]) == D3D_OK;
		IDirect3DTexture9 *texture = NULL;
		const bool made = device->CreateTexture(16, 16, 1, 0, formats[i], D3DPOOL_MANAGED, &texture, NULL) == D3D_OK;
		CHECK_EQ(check, made);
		if (texture != NULL) texture->Release();
		offered += check ? 1 : 0;
	}
	CHECK_EQ(offered, 13);		// the agreed texture list: 8 uncompressed and DXT1-5
	CHECK(adapter->CheckDeviceFormat(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, 0, D3DRTYPE_TEXTURE, D3DFMT_P8) != D3D_OK);
	CHECK(adapter->CheckDeviceFormat(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, 0, D3DRTYPE_TEXTURE, D3DFMT_V8U8) != D3D_OK);
	CHECK_EQ(adapter->CheckDeviceFormat(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, D3DFMT_A8R8G8B8), D3D_OK);
	CHECK(adapter->CheckDeviceFormat(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, D3DFMT_DXT1) != D3D_OK);
	CHECK_EQ(adapter->CheckDeviceFormat(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, D3DFMT_D24S8), D3D_OK);
	CHECK(adapter->CheckDeviceFormat(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, D3DFMT_D32) != D3D_OK);
	IDirect3DTexture9 *texture = NULL;
	CHECK_EQ(device->CreateTexture(16, 16, 0, D3DUSAGE_AUTOGENMIPMAP, D3DFMT_DXT1, D3DPOOL_DEFAULT, &texture, NULL), D3DERR_INVALIDCALL);
	void *shared = NULL;
	CHECK_EQ(device->CreateTexture(16, 16, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &texture, &shared), D3DERR_INVALIDCALL);
	CHECK_EQ(device->Release(), 0u);
	adapter->Release();
}

TEST(posix_caps_name_no_vendor_and_shaders_1_1)
{
	PosixDirect3D9 *adapter = new PosixDirect3D9;
	D3DADAPTER_IDENTIFIER9 identifier;
	CHECK_EQ(adapter->GetAdapterIdentifier(0, 0, &identifier), D3D_OK);
	CHECK_EQ(identifier.VendorId, 0u);
	CHECK_EQ(identifier.DeviceId, 0u);
	CHECK_STR(identifier.Driver, "posixd3d9");
	D3DCAPS9 caps;
	CHECK_EQ(adapter->GetDeviceCaps(0, D3DDEVTYPE_HAL, &caps), D3D_OK);
	// A3e: 1.1 and nothing higher, which is what the engine's generic chipset class asks for.
	CHECK_EQ(caps.VertexShaderVersion, (RenderUInt32)D3DVS_VERSION(1, 1));
	CHECK_EQ(caps.PixelShaderVersion, (RenderUInt32)D3DPS_VERSION(1, 1));
	CHECK_EQ(caps.MaxVertexShaderConst, 96u);
	CHECK(caps.PixelShader1xMaxValue == 1.0f);
	CHECK_EQ(caps.MaxSimultaneousTextures, 8u);
	CHECK_EQ(caps.MaxTextureWidth, 8192u);
	CHECK((caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) != 0);
	CHECK((caps.DevCaps & D3DDEVCAPS_NPATCHES) == 0);
	CHECK_EQ(caps.TextureOpCaps & (RenderUInt32)(D3DTEXOPCAPS_BUMPENVMAP | D3DTEXOPCAPS_BUMPENVMAPLUMINANCE | D3DTEXOPCAPS_PREMODULATE), 0u);
	CHECK_EQ(adapter->GetDeviceCaps(1, D3DDEVTYPE_HAL, &caps), D3DERR_INVALIDCALL);
	adapter->Release();
}

// The caps claim a stage operation exactly when the fixed-function generator can write it: A3 draws
// every stage through that generator, so a claim it cannot honour would be a draw that fails.
TEST(posix_caps_texture_ops_are_the_generators)
{
	PosixDirect3D9 *adapter = new PosixDirect3D9;
	D3DCAPS9 caps;
	adapter->GetDeviceCaps(0, D3DDEVTYPE_HAL, &caps);
	adapter->Release();
	int claimed = 0;
	for (FixedFunctionValue op = FF_TOP_DISABLE; op <= FF_TOP_LERP; ++op)
	{
		CombinerDescription description;
		memset(&description, 0, sizeof(description));
		description.StageCount = 1;
		CombinerStage &stage = description.Stages[0];
		// DISABLE is not an operation the generator writes but the end of the chain: a colour DISABLE
		// ends the description before the generator sees it (dx11backend.cpp's builder), and an alpha
		// DISABLE keeps the alpha the stage before left.  So DISABLE is asked as an alpha operation.
		const bool disable = (op == FF_TOP_DISABLE);
		stage.ColourOperation = disable ? FF_TOP_SELECTARG1 : op;
		stage.ColourArgument0 = FF_TA_CURRENT;
		stage.ColourArgument1 = FF_TA_TEXTURE;
		stage.ColourArgument2 = FF_TA_DIFFUSE;
		stage.AlphaOperation = disable ? FF_TOP_DISABLE : FF_TOP_SELECTARG1;
		stage.AlphaArgument1 = FF_TA_TEXTURE;
		stage.TextureBound = true;
		std::string hlsl;
		const bool written = CombinerShader_Generate(description, COMBINER_SHADER_TARGET_SDL3_GPU, hlsl);
		const bool claim = (caps.TextureOpCaps & (1u << (op - 1))) != 0;		// D3DTEXOPCAPS: D3DTOP_x is bit x - 1
		if (written != claim)
			printf("  stage op %u: the generator %s it, the caps %s it\n", (unsigned)op,
				written ? "writes" : "refuses", claim ? "claim" : "do not claim");
		CHECK_EQ(written, claim);
		claimed += claim ? 1 : 0;
	}
	CHECK_EQ(claimed, 23);
}

TEST(posix_d3dx_creates_textures_as_d3dx_does)
{
	LPDIRECT3DTEXTURE9 texture = (LPDIRECT3DTEXTURE9)1;
	CHECK_EQ(D3DX9Posix_Create_Texture(NULL, 16, 16, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture), D3DERR_INVALIDCALL);
	CHECK(texture == NULL);		// -nodevice's contract: failure and a null texture

	PosixDirect3D9 *adapter = NULL;
	IDirect3DDevice9 *device = headless_device(&adapter);
	if (device == NULL) { adapter->Release(); return; }
	D3DSURFACE_DESC desc;
	// a format the device refuses is replaced by the nearest it takes: V8U8 has no alpha
	CHECK_EQ(D3DX9Posix_Create_Texture(device, 16, 16, 1, 0, D3DFMT_V8U8, D3DPOOL_MANAGED, &texture), D3D_OK);
	CHECK(texture != NULL);
	if (texture != NULL) {
		texture->GetLevelDesc(0, &desc);
		CHECK_EQ(desc.Format, D3DFMT_X8R8G8B8);
		texture->Release();
	}
	// D3DX_DEFAULT levels are the full chain; a DXT texture cannot autogenerate, so D3DX makes it plainly
	CHECK_EQ(D3DX9Posix_Create_Texture(device, 64, 64, D3DX_DEFAULT, D3DUSAGE_AUTOGENMIPMAP, D3DFMT_DXT1, D3DPOOL_MANAGED, &texture), D3D_OK);
	CHECK(texture != NULL);
	if (texture != NULL) {
		CHECK_EQ(texture->GetLevelCount(), 7u);
		texture->GetLevelDesc(0, &desc);
		CHECK_EQ(desc.Format, D3DFMT_DXT1);
		texture->Release();
	}
	CHECK_EQ(device->Release(), 0u);
	adapter->Release();
}

TEST(posix_d3dx_filters_mip_levels_from_the_top)
{
	PosixDirect3D9 *adapter = NULL;
	IDirect3DDevice9 *device = headless_device(&adapter);
	if (device == NULL) { adapter->Release(); return; }
	LPDIRECT3DTEXTURE9 texture = NULL;
	CHECK_EQ(device->CreateTexture(4, 4, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, NULL), D3D_OK);
	D3DLOCKED_RECT locked;
	texture->LockRect(0, &locked, NULL, 0);
	for (int y = 0; y < 4; ++y)			// blue 0x00 in the left half, 0xff in the right; alpha 0xff
		for (int x = 0; x < 4; ++x)
			((uint32_t *)((uint8_t *)locked.pBits + y * locked.Pitch))[x] = (x < 2) ? 0xff000000u : 0xff0000ffu;
	texture->UnlockRect(0);
	CHECK_EQ(D3DX9Posix_Filter_Texture(texture, NULL, 0, D3DX_FILTER_BOX), D3D_OK);
	IDirect3DSurface9 *level1 = NULL, *level2 = NULL;
	texture->GetSurfaceLevel(1, &level1);
	texture->GetSurfaceLevel(2, &level2);
	CHECK_EQ(pixel_of(level1, 0, 0), 0xff000000u);
	CHECK_EQ(pixel_of(level1, 1, 1), 0xff0000ffu);
	CHECK_EQ(pixel_of(level2, 0, 0), 0xff000080u);	// (0 + 255) / 2 = 127.5, written as 128
	level1->Release(); level2->Release();
	CHECK_EQ(D3DX9Posix_Filter_Texture(texture, NULL, 3, D3DX_FILTER_BOX), D3DERR_INVALIDCALL);	// past the last level
	CHECK_EQ(D3DX9Posix_Filter_Texture(texture, NULL, 0, 0x7f), D3DERR_INVALIDCALL);			// no such filter
	texture->Release();

	// a DXT chain would need compressing: refused, not written wrong
	CHECK_EQ(device->CreateTexture(8, 8, 0, 0, D3DFMT_DXT1, D3DPOOL_MANAGED, &texture, NULL), D3D_OK);
	CHECK_EQ(D3DX9Posix_Filter_Texture(texture, NULL, D3DX_DEFAULT, D3DX_DEFAULT), D3DERR_INVALIDCALL);
	texture->Release();
	CHECK_EQ(device->Release(), 0u);
	adapter->Release();
}

TEST(posix_d3dx_loads_surfaces_with_conversion_and_scaling)
{
	PosixDirect3D9 *adapter = NULL;
	IDirect3DDevice9 *device = headless_device(&adapter);
	if (device == NULL) { adapter->Release(); return; }
	IDirect3DSurface9 *source = NULL, *dest = NULL;
	device->CreateOffscreenPlainSurface(4, 4, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &source, NULL);
	device->CreateOffscreenPlainSurface(2, 2, D3DFMT_R5G6B5, D3DPOOL_SYSTEMMEM, &dest, NULL);
	D3DLOCKED_RECT locked;
	source->LockRect(&locked, NULL, 0);
	for (int y = 0; y < 4; ++y)
		for (int x = 0; x < 4; ++x)
			((uint32_t *)((uint8_t *)locked.pBits + y * locked.Pitch))[x] = 0xffff0000u;		// red
	source->UnlockRect();
	CHECK_EQ(D3DX9Posix_Load_Surface_From_Surface(dest, NULL, NULL, source, NULL, NULL, D3DX_DEFAULT, 0), D3D_OK);
	dest->LockRect(&locked, NULL, D3DLOCK_READONLY);
	CHECK_EQ(((uint16_t *)locked.pBits)[0], (uint16_t)0xf800u);
	dest->UnlockRect();
	RenderRect outside = { 0, 0, 5, 5 };
	CHECK_EQ(D3DX9Posix_Load_Surface_From_Surface(dest, NULL, NULL, source, NULL, &outside, D3DX_FILTER_NONE, 0), D3DERR_INVALIDCALL);
	CHECK_EQ(D3DX9Posix_Load_Surface_From_Surface(dest, NULL, NULL, source, NULL, NULL, D3DX_FILTER_NONE, 0xff00ff00u), D3DERR_NOTAVAILABLE);
	CHECK_EQ(D3DX9Posix_Load_Surface_From_Surface(dest, NULL, NULL, NULL, NULL, NULL, D3DX_FILTER_NONE, 0), D3DERR_INVALIDCALL);
	source->Release(); dest->Release();
	CHECK_EQ(device->Release(), 0u);
	adapter->Release();
}
