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

// PosixImageOps.cpp: see PosixImageOps.h.

#include "PosixImageOps.h"

#include <math.h>
#include <new>
#include <string.h>
#include <vector>

bool posixRegionOf( const PosixImage &image, const RenderRect *rect, PosixRegion *region )
{
	if (rect == NULL)
	{
		region->left = region->top = 0;
		region->right = image.width();
		region->bottom = image.height();
		return image.width() > 0 && image.height() > 0;
	}
	if (rect->left < 0 || rect->top < 0 || rect->right <= rect->left || rect->bottom <= rect->top)
		return false;
	if ((unsigned int)rect->right > image.width() || (unsigned int)rect->bottom > image.height())
		return false;
	region->left = (unsigned int)rect->left;
	region->top = (unsigned int)rect->top;
	region->right = (unsigned int)rect->right;
	region->bottom = (unsigned int)rect->bottom;
	return true;
}

namespace {

inline unsigned int regionWidth( const PosixRegion &r ) { return r.right - r.left; }
inline unsigned int regionHeight( const PosixRegion &r ) { return r.bottom - r.top; }

/** Whether a block format can address this region: it starts on a block, and ends on one or at the edge. */
bool blockAligned( const PosixImage &image, const PosixRegion &r )
{
	const unsigned int bw = image.layout().blockWidth, bh = image.layout().blockHeight;
	return r.left % bw == 0 && r.top % bh == 0
		&& (r.right % bw == 0 || r.right == image.width())
		&& (r.bottom % bh == 0 || r.bottom == image.height());
}

/** The bytes of whole blocks, one row of blocks at a time, from one image to another of the same format. */
void copyBlocks( PosixImage &dest, const PosixRegion &d, const PosixImage &source, const PosixRegion &s )
{
	const PosixFormatLayout &layout = source.layout();
	const unsigned int blocksAcross = (regionWidth( s ) + layout.blockWidth - 1) / layout.blockWidth;
	const unsigned int blockRows = (regionHeight( s ) + layout.blockHeight - 1) / layout.blockHeight;
	const size_t rowBytes = (size_t)blocksAcross * layout.bytesPerBlock;
	for (unsigned int row = 0; row < blockRows; ++row)
	{
		const uint8_t *from = source.bytes() + (size_t)(s.top / layout.blockHeight + row) * source.rowPitch()
			+ (size_t)(s.left / layout.blockWidth) * layout.bytesPerBlock;
		uint8_t *to = dest.bytes() + (size_t)(d.top / layout.blockHeight + row) * dest.rowPitch()
			+ (size_t)(d.left / layout.blockWidth) * layout.bytesPerBlock;
		memmove( to, from, rowBytes );
	}
}

/** A region of an image as colours, row by row. */
bool decodeRegion( const PosixImage &image, const PosixRegion &r, std::vector<PosixColor> &out )
{
	if (!posixCanDecode( image.format() ))
		return false;
	const PosixFormatLayout &layout = image.layout();
	const unsigned int width = regionWidth( r ), height = regionHeight( r );
	out.resize( (size_t)width * height );
	PosixColor block[16];
	const unsigned int firstBlockX = r.left / layout.blockWidth, lastBlockX = (r.right - 1) / layout.blockWidth;
	const unsigned int firstBlockY = r.top / layout.blockHeight, lastBlockY = (r.bottom - 1) / layout.blockHeight;
	for (unsigned int by = firstBlockY; by <= lastBlockY; ++by)
	{
		for (unsigned int bx = firstBlockX; bx <= lastBlockX; ++bx)
		{
			const uint8_t *address = image.bytes() + (size_t)by * image.rowPitch() + (size_t)bx * layout.bytesPerBlock;
			if (!posixDecodeBlock( image.format(), address, block ))
				return false;
			for (unsigned int py = 0; py < layout.blockHeight; ++py)
			{
				const unsigned int y = by * layout.blockHeight + py;
				if (y < r.top || y >= r.bottom)
					continue;
				for (unsigned int px = 0; px < layout.blockWidth; ++px)
				{
					const unsigned int x = bx * layout.blockWidth + px;
					if (x < r.left || x >= r.right)
						continue;
					out[(size_t)(y - r.top) * width + (x - r.left)] = block[py * layout.blockWidth + px];
				}
			}
		}
	}
	return true;
}

/** Colours, row by row, into a region of an uncompressed image. */
bool encodeRegion( PosixImage &image, const PosixRegion &r, const std::vector<PosixColor> &colors )
{
	if (!posixCanEncode( image.format() ))
		return false;
	const unsigned int width = regionWidth( r ), height = regionHeight( r );
	const unsigned int bytes = image.layout().bytesPerBlock;
	for (unsigned int y = 0; y < height; ++y)
	{
		uint8_t *row = image.bytes() + (size_t)(r.top + y) * image.rowPitch() + (size_t)r.left * bytes;
		for (unsigned int x = 0; x < width; ++x)
			posixEncodePixel( image.format(), colors[(size_t)y * width + x], row + (size_t)x * bytes );
	}
	return true;
}

PosixColor at( const std::vector<PosixColor> &image, unsigned int width, unsigned int x, unsigned int y )
{
	return image[(size_t)y * width + x];
}

/** Resamples a sw x sh image of colours to dw x dh. */
void resample( const std::vector<PosixColor> &source, unsigned int sw, unsigned int sh,
	std::vector<PosixColor> &dest, unsigned int dw, unsigned int dh, PosixFilter filter )
{
	dest.assign( (size_t)dw * dh, PosixColor() );
	const PosixColor clear = { 0.0f, 0.0f, 0.0f, 0.0f };
	for (unsigned int y = 0; y < dh; ++y)
	{
		for (unsigned int x = 0; x < dw; ++x)
		{
			PosixColor &out = dest[(size_t)y * dw + x];
			switch (filter)
			{
				case POSIX_FILTER_NONE:
					out = (x < sw && y < sh) ? at( source, sw, x, y ) : clear;
					break;

				case POSIX_FILTER_POINT:
				{
					unsigned int sx = (unsigned int)(((double)x + 0.5) * sw / dw);
					unsigned int sy = (unsigned int)(((double)y + 0.5) * sh / dh);
					if (sx >= sw) sx = sw - 1;
					if (sy >= sh) sy = sh - 1;
					out = at( source, sw, sx, sy );
					break;
				}

				case POSIX_FILTER_LINEAR:
				{
					double u = ((double)x + 0.5) * sw / dw - 0.5, v = ((double)y + 0.5) * sh / dh - 0.5;
					if (u < 0) u = 0;
					if (v < 0) v = 0;
					unsigned int x0 = (unsigned int)u, y0 = (unsigned int)v;
					if (x0 >= sw) x0 = sw - 1;
					if (y0 >= sh) y0 = sh - 1;
					const unsigned int x1 = (x0 + 1 < sw) ? x0 + 1 : x0, y1 = (y0 + 1 < sh) ? y0 + 1 : y0;
					const float fx = (float)(u - x0), fy = (float)(v - y0);
					const PosixColor a = at( source, sw, x0, y0 ), b = at( source, sw, x1, y0 );
					const PosixColor c = at( source, sw, x0, y1 ), d = at( source, sw, x1, y1 );
					out.r = (a.r * (1 - fx) + b.r * fx) * (1 - fy) + (c.r * (1 - fx) + d.r * fx) * fy;
					out.g = (a.g * (1 - fx) + b.g * fx) * (1 - fy) + (c.g * (1 - fx) + d.g * fx) * fy;
					out.b = (a.b * (1 - fx) + b.b * fx) * (1 - fy) + (c.b * (1 - fx) + d.b * fx) * fy;
					out.a = (a.a * (1 - fx) + b.a * fx) * (1 - fy) + (c.a * (1 - fx) + d.a * fx) * fy;
					break;
				}

				case POSIX_FILTER_BOX:
				default:
				{
					// The source area this pixel covers, and each source pixel weighted by how much of it.
					const double x0 = (double)x * sw / dw, x1 = (double)(x + 1) * sw / dw;
					const double y0 = (double)y * sh / dh, y1 = (double)(y + 1) * sh / dh;
					double r = 0, g = 0, b = 0, a = 0, total = 0;
					for (unsigned int sy = (unsigned int)y0; sy < sh && sy < y1; ++sy)
					{
						const double wy = fmin( y1, sy + 1.0 ) - fmax( y0, (double)sy );
						if (wy <= 0) continue;
						for (unsigned int sx = (unsigned int)x0; sx < sw && sx < x1; ++sx)
						{
							const double wx = fmin( x1, sx + 1.0 ) - fmax( x0, (double)sx );
							if (wx <= 0) continue;
							const double w = wx * wy;
							const PosixColor s = at( source, sw, sx, sy );
							r += s.r * w; g += s.g * w; b += s.b * w; a += s.a * w;
							total += w;
						}
					}
					if (total > 0)
					{
						out.r = (float)(r / total); out.g = (float)(g / total);
						out.b = (float)(b / total); out.a = (float)(a / total);
					}
					else
						out = clear;
					break;
				}
			}
		}
	}
}

} // namespace

