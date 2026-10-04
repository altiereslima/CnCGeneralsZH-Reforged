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

// PosixResources9.cpp: see PosixResources9.h.

#include "PosixResources9.h"

#include <new>
#include <string.h>

void (*posixResourceDestroyed)( const void *resource ) = NULL;

//-------------------------------------------------------------------------------------------------
// Surfaces (the formats and PosixImage itself are PosixImage.cpp)
//-------------------------------------------------------------------------------------------------

static bool sameGuid( const D3D9PosixGuid &a, const D3D9PosixGuid &b )
{
	return a.Data1 == b.Data1 && a.Data2 == b.Data2 && a.Data3 == b.Data3
		&& memcmp( a.Data4, b.Data4, sizeof( a.Data4 ) ) == 0;
}

PosixSurface9::PosixSurface9()
	: m_container( NULL ), m_containerType( D3DRTYPE_SURFACE ), m_image( &m_ownImage ),
		m_multisample( D3DMULTISAMPLE_NONE ), m_multisampleQuality( 0 ), m_references( 1 )
{
	m_info.usage = 0;
	m_info.pool = D3DPOOL_DEFAULT;
	m_info.priority = 0;
}

PosixSurface9::PosixSurface9( IDirect3DBaseTexture9 *container, D3DRESOURCETYPE containerType, PosixImage *image,
	const PosixResourceInfo &info )
	: m_container( container ), m_containerType( containerType ), m_image( image ), m_info( info ),
		m_multisample( D3DMULTISAMPLE_NONE ), m_multisampleQuality( 0 ), m_references( 0 )
{
}

PosixSurface9::~PosixSurface9()
{
	if (m_container == NULL && posixResourceDestroyed != NULL)
		posixResourceDestroyed( this );
}

PosixSurface9 *PosixSurface9::createStandalone( unsigned int width, unsigned int height, D3DFORMAT format,
	RenderUInt32 usage, D3DPOOL pool, D3DMULTISAMPLE_TYPE multisample, RenderUInt32 quality )
{
	PosixSurface9 *surface = new (std::nothrow) PosixSurface9;
	if (surface == NULL)
		return NULL;
	if (!surface->m_ownImage.create( format, width, height, 1 ))
	{
		delete surface;
		return NULL;
	}
	surface->m_info.usage = usage;
	surface->m_info.pool = pool;
	surface->m_multisample = multisample;
	surface->m_multisampleQuality = quality;
	return surface;
}

uint32_t PosixSurface9::AddRef()
{
	if (m_container != NULL)
		return m_container->AddRef();
	return ++m_references;
}

uint32_t PosixSurface9::Release()
{
	if (m_container != NULL)
		return m_container->Release();
	const uint32_t left = --m_references;
	if (left == 0)
		delete this;
	return left;
}

RenderUInt32 PosixSurface9::SetPriority( RenderUInt32 priority )
{
	const RenderUInt32 previous = m_info.priority;
	m_info.priority = priority;
	return previous;
}

RenderUInt32 PosixSurface9::GetPriority()
{
	return m_info.priority;
}

D3DRESOURCETYPE PosixSurface9::GetType()
{
	return D3DRTYPE_SURFACE;
}

RenderResult PosixSurface9::GetContainer( const D3D9PosixGuid &riid, void **container )
{
	if (container == NULL)
		return D3DERR_INVALIDCALL;
	*container = NULL;
	if (m_container == NULL)
		return POSIX_D3D_NOINTERFACE;		// a standalone surface's container is the device, which nothing asks for
	const bool asked = (m_containerType == D3DRTYPE_TEXTURE && sameGuid( riid, IID_IDirect3DTexture9 ))
		|| (m_containerType == D3DRTYPE_CUBETEXTURE && sameGuid( riid, IID_IDirect3DCubeTexture9 ));
	if (!asked)
		return POSIX_D3D_NOINTERFACE;
	m_container->AddRef();
	*container = m_container;
	return D3D_OK;
}

RenderResult PosixSurface9::GetDesc( D3DSURFACE_DESC *desc )
{
	if (desc == NULL)
		return D3DERR_INVALIDCALL;
	desc->Format = m_image->format();
	desc->Type = D3DRTYPE_SURFACE;
	desc->Usage = m_info.usage;
	desc->Pool = m_info.pool;
	desc->MultiSampleType = m_multisample;
	desc->MultiSampleQuality = m_multisampleQuality;
	desc->Width = m_image->width();
	desc->Height = m_image->height();
	return D3D_OK;
}

