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

// Decision 7 phase A2's checkpoint: every texture of the real install goes through WW3D2's own loader
// (TextureClass, TextureLoader, DDSFileClass, Targa, BitmapHandlerClass) into the POSIX device, and is
// read back from the device and checked on the CPU against the file.
//
// The device is brought up the way W3DDisplay::init brings it up with no window, which is -headless
// off Windows (decision 8): WW3D::Init with a null window, then Set_Render_Device.
//
// Which textures: every .tga and .dds path in the install's archives - Art\Textures, its localised
// twin, Art\Terrain and the maps' previews.  The first two are asked for by bare name through
// W3DFileSystem, as the game asks; the others are opened by their path, since the game reads them with
// other code (the terrain tiles) or by path, and so are the 27 Art\Textures files whose names the
// localised folder also holds (the game only ever reaches the localised one).  The loader is WW3D2's
// either way.
//
// What is checked, per texture:
//   - it loaded, and is not the missing-texture stand-in;
//   - the device's format, size and level count;
//   - a .dds: every level's blocks, byte for byte, against the file's;
//   - a .tga: the file decoded here (not by Targa), turned the way the loader turns it (top row
//     first), point-sampled to the power-of-two size as BitmapHandlerClass does; every mip level
//     WW3D2's generator defines, against the same 2x2 combine computed here.  An X8R8G8B8 texture's
//     X byte is not compared;
//   - level 0 read a second way, through GetSurfaceLevel, gives the same bytes.
// It prints a hash of every compared byte: a Windows run should print the same one.
//
// What it cannot see: drawing (A3); textures the game builds itself rather than loads (the terrain
// atlas, render targets); cube and volume textures (the install has none as files); texture reduction
// (the default is none); an HSV shift (house colours are shifted at draw time, not here).  Mip levels
// whose narrower side has reached 1 are not compared: WW3D2's generator writes one pixel or none
// there, on Windows too, so what they hold is the device's initial memory.
//
// RULE 9: the engine never runs with the real install as its root.  The test builds a read-only farm
// of symbolic links to the install's archives under $TMPDIR and mounts that, with the user data in the
// same temporary folder; the install itself is only read.  Needs ZH_DATA_DIR (a folder holding
// generals/ and zerohour/); without it the test exits 77, which ctest reports as Skipped.

#include "test_harness.h"

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Common/ArchiveFileSystem.h"
#include "Common/AsciiString.h"
#include "Common/file.h"
#include "Common/FileSystem.h"
#include "Common/GameMemory.h"
#include "Common/LocalFileSystem.h"
#include "Common/NameKeyGenerator.h"
#include "PosixDevice/Common/PosixLocalFileSystem.h"
#include "Win32Device/Common/Win32BIGFileSystem.h"
#include "W3DDevice/GameClient/W3DFileSystem.h"

#include "posixpath.h"
#include "ffactory.h"
#include "wwfile.h"
#include "wwmath.h"
#include "ww3d.h"
#include "dx8wrapper.h"
#include "texture.h"
#include "missingtexture.h"
#include "ffprobe.h"
#include "dx11runtime.h"

// Main/PosixMain.cpp's, with its values: the test cannot link that file, which holds main().  The window
// is null, as under -headless.
const Char *g_strFile = "data\\Generals.str";
const Char *g_csfFile = "data\\%s\\Generals.csf";
static char s_noAppPrefix[] = "";
char *gAppPrefix = s_noAppPrefix;
RenderWindow ApplicationHWnd = NULL;
Bool ApplicationIsBorderless = FALSE;

namespace {

std::string lower(std::string s)
{
	for (size_t i = 0; i < s.size(); ++i) {
		if (s[i] >= 'A' && s[i] <= 'Z') s[i] = (char)(s[i] - 'A' + 'a');
	}
	return s;
}

bool ends_with_nocase(const std::string & s, const std::string & tail)
{
	return s.size() >= tail.size() && lower(s.substr(s.size() - tail.size())) == tail;
}

bool starts_with(const std::string & s, const char * head)
{
	return s.compare(0, strlen(head), head) == 0;
}

unsigned int big_endian(const unsigned char * p)
{
	return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) | ((unsigned int)p[2] << 8) | p[3];
}

unsigned int le16(const unsigned char * p) { return p[0] | ((unsigned int)p[1] << 8); }
unsigned int le32(const unsigned char * p) { return le16(p) | (le16(p + 2) << 16); }