RenderResult posixCopyImage( PosixImage &dest, const PosixRegion &destRegion,
	const PosixImage &source, const PosixRegion &sourceRegion, PosixFilter filter )
{
	if (dest.bytes() == NULL || source.bytes() == NULL)
		return D3DERR_INVALIDCALL;
	const unsigned int sw = regionWidth( sourceRegion ), sh = regionHeight( sourceRegion );
	const unsigned int dw = regionWidth( destRegion ), dh = regionHeight( destRegion );
	if (sw == 0 || sh == 0 || dw == 0 || dh == 0)
		return D3DERR_INVALIDCALL;

	// The same format at the same size: the blocks themselves, which is also the only way DXT moves.
	// Both regions block-aligned and equal in size means a partial last block can only be where both
	// end at their image's edge, so the blocks line up one for one.
	if (dest.format() == source.format() && sw == dw && sh == dh
			&& blockAligned( source, sourceRegion ) && blockAligned( dest, destRegion ))
	{
		copyBlocks( dest, destRegion, source, sourceRegion );
		dest.markWritten();
		return D3D_OK;
	}

	if (!posixCanDecode( source.format() ) || !posixCanEncode( dest.format() ))
		return D3DERR_INVALIDCALL;

	std::vector<PosixColor> colors, scaled;
	try
	{
		if (!decodeRegion( source, sourceRegion, colors ))
			return D3DERR_INVALIDCALL;
		if (sw == dw && sh == dh)
			scaled.swap( colors );
		else
			resample( colors, sw, sh, scaled, dw, dh, filter );
	}
	catch (const std::bad_alloc &)
	{
		return POSIX_D3D_OUTOFMEMORY;
	}
	if (!encodeRegion( dest, destRegion, scaled ))
		return D3DERR_INVALIDCALL;
	dest.markWritten();
	return D3D_OK;
}