RenderResult PosixSurface9::LockRect( D3DLOCKED_RECT *locked, const RenderRect *rect, RenderUInt32 flags )
{
	return m_image->lockRect( locked, rect, flags );
}

RenderResult PosixSurface9::UnlockRect()
{
	return m_image->unlock();
}

//-------------------------------------------------------------------------------------------------
// 2D textures
//-------------------------------------------------------------------------------------------------

/** How many levels a texture gets for `levels` asked, as D3D9 counts them. */
static unsigned int levelsToMake( unsigned int levels, RenderUInt32 usage, unsigned int width, unsigned int height,
	unsigned int depth )
{
	if (usage & D3DUSAGE_AUTOGENMIPMAP)
		return 1;		// the chain is the device's to make; the texture shows one level
	const unsigned int full = posixFullChainLength( width, height, depth );
	return (levels == 0 || levels > full) ? full : levels;
}

static void fillDesc( D3DSURFACE_DESC *desc, const PosixImage &image, const PosixResourceInfo &info )
{
	desc->Format = image.format();
	desc->Type = D3DRTYPE_SURFACE;
	desc->Usage = info.usage;
	desc->Pool = info.pool;
	desc->MultiSampleType = D3DMULTISAMPLE_NONE;
	desc->MultiSampleQuality = 0;
	desc->Width = image.width();
	desc->Height = image.height();
}

PosixTexture9::PosixTexture9()
	: m_lod( 0 )
{
	m_info.usage = 0;
	m_info.pool = D3DPOOL_MANAGED;
	m_info.priority = 0;
}

PosixTexture9::~PosixTexture9()
{
	if (posixResourceDestroyed != NULL)
		posixResourceDestroyed( this );
	for (size_t i = 0; i < m_surfaces.size(); ++i)
	{
		if (m_surfaces[i] != NULL)
			m_surfaces[i]->destroyOwned();
	}
}

PosixTexture9 *PosixTexture9::create( unsigned int width, unsigned int height, unsigned int levels,
	RenderUInt32 usage, D3DFORMAT format, D3DPOOL pool )
{
	if (width == 0 || height == 0 || !posixFormatLayout( format, NULL ))
		return NULL;
	PosixTexture9 *texture = new (std::nothrow) PosixTexture9;
	if (texture == NULL)
		return NULL;
	const unsigned int count = levelsToMake( levels, usage, width, height, 1 );
	texture->m_levels.resize( count );
	texture->m_surfaces.assign( count, (PosixSurface9 *)NULL );
	for (unsigned int level = 0; level < count; ++level)
	{
		if (!texture->m_levels[level].create( format, posixMipSize( width, level ), posixMipSize( height, level ), 1 ))
		{
			texture->Release();
			return NULL;
		}
	}
	texture->m_info.usage = usage;
	texture->m_info.pool = pool;
	return texture;
}

RenderUInt32 PosixTexture9::SetPriority( RenderUInt32 priority )
{
	const RenderUInt32 previous = m_info.priority;
	m_info.priority = priority;
	return previous;
}

RenderUInt32 PosixTexture9::GetPriority() { return m_info.priority; }
D3DRESOURCETYPE PosixTexture9::GetType() { return D3DRTYPE_TEXTURE; }

RenderUInt32 PosixTexture9::SetLOD( RenderUInt32 lod )
{
	const RenderUInt32 previous = m_lod;
	if (m_info.pool == D3DPOOL_MANAGED)		// D3D9 honours it for managed textures only
		m_lod = (lod < m_levels.size()) ? lod : (RenderUInt32)(m_levels.size() - 1);
	return previous;
}

RenderUInt32 PosixTexture9::GetLOD() { return m_lod; }
RenderUInt32 PosixTexture9::GetLevelCount() { return (RenderUInt32)m_levels.size(); }

RenderResult PosixTexture9::GetLevelDesc( unsigned int level, D3DSURFACE_DESC *desc )
{
	if (level >= m_levels.size() || desc == NULL)
		return D3DERR_INVALIDCALL;
	fillDesc( desc, m_levels[level], m_info );
	return D3D_OK;
}

