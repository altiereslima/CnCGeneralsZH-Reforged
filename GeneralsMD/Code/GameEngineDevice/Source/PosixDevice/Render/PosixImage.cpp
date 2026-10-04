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

// PosixImage.cpp: see PosixImage.h.

#include "PosixImage.h"

#include <new>

//-------------------------------------------------------------------------------------------------
// Formats
//-------------------------------------------------------------------------------------------------

bool posixFormatLayout( D3DFORMAT format, PosixFormatLayout *layout )
{
	unsigned int width = 1, height = 1, bytes = 0;
	switch (format)
	{
		case D3DFMT_R3G3B2: case D3DFMT_A8: case D3DFMT_P8: case D3DFMT_L8: case D3DFMT_A4L4:
			bytes = 1;
			break;
		case D3DFMT_R5G6B5: case D3DFMT_X1R5G5B5: case D3DFMT_A1R5G5B5: case D3DFMT_A4R4G4B4:
		case D3DFMT_A8R3G3B2: case D3DFMT_X4R4G4B4: case D3DFMT_A8P8: case D3DFMT_A8L8: case D3DFMT_V8U8:
		case D3DFMT_L6V5U5: case D3DFMT_D16_LOCKABLE: case D3DFMT_D15S1: case D3DFMT_D16: case D3DFMT_L16:
		case D3DFMT_INDEX16: case D3DFMT_R16F: case D3DFMT_CxV8U8:
			bytes = 2;
			break;
		case D3DFMT_R8G8B8:
			bytes = 3;
			break;
		case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: case D3DFMT_A2B10G10R10: case D3DFMT_A8B8G8R8:
		case D3DFMT_X8B8G8R8: case D3DFMT_G16R16: case D3DFMT_A2R10G10B10: case D3DFMT_X8L8V8U8:
		case D3DFMT_Q8W8V8U8: case D3DFMT_V16U16: case D3DFMT_A2W10V10U10: case D3DFMT_D32:
		case D3DFMT_D24S8: case D3DFMT_D24X8: case D3DFMT_D24X4S4: case D3DFMT_D32F_LOCKABLE:
		case D3DFMT_D24FS8: case D3DFMT_INDEX32: case D3DFMT_R32F: case D3DFMT_G16R16F:
			bytes = 4;
			break;
		case D3DFMT_A16B16G16R16: case D3DFMT_Q16W16V16U16: case D3DFMT_A16B16G16R16F: case D3DFMT_G32R32F:
			bytes = 8;
			break;
		case D3DFMT_A32B32G32R32F:
			bytes = 16;
			break;
		case D3DFMT_UYVY: case D3DFMT_YUY2: case D3DFMT_R8G8_B8G8: case D3DFMT_G8R8_G8B8:
			width = 2;		// two pixels share one chroma pair
			bytes = 4;
			break;
		case D3DFMT_DXT1:
			width = height = 4;
			bytes = 8;
			break;
		case D3DFMT_DXT2: case D3DFMT_DXT3: case D3DFMT_DXT4: case D3DFMT_DXT5:
			width = height = 4;
			bytes = 16;
			break;
		default:
			return false;
	}
	if (layout != NULL)
	{
		layout->blockWidth = width;
		layout->blockHeight = height;
		layout->bytesPerBlock = bytes;
	}
	return true;
}

unsigned int posixFullChainLength( unsigned int width, unsigned int height, unsigned int depth )
{
	unsigned int largest = width;
	if (height > largest) largest = height;
	if (depth > largest) largest = depth;
	unsigned int levels = 1;
	while (largest > 1)
	{
		largest >>= 1;
		++levels;
	}
	return levels;
}

//-------------------------------------------------------------------------------------------------
// PosixImage
//-------------------------------------------------------------------------------------------------

PosixImage::PosixImage()
	: m_format( D3DFMT_UNKNOWN ), m_width( 0 ), m_height( 0 ), m_depth( 0 ),
		m_rowPitch( 0 ), m_rowCount( 0 ), m_slicePitch( 0 ), m_version( 0 ), m_locked( false )
{
	m_layout.blockWidth = m_layout.blockHeight = 1;
	m_layout.bytesPerBlock = 0;
}

