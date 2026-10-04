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
#include "dds_image.h"

#include <string.h>

namespace {

unsigned littleEndian32(const unsigned char *p)
{
	return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

// DDS_HEADER field offsets, counted from the start of the file (after the 4-byte "DDS " magic).
const size_t HEADER_BYTES = 4 + 124;
const size_t HEIGHT_AT = 4 + 8;
const size_t WIDTH_AT = 4 + 12;
const size_t MIP_COUNT_AT = 4 + 24;
const size_t FOURCC_AT = 4 + 80;
const unsigned DDSD_MIPMAPCOUNT = 0x20000;
const size_t FLAGS_AT = 4 + 4;

} // namespace

bool parseDds(std::vector<unsigned char> file, DdsImage *image, std::string *why)
{
	if (file.size() < HEADER_BYTES || memcmp(&file[0], "DDS ", 4) != 0) {
		*why = "not a DDS";
		return false;
	}
	const unsigned char *fourcc = &file[FOURCC_AT];
	size_t blockBytes;
	if (memcmp(fourcc, "DXT1", 4) == 0) {
		image->format = DDS_BC1;
		blockBytes = 8;
	} else if (memcmp(fourcc, "DXT3", 4) == 0) {
		image->format = DDS_BC2;
		blockBytes = 16;
	} else if (memcmp(fourcc, "DXT5", 4) == 0) {
		image->format = DDS_BC3;
		blockBytes = 16;
	} else {
		*why = std::string("unsupported format ") + std::string((const char *)fourcc, 4);
		return false;
	}

	unsigned width = littleEndian32(&file[WIDTH_AT]);
	unsigned height = littleEndian32(&file[HEIGHT_AT]);
	unsigned mips = (littleEndian32(&file[FLAGS_AT]) & DDSD_MIPMAPCOUNT) ? littleEndian32(&file[MIP_COUNT_AT]) : 1;
	if (mips == 0) mips = 1;

	size_t offset = HEADER_BYTES;
	image->levels.clear();
	for (unsigned level = 0; level < mips; ++level) {
		const size_t blocksWide = (width + 3) / 4;
		const size_t blocksHigh = (height + 3) / 4;
		const size_t size = blocksWide * blocksHigh * blockBytes;
		if (offset + size > file.size()) {
			break;	// a truncated chain keeps the levels it has
		}
		DdsLevel entry;
		entry.width = width;
		entry.height = height;
		entry.offset = offset;
		entry.size = size;
		image->levels.push_back(entry);
		offset += size;
		width = width > 1 ? width / 2 : 1;
		height = height > 1 ? height / 2 : 1;
	}
	if (image->levels.empty()) {
		*why = "no complete mip level";
		return false;
	}
	image->bytes.swap(file);
	return true;
}