// The paths in a BIG archive's directory, lowercased with '\' separators; empty if it is not one.
std::vector<std::string> archive_paths(const std::string & real)
{
	std::vector<std::string> paths;
	FILE * fp = fopen(real.c_str(), "rb");
	if (fp == NULL) return paths;
	unsigned char header[16];
	if (fread(header, 1, 16, fp) == 16 && memcmp(header, "BIGF", 4) == 0) {
		const unsigned int count = big_endian(header + 8);
		for (unsigned int i = 0; i < count; ++i) {
			unsigned char pair[8];
			if (fread(pair, 1, 8, fp) != 8) break;
			std::string name;
			int c;
			while ((c = fgetc(fp)) != 0 && c != EOF) name += (char)(c == '/' ? '\\' : c);
			if (c == EOF) break;
			paths.push_back(lower(name));
		}
	}
	fclose(fp);
	return paths;
}

std::vector<std::string> big_names_in(const std::string & directory)
{
	std::vector<std::string> names;
	DIR * dir = opendir(directory.c_str());
	if (dir == NULL) return names;
	while (struct dirent * e = readdir(dir)) {
		const std::string name = e->d_name;
		if (name != "." && name != ".." && ends_with_nocase(name, ".big")) names.push_back(name);
	}
	closedir(dir);
	std::sort(names.begin(), names.end(), [](const std::string & a, const std::string & b) { return strcasecmp(a.c_str(), b.c_str()) < 0; });
	return names;
}

void link_archives(const std::string & from, const std::string & to)
{
	mkdir(to.c_str(), 0777);
	const std::vector<std::string> names = big_names_in(from);
	for (size_t i = 0; i < names.size(); ++i) {
		if (symlink((from + "/" + names[i]).c_str(), (to + "/" + names[i]).c_str()) != 0) {
			printf("  could not link %s\n", names[i].c_str());
		}
	}
}

std::string temp_root(const char * name)
{
	const char * temp = getenv("TMPDIR");
	std::string base = (temp != NULL && *temp) ? temp : "/tmp";
	if (!base.empty() && base[base.size() - 1] == '/') base.erase(base.size() - 1);
	char unique[64];
	snprintf(unique, sizeof(unique), "/%s_%d", name, (int)getpid());
	return base + unique;
}

unsigned long long fnv1a(unsigned long long hash, const void * bytes, size_t n)
{
	const unsigned char * p = (const unsigned char *)bytes;
	for (size_t i = 0; i < n; ++i) {
		hash ^= p[i];
		hash *= 1099511628211ULL;
	}
	return hash;
}

// ---- the file factory: the game's for bare names, the path itself for the rest ----------------------

// A path the game does not look up by bare name (Art\Terrain, a map's preview), opened as it stands
// through TheFileSystem, as GameFileClass opens the path it settles on.
class PathFileClass : public FileClass
{
public:
	PathFileClass(char const * filename) : m_file(NULL) { Set_Name(filename); }
	virtual ~PathFileClass(void) { Close(); }
	virtual char const * File_Name(void) const { return m_name.c_str(); }
	virtual char const * Set_Name(char const * filename) { m_name = filename ? filename : ""; return m_name.c_str(); }
	virtual int Create(void) { return 0; }
	virtual int Delete(void) { return 0; }
	virtual bool Is_Available(int) { return TheFileSystem->doesFileExist(m_name.c_str()) != FALSE; }
	virtual bool Is_Open(void) const { return m_file != NULL; }
	virtual int Open(char const * filename, int rights) { Set_Name(filename); return Open(rights); }
	virtual int Open(int rights)
	{
		if (rights != READ) return false;
		m_file = TheFileSystem->openFile(m_name.c_str(), File::READ | File::BINARY);
		return m_file != NULL;
	}
	virtual int Read(void * buffer, int len) { return m_file ? m_file->read(buffer, len) : 0; }
	virtual int Seek(int pos, int dir)
	{
		const File::seekMode mode = dir == SEEK_SET ? File::START : dir == SEEK_END ? File::END : File::CURRENT;
		return m_file ? m_file->seek(pos, mode) : -1;
	}
	virtual int Size(void) { return m_file ? m_file->size() : -1; }
	virtual int Write(void const *, int) { return 0; }
	virtual void Close(void)
	{
		if (m_file) m_file->close();
		m_file = NULL;
	}

private:
	std::string m_name;
	File * m_file;
};