RenderResult PosixTexture9::GetSurfaceLevel( unsigned int level, IDirect3DSurface9 **surface )
{
	if (surface == NULL)
		return D3DERR_INVALIDCALL;
	*surface = NULL;
	if (level >= m_levels.size())
		return D3DERR_INVALIDCALL;
	if (m_surfaces[level] == NULL)
	{
		m_surfaces[level] = new (std::nothrow) PosixSurface9( this, D3DRTYPE_TEXTURE, &m_levels[level], m_info );
		if (m_surfaces[level] == NULL)
			return POSIX_D3D_OUTOFMEMORY;
	}
	m_surfaces[level]->AddRef();		// counts on this texture
	*surface = m_surfaces[level];
	return D3D_OK;
}

RenderResult PosixTexture9::LockRect( unsigned int level, D3DLOCKED_RECT *locked, const RenderRect *rect, RenderUInt32 flags )
{
	if (level >= m_levels.size())
		return D3DERR_INVALIDCALL;
	return m_levels[level].lockRect( locked, rect, flags );
}

RenderResult PosixTexture9::UnlockRect( unsigned int level )
{
	if (level >= m_levels.size())
		return D3DERR_INVALIDCALL;
	return m_levels[level].unlock();
}

RenderResult PosixTexture9::AddDirtyRect( const RenderRect * )
{
	if (!m_levels.empty())
		m_levels[0].markWritten();
	return D3D_OK;
}

//-------------------------------------------------------------------------------------------------
// Cube textures
//-------------------------------------------------------------------------------------------------

static const unsigned int CUBE_FACES = 6;

PosixCubeTexture9::PosixCubeTexture9()
	: m_levelCount( 0 ), m_lod( 0 )
{
	m_info.usage = 0;
	m_info.pool = D3DPOOL_MANAGED;
	m_info.priority = 0;
}

PosixCubeTexture9::~PosixCubeTexture9()
{
	if (posixResourceDestroyed != NULL)
		posixResourceDestroyed( this );
	for (size_t i = 0; i < m_surfaces.size(); ++i)
	{
		if (m_surfaces[i] != NULL)
			m_surfaces[i]->destroyOwned();
	}
}

PosixCubeTexture9 *PosixCubeTexture9::create( unsigned int edge, unsigned int levels, RenderUInt32 usage,
	D3DFORMAT format, D3DPOOL pool )
{
	if (edge == 0 || !posixFormatLayout( format, NULL ))
		return NULL;
	PosixCubeTexture9 *texture = new (std::nothrow) PosixCubeTexture9;
	if (texture == NULL)
		return NULL;
	const unsigned int count = levelsToMake( levels, usage, edge, edge, 1 );
	texture->m_levelCount = count;
	texture->m_images.resize( CUBE_FACES * count );
	texture->m_surfaces.assign( CUBE_FACES * count, (PosixSurface9 *)NULL );
	for (unsigned int face = 0; face < CUBE_FACES; ++face)
	{
		for (unsigned int level = 0; level < count; ++level)
		{
			const unsigned int size = posixMipSize( edge, level );
			if (!texture->m_images[face * count + level].create( format, size, size, 1 ))
			{
				texture->Release();
				return NULL;
			}
		}
	}
	texture->m_info.usage = usage;
	texture->m_info.pool = pool;
	return texture;
}

PosixImage *PosixCubeTexture9::image( unsigned int face, unsigned int level )
{
	if (face >= CUBE_FACES || level >= m_levelCount)
		return NULL;
	return &m_images[face * m_levelCount + level];
}

RenderUInt32 PosixCubeTexture9::SetPriority( RenderUInt32 priority )
{
	const RenderUInt32 previous = m_info.priority;
	m_info.priority = priority;
	return previous;
}

RenderUInt32 PosixCubeTexture9::GetPriority() { return m_info.priority; }
D3DRESOURCETYPE PosixCubeTexture9::GetType() { return D3DRTYPE_CUBETEXTURE; }

RenderUInt32 PosixCubeTexture9::SetLOD( RenderUInt32 lod )
{
	const RenderUInt32 previous = m_lod;
	if (m_info.pool == D3DPOOL_MANAGED)
		m_lod = (lod < m_levelCount) ? lod : m_levelCount - 1;
	return previous;
}

RenderUInt32 PosixCubeTexture9::GetLOD() { return m_lod; }
RenderUInt32 PosixCubeTexture9::GetLevelCount() { return m_levelCount; }

