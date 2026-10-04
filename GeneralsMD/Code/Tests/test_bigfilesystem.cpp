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

// The BIG archive reader on POSIX, against a real Zero Hour and Generals install (C1 (f)).
//
// Win32BIGFileSystem is built here as it is on Windows - one source - so this checks what the
// platform underneath it gives it: the listing (PosixLocalFileSystem), the files (LocalFile, RAMFile),
// the mount order they produce, and the bytes that come out.  Two things are compared with an
// independent reading of the archives, written here from the rules rather than from the code:
//   - for every path in every archive, which archive the game resolves it to (the root's *.big in
//     name order, first claim kept; the Patch*.big again, each overwriting; the base game's *.big
//     filling what is left; in Art\Textures, the larger of TexturesZH.big's and Textures.big's copy);
//   - for every path the game can open, the bytes it reads, against the bytes at the owning
//     archive's offset.
// It prints a hash of every path and its bytes: a Windows run should print the same one.
//
// RULE 9: the engine never runs with the real install as its root.  The test builds a farm of
// symbolic links to the install's archives under $TMPDIR, makes it read-only, and mounts that.  The
// install itself is only read.  Needs ZH_DATA_DIR (a folder holding generals/ and zerohour/); without
// it the test exits 77, which ctest reports as Skipped.

#include "test_harness.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "Common/ArchiveFileSystem.h"
#include "Common/AsciiString.h"
#include "Common/file.h"
#include "Common/GameMemory.h"
#include "Common/LocalFileSystem.h"
#include "PosixDevice/Common/PosixLocalFileSystem.h"
#include "Win32Device/Common/Win32BIGFileSystem.h"

#include "posixpath.h"

extern int g_messageBoxes;

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
	return s.size() >= tail.size() && lower(s.substr(s.size() - tail.size())) == lower(tail);
}

struct Entry
{
	std::string path;		// as stored, lowercased, '/' as '\'
	unsigned int offset;
	unsigned int size;
};

struct Archive
{
	std::string name;		// as the game names it: relative to the root, '\' joined
	std::string real;		// where the test reads it
	std::vector<Entry> entries;
	std::map<std::string, size_t> byPath;
};

unsigned int big_endian(const unsigned char * p)
{
	return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) | ((unsigned int)p[2] << 8) | p[3];
}

// A BIG archive's directory, read straight from the file: "BIGF", size, count (big-endian), header
// size, then offset, size (both big-endian) and a NUL-terminated name per entry.  False when the file
// does not start with "BIGF".
bool read_directory(Archive & archive)
{
	FILE * fp = fopen(archive.real.c_str(), "rb");
	if (fp == NULL) return false;
	unsigned char header[16];
	bool ok = fread(header, 1, 16, fp) == 16 && memcmp(header, "BIGF", 4) == 0;
	if (ok) {
		const unsigned int count = big_endian(header + 8);
		for (unsigned int i = 0; i < count && ok; ++i) {
			unsigned char pair[8];
			ok = fread(pair, 1, 8, fp) == 8;
			std::string name;
			int c;
			while (ok && (c = fgetc(fp)) != 0) {
				if (c == EOF) { ok = false; break; }
				name += (char)(c == '/' ? '\\' : c);
			}
			if (!ok) break;
			Entry entry;
			entry.path = lower(name);
			entry.offset = big_endian(pair);
			entry.size = big_endian(pair + 4);
			archive.byPath[entry.path] = archive.entries.size();
			archive.entries.push_back(entry);
		}
	}
	fclose(fp);
	return ok;
}

std::string bytes_at(const std::string & real, unsigned int offset, unsigned int size)
{
	std::string out(size, '\0');
	const int fd = open(real.c_str(), O_RDONLY);
	if (fd < 0) return "(unreadable)";
	size_t done = 0;
	while (done < size) {
		const ssize_t got = pread(fd, &out[done], size - done, (off_t)(offset + done));
		if (got <= 0) break;
		done += (size_t)got;
	}
	close(fd);
	out.resize(done);
	return out;
}

std::string read_through_game(const std::string & path)
{
	File * file = TheArchiveFileSystem->openFile(path.c_str(), File::READ | File::BINARY);
	if (file == NULL) return "(not opened)";
	std::string out;
	char buffer[65536];
	Int got;
	while ((got = file->read(buffer, sizeof(buffer))) > 0) out.append(buffer, got);
	file->close();
	return out;
}

// The *.big names in a real directory, case-insensitively sorted as the engine's FilenameList sorts.
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

std::string temp_root(const char * name)
{
	const char * temp = getenv("TMPDIR");
	std::string base = (temp != NULL && *temp) ? temp : "/tmp";
	if (!base.empty() && base[base.size() - 1] == '/') base.erase(base.size() - 1);
	char unique[64];
	snprintf(unique, sizeof(unique), "/%s_%d", name, (int)getpid());
	return base + unique;
}

// Links every *.big (the AppleDouble "._" ones too, as the install has them) from `from` into `to`.
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