class TestFileFactoryClass : public FileFactoryClass
{
public:
	virtual FileClass * Get_File(char const * filename)
	{
		if (strchr(filename, '/') != NULL || strchr(filename, '\\') != NULL) return new PathFileClass(filename);
		return TheW3DFileSystem->Get_File(filename);
	}
	virtual void Return_File(FileClass * file)
	{
		if (PathFileClass * path = dynamic_cast<PathFileClass *>(file)) {
			delete path;
		} else {
			TheW3DFileSystem->Return_File(file);
		}
	}
};

TestFileFactoryClass s_factory;

std::string read_through_game(const std::string & path)
{
	File * file = TheFileSystem->openFile(path.c_str(), File::READ | File::BINARY);
	if (file == NULL) return std::string();
	std::string out;
	char buffer[65536];
	Int got;
	while ((got = file->read(buffer, sizeof(buffer))) > 0) out.append(buffer, got);
	file->close();
	return out;
}

// ---- the expectation, from the file alone -----------------------------------------------------------

// A picture as the loader lays it out: top row first, left to right; each texel a D3DFMT_A8R8G8B8 dword.
struct Picture
{
	unsigned int width = 0, height = 0;
	std::vector<uint32_t> texels;
	uint32_t at(unsigned int x, unsigned int y) const { return texels[(size_t)y * width + x]; }
};

// Uncompressed (2) and run-length (10) true-colour TGAs of 24 and 32 bits: every one the install has.
// Anything else is refused with the reason, and counts as a failure.
bool decode_tga(const std::string & bytes, Picture & out, unsigned int & depth, std::string & why)
{
	const unsigned char * b = (const unsigned char *)bytes.data();
	if (bytes.size() < 18) { why = "shorter than a TGA header"; return false; }
	const unsigned int type = b[2];
	depth = b[16];
	const unsigned int descriptor = b[17];
	if (b[1] != 0 || (type != 2 && type != 10) || (depth != 24 && depth != 32)) {
		char text[96];
		snprintf(text, sizeof(text), "colour map %u, type %u, depth %u: not a kind this test decodes", b[1], type, depth);
		why = text;
		return false;
	}
	const unsigned int width = le16(b + 12), height = le16(b + 14), bpp = depth / 8;
	size_t at = 18 + b[0] + (size_t)le16(b + 5) * ((b[7] + 7) / 8);
	out.width = width;
	out.height = height;
	out.texels.assign((size_t)width * height, 0);
	const size_t total = (size_t)width * height;
	size_t stored = 0;
	auto put = [&](const unsigned char * p) {
		const unsigned int row = (unsigned int)(stored / width), column = (unsigned int)(stored % width);
		const unsigned int y = (descriptor & 0x20) ? row : height - 1 - row;
		const unsigned int x = (descriptor & 0x10) ? width - 1 - column : column;
		const uint32_t alpha = bpp == 4 ? p[3] : 0xFF;
		out.texels[(size_t)y * width + x] = p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | (alpha << 24);
		++stored;
	};
	while (stored < total) {
		if (type == 2) {
			if (at + bpp > bytes.size()) break;
			put(b + at);
			at += bpp;
		} else {
			if (at >= bytes.size()) break;
			const unsigned int packet = b[at++];
			const unsigned int count = (packet & 0x7F) + 1;
			if (packet & 0x80) {
				if (at + bpp > bytes.size()) break;
				for (unsigned int i = 0; i < count && stored < total; ++i) put(b + at);
				at += bpp;
			} else {
				for (unsigned int i = 0; i < count && stored < total; ++i) {
					if (at + bpp > bytes.size()) break;
					put(b + at);
					at += bpp;
				}
			}
		}
	}
	if (stored != total) { why = "the pixel data ends early"; return false; }
	return true;
}

unsigned int power_of_two_at_least(unsigned int n)
{
	unsigned int p = 1;
	while (p < n) p <<= 1;
	return p;
}

// BitmapHandlerClass::Copy_Image's point sampling: dest (x, y) takes source (x*sw/dw, y*sh/dh).
Picture point_sample(const Picture & source, unsigned int width, unsigned int height)
{
	Picture out;
	out.width = width;
	out.height = height;
	out.texels.resize((size_t)width * height);
	for (unsigned int y = 0; y < height; ++y) {
		for (unsigned int x = 0; x < width; ++x) {
			out.texels[(size_t)y * width + x] = source.at(x * source.width / width, y * source.height / height);
		}
	}
	return out;
}