RenderResult PosixCubeTexture9::GetLevelDesc( unsigned int level, D3DSURFACE_DESC *desc )
{
	PosixImage *first = image( 0, level );
	if (first == NULL || desc == NULL)
		return D3DERR_INVALIDCALL;
	fillDesc( desc, *first, m_info );
	return D3D_OK;
}

RenderResult PosixCubeTexture9::GetCubeMapSurface( D3DCUBEMAP_FACES face, unsigned int level, IDirect3DSurface9 **surface )
{
	if (surface == NULL)
		return D3DERR_INVALIDCALL;
	*surface = NULL;
	PosixImage *faceImage = image( (unsigned int)face, level );
	if (faceImage == NULL)
		return D3DERR_INVALIDCALL;
	PosixSurface9 *&slot = m_surfaces[(unsigned int)face * m_levelCount + level];
	if (slot == NULL)
	{
		slot = new (std::nothrow) PosixSurface9( this, D3DRTYPE_CUBETEXTURE, faceImage, m_info );
		if (slot == NULL)
			return POSIX_D3D_OUTOFMEMORY;
	}
	slot->AddRef();
	*surface = slot;
	return D3D_OK;
}

RenderResult PosixCubeTexture9::LockRect( D3DCUBEMAP_FACES face, unsigned int level, D3DLOCKED_RECT *locked,
	const RenderRect *rect, RenderUInt32 flags )
{
	PosixImage *faceImage = image( (unsigned int)face, level );
	if (faceImage == NULL)
		return D3DERR_INVALIDCALL;
	return faceImage->lockRect( locked, rect, flags );
}

RenderResult PosixCubeTexture9::UnlockRect( D3DCUBEMAP_FACES face, unsigned int level )
{
	PosixImage *faceImage = image( (unsigned int)face, level );
	if (faceImage == NULL)
		return D3DERR_INVALIDCALL;
	return faceImage->unlock();
}

RenderResult PosixCubeTexture9::AddDirtyRect( D3DCUBEMAP_FACES face, const RenderRect * )
{
	PosixImage *faceImage = image( (unsigned int)face, 0 );
	if (faceImage == NULL)
		return D3DERR_INVALIDCALL;
	faceImage->markWritten();
	return D3D_OK;
}

//-------------------------------------------------------------------------------------------------
// Volume textures
//-------------------------------------------------------------------------------------------------

PosixVolume9::PosixVolume9( PosixVolumeTexture9 *container, PosixImage *image, const PosixResourceInfo &info )
	: m_container( container ), m_image( image ), m_info( info )
{
}

uint32_t PosixVolume9::AddRef() { return m_container->AddRef(); }
uint32_t PosixVolume9::Release() { return m_container->Release(); }

RenderResult PosixVolume9::GetDesc( D3DVOLUME_DESC *desc )
{
	if (desc == NULL)
		return D3DERR_INVALIDCALL;
	desc->Format = m_image->format();
	desc->Type = D3DRTYPE_VOLUME;
	desc->Usage = m_info.usage;
	desc->Pool = m_info.pool;
	desc->Width = m_image->width();
	desc->Height = m_image->height();
	desc->Depth = m_image->depth();
	return D3D_OK;
}

RenderResult PosixVolume9::LockBox( D3DLOCKED_BOX *locked, const D3DBOX *box, RenderUInt32 flags )
{
	return m_image->lockBox( locked, box, flags );
}

RenderResult PosixVolume9::UnlockBox()
{
	return m_image->unlock();
}

PosixVolumeTexture9::PosixVolumeTexture9()
	: m_lod( 0 )
{
	m_info.usage = 0;
	m_info.pool = D3DPOOL_MANAGED;
	m_info.priority = 0;
}

PosixVolumeTexture9::~PosixVolumeTexture9()
{
	if (posixResourceDestroyed != NULL)
		posixResourceDestroyed( this );
	for (size_t i = 0; i < m_volumes.size(); ++i)
	{
		if (m_volumes[i] != NULL)
			m_volumes[i]->destroyOwned();
	}
}

