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

// FILE: PosixResources9.h ////////////////////////////////////////////////////////////////////////
// Desc:   The Direct3D 9 resources off Windows, in memory (decision 7, phase A2).
///////////////////////////////////////////////////////////////////////////////////////////////////

/* Textures (2D, cube, volume), their surfaces and volumes, standalone surfaces (render targets, depth
	 surfaces, offscreen plain ones), and vertex and index buffers, all held in memory.  The device's
	 resource methods (PosixDevice9Resources.cpp) make them; A3's draw reads them, and each image and
	 buffer counts its writes (version()) so the draw knows when an upload is stale.

	 The rules are Direct3D 9's, where the renderer can see them:
	 - A format's storage is its block layout: 1x1 blocks of its pixel size, 2x1 for the packed YUV
		 formats, 4x4 for DXT1 (8 bytes) and DXT2-5 (16).  Compressed data is kept compressed.  A row
		 pitch is one row of blocks, with no padding: the renderer always steps by the pitch it is given.
	 - A level count of 0 means the full chain down to 1x1; AUTOGENMIPMAP exposes one level.
	 - A lock gives the address of the rectangle (or box) asked for, which for a block format must lie
		 on block boundaries.  Locking a locked image, or unlocking an unlocked one, is D3DERR_INVALIDCALL.
		 A buffer may be locked more than once, as D3D9 allows, and needs as many unlocks.
	 - Reference counts are COM's: a texture's surfaces and volumes have no count of their own, and
		 AddRef or Release on one counts on the texture, so a surface keeps its texture alive.
		 GetSurfaceLevel and GetContainer AddRef what they hand back.

	 Nothing here draws, and nothing needs a window or a GPU. */

#pragma once

#ifndef POSIXRESOURCES9_H
#define POSIXRESOURCES9_H

#include "Platform/D3D9Posix.h"
#include "PosixDevice9.h"		// PosixRefCounted, the device's COM counting
#include "PosixImage.h"		// PosixImage, PosixFormatLayout, posixMipSize

#include <atomic>
#include <stddef.h>
#include <stdint.h>
#include <vector>

/** Called with the object's address as a texture (2D, cube, volume), a standalone surface, or a vertex or
	* index buffer is destroyed, first thing in its destructor, on whatever thread drops the last reference
	* (the texture loader's included).  A3c drops that object's GPU copy by it, using the address only as a
	* key.  Null (the default, and headless) calls nothing. */
extern void (*posixResourceDestroyed)( const void *resource );

/** COM's "no such interface", which GetContainer answers for a container that is not the one asked for. */
#define POSIX_D3D_NOINTERFACE			((RenderResult)0x80004002u)

//-------------------------------------------------------------------------------------------------

/** The resource properties every resource keeps. */
struct PosixResourceInfo
{
	RenderUInt32 usage;
	D3DPOOL pool;
	RenderUInt32 priority;
};

//-------------------------------------------------------------------------------------------------

class PosixTexture9;
class PosixCubeTexture9;

/** A surface: standalone (a render target, a depth surface, an offscreen plain surface), with its
	* own image and count, or a level of a texture or cube face, whose image and count are the
	* texture's. */
class PosixSurface9 : public IDirect3DSurface9
{
public:
	/** A standalone surface.  Returns NULL if the image cannot be made. */
	static PosixSurface9 *createStandalone( unsigned int width, unsigned int height, D3DFORMAT format,
		RenderUInt32 usage, D3DPOOL pool, D3DMULTISAMPLE_TYPE multisample, RenderUInt32 quality );
	/** A texture's level: the texture owns the image and holds this object until it goes. */
	PosixSurface9( IDirect3DBaseTexture9 *container, D3DRESOURCETYPE containerType, PosixImage *image,
		const PosixResourceInfo &info );

	virtual uint32_t AddRef();
	virtual uint32_t Release();
	virtual RenderUInt32 SetPriority( RenderUInt32 priority );
	virtual RenderUInt32 GetPriority();
	virtual D3DRESOURCETYPE GetType();
	virtual RenderResult GetContainer( const D3D9PosixGuid &riid, void **container );
	virtual RenderResult GetDesc( D3DSURFACE_DESC *desc );
	virtual RenderResult LockRect( D3DLOCKED_RECT *locked, const RenderRect *rect, RenderUInt32 flags );
	virtual RenderResult UnlockRect();

	/** The pixels, for the device's copies and fills. */
	PosixImage &image() { return *m_image; }
	/** Deletes a texture-owned surface; only its texture calls this. */
	void destroyOwned() { delete this; }

protected:
	virtual ~PosixSurface9();

private:
	PosixSurface9();