// The generator's 2x2 combine (BitmapHandlerClass::Combine_A8R8G8B8): each channel's top six bits of
// the four texels, quartered and summed.
Picture combine(const Picture & level)
{
	Picture out;
	out.width = level.width / 2;
	out.height = level.height / 2;
	out.texels.resize((size_t)out.width * out.height);
	for (unsigned int y = 0; y < out.height; ++y) {
		for (unsigned int x = 0; x < out.width; ++x) {
			const uint32_t a = (level.at(2 * x, 2 * y) & 0xfcfcfcfc) >> 2, b = (level.at(2 * x + 1, 2 * y) & 0xfcfcfcfc) >> 2;
			const uint32_t c = (level.at(2 * x, 2 * y + 1) & 0xfcfcfcfc) >> 2, d = (level.at(2 * x + 1, 2 * y + 1) & 0xfcfcfcfc) >> 2;
			out.texels[(size_t)y * out.width + x] = a + b + c + d;
		}
	}
	return out;
}

struct DdsFile
{
	unsigned int width = 0, height = 0, levels = 0, blockBytes = 0;
	D3DFORMAT format = D3DFMT_UNKNOWN;
	std::vector<size_t> offsets, sizes;
};

// A DDS header read here: the size, the level count (0 means 1), a DXT1/3/5 FourCC, and the levels laid
// end to end after the 128-byte header.
bool parse_dds(const std::string & bytes, DdsFile & out, std::string & why)
{
	const unsigned char * b = (const unsigned char *)bytes.data();
	if (bytes.size() < 128 || memcmp(b, "DDS ", 4) != 0 || le32(b + 4) != 124) { why = "no DDS header"; return false; }
	out.height = le32(b + 12);
	out.width = le32(b + 16);
	out.levels = le32(b + 28) ? le32(b + 28) : 1;
	if (!(le32(b + 80) & 4)) { why = "an uncompressed DDS: not a kind this test compares"; return false; }
	if (memcmp(b + 84, "DXT1", 4) == 0) { out.format = D3DFMT_DXT1; out.blockBytes = 8; }
	else if (memcmp(b + 84, "DXT3", 4) == 0) { out.format = D3DFMT_DXT3; out.blockBytes = 16; }
	else if (memcmp(b + 84, "DXT5", 4) == 0) { out.format = D3DFMT_DXT5; out.blockBytes = 16; }
	else { why = "a FourCC this test does not compare"; return false; }
	size_t at = 128;
	for (unsigned int i = 0; i < out.levels; ++i) {
		const unsigned int w = std::max(1u, out.width >> i), h = std::max(1u, out.height >> i);
		const size_t size = (size_t)std::max(1u, (w + 3) / 4) * std::max(1u, (h + 3) / 4) * out.blockBytes;
		out.offsets.push_back(at);
		out.sizes.push_back(size);
		at += size;
	}
	if (at > bytes.size()) { why = "the levels run past the end of the file"; return false; }
	return true;
}

// The loader's level count for a .dds loaded whole (TextureLoadTaskClass::Begin_Compressed_Load): the
// file's, but no level whose shorter side is under 4.
unsigned int dds_levels_kept(const DdsFile & dds)
{
	unsigned int count = 1, w = 4, h = 4;
	while (w < dds.width && h < dds.height) {
		w += w;
		h += h;
		++count;
	}
	return std::min(count, dds.levels);
}

// ---- the run ------------------------------------------------------------------------------------------

struct Tally
{
	int textures = 0, loaded = 0, missing = 0, refused = 0, wrongShape = 0, wrongBytes = 0, secondRead = 0;
	int dds = 0, tga = 0, byName = 0, byPath = 0, shadowed = 0, scaled = 0;
	long long levelsCompared = 0, levelsNotDefined = 0;
	unsigned long long hash = 1469598103934665603ULL;
};

int s_reported = 0;
std::set<std::string> s_localised;		// the leaf names in Data\English\Art\Textures
void report(const std::string & path, const char * what)
{
	if (++s_reported <= 25) printf("  %s: %s\n", path.c_str(), what);
}

// Compares a level's rows against the expectation, `rowBytes` bytes per row (texel or block rows), and
// hashes what matched.  An X8R8G8B8 level compares three bytes of every four.
bool compare_level(IDirect3DTexture9 * texture, unsigned int level, const unsigned char * expected, size_t expectedPitch,
	unsigned int rows, size_t rowBytes, bool skipX, Tally & tally)
{
	D3DLOCKED_RECT locked;
	if (texture->LockRect(level, &locked, NULL, D3DLOCK_READONLY) != D3D_OK) return false;
	bool same = true;
	for (unsigned int r = 0; r < rows && same; ++r) {
		const unsigned char * got = (const unsigned char *)locked.pBits + (size_t)r * locked.Pitch;
		const unsigned char * want = expected + (size_t)r * expectedPitch;
		if (skipX) {
			for (size_t i = 0; i < rowBytes; i += 4) {
				if (memcmp(got + i, want + i, 3) != 0) { same = false; break; }
				tally.hash = fnv1a(tally.hash, got + i, 3);
			}
		} else {
			same = memcmp(got, want, rowBytes) == 0;
			tally.hash = fnv1a(tally.hash, got, rowBytes);
		}
	}
	texture->UnlockRect(level);
	return same;
}