unsigned long long fnv1a(unsigned long long hash, const std::string & bytes)
{
	for (size_t i = 0; i < bytes.size(); ++i) {
		hash ^= (unsigned char)bytes[i];
		hash *= 1099511628211ULL;
	}
	return hash;
}

const char * game_data()
{
	const char * data = getenv("ZH_DATA_DIR");
	if (data == NULL || data[0] == 0) {
		printf("skip: no game data (ZH_DATA_DIR)\n");
		exit(77);
	}
	return data;
}

// The whole check, with the farm made under `root`.
void run_suite(const std::string & root, const char * where)
{
	const char * data = game_data();
	const std::string zerohour = std::string(data) + "/zerohour";
	const std::string generals = std::string(data) + "/generals";
	printf("  %s: %s\n", where, root.c_str());

	// The farm: the root's archives, Data\INI's stray INIZH.big, and the base game as ZH_Generals.
	const std::string farm = root + "/zerohour";
	mkdir(root.c_str(), 0777);
	link_archives(zerohour, farm);
	mkdir((farm + "/Data").c_str(), 0777);
	link_archives(zerohour + "/Data/INI", farm + "/Data/INI");
	link_archives(generals, farm + "/ZH_Generals");
	chmod((farm + "/ZH_Generals").c_str(), 0555);
	chmod((farm + "/Data/INI").c_str(), 0555);
	chmod((farm + "/Data").c_str(), 0555);
	chmod(farm.c_str(), 0555);
	setenv("ZH_USER_DATA_DIR", (root + "/userdata").c_str(), 1);		// no Options.ini, no Registry.ini; decided once a process

	// ---- the independent reading ----
	std::vector<Archive> archives;
	int notBig = 0;
	const std::vector<std::string> rootNames = big_names_in(farm);
	const std::vector<std::string> baseNames = big_names_in(farm + "/ZH_Generals");
	std::vector<size_t> rootOrder, patchOrder, baseOrder;
	for (size_t i = 0; i < rootNames.size() + baseNames.size(); ++i) {
		const bool base = i >= rootNames.size();
		Archive archive;
		archive.name = base ? "ZH_Generals\\" + baseNames[i - rootNames.size()] : rootNames[i];
		archive.real = base ? farm + "/ZH_Generals/" + baseNames[i - rootNames.size()] : farm + "/" + rootNames[i];
		if (!read_directory(archive)) {
			++notBig;
			continue;
		}
		(base ? baseOrder : rootOrder).push_back(archives.size());
		if (!base && strncasecmp(archive.name.c_str(), "patch", 5) == 0) patchOrder.push_back(archives.size());
		archives.push_back(archive);
	}
	std::map<std::string, size_t> owner;
	for (size_t i = 0; i < rootOrder.size(); ++i) {
		for (size_t e = 0; e < archives[rootOrder[i]].entries.size(); ++e) owner.insert(std::make_pair(archives[rootOrder[i]].entries[e].path, rootOrder[i]));
	}
	for (size_t i = 0; i < patchOrder.size(); ++i) {
		for (size_t e = 0; e < archives[patchOrder[i]].entries.size(); ++e) owner[archives[patchOrder[i]].entries[e].path] = patchOrder[i];
	}
	for (size_t i = 0; i < baseOrder.size(); ++i) {
		for (size_t e = 0; e < archives[baseOrder[i]].entries.size(); ++e) owner.insert(std::make_pair(archives[baseOrder[i]].entries[e].path, baseOrder[i]));
	}
	size_t superior = archives.size();
	for (size_t i = 0; i < archives.size(); ++i) {
		if (ends_with_nocase(archives[i].name, "Textures.big") && superior == archives.size()) superior = i;
	}
	int texturesMoved = 0;
	if (superior < archives.size()) {
		for (std::map<std::string, size_t>::iterator it = owner.begin(); it != owner.end(); ++it) {
			const std::string & path = it->first;
			if (path.compare(0, 13, "art\\textures\\") != 0 || path.find('\\', 13) != std::string::npos) continue;
			if (!ends_with_nocase(archives[it->second].name, "TexturesZH.big")) continue;
			std::map<std::string, size_t>::iterator there = archives[superior].byPath.find(path);
			if (there == archives[superior].byPath.end()) continue;
			const Archive & inferior = archives[it->second];
			if (archives[superior].entries[there->second].size > inferior.entries[inferior.byPath.find(path)->second].size) {
				it->second = superior;
				++texturesMoved;
			}
		}
	}
	printf("  %zu archives (%d files named .big that are not archives), %zu paths, %d textures moved to the larger copy\n",
		archives.size(), notBig, owner.size(), texturesMoved);
	CHECK(archives.size() >= 30);
	CHECK(baseOrder.size() > 0);

	// ---- the game's reading ----
	char previous[4096];
	CHECK(getcwd(previous, sizeof(previous)) != NULL);
	CHECK_EQ(chdir(farm.c_str()), 0);
	static bool booted = false;
	if (!booted) {
		initMemoryManager();
		booted = true;
	}
	delete TheArchiveFileSystem;
	delete TheLocalFileSystem;
	PosixPath_Forget_All();
	TheLocalFileSystem = NEW PosixLocalFileSystem;
	TheArchiveFileSystem = NEW Win32BIGFileSystem;
	const int messageBoxesBefore = g_messageBoxes;
	TheArchiveFileSystem->init();
	CHECK_EQ(g_messageBoxes, messageBoxesBefore);		// the base game was found in ZH_Generals, as the fallback list says

	// A name whose last component has no '.' is never filed as a file: the directory walk takes every
	// token without a '.' for a directory (as openFile's does - defect 12), on Windows as here.  So the
	// game cannot open such an entry at all; the rules leave it out, and it is counted.
	int unaddressable = 0;
	for (std::map<std::string, size_t>::iterator it = owner.begin(); it != owner.end(); ) {
		const std::string & path = it->first;
		const size_t slash = path.rfind('\\');
		const std::string leaf = slash == std::string::npos ? path : path.substr(slash + 1);
		if (leaf.find('.') == std::string::npos) {
			printf("  not addressable, as on Windows: %s (in %s)\n", path.c_str(), archives[it->second].name.c_str());
			++unaddressable;
			owner.erase(it++);
		} else {
			++it;
		}
	}
	CHECK(unaddressable < 10);

	int wrongOwner = 0;
	for (std::map<std::string, size_t>::iterator it = owner.begin(); it != owner.end(); ++it) {
		const AsciiString got = TheArchiveFileSystem->getArchiveFilenameForFile(AsciiString(it->first.c_str()));
		if (std::string(got.str()) != archives[it->second].name) {
			if (++wrongOwner <= 10) printf("  %s: the game has %s, the rules say %s\n", it->first.c_str(), got.str(), archives[it->second].name.c_str());
		}
	}
	CHECK_EQ(wrongOwner, 0);

	// The bytes: every file the game can open, in path order.
	int compared = 0, differing = 0;
	unsigned long long hash = 1469598103934665603ULL;
	for (std::map<std::string, size_t>::iterator it = owner.begin(); it != owner.end(); ++it) {
		const std::string & path = it->first;
		const Archive & archive = archives[it->second];
		const Entry & entry = archive.entries[archive.byPath.find(path)->second];
		const std::string expected = bytes_at(archive.real, entry.offset, entry.size);
		const std::string got = read_through_game(path);
		++compared;
		if (got != expected) {
			if (++differing <= 10) printf("  %s: %zu bytes read, %zu expected\n", path.c_str(), got.size(), expected.size());
		}
		hash = fnv1a(fnv1a(hash, path), got);
	}
	CHECK_EQ(differing, 0);
	CHECK_EQ(compared, (int)owner.size());
	printf("  compared all %d files byte for byte; hash of paths and bytes %016llx (a Windows run should match)\n", compared, hash);

	CHECK_EQ(chdir(previous), 0);
	chmod(farm.c_str(), 0755);
	chmod((farm + "/Data").c_str(), 0755);
	chmod((farm + "/Data/INI").c_str(), 0755);
	chmod((farm + "/ZH_Generals").c_str(), 0755);
	const std::string command = "rm -rf '" + root + "'";
	if (system(command.c_str()) != 0) printf("  could not remove %s\n", root.c_str());
}

} // namespace