	IDirect3DBaseTexture9 *m_container;		///< NULL for a standalone surface
	D3DRESOURCETYPE m_containerType;
	PosixImage *m_image;
	PosixImage m_ownImage;								///< a standalone surface's pixels
	PosixResourceInfo m_info;
	D3DMULTISAMPLE_TYPE m_multisample;
	RenderUInt32 m_multisampleQuality;
	std::atomic<uint32_t> m_references;		///< a standalone surface's own count
};

/** A 2D texture and its mip chain. */
class PosixTexture9 : public PosixRefCounted<IDirect3DTexture9>
{
public:
	/** Returns NULL if the format has no layout or memory runs out. */
	static PosixTexture9 *create( unsigned int width, unsigned int height, unsigned int levels,
		RenderUInt32 usage, D3DFORMAT format, D3DPOOL pool );

	virtual RenderUInt32 SetPriority( RenderUInt32 priority );
	virtual RenderUInt32 GetPriority();
	virtual D3DRESOURCETYPE GetType();
	virtual RenderUInt32 SetLOD( RenderUInt32 lod );
	virtual RenderUInt32 GetLOD();
	virtual RenderUInt32 GetLevelCount();
	virtual RenderResult GetLevelDesc( unsigned int level, D3DSURFACE_DESC *desc );
	virtual RenderResult GetSurfaceLevel( unsigned int level, IDirect3DSurface9 **surface );
	virtual RenderResult LockRect( unsigned int level, D3DLOCKED_RECT *locked, const RenderRect *rect, RenderUInt32 flags );
	virtual RenderResult UnlockRect( unsigned int level );
	virtual RenderResult AddDirtyRect( const RenderRect *dirty );

	PosixImage &level( unsigned int index ) { return m_levels[index]; }
	unsigned int levelCount() const { return (unsigned int)m_levels.size(); }

protected:
	virtual ~PosixTexture9();

private:
	PosixTexture9();

	std::vector<PosixImage> m_levels;
	std::vector<PosixSurface9 *> m_surfaces;	///< made on first GetSurfaceLevel
	PosixResourceInfo m_info;
	RenderUInt32 m_lod;
};

/** A cube texture: six faces, each with the same mip chain. */
class PosixCubeTexture9 : public PosixRefCounted<IDirect3DCubeTexture9>
{
public:
	static PosixCubeTexture9 *create( unsigned int edge, unsigned int levels, RenderUInt32 usage,
		D3DFORMAT format, D3DPOOL pool );

	virtual RenderUInt32 SetPriority( RenderUInt32 priority );
	virtual RenderUInt32 GetPriority();
	virtual D3DRESOURCETYPE GetType();
	virtual RenderUInt32 SetLOD( RenderUInt32 lod );
	virtual RenderUInt32 GetLOD();
	virtual RenderUInt32 GetLevelCount();
	virtual RenderResult GetLevelDesc( unsigned int level, D3DSURFACE_DESC *desc );
	virtual RenderResult GetCubeMapSurface( D3DCUBEMAP_FACES face, unsigned int level, IDirect3DSurface9 **surface );
	virtual RenderResult LockRect( D3DCUBEMAP_FACES face, unsigned int level, D3DLOCKED_RECT *locked,
		const RenderRect *rect, RenderUInt32 flags );
	virtual RenderResult UnlockRect( D3DCUBEMAP_FACES face, unsigned int level );
	virtual RenderResult AddDirtyRect( D3DCUBEMAP_FACES face, const RenderRect *dirty );

	PosixImage *image( unsigned int face, unsigned int level );
	unsigned int levelCount() const { return m_levelCount; }

protected:
	virtual ~PosixCubeTexture9();

private:
	PosixCubeTexture9();

	unsigned int m_levelCount;
	std::vector<PosixImage> m_images;					///< face-major: face * levels + level
	std::vector<PosixSurface9 *> m_surfaces;
	PosixResourceInfo m_info;
	RenderUInt32 m_lod;
};

class PosixVolumeTexture9;

/** A volume texture's level; its count is the texture's. */
class PosixVolume9 : public IDirect3DVolume9
{
public:
	PosixVolume9( PosixVolumeTexture9 *container, PosixImage *image, const PosixResourceInfo &info );

	virtual uint32_t AddRef();
	virtual uint32_t Release();
	virtual RenderResult GetDesc( D3DVOLUME_DESC *desc );
	virtual RenderResult LockBox( D3DLOCKED_BOX *locked, const D3DBOX *box, RenderUInt32 flags );
	virtual RenderResult UnlockBox();