// Level 0 through GetSurfaceLevel, against the texture's own lock.  One lock at a time: D3D9 refuses a
// second lock of a level that is locked.
bool second_read_agrees(IDirect3DTexture9 * texture, unsigned int rows)
{
	IDirect3DSurface9 * surface = NULL;
	if (texture->GetSurfaceLevel(0, &surface) != D3D_OK || surface == NULL) return false;
	D3DLOCKED_RECT locked;
	std::string viaSurface, viaTexture;
	if (surface->LockRect(&locked, NULL, D3DLOCK_READONLY) == D3D_OK) {
		viaSurface.assign((const char *)locked.pBits, (size_t)locked.Pitch * rows);
		surface->UnlockRect();
	}
	surface->Release();
	if (texture->LockRect(0, &locked, NULL, D3DLOCK_READONLY) == D3D_OK) {
		viaTexture.assign((const char *)locked.pBits, (size_t)locked.Pitch * rows);
		texture->UnlockRect(0);
	}
	return !viaSurface.empty() && viaSurface == viaTexture;
}

// Loads one install texture through WW3D2 and checks it.  `mutate` writes into the loaded level 0 before
// the comparison: the controls use it to show the comparison sees the device's memory.
bool check_texture(const std::string & path, Tally & tally, IDirect3DTexture9 * missing, bool mutate = false, bool flipExpectation = false)
{
	++tally.textures;
	const bool isDds = ends_with_nocase(path, ".dds");
	isDds ? ++tally.dds : ++tally.tga;

	// The name the loader is given: bare where the game asks by bare name, the path otherwise.  An
	// Art\Textures file whose name the localised folder also holds is one the game never reaches by
	// name (W3DFileSystem looks in Data\English\Art\Textures first), so it goes by path too.
	std::string name;
	const std::string leaf = path.substr(path.rfind('\\') + 1);
	const bool shadowed = starts_with(path, "art\\textures\\") && s_localised.count(leaf) != 0;
	if (shadowed) ++tally.shadowed;
	if (!shadowed && (starts_with(path, "art\\textures\\") || starts_with(path, "data\\english\\art\\textures\\"))) {
		name = leaf;
		++tally.byName;
	} else {
		name = path;
		std::replace(name.begin(), name.end(), '\\', '/');
		++tally.byPath;
	}

	const std::string bytes = read_through_game(path);
	Picture picture;
	DdsFile dds;
	unsigned int depth = 0;
	std::string why;
	if (isDds ? !parse_dds(bytes, dds, why) : !decode_tga(bytes, picture, depth, why)) {
		++tally.refused;
		report(path, why.c_str());
		return false;
	}

	TextureClass * texture = NEW_REF(TextureClass, (name.c_str(), NULL, MIP_LEVELS_ALL, WW3D_FORMAT_UNKNOWN, isDds, true));
	texture->Init();
	IDirect3DTexture9 * d3d = texture->Peek_D3D_Texture();
	bool ok = true;
	if (d3d == NULL || d3d == missing) {
		++tally.missing;
		report(path, d3d ? "loaded as the missing texture" : "no texture");
		texture->Release_Ref();
		return false;
	}
	++tally.loaded;

	D3DSURFACE_DESC top;
	d3d->GetLevelDesc(0, &top);
	const unsigned int levels = d3d->GetLevelCount();
	if (mutate) {
		D3DLOCKED_RECT locked;
		if (d3d->LockRect(0, &locked, NULL, 0) == D3D_OK) {
			((unsigned char *)locked.pBits)[0] ^= 0x5A;
			d3d->UnlockRect(0);
		}
	}

	if (isDds) {
		const unsigned int wantLevels = dds_levels_kept(dds);
		if (top.Format != dds.format || top.Width != dds.width || top.Height != dds.height || levels != wantLevels) {
			++tally.wrongShape;
			char text[160];
			snprintf(text, sizeof(text), "device %ux%u format %d, %u levels; file %ux%u format %d, %u levels kept",
				top.Width, top.Height, (int)top.Format, levels, dds.width, dds.height, (int)dds.format, wantLevels);
			report(path, text);
			ok = false;
		} else {
			for (unsigned int i = 0; i < levels && ok; ++i) {
				const unsigned int w = std::max(1u, dds.width >> i), h = std::max(1u, dds.height >> i);
				const unsigned int blockRows = std::max(1u, (h + 3) / 4);
				const size_t rowBytes = (size_t)std::max(1u, (w + 3) / 4) * dds.blockBytes;
				if (!compare_level(d3d, i, (const unsigned char *)bytes.data() + dds.offsets[i], rowBytes, blockRows, rowBytes, false, tally)) {
					++tally.wrongBytes;
					char text[64];
					snprintf(text, sizeof(text), "level %u's blocks differ from the file's", i);
					report(path, text);
					ok = false;
				}
				++tally.levelsCompared;
			}
		}
	} else {
		const unsigned int width = power_of_two_at_least(picture.width), height = power_of_two_at_least(picture.height);
		const D3DFORMAT format = depth == 32 ? D3DFMT_A8R8G8B8 : D3DFMT_X8R8G8B8;
		unsigned int wantLevels = 0;
		for (unsigned int w = width, h = height; w > 0 || h > 0; w >>= 1, h >>= 1) ++wantLevels;
		if (width != picture.width || height != picture.height) ++tally.scaled;
		if (top.Format != format || top.Width != width || top.Height != height || levels != wantLevels) {
			++tally.wrongShape;
			char text[160];
			snprintf(text, sizeof(text), "device %ux%u format %d, %u levels; expected %ux%u format %d, %u levels",
				top.Width, top.Height, (int)top.Format, levels, width, height, (int)format, wantLevels);
			report(path, text);
			ok = false;
		} else {
			Picture expected = point_sample(picture, width, height);
			if (flipExpectation) {
				Picture flipped = expected;
				for (unsigned int y = 0; y < height; ++y) {
					memcpy(&flipped.texels[(size_t)y * width], &expected.texels[(size_t)(height - 1 - y) * width], width * 4);
				}
				expected = flipped;
			}
			for (unsigned int i = 0; i < levels && ok; ++i) {
				// Defined while both sides are at least 2; a 1x1 last level is defined when its parent was.
				const bool defined = expected.width >= 2 ? expected.height >= 2 : (expected.width == 1 && expected.height == 1);
				if (!defined) {
					tally.levelsNotDefined += levels - i;
					break;
				}
				if (!compare_level(d3d, i, (const unsigned char *)expected.texels.data(), (size_t)expected.width * 4,
						expected.height, (size_t)expected.width * 4, format == D3DFMT_X8R8G8B8, tally)) {
					++tally.wrongBytes;
					char text[64];
					snprintf(text, sizeof(text), "level %u differs from the file's picture", i);
					report(path, text);
					ok = false;
				}
				++tally.levelsCompared;
				if (expected.width < 2 || expected.height < 2) {
					tally.levelsNotDefined += levels - i - 1;
					break;
				}
				expected = combine(expected);
			}
		}
	}

	if (ok) {
		const unsigned int rows = isDds ? std::max(1u, (top.Height + 3) / 4) : top.Height;
		if (!second_read_agrees(d3d, rows)) {
			++tally.secondRead;
			report(path, "GetSurfaceLevel(0) reads back something else");
			ok = false;
		}
	}
	texture->Release_Ref();
	return ok;
}