TEST(big_archives_mount_as_windows_mounts_them_on_this_volume)
{
	run_suite(temp_root("test_bigfilesystem"), "default volume");
}

// The archives' names come off the disk and the engine asks for them again in its own spelling
// ("ZH_Generals\\Textures.big", "Patch*.big"), so a case-sensitive volume is where a mismatch would
// show.  A small case-sensitive APFS image holds the farm; the links in it point at the install.
TEST(big_archives_mount_as_windows_mounts_them_on_a_case_sensitive_volume)
{
	game_data();
#if defined(__APPLE__)
	const std::string image_root = temp_root("test_bigfilesystem_image");
	const std::string image = image_root + ".sparseimage";
	const std::string mount = image_root + "_mount";
	mkdir(mount.c_str(), 0777);
	const std::string create = "hdiutil create -quiet -size 16m -type SPARSE -fs 'Case-sensitive APFS' -volname ZHBIG '" + image_root + "' 2>&1";
	const std::string attach = "hdiutil attach -quiet -nobrowse -mountpoint '" + mount + "' '" + image + "' 2>&1";
	if (system(create.c_str()) != 0 || system(attach.c_str()) != 0) {
		printf("  FAILED to create or attach a case-sensitive APFS image at %s; hdiutil is required on macOS\n", mount.c_str());
		CHECK(false);
		remove(image.c_str());
		rmdir(mount.c_str());
		return;
	}
	run_suite(mount + "/root", "case-sensitive APFS image");
	const std::string detach = "hdiutil detach -quiet '" + mount + "' 2>&1";
	if (system(detach.c_str()) != 0) printf("  could not detach %s\n", mount.c_str());
	remove(image.c_str());
	rmdir(mount.c_str());
#else
	printf("  not needed off macOS: the default-volume run is already case-sensitive here\n");
#endif
}