bool PosixImage::create( D3DFORMAT format, unsigned int width, unsigned int height, unsigned int depth )
{
	PosixFormatLayout layout;
	if (!posixFormatLayout( format, &layout ) || width == 0 || height == 0 || depth == 0)
		return false;

	const uint64_t blocksAcross = (width + layout.blockWidth - 1) / layout.blockWidth;
	const uint64_t blocksDown = (height + layout.blockHeight - 1) / layout.blockHeight;
	const uint64_t rowPitch = blocksAcross * layout.bytesPerBlock;
	const uint64_t slicePitch = rowPitch * blocksDown;
	const uint64_t total = slicePitch * depth;
	if (rowPitch > 0x7fffffffu || total > ((uint64_t)1 << 32))
		return false;		// a pitch has to fit D3DLOCKED_RECT's int, and nothing the game makes is near

	try
	{
		m_bytes.assign( (size_t)total, 0 );
	}
	catch (const std::bad_alloc &)
	{
		return false;
	}
	m_format = format;
	m_width = width;
	m_height = height;
	m_depth = depth;
	m_layout = layout;
	m_rowPitch = (unsigned int)rowPitch;
	m_rowCount = (unsigned int)blocksDown;
	m_slicePitch = (unsigned int)slicePitch;
	m_version = 0;
	m_locked = false;
	return true;
}

RenderResult PosixImage::lockRegion( unsigned int left, unsigned int top, unsigned int front,
	unsigned int right, unsigned int bottom, unsigned int back, RenderUInt32 flags, uint8_t **address )
{
	if (m_locked || m_bytes.empty())
		return D3DERR_INVALIDCALL;
	if (left >= right || top >= bottom || front >= back || right > m_width || bottom > m_height || back > m_depth)
		return D3DERR_INVALIDCALL;
	// A block format is addressed by whole blocks: a region must start on a block and end on one or
	// at the image's edge.
	const unsigned int bw = m_layout.blockWidth, bh = m_layout.blockHeight;
	if (left % bw != 0 || top % bh != 0)
		return D3DERR_INVALIDCALL;
	if ((right % bw != 0 && right != m_width) || (bottom % bh != 0 && bottom != m_height))
		return D3DERR_INVALIDCALL;

	*address = &m_bytes[0] + (size_t)front * m_slicePitch + (size_t)(top / bh) * m_rowPitch
		+ (size_t)(left / bw) * m_layout.bytesPerBlock;
	m_locked = true;
	if ((flags & D3DLOCK_READONLY) == 0)
		++m_version;
	return D3D_OK;
}

RenderResult PosixImage::lockRect( D3DLOCKED_RECT *locked, const RenderRect *rect, RenderUInt32 flags )
{
	if (locked == NULL || m_depth != 1)
		return D3DERR_INVALIDCALL;
	unsigned int left = 0, top = 0, right = m_width, bottom = m_height;
	if (rect != NULL)
	{
		if (rect->left < 0 || rect->top < 0 || rect->right < 0 || rect->bottom < 0)
			return D3DERR_INVALIDCALL;
		left = (unsigned int)rect->left;
		top = (unsigned int)rect->top;
		right = (unsigned int)rect->right;
		bottom = (unsigned int)rect->bottom;
	}
	uint8_t *address = NULL;
	const RenderResult result = lockRegion( left, top, 0, right, bottom, 1, flags, &address );
	if (result != D3D_OK)
	{
		locked->pBits = NULL;
		locked->Pitch = 0;
		return result;
	}
	locked->pBits = address;
	locked->Pitch = (int)m_rowPitch;
	return D3D_OK;
}

RenderResult PosixImage::lockBox( D3DLOCKED_BOX *locked, const D3DBOX *box, RenderUInt32 flags )
{
	if (locked == NULL)
		return D3DERR_INVALIDCALL;
	unsigned int left = 0, top = 0, front = 0, right = m_width, bottom = m_height, back = m_depth;
	if (box != NULL)
	{
		left = box->Left; top = box->Top; front = box->Front;
		right = box->Right; bottom = box->Bottom; back = box->Back;
	}
	uint8_t *address = NULL;
	const RenderResult result = lockRegion( left, top, front, right, bottom, back, flags, &address );
	if (result != D3D_OK)
	{
		locked->pBits = NULL;
		locked->RowPitch = locked->SlicePitch = 0;
		return result;
	}
	locked->pBits = address;
	locked->RowPitch = (int)m_rowPitch;
	locked->SlicePitch = (int)m_slicePitch;
	return D3D_OK;
}

RenderResult PosixImage::unlock()
{
	if (!m_locked)
		return D3DERR_INVALIDCALL;
	m_locked = false;
	return D3D_OK;
}