// ---- the setup, shared by the tests -------------------------------------------------------------------

std::string s_root;
std::vector<std::string> s_paths;		// every .tga and .dds in the archives, sorted
bool s_ready = false;
int s_initResult = -1, s_deviceResult = -1;

const char * game_data()
{
	const char * data = getenv("ZH_DATA_DIR");
	if (data == NULL || data[0] == 0) {
		printf("skip: no game data (ZH_DATA_DIR)\n");
		exit(77);
	}
	return data;
}

void set_up()
{
	if (s_ready) return;
	s_ready = true;
	const char * data = game_data();
	const std::string zerohour = std::string(data) + "/zerohour";
	const std::string generals = std::string(data) + "/generals";

	// The farm, as test_bigfilesystem makes it: the root's archives, Data\INI's, the base game's.
	s_root = temp_root("test_install_textures");
	const std::string farm = s_root + "/zerohour";
	mkdir(s_root.c_str(), 0777);
	link_archives(zerohour, farm);
	mkdir((farm + "/Data").c_str(), 0777);
	link_archives(zerohour + "/Data/INI", farm + "/Data/INI");
	link_archives(generals, farm + "/ZH_Generals");
	chmod((farm + "/ZH_Generals").c_str(), 0555);
	chmod((farm + "/Data/INI").c_str(), 0555);
	chmod((farm + "/Data").c_str(), 0555);
	chmod(farm.c_str(), 0555);
	setenv("ZH_USER_DATA_DIR", (s_root + "/userdata").c_str(), 1);
	printf("  farm: %s\n", farm.c_str());

	// The list, read from the archives here rather than asked of the game.
	std::set<std::string> paths;
	const char * folders[] = { "", "/Data/INI", "/ZH_Generals" };
	for (size_t f = 0; f < sizeof(folders) / sizeof(folders[0]); ++f) {
		const std::vector<std::string> names = big_names_in(farm + folders[f]);
		for (size_t i = 0; i < names.size(); ++i) {
			if (starts_with(names[i], "._")) continue;
			const std::vector<std::string> inside = archive_paths(farm + folders[f] + "/" + names[i]);
			for (size_t p = 0; p < inside.size(); ++p) {
				if (ends_with_nocase(inside[p], ".tga") || ends_with_nocase(inside[p], ".dds")) paths.insert(inside[p]);
			}
		}
	}
	s_paths.assign(paths.begin(), paths.end());
	for (size_t i = 0; i < s_paths.size(); ++i) {
		if (starts_with(s_paths[i], "data\\english\\art\\textures\\")) s_localised.insert(s_paths[i].substr(s_paths[i].rfind('\\') + 1));
	}

	if (chdir(farm.c_str()) != 0) printf("  could not enter the farm\n");
	initMemoryManager();
	TheNameKeyGenerator = NEW NameKeyGenerator;		// FileSystem's existence cache keys on it
	TheNameKeyGenerator->init();
	TheLocalFileSystem = NEW PosixLocalFileSystem;
	TheArchiveFileSystem = NEW Win32BIGFileSystem;
	TheFileSystem = NEW FileSystem;
	TheFileSystem->init();

	// W3DDisplay::init's order, with the window null: the file factory, the maths, WW3D, the settings
	// it makes before the device, then the device.
	TheW3DFileSystem = NEW W3DFileSystem;
	_TheFileFactory = &s_factory;
	WWMath::Init();
	s_initResult = WW3D::Init(ApplicationHWnd);
	WW3D::Set_Prelit_Mode(WW3D::PRELIT_MODE_LIGHTMAP_MULTI_PASS);
	WW3D::Enable_Static_Sort_Lists(true);
	WW3D::Set_Thumbnail_Enabled(false);
	WW3D::Set_Screen_UV_Bias(TRUE);
	WW3D::Set_Texture_Bitdepth(32);
	FixedFunctionProbe_Enable(true);
	CombinerShaders_Enable(true);
	Direct3D11_Enable(false);
	Direct3D11_Present_Enable(false);
	s_deviceResult = WW3D::Set_Render_Device(0, 800, 600, 32, 1, true);
}

} // namespace

