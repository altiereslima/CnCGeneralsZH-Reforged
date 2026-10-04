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

// FILE: PosixImage.h /////////////////////////////////////////////////////////////////////////////
// Desc:   One image in memory, in a Direct3D 9 format (decision 7, phase A2).
///////////////////////////////////////////////////////////////////////////////////////////////////

/* The pixels of a texture level or a surface, kept as D3D9 lays a format out: blocks of the format's
	 pixel size, 2x1 for the packed YUV formats, 4x4 for DXT1 (8 bytes) and DXT2-5 (16), each row of
	 blocks right after the last.  Off Windows every resource of the device is made of these
	 (PosixResources9.h).  On Windows the same images, with PosixImageOps and PosixPixelCodec, are
	 the port's D3DX texture helpers over the real device where d3dx9_43.dll is not there
	 (d3dx9portable_texture.cpp): Windows on ARM64. */

#pragma once

#ifndef POSIXIMAGE_H
#define POSIXIMAGE_H

#if defined(_WIN32)
#include <d3d9.h>
#include "Platform/RenderTypes.h"
#else
#include "Platform/D3D9Posix.h"
#endif

#include <stddef.h>
#include <stdint.h>
#include <vector>

/** How a format is stored: blocks of blockWidth x blockHeight pixels, bytesPerBlock each. */
struct PosixFormatLayout
{
	unsigned int blockWidth;
	unsigned int blockHeight;
	unsigned int bytesPerBlock;
};

/** The layout of a format that can hold pixels; false for UNKNOWN, VERTEXDATA and anything not listed. */
bool posixFormatLayout( D3DFORMAT format, PosixFormatLayout *layout );

/** D3D9's own error for a failed allocation. */
#define POSIX_D3D_OUTOFMEMORY			((RenderResult)0x8007000Eu)

/** One image in memory: a level of a texture, a cube face's level, a volume level, or a standalone
	* surface.  Width, height and depth are in pixels; rows and slices are whole blocks. */
class PosixImage
{
public:
	PosixImage();

	/** Sizes and allocates the image, zero-filled; false if the format has no layout or memory runs out. */
	bool create( D3DFORMAT format, unsigned int width, unsigned int height, unsigned int depth );

	D3DFORMAT format() const { return m_format; }
	unsigned int width() const { return m_width; }
	unsigned int height() const { return m_height; }
	unsigned int depth() const { return m_depth; }
	const PosixFormatLayout &layout() const { return m_layout; }
	unsigned int rowPitch() const { return m_rowPitch; }			///< bytes in one row of blocks
	unsigned int rowCount() const { return m_rowCount; }			///< rows of blocks in one slice
	unsigned int slicePitch() const { return m_slicePitch; }	///< bytes in one slice
	uint8_t *bytes() { return m_bytes.empty() ? NULL : &m_bytes[0]; }
	const uint8_t *bytes() const { return m_bytes.empty() ? NULL : &m_bytes[0]; }
	size_t size() const { return m_bytes.size(); }

	/** Counts writes: every lock that is not READONLY, and every write through markWritten(). */
	uint32_t version() const { return m_version; }
	void markWritten() { ++m_version; }

	/** LockRect and LockBox.  NULL rect or box means the whole image. */
	RenderResult lockRect( D3DLOCKED_RECT *locked, const RenderRect *rect, RenderUInt32 flags );
	RenderResult lockBox( D3DLOCKED_BOX *locked, const D3DBOX *box, RenderUInt32 flags );
	RenderResult unlock();
	bool isLocked() const { return m_locked; }

private:
	RenderResult lockRegion( unsigned int left, unsigned int top, unsigned int front,
		unsigned int right, unsigned int bottom, unsigned int back, RenderUInt32 flags, uint8_t **address );

	D3DFORMAT m_format;
	unsigned int m_width, m_height, m_depth;
	PosixFormatLayout m_layout;
	unsigned int m_rowPitch, m_rowCount, m_slicePitch;
	std::vector<uint8_t> m_bytes;
	uint32_t m_version;
	bool m_locked;
};

/** The size of mip level `level` of a dimension, never below 1. */
inline unsigned int posixMipSize( unsigned int size, unsigned int level )
{
	const unsigned int shifted = (level < 32) ? (size >> level) : 0;
	return shifted > 0 ? shifted : 1;
}

/** The number of levels a full chain from width x height x depth has. */
unsigned int posixFullChainLength( unsigned int width, unsigned int height, unsigned int depth );

#endif // POSIXIMAGE_H