PosixVolumeTexture9 *PosixVolumeTexture9::create( unsigned int width, unsigned int height, unsigned int depth,
	unsigned int levels, RenderUInt32 usage, D3DFORMAT format, D3DPOOL pool )
{
	if (width == 0 || height == 0 || depth == 0 || !posixFormatLayout( format, NULL ))
		return NULL;
	PosixVolumeTexture9 *texture = new (std::nothrow) PosixVolumeTexture9;
	if (texture == NULL)
		return NULL;
	const unsigned int count = levelsToMake( levels, usage, width, height, depth );
	texture->m_levels.resize( count );
	texture->m_volumes.assign( count, (PosixVolume9 *)NULL );
	for (unsigned int level = 0; level < count; ++level)
	{
		if (!texture->m_levels[level].create( format, posixMipSize( width, level ), posixMipSize( height, level ),
					posixMipSize( depth, level ) ))
		{
			texture->Release();
			return NULL;
		}
	}
	texture->m_info.usage = usage;
	texture->m_info.pool = pool;
	return texture;
}

RenderUInt32 PosixVolumeTexture9::SetPriority( RenderUInt32 priority )
{
	const RenderUInt32 previous = m_info.priority;
	m_info.priority = priority;
	return previous;
}

RenderUInt32 PosixVolumeTexture9::GetPriority() { return m_info.priority; }
D3DRESOURCETYPE PosixVolumeTexture9::GetType() { return D3DRTYPE_VOLUMETEXTURE; }

RenderUInt32 PosixVolumeTexture9::SetLOD( RenderUInt32 lod )
{
	const RenderUInt32 previous = m_lod;
	if (m_info.pool == D3DPOOL_MANAGED)
		m_lod = (lod < m_levels.size()) ? lod : (RenderUInt32)(m_levels.size() - 1);
	return previous;
}

RenderUInt32 PosixVolumeTexture9::GetLOD() { return m_lod; }
RenderUInt32 PosixVolumeTexture9::GetLevelCount() { return (RenderUInt32)m_levels.size(); }

RenderResult PosixVolumeTexture9::GetLevelDesc( unsigned int level, D3DVOLUME_DESC *desc )
{
	if (level >= m_levels.size() || desc == NULL)
		return D3DERR_INVALIDCALL;
	const PosixImage &image = m_levels[level];
	desc->Format = image.format();
	desc->Type = D3DRTYPE_VOLUME;
	desc->Usage = m_info.usage;
	desc->Pool = m_info.pool;
	desc->Width = image.width();
	desc->Height = image.height();
	desc->Depth = image.depth();
	return D3D_OK;
}

RenderResult PosixVolumeTexture9::GetVolumeLevel( unsigned int level, IDirect3DVolume9 **volume )
{
	if (volume == NULL)
		return D3DERR_INVALIDCALL;
	*volume = NULL;
	if (level >= m_levels.size())
		return D3DERR_INVALIDCALL;
	if (m_volumes[level] == NULL)
	{
		m_volumes[level] = new (std::nothrow) PosixVolume9( this, &m_levels[level], m_info );
		if (m_volumes[level] == NULL)
			return POSIX_D3D_OUTOFMEMORY;
	}
	m_volumes[level]->AddRef();
	*volume = m_volumes[level];
	return D3D_OK;
}

RenderResult PosixVolumeTexture9::LockBox( unsigned int level, D3DLOCKED_BOX *locked, const D3DBOX *box, RenderUInt32 flags )
{
	if (level >= m_levels.size())
		return D3DERR_INVALIDCALL;
	return m_levels[level].lockBox( locked, box, flags );
}

RenderResult PosixVolumeTexture9::UnlockBox( unsigned int level )
{
	if (level >= m_levels.size())
		return D3DERR_INVALIDCALL;
	return m_levels[level].unlock();
}

//-------------------------------------------------------------------------------------------------
// Buffers
//-------------------------------------------------------------------------------------------------

bool PosixBufferStorage::create( unsigned int length )
{
	if (length == 0)
		return false;
	try
	{
		m_bytes.assign( length, 0 );
	}
	catch (const std::bad_alloc &)
	{
		return false;
	}
	return true;
}

// D3D9: offset and size 0 lock the whole buffer; a buffer may be locked more than once.  DISCARD and
// NOOVERWRITE promise the GPU is not reading what is overwritten, which in memory is always true.
RenderResult PosixBufferStorage::lock( unsigned int offset, unsigned int size, void **data, RenderUInt32 flags )
{
	if (data == NULL)
		return D3DERR_INVALIDCALL;
	*data = NULL;
	const unsigned int length = (unsigned int)m_bytes.size();
	if (offset > length || (size != 0 && size > length - offset))
		return D3DERR_INVALIDCALL;
	*data = &m_bytes[0] + offset;
	++m_locks;
	if ((flags & D3DLOCK_READONLY) == 0)
		++m_version;
	return D3D_OK;
}