	void destroyOwned() { delete this; }

protected:
	virtual ~PosixVolume9() {}

private:
	PosixVolumeTexture9 *m_container;
	PosixImage *m_image;
	PosixResourceInfo m_info;
};

/** A volume texture and its mip chain. */
class PosixVolumeTexture9 : public PosixRefCounted<IDirect3DVolumeTexture9>
{
public:
	static PosixVolumeTexture9 *create( unsigned int width, unsigned int height, unsigned int depth,
		unsigned int levels, RenderUInt32 usage, D3DFORMAT format, D3DPOOL pool );

	virtual RenderUInt32 SetPriority( RenderUInt32 priority );
	virtual RenderUInt32 GetPriority();
	virtual D3DRESOURCETYPE GetType();
	virtual RenderUInt32 SetLOD( RenderUInt32 lod );
	virtual RenderUInt32 GetLOD();
	virtual RenderUInt32 GetLevelCount();
	virtual RenderResult GetLevelDesc( unsigned int level, D3DVOLUME_DESC *desc );
	virtual RenderResult GetVolumeLevel( unsigned int level, IDirect3DVolume9 **volume );
	virtual RenderResult LockBox( unsigned int level, D3DLOCKED_BOX *locked, const D3DBOX *box, RenderUInt32 flags );
	virtual RenderResult UnlockBox( unsigned int level );

	PosixImage &level( unsigned int index ) { return m_levels[index]; }

protected:
	virtual ~PosixVolumeTexture9();

private:
	PosixVolumeTexture9();

	std::vector<PosixImage> m_levels;
	std::vector<PosixVolume9 *> m_volumes;
	PosixResourceInfo m_info;
	RenderUInt32 m_lod;
};

//-------------------------------------------------------------------------------------------------

/** A vertex or index buffer's bytes and locks. */
class PosixBufferStorage
{
public:
	PosixBufferStorage() : m_locks( 0 ), m_version( 0 ) {}
	bool create( unsigned int length );
	RenderResult lock( unsigned int offset, unsigned int size, void **data, RenderUInt32 flags );
	RenderResult unlock();
	unsigned int length() const { return (unsigned int)m_bytes.size(); }
	const uint8_t *bytes() const { return m_bytes.empty() ? NULL : &m_bytes[0]; }
	uint32_t version() const { return m_version; }
	unsigned int lockCount() const { return m_locks; }

private:
	std::vector<uint8_t> m_bytes;
	unsigned int m_locks;
	uint32_t m_version;
};

class PosixVertexBuffer9 : public PosixRefCounted<IDirect3DVertexBuffer9>
{
public:
	static PosixVertexBuffer9 *create( unsigned int length, RenderUInt32 usage, RenderUInt32 fvf, D3DPOOL pool );

	virtual RenderUInt32 SetPriority( RenderUInt32 priority );
	virtual RenderUInt32 GetPriority();
	virtual D3DRESOURCETYPE GetType();
	virtual RenderResult Lock( unsigned int offset, unsigned int size, void **data, RenderUInt32 flags );
	virtual RenderResult Unlock();
	virtual RenderResult GetDesc( D3DVERTEXBUFFER_DESC *desc );

	PosixBufferStorage &storage() { return m_storage; }

protected:
	virtual ~PosixVertexBuffer9();

private:
	PosixVertexBuffer9() : m_fvf( 0 ) {}
	PosixBufferStorage m_storage;
	PosixResourceInfo m_info;
	RenderUInt32 m_fvf;
};

class PosixIndexBuffer9 : public PosixRefCounted<IDirect3DIndexBuffer9>
{
public:
	/** Only INDEX16 and INDEX32 are index formats. */
	static PosixIndexBuffer9 *create( unsigned int length, RenderUInt32 usage, D3DFORMAT format, D3DPOOL pool );

	virtual RenderUInt32 SetPriority( RenderUInt32 priority );
	virtual RenderUInt32 GetPriority();
	virtual D3DRESOURCETYPE GetType();
	virtual RenderResult Lock( unsigned int offset, unsigned int size, void **data, RenderUInt32 flags );
	virtual RenderResult Unlock();
	virtual RenderResult GetDesc( D3DINDEXBUFFER_DESC *desc );

	PosixBufferStorage &storage() { return m_storage; }
	D3DFORMAT format() const { return m_format; }

protected:
	virtual ~PosixIndexBuffer9();

private:
	PosixIndexBuffer9() : m_format( D3DFMT_INDEX16 ) {}
	PosixBufferStorage m_storage;
	PosixResourceInfo m_info;
	D3DFORMAT m_format;
};

#endif // POSIXRESOURCES9_H