TEST(headless_ww3d_init_reaches_the_cpu_device)
{
	set_up();
	CHECK_EQ(s_initResult, (int)WW3D_ERROR_OK);
	CHECK_EQ(s_deviceResult, (int)WW3D_ERROR_OK);
	CHECK(DX8Wrapper::_Get_D3D_Device() != NULL);
	if (DX8Wrapper::_Get_D3D_Device() == NULL) return;
	printf("  adapter: %s\n", WW3D::Get_Render_Device_Name(WW3D::Get_Render_Device()));
	CHECK(DX8Wrapper::Get_Current_Caps()->Support_DXTC());

	// The implicit back buffer a null-window device still has, at the size asked for.
	IDirect3DSurface9 * back = NULL;
	CHECK_EQ(DX8Wrapper::_Get_D3D_Device()->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back), D3D_OK);
	if (back) {
		D3DSURFACE_DESC desc;
		back->GetDesc(&desc);
		CHECK_EQ(desc.Width, 800u);
		CHECK_EQ(desc.Height, 600u);
		back->Release();
	}

	// The missing-texture stand-in is made with the device; every load that fails ends as it.
	IDirect3DTexture9 * missing = MissingTexture::_Get_Missing_Texture();
	CHECK(missing != NULL);
	if (missing) missing->Release();
}