RenderResult posixFillImage( PosixImage &dest, const PosixRegion &region, const PosixColor &color )
{
	if (dest.bytes() == NULL || !posixCanEncode( dest.format() ))
		return D3DERR_INVALIDCALL;
	uint8_t pixel[16];
	posixEncodePixel( dest.format(), color, pixel );
	const unsigned int bytes = dest.layout().bytesPerBlock;
	for (unsigned int y = region.top; y < region.bottom; ++y)
	{
		uint8_t *row = dest.bytes() + (size_t)y * dest.rowPitch() + (size_t)region.left * bytes;
		for (unsigned int x = region.left; x < region.right; ++x, row += bytes)
			memcpy( row, pixel, bytes );
	}
	dest.markWritten();
	return D3D_OK;
}

bool posixWriteFromBgra( PosixImage &image, const uint8_t *bgra, unsigned int width, unsigned int height )
{
	if (bgra == NULL || image.bytes() == NULL || width != image.width() || height != image.height()
		|| image.depth() != 1)
		return false;
	const D3DFORMAT format = image.format();
	if (format != D3DFMT_A8R8G8B8 && format != D3DFMT_X8R8G8B8 && format != D3DFMT_R5G6B5)
		return false;
	const size_t sourcePitch = (size_t)width * 4;
	for (unsigned int y = 0; y < height; ++y)
	{
		const uint8_t *from = bgra + (size_t)y * sourcePitch;
		uint8_t *to = image.bytes() + (size_t)y * image.rowPitch();
		if (format == D3DFMT_R5G6B5)
		{
			for (unsigned int x = 0; x < width; ++x, from += 4, to += 2)
			{
				const PosixColor colour = { from[2] / 255.0f, from[1] / 255.0f, from[0] / 255.0f, 1.0f };
				posixEncodePixel( format, colour, to );
			}
			continue;
		}
		// D3DFMT_A8R8G8B8 is B, G, R, A in memory: the GPU's bytes as they are.
		memcpy( to, from, sourcePitch );
		if (format == D3DFMT_X8R8G8B8)
			for (unsigned int x = 0; x < width; ++x)
				to[x * 4 + 3] = 0xFF;
	}
	return true;
}

RenderResult posixFillDepth( PosixImage &dest, const PosixRegion &region, RenderUInt32 flags, float z,
	RenderUInt32 stencil )
{
	uint32_t depthMask = 0, stencilMask = 0;
	switch (dest.format())
	{
		case D3DFMT_D24S8:		depthMask = 0xffffff00u; stencilMask = 0xffu; break;
		case D3DFMT_D24X4S4:	depthMask = 0xffffff00u; stencilMask = 0x0fu; break;
		case D3DFMT_D15S1:		depthMask = 0xfffeu; stencilMask = 0x1u; break;
		case D3DFMT_D24X8:		depthMask = 0xffffff00u; break;
		case D3DFMT_D16: case D3DFMT_D16_LOCKABLE:	depthMask = 0xffffu; break;
		case D3DFMT_D32: case D3DFMT_D32F_LOCKABLE:	depthMask = 0xffffffffu; break;
		default:
			return D3DERR_INVALIDCALL;
	}
	uint32_t mask = 0;
	if (flags & D3DCLEAR_ZBUFFER) mask |= depthMask;
	if (flags & D3DCLEAR_STENCIL) mask |= stencilMask;
	if (mask == 0)
		return D3D_OK;

	uint8_t encoded[4] = { 0, 0, 0, 0 };
	posixEncodeDepth( dest.format(), z, stencil, encoded );
	const unsigned int bytes = dest.layout().bytesPerBlock;
	uint32_t fresh = 0;
	memcpy( &fresh, encoded, bytes );		// little-endian, as the formats are laid out
	for (unsigned int y = region.top; y < region.bottom; ++y)
	{
		uint8_t *row = dest.bytes() + (size_t)y * dest.rowPitch() + (size_t)region.left * bytes;
		for (unsigned int x = region.left; x < region.right; ++x, row += bytes)
		{
			uint32_t old = 0;
			memcpy( &old, row, bytes );
			const uint32_t value = (old & ~mask) | (fresh & mask);
			memcpy( row, &value, bytes );
		}
	}
	dest.markWritten();
	return D3D_OK;
}
