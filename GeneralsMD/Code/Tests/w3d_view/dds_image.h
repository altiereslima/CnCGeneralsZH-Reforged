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
 * A DDS as the game's art ships it: DXT1, DXT3 or DXT5 (BC1, BC2, BC3), 2D, with a mip chain.
 * Measured across TexturesZH.big and Textures.big: 3,964 DXT1, 3,138 DXT5, 9 DXT3, nothing else.
 * The blocks are handed to the GPU as they are; nothing is decoded.
 */
#pragma once

#include <string>
#include <vector>

enum DdsFormat { DDS_BC1, DDS_BC2, DDS_BC3 };

struct DdsLevel
{
	unsigned width;
	unsigned height;
	size_t offset;	// into DdsImage::bytes
	size_t size;
};

struct DdsImage
{
	DdsFormat format;
	std::vector<DdsLevel> levels;
	std::vector<unsigned char> bytes;	// the whole file; levels point into it
};

// False with a reason for anything that is not one of the three formats above.
bool parseDds(std::vector<unsigned char> file, DdsImage *image, std::string *why);