TEST(every_install_texture_loads_through_ww3d2_and_reads_back)
{
	set_up();
	if (DX8Wrapper::_Get_D3D_Device() == NULL) { CHECK(false); return; }
	IDirect3DTexture9 * missing = MissingTexture::_Get_Missing_Texture();
	Tally tally;
	for (size_t i = 0; i < s_paths.size(); ++i) check_texture(s_paths[i], tally, missing);
	if (missing) missing->Release();

	printf("  %d textures (%d .dds, %d .tga; %d by bare name, %d by path, of which %d Art\\Textures files the localised folder shadows), %d loaded\n",
		tally.textures, tally.dds, tally.tga, tally.byName, tally.byPath, tally.shadowed, tally.loaded);
	printf("  %lld levels compared; %lld levels WW3D2 leaves undefined not compared; %d .tga scaled to a power of two\n",
		tally.levelsCompared, tally.levelsNotDefined, tally.scaled);
	printf("  missing %d, refused by this test's decoder %d, wrong shape %d, wrong bytes %d, second read differs %d\n",
		tally.missing, tally.refused, tally.wrongShape, tally.wrongBytes, tally.secondRead);
	printf("  hash of every compared byte %016llx (a Windows run should match)\n", tally.hash);
	CHECK_EQ((int)s_paths.size(), 7342);
	CHECK_EQ(tally.shadowed, 27);
	CHECK_EQ(tally.loaded, tally.textures);
	CHECK_EQ(tally.missing, 0);
	CHECK_EQ(tally.refused, 0);
	CHECK_EQ(tally.wrongShape, 0);
	CHECK_EQ(tally.wrongBytes, 0);
	CHECK_EQ(tally.secondRead, 0);
}

// The controls: each must make the check above fail, or the check proves nothing.
TEST(the_checks_fail_when_they_should)
{
	set_up();
	if (DX8Wrapper::_Get_D3D_Device() == NULL) { CHECK(false); return; }
	IDirect3DTexture9 * missing = MissingTexture::_Get_Missing_Texture();
	std::string firstDds, firstTga, firstTall;
	for (size_t i = 0; i < s_paths.size(); ++i) {
		if (firstDds.empty() && ends_with_nocase(s_paths[i], ".dds")) firstDds = s_paths[i];
		if (firstTga.empty() && ends_with_nocase(s_paths[i], ".tga")) firstTga = s_paths[i];
	}
	s_reported = 0;
	Tally tally;

	// A byte of the loaded level 0 changed on the device: the comparison sees it.
	CHECK(check_texture(firstDds, tally, missing));
	CHECK(!check_texture(firstDds, tally, missing, true));
	CHECK(check_texture(firstTga, tally, missing));
	CHECK(!check_texture(firstTga, tally, missing, true));

	// The expectation turned upside down: the orientation is checked (on the first TGA whose top and
	// bottom rows differ).
	for (size_t i = 0; i < s_paths.size() && firstTall.empty(); ++i) {
		if (!ends_with_nocase(s_paths[i], ".tga")) continue;
		Picture picture;
		unsigned int depth;
		std::string why;
		if (decode_tga(read_through_game(s_paths[i]), picture, depth, why) && picture.height > 1
			&& memcmp(&picture.texels[0], &picture.texels[(size_t)(picture.height - 1) * picture.width], picture.width * 4) != 0) {
			firstTall = s_paths[i];
		}
	}
	CHECK(!firstTall.empty());
	CHECK(!check_texture(firstTall, tally, missing, false, true));

	// A name nothing holds: the loader gives the missing texture, which is what the check refuses.
	TextureClass * absent = NEW_REF(TextureClass, ("no_such_texture_a2.tga", NULL, MIP_LEVELS_ALL, WW3D_FORMAT_UNKNOWN, true, true));
	absent->Init();
	CHECK(missing != NULL && absent->Peek_D3D_Texture() == missing);
	absent->Release_Ref();
	if (missing) missing->Release();
}

TEST(the_farm_is_removed)
{
	if (s_root.empty()) return;
	const std::string farm = s_root + "/zerohour";
	if (chdir(s_root.c_str()) != 0) printf("  could not leave the farm\n");
	chmod(farm.c_str(), 0755);
	chmod((farm + "/Data").c_str(), 0755);
	chmod((farm + "/Data/INI").c_str(), 0755);
	chmod((farm + "/ZH_Generals").c_str(), 0755);
	const std::string command = "rm -rf '" + s_root + "'";
	CHECK_EQ(system(command.c_str()), 0);
}