RenderResult PosixBufferStorage::unlock()
{
	if (m_locks == 0)
		return D3DERR_INVALIDCALL;
	--m_locks;
	return D3D_OK;
}

PosixVertexBuffer9::~PosixVertexBuffer9()
{
	if (posixResourceDestroyed != NULL)
		posixResourceDestroyed( this );
}

PosixVertexBuffer9 *PosixVertexBuffer9::create( unsigned int length, RenderUInt32 usage, RenderUInt32 fvf, D3DPOOL pool )
{
	PosixVertexBuffer9 *buffer = new (std::nothrow) PosixVertexBuffer9;
	if (buffer == NULL)
		return NULL;
	if (!buffer->m_storage.create( length ))
	{
		buffer->Release();
		return NULL;
	}
	buffer->m_info.usage = usage;
	buffer->m_info.pool = pool;
	buffer->m_info.priority = 0;
	buffer->m_fvf = fvf;
	return buffer;
}

RenderUInt32 PosixVertexBuffer9::SetPriority( RenderUInt32 priority )
{
	const RenderUInt32 previous = m_info.priority;
	m_info.priority = priority;
	return previous;
}

RenderUInt32 PosixVertexBuffer9::GetPriority() { return m_info.priority; }
D3DRESOURCETYPE PosixVertexBuffer9::GetType() { return D3DRTYPE_VERTEXBUFFER; }

RenderResult PosixVertexBuffer9::Lock( unsigned int offset, unsigned int size, void **data, RenderUInt32 flags )
{
	return m_storage.lock( offset, size, data, flags );
}

RenderResult PosixVertexBuffer9::Unlock()
{
	return m_storage.unlock();
}

RenderResult PosixVertexBuffer9::GetDesc( D3DVERTEXBUFFER_DESC *desc )
{
	if (desc == NULL)
		return D3DERR_INVALIDCALL;
	desc->Format = D3DFMT_VERTEXDATA;
	desc->Type = D3DRTYPE_VERTEXBUFFER;
	desc->Usage = m_info.usage;
	desc->Pool = m_info.pool;
	desc->Size = m_storage.length();
	desc->FVF = m_fvf;
	return D3D_OK;
}

PosixIndexBuffer9::~PosixIndexBuffer9()
{
	if (posixResourceDestroyed != NULL)
		posixResourceDestroyed( this );
}

PosixIndexBuffer9 *PosixIndexBuffer9::create( unsigned int length, RenderUInt32 usage, D3DFORMAT format, D3DPOOL pool )
{
	if (format != D3DFMT_INDEX16 && format != D3DFMT_INDEX32)
		return NULL;
	PosixIndexBuffer9 *buffer = new (std::nothrow) PosixIndexBuffer9;
	if (buffer == NULL)
		return NULL;
	if (!buffer->m_storage.create( length ))
	{
		buffer->Release();
		return NULL;
	}
	buffer->m_info.usage = usage;
	buffer->m_info.pool = pool;
	buffer->m_info.priority = 0;
	buffer->m_format = format;
	return buffer;
}

RenderUInt32 PosixIndexBuffer9::SetPriority( RenderUInt32 priority )
{
	const RenderUInt32 previous = m_info.priority;
	m_info.priority = priority;
	return previous;
}

RenderUInt32 PosixIndexBuffer9::GetPriority() { return m_info.priority; }
D3DRESOURCETYPE PosixIndexBuffer9::GetType() { return D3DRTYPE_INDEXBUFFER; }

RenderResult PosixIndexBuffer9::Lock( unsigned int offset, unsigned int size, void **data, RenderUInt32 flags )
{
	return m_storage.lock( offset, size, data, flags );
}

RenderResult PosixIndexBuffer9::Unlock()
{
	return m_storage.unlock();
}

RenderResult PosixIndexBuffer9::GetDesc( D3DINDEXBUFFER_DESC *desc )
{
	if (desc == NULL)
		return D3DERR_INVALIDCALL;
	desc->Format = m_format;
	desc->Type = D3DRTYPE_INDEXBUFFER;
	desc->Usage = m_info.usage;
	desc->Pool = m_info.pool;
	desc->Size = m_storage.length();
	return D3D_OK;
}
