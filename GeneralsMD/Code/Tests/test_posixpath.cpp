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
 * test_posixpath - the engine's paths resolved against a POSIX file system (C1, posixpath.h), and the
 * zh_* forwarders built on it (zhio.h).
 *
 * One suite, run against a fixture tree laid out in the shapes of a real Zero Hour install - the
 * places where the code's spelling and the disk's are known to differ: the cursors
 * (data\cursors\SCCPointer.ANI against Data/Cursors/sccpointer.ani), the Bink movies
 * (Data/english/.../EA_LOGO.bik against Data/English/.../EA_LOGO.BIK), gensecZH.big.
 *
 * Case matters only on a case-sensitive volume, and the default macOS volume is not one - neither is
 * the exFAT the Steam installs sit on.  So the suite runs twice:
 *   - in $TMPDIR, whatever that volume is, and it says which;
 *   - on macOS, on a small case-sensitive APFS image it creates and attaches with hdiutil.  That run
 *     is the proof that case is handled (macOS is the platform this project gates on), so it is not
 *     optional: an image that cannot be created or attached, or one that turns out not to be
 *     case-sensitive, fails the test.  Off macOS the first run is on the platform's own file system,
 *     which on Linux is case-sensitive already.
 * Checks that only mean something on a case-sensitive volume (the on-disk spelling of a result, two
 * names differing only in case) run only there, and say so when they do not.
 */
#include "test_harness.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "posixpath.h"
#include "zhio.h"
#include "rawfile.h"

namespace {

// ---- the fixture, written with plain POSIX calls, independently of the code under test ----

void make_directory(const std::string & path)
{
	mkdir(path.c_str(), 0777);
}

void write_file(const std::string & path, const std::string & contents)
{
	FILE * file = fopen(path.c_str(), "wb");
	if (file != NULL) {
		fwrite(contents.data(), 1, contents.size(), file);
		fclose(file);
	}
}

std::string read_file(const std::string & path)
{
	std::string contents;
	FILE * file = fopen(path.c_str(), "rb");
	if (file == NULL) return contents;
	char buffer[256];
	size_t got;
	while ((got = fread(buffer, 1, sizeof(buffer), file)) > 0) contents.append(buffer, got);
	fclose(file);
	return contents;
}

std::string lower(std::string s)
{
	for (size_t i = 0; i < s.size(); ++i) {
		if (s[i] >= 'A' && s[i] <= 'Z') s[i] = (char)(s[i] - 'A' + 'a');
	}
	return s;
}

void build_fixture(const std::string & root)
{
	make_directory(root);
	make_directory(root + "/Data");
	make_directory(root + "/Data/INI");
	make_directory(root + "/Data/Cursors");
	make_directory(root + "/Data/English");
	make_directory(root + "/Data/English/Movies");
	make_directory(root + "/Data/Scripts");
	make_directory(root + "/Save");
	write_file(root + "/Data/INI/INIZH.big", "big");
	write_file(root + "/Data/Cursors/sccpointer.ani", "cursor");
	write_file(root + "/Data/English/Movies/EA_LOGO.BIK", "movie");
	write_file(root + "/Data/Scripts/SkirmishScripts.scb", "scripts");
	write_file(root + "/gensecZH.big", "gensec");
}

bool is_directory_here(const std::string & path)
{
	struct stat status;
	return stat(path.c_str(), &status) == 0 && S_ISDIR(status.st_mode);
}

// Whether names in this directory differ by case: create one spelling, look for the other.
bool is_case_sensitive(const std::string & directory)
{
	const std::string probe = directory + "/case_probe";
	write_file(probe, "x");
	struct stat status;
	const bool sensitive = stat((directory + "/CASE_PROBE").c_str(), &status) != 0;
	remove(probe.c_str());
	return sensitive;
}

// Resolves for reading and returns the contents, "" when it did not resolve.
std::string resolved_contents(const std::string & engine_path)
{
	std::string real;
	if (!PosixPath_Resolve(engine_path.c_str(), POSIX_PATH_EXISTING, real)) return std::string();
	return read_file(real);
}

std::string resolved(const std::string & engine_path, PosixPathIntent intent)
{
	std::string real;
	if (!PosixPath_Resolve(engine_path.c_str(), intent, real)) return "(unresolved)";
	return real;
}

// The listing, joined with '|', in the order it was found.
std::string listed(const char * current, const char * original, const char * search, bool subdirectories)
{
	std::vector<std::string> found;
	PosixPath_List_Like_Win32(current, original, search, subdirectories, found);
	std::string joined;
	for (size_t i = 0; i < found.size(); ++i) joined += (i ? "|" : "") + found[i];
	return joined;
}

// Win32LocalFileSystem::getFileListInDirectory's contract, relative to the install root.  Returns the
// number of checks that ran only because the volume is case-sensitive.
unsigned listing_checks(const std::string & root, bool sensitive)
{
	unsigned sensitive_only = 0;
	make_directory(root + "/List");
	make_directory(root + "/List/Sub");
	make_directory(root + "/List/Sub/Deep");
	make_directory(root + "/List/Dotted.dir");		// has a dot: "*." passes it over, so no recursion
	make_directory(root + "/List/zdir.ini");		// a directory, however it is named, is not a file
	write_file(root + "/List/a.ini", "");
	write_file(root + "/List/B.INI", "");
	write_file(root + "/List/c.ini.bak", "");
	write_file(root + "/List/noext", "");
	write_file(root + "/List/Patch2.big", "");
	write_file(root + "/List/patch1.BIG", "");
	write_file(root + "/List/x.big", "");
	write_file(root + "/List/Sub/d.ini", "");
	write_file(root + "/List/Sub/Deep/e.ini", "");
	write_file(root + "/List/Dotted.dir/f.ini", "");
	write_file(root + "/List/zdir.ini/g.ini", "");

	// INI::loadDirectory's call: the files in byte order, then each subdirectory's, joined with '\'
	// after the engine's own spelling of the directory.
	CHECK_STR(listed("", "List\\", "*.ini", true).c_str(), "List\\B.INI|List\\a.ini|List\\Sub\\d.ini|List\\Sub\\Deep\\e.ini");
	// The engine's spelling of the directory is kept; what was found carries the disk's.
	CHECK_STR(listed("", "list\\", "*.INI", true).c_str(), "list\\B.INI|list\\a.ini|list\\Sub\\d.ini|list\\Sub\\Deep\\e.ini");
	CHECK_STR(listed("", "List\\", "*.ini", false).c_str(), "List\\B.INI|List\\a.ini");
	CHECK_STR(listed("Sub\\", "List\\", "*.ini", false).c_str(), "List\\Sub\\d.ini");
	CHECK_STR(listed("", "List\\", "Patch*.big", false).c_str(), "List\\Patch2.big|List\\patch1.BIG");
	CHECK_STR(listed("", "List\\", "*", false).c_str(),
		"List\\B.INI|List\\Patch2.big|List\\a.ini|List\\c.ini.bak|List\\noext|List\\patch1.BIG|List\\x.big");
	CHECK_STR(listed("", "List\\", "*.*", false).c_str(), listed("", "List\\", "*", false).c_str());
	CHECK_STR(listed("", "List\\", "*.", false).c_str(), "List\\noext");
	CHECK_STR(listed("", "List\\", "", true).c_str(), "");		// FindFirstFile("dir\\") finds nothing
	CHECK_STR(listed("", "Nowhere\\", "*.ini", true).c_str(), "");
	CHECK_STR(listed("", "", "*.big", false).c_str(), "gensecZH.big");	// the install root itself
	CHECK_STR(listed("", (root + "/List/Sub/").c_str(), "*.ini", false).c_str(), (root + "/List/Sub/d.ini").c_str());

	// Two names differing only in case: both are listed, byte order first, so the engine's
	// case-insensitive set keeps the same one every time.
	if (sensitive) {
		write_file(root + "/List/Sub/D.ini", "");
		CHECK_STR(listed("Sub\\", "List\\", "*.ini", false).c_str(), "List\\Sub\\D.ini|List\\Sub\\d.ini");
		++sensitive_only;
	}
	return sensitive_only;
}

unsigned run_suite(const std::string & root, const char * where)
{
	build_fixture(root);
	const bool sensitive = is_case_sensitive(root);
	printf("  %s: %s, a %s volume\n", where, root.c_str(), sensitive ? "case-SENSITIVE" : "case-insensitive");
	PosixPath_Forget_All();

	char previous[4096];
	if (getcwd(previous, sizeof(previous)) == NULL || chdir(root.c_str()) != 0) {
		printf("  could not enter %s\n", root.c_str());
		CHECK(false);
		return 0;
	}
	unsigned sensitive_only = 0;

	// The code's spelling against the disk's, relative to the install root.
	CHECK_STR(resolved_contents("Data\\INI\\INIZH.big").c_str(), "big");
	CHECK_STR(resolved_contents("data\\cursors\\SCCPointer.ANI").c_str(), "cursor");
	CHECK_STR(resolved_contents("Data/english/Movies/EA_LOGO.bik").c_str(), "movie");
	CHECK_STR(resolved_contents("genseczh.big").c_str(), "gensec");
	CHECK_STR(resolved_contents("Data\\Scripts/SkirmishScripts.scb").c_str(), "scripts");
	CHECK_STR(resolved_contents("Data\\.\\INI\\..\\Scripts\\SKIRMISHSCRIPTS.SCB").c_str(), "scripts");
	CHECK_STR(resolved_contents("Data\\\\Scripts\\\\SkirmishScripts.scb").c_str(), "scripts");	// doubled separators
	if (sensitive) {
		// What the operating system is actually handed.
		CHECK_STR(resolved("data\\cursors\\SCCPointer.ANI", POSIX_PATH_EXISTING).c_str(), "Data/Cursors/sccpointer.ani");
		CHECK_STR(resolved("Data/english/Movies/EA_LOGO.bik", POSIX_PATH_EXISTING).c_str(), "Data/English/Movies/EA_LOGO.BIK");
		CHECK_STR(resolved("genseczh.big", POSIX_PATH_EXISTING).c_str(), "gensecZH.big");
		sensitive_only += 3;
	}

	// Absolute paths: the user-data directory is one, with the engine's joins after it, and some code
	// lowercases the whole thing.
	CHECK_STR(resolved_contents(root + "\\DATA\\SCRIPTS\\skirmishscripts.SCB").c_str(), "scripts");
	CHECK_STR(resolved_contents(lower(root) + "/data/scripts/skirmishscripts.scb").c_str(), "scripts");

	// What does not exist, and what no POSIX system can have.
	std::string real;
	CHECK(!PosixPath_Resolve("Data\\INI\\Nope.ini", POSIX_PATH_EXISTING, real));
	CHECK(!PosixPath_Resolve("", POSIX_PATH_EXISTING, real));
	CHECK(!PosixPath_Resolve("C:\\Games\\Data\\INI\\INIZH.big", POSIX_PATH_EXISTING, real));
	CHECK(!PosixPath_Resolve("\\\\server\\share\\INIZH.big", POSIX_PATH_EXISTING, real));

	// Creating: existing directories keep the disk's spelling, a new last component the engine's.
	if (sensitive) {
		CHECK_STR(resolved("SAVE\\NewGame.sav", POSIX_PATH_CREATE_LEAF).c_str(), "Save/NewGame.sav");
		CHECK_STR(resolved("save\\Sub\\Deeper\\x.txt", POSIX_PATH_CREATE_PATH).c_str(), "Save/Sub/Deeper/x.txt");
		sensitive_only += 2;
	}
	CHECK(!PosixPath_Resolve("NoDir\\x.txt", POSIX_PATH_CREATE_LEAF, real));
	CHECK(PosixPath_Resolve("NoDir\\Sub\\x.txt", POSIX_PATH_CREATE_PATH, real));

	// D2: two names differing only in case.  Only a case-sensitive volume can hold both.
	if (sensitive) {
		make_directory(root + "/Amb");
		write_file(root + "/Amb/Foo.txt", "upper");
		write_file(root + "/Amb/foo.txt", "lower");
		CHECK_STR(resolved_contents("amb\\FOO.TXT").c_str(), "upper");	// no exact spelling: byte order, 'F' < 'f'
		CHECK_STR(resolved_contents("Amb\\foo.txt").c_str(), "lower");	// the exact spelling wins
		CHECK_STR(resolved_contents("Amb\\Foo.txt").c_str(), "upper");
		sensitive_only += 3;
	}

	// A file that appears after its directory was listed, created outside the resolver: the
	// directory's time moves, and the next lookup reads it again.
	CHECK(!PosixPath_Resolve("data\\LATE\\file.txt", POSIX_PATH_EXISTING, real));
	make_directory(root + "/Data/late");
	write_file(root + "/Data/late/FILE.TXT", "late");
	CHECK_STR(resolved_contents("data\\LATE\\file.txt").c_str(), "late");

	// The forwarders.
	FILE * file = zh_fopen("DATA\\SCRIPTS\\New.txt", "w");
	CHECK(file != NULL);
	if (file != NULL) {
		fputs("written", file);
		fclose(file);
	}
	CHECK_STR(read_file(root + "/Data/Scripts/New.txt").c_str(), "written");
	file = zh_fopen("data\\scripts\\NEW.TXT", "r");
	CHECK(file != NULL);
	if (file != NULL) fclose(file);
	CHECK(zh_fopen("Data\\Scripts\\Absent.txt", "r") == NULL && errno == ENOENT);
	CHECK(zh_fopen("Absent\\New.txt", "w") == NULL);	// a missing directory is not created, as on Windows
	CHECK_EQ(zh_access("data\\scripts\\new.txt", F_OK), 0);
	CHECK_EQ(zh_rename("data\\scripts\\new.txt", "DATA\\scripts\\Renamed.txt"), 0);
	CHECK_STR(read_file(root + "/Data/Scripts/Renamed.txt").c_str(), "written");
	CHECK(zh_access("Data\\Scripts\\New.txt", F_OK) != 0);
	CHECK_EQ(zh_remove("data\\SCRIPTS\\renamed.TXT"), 0);
	CHECK(zh_access("Data\\Scripts\\Renamed.txt", F_OK) != 0);
	write_file(root + "/Data/Scripts/Unlinked.txt", "x");
	CHECK_EQ(zh_unlink("DATA\\scripts\\unlinked.TXT"), 0);
	CHECK(zh_access("Data\\Scripts\\Unlinked.txt", F_OK) != 0);
	make_directory(root + "/Data/Scripts/EmptyDir");
	CHECK(zh_unlink("Data\\Scripts\\EmptyDir") != 0);		// unlike zh_remove, never a directory
	CHECK(is_directory_here(root + "/Data/Scripts/EmptyDir"));
	CHECK(zh_unlink("Data\\Scripts\\Absent.txt") != 0 && errno == ENOENT);
	struct stat found;
	CHECK_EQ(zh_stat("data\\INI\\inizh.BIG", &found), 0);
	CHECK(S_ISREG(found.st_mode) && found.st_size == 3);
	CHECK_EQ(zh_stat("DATA\\scripts\\emptydir", &found), 0);
	CHECK(S_ISDIR(found.st_mode));
	CHECK(zh_stat("Data\\INI\\Absent.big", &found) != 0 && errno == ENOENT);
	CHECK_EQ(zh_mkdir("SAVE\\Replays"), 0);
	struct stat status;
	CHECK(stat((root + "/Save/Replays").c_str(), &status) == 0 && S_ISDIR(status.st_mode));
	const int handle = zh_open("save\\replays\\Last.rep", O_CREAT | O_WRONLY | O_TRUNC, 0666);
	CHECK(handle >= 0);
	if (handle >= 0) {
		CHECK_EQ((int)write(handle, "rep", 3), 3);
		close(handle);
	}
	CHECK_STR(read_file(root + "/Save/Replays/Last.rep").c_str(), "rep");

	// RawFileClass (WWLib), which the W3D and INI loaders open files through: its POSIX arms open and
	// delete through zh_open and zh_unlink, so the engine's spelling reaches the file (C1 (d)).
	{
		RawFileClass reader;
		CHECK(reader.Is_Available() == false);
		CHECK(reader.Open("data\\scripts\\SKIRMISHSCRIPTS.SCB", FileClass::READ) != 0);
		char buffer[16] = { 0 };
		CHECK_EQ(reader.Read(buffer, sizeof(buffer) - 1), 7);
		CHECK_STR(buffer, "scripts");
		reader.Close();
		RawFileClass writer("SAVE\\raw.bin");
		CHECK(writer.Open(FileClass::WRITE) != 0);
		CHECK_EQ(writer.Write("raw", 3), 3);
		writer.Close();
		CHECK_STR(read_file(root + "/Save/raw.bin").c_str(), "raw");
		CHECK(writer.Is_Available());
		CHECK(writer.Delete());
		CHECK(zh_access("Save\\raw.bin", F_OK) != 0);
	}

	sensitive_only += listing_checks(root, sensitive);

	if (chdir(previous) != 0) CHECK(false);
	if (!sensitive) {
		printf("  %s: the %u checks that need a case-sensitive volume did not run here\n", where, 9u);
	}
	return sensitive_only;
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

void remove_tree(const std::string & root)
{
	const std::string command = "rm -rf '" + root + "'";
	if (system(command.c_str()) != 0) printf("  could not remove %s\n", root.c_str());
}

} // namespace

// The patterns the engine passes, as FindFirstFile reads them (without 8.3 short names).
TEST(windows_patterns_match_as_findfirstfile_does)
{
	CHECK(PosixPath_Matches_Pattern("*.ini", "GameData.ini"));
	CHECK(PosixPath_Matches_Pattern("*.ini", "WEAPON.INI"));
	CHECK(PosixPath_Matches_Pattern("*.ini", ".ini"));
	CHECK(!PosixPath_Matches_Pattern("*.ini", "GameData.ini.bak"));
	CHECK(!PosixPath_Matches_Pattern("*.ini", "GameDataini"));
	CHECK(PosixPath_Matches_Pattern("*.big", "INIZH.big"));
	CHECK(PosixPath_Matches_Pattern("Patch*.big", "Patch.big"));
	CHECK(PosixPath_Matches_Pattern("Patch*.big", "patch104.BIG"));
	CHECK(!PosixPath_Matches_Pattern("Patch*.big", "MyPatch.big"));
	CHECK(PosixPath_Matches_Pattern("*.w3d", "AVTank.W3D"));
	CHECK(PosixPath_Matches_Pattern("*.tga", "SCCPointer.tga"));
	CHECK(PosixPath_Matches_Pattern("*.big.*", "a.big.x.big.y"));	// backtracking past an earlier match
	CHECK(PosixPath_Matches_Pattern("?.big", "a.big"));
	CHECK(!PosixPath_Matches_Pattern("?.big", "ab.big"));
	CHECK(PosixPath_Matches_Pattern("*", "anything.at.all"));
	CHECK(PosixPath_Matches_Pattern("*.*", "noext"));
	CHECK(PosixPath_Matches_Pattern("*.*", "a.b"));
	CHECK(PosixPath_Matches_Pattern("*.", "Sub"));
	CHECK(!PosixPath_Matches_Pattern("*.", "Dotted.dir"));
	CHECK(!PosixPath_Matches_Pattern("", "a.ini"));
	CHECK(!PosixPath_Matches_Pattern(NULL, "a.ini"));
}

// D6: a TEXT LocalFile reads as Windows' _read reads _O_TEXT, however the reads are sized.
TEST(text_reads_as_windows_text_mode)
{
	const std::string path = temp_root("test_posixpath_text");
	// CRLF lines, a lone CR, a CR before a CR, blank CRLF lines, and a final CR with nothing after it.
	const std::string raw = "[Section]\r\nKey = 1\r\nlone\rcr\r\r\n\r\n\r\nend\r";
	const std::string windows = "[Section]\nKey = 1\nlone\rcr\r\n\n\nend\r";
	write_file(path, raw);
	unsigned mismatched = 0;
	for (unsigned chunk = 1; chunk <= raw.size() + 1; ++chunk) {
		const int handle = open(path.c_str(), O_RDONLY);
		std::string text;
		char buffer[64];
		int got;
		while ((got = zh_read_text(handle, buffer, chunk)) > 0) text.append(buffer, got);
		const off_t end = lseek(handle, 0, SEEK_CUR);
		close(handle);
		if (text != windows || end != (off_t)raw.size()) {
			printf("  chunks of %u read \"%s\", ending at %lld\n", chunk, text.c_str(), (long long)end);
			++mismatched;
		}
	}
	CHECK_EQ(mismatched, 0u);

	// LocalFile::scanString and its siblings read a byte at a time and put the last one back by
	// seeking back one byte: after a "\r\n" read as '\n', that lands on the '\n', as on Windows.
	const int handle = open(path.c_str(), O_RDONLY);
	char c = 0;
	for (int i = 0; i < 10; ++i) zh_read_text(handle, &c, 1);	// "[Section]" and the "\r\n"
	CHECK_EQ((int)c, (int)'\n');
	CHECK_EQ((long long)lseek(handle, 0, SEEK_CUR), 11LL);
	lseek(handle, -1, SEEK_CUR);
	CHECK_EQ(zh_read_text(handle, &c, 1), 1);
	CHECK_EQ((int)c, (int)'\n');
	CHECK_EQ(zh_read_text(handle, &c, 1), 1);
	CHECK_EQ((int)c, (int)'K');
	close(handle);
	remove(path.c_str());
}

TEST(engine_paths_resolve_on_this_volume)
{
	const std::string root = temp_root("test_posixpath");
	run_suite(root, "default volume");
	remove_tree(root);
}

TEST(engine_paths_resolve_on_a_case_sensitive_volume)
{
#if defined(__APPLE__)
	// A 16 MB sparse image, formatted case-sensitive APFS, attached without appearing in Finder.
	const std::string image_root = temp_root("test_posixpath_image");
	const std::string image = image_root + ".sparseimage";
	const std::string mount = image_root + "_mount";
	mkdir(mount.c_str(), 0777);
	const std::string create = "hdiutil create -quiet -size 16m -type SPARSE -fs 'Case-sensitive APFS' -volname ZHCS '" + image_root + "' 2>&1";
	const std::string attach = "hdiutil attach -quiet -nobrowse -mountpoint '" + mount + "' '" + image + "' 2>&1";
	if (system(create.c_str()) != 0 || system(attach.c_str()) != 0) {
		// Not a skip: without this run nothing here has shown that case is handled.
		printf("  FAILED to create or attach a case-sensitive APFS image at %s; hdiutil is required on macOS\n", mount.c_str());
		CHECK(false);
		remove(image.c_str());
		rmdir(mount.c_str());
		return;
	}
	const unsigned sensitive_checks = run_suite(mount + "/root", "case-sensitive APFS image");
	CHECK(sensitive_checks > 0);	// the image really is case-sensitive, or this run proved nothing
	const std::string detach = "hdiutil detach -quiet '" + mount + "' 2>&1";
	if (system(detach.c_str()) != 0) printf("  could not detach %s\n", mount.c_str());
	remove(image.c_str());
	rmdir(mount.c_str());
#else
	printf("  not needed off macOS: the default-volume run is already case-sensitive here\n");
#endif
}

// P1 (decision 9): the fork's overlay, a read root searched before the install for every relative
// path that is read, never written; listings the union of the roots.  The expectations are the
// Windows result of copying the overlay's files into the install's folder, worked out by hand.
TEST(overlay_reads_first_writes_never_lists_as_one_folder)
{
	const std::string base = temp_root("test_posixpath_overlay");
	const std::string install = base + "/install", overlay = base + "/overlay";
	make_directory(base);
	make_directory(install);
	make_directory(install + "/Data");
	make_directory(install + "/Data/INI");
	make_directory(install + "/Data/INI/Object");
	write_file(install + "/Data/INI/GameData.ini", "install gamedata");
	write_file(install + "/Data/INI/Weapon.ini", "install weapon");
	write_file(install + "/Data/INI/Object/Tank.ini", "install tank");
	write_file(install + "/INIZH.big", "install inizh");
	write_file(install + "/TexturesZH.big", "install textures");
	write_file(install + "/Patch.str", "install patch");
	make_directory(overlay);
	make_directory(overlay + "/data");						// another case, as a case-sensitive volume can hold
	make_directory(overlay + "/data/ini");
	make_directory(overlay + "/data/ini/Reforged");
	write_file(overlay + "/data/ini/weapon.INI", "overlay weapon");	// the same file, another spelling
	write_file(overlay + "/data/ini/FXListReforged.ini", "overlay fxlist");
	write_file(overlay + "/data/ini/Reforged/Extra.ini", "overlay extra");
	write_file(overlay + "/ReforgedTextures.big", "overlay textures");
	write_file(overlay + "/Patch.str", "overlay patch");

	char previous[4096];
	CHECK(getcwd(previous, sizeof(previous)) != NULL);
	CHECK_EQ(chdir(install.c_str()), 0);
	PosixPath_Forget_All();

	// armed control: with no overlay nothing of it is seen
	PosixPath_Set_Overlays(std::vector<std::string>());
	CHECK_STR(resolved_contents("Data\\INI\\FXListReforged.ini").c_str(), "");
	CHECK_STR(resolved_contents("Patch.str").c_str(), "install patch");
	CHECK_STR(listed("", "", "*.big", false).c_str(), "INIZH.big|TexturesZH.big");

	char real_overlay[4096];
	CHECK(realpath(overlay.c_str(), real_overlay) != NULL);
	PosixPath_Set_Overlays(std::vector<std::string>(1, real_overlay));

	// reads: the overlay's copy first, the install's where the overlay has none
	CHECK_STR(resolved_contents("Data\\INI\\Weapon.ini").c_str(), "overlay weapon");
	CHECK_STR(resolved_contents("Data\\INI\\FXListReforged.ini").c_str(), "overlay fxlist");
	CHECK_STR(resolved_contents("Data\\INI\\GameData.ini").c_str(), "install gamedata");
	CHECK_STR(resolved_contents("Patch.str").c_str(), "overlay patch");
	CHECK_EQ(zh_access("ReforgedTextures.big", F_OK), 0);
	FILE * read = zh_fopen("data\\INI\\WEAPON.ini", "rb");
	CHECK(read != NULL);
	if (read != NULL) fclose(read);

	// listings: one folder holding both, one entry a name, byte order over the union
	CHECK_STR(listed("", "", "*.big", false).c_str(), "INIZH.big|ReforgedTextures.big|TexturesZH.big");
	CHECK_STR(listed("", "Data\\INI\\", "*.ini", false).c_str(),
		"Data\\INI\\FXListReforged.ini|Data\\INI\\GameData.ini|Data\\INI\\weapon.INI");
	CHECK_STR(listed("", "Data\\INI\\", "*.ini", true).c_str(),
		"Data\\INI\\FXListReforged.ini|Data\\INI\\GameData.ini|Data\\INI\\weapon.INI"
		"|Data\\INI\\Object\\Tank.ini|Data\\INI\\Reforged\\Extra.ini");

	// writes: the install only, whatever the overlay holds
	CHECK_STR(resolved("Patch.str", POSIX_PATH_CREATE_LEAF).c_str(), "Patch.str");
	CHECK_STR(resolved("Patch.str", POSIX_PATH_EXISTING_IN_ROOT).c_str(), "Patch.str");
	CHECK_STR(resolved("Data\\INI\\FXListReforged.ini", POSIX_PATH_EXISTING_IN_ROOT).c_str(), "(unresolved)");
	CHECK(zh_unlink("Data\\INI\\FXListReforged.ini") != 0);		// only in the overlay: nothing to delete
	CHECK_STR(read_file(overlay + "/data/ini/FXListReforged.ini").c_str(), "overlay fxlist");
	CHECK_EQ(zh_unlink("Patch.str"), 0);							// in both: the install's goes
	CHECK_STR(read_file(overlay + "/Patch.str").c_str(), "overlay patch");
	CHECK_STR(read_file(install + "/Patch.str").c_str(), "");
	FILE * written = zh_fopen("Data\\INI\\Weapon.ini", "r+b");		// an update: the install's copy
	CHECK(written != NULL);
	if (written != NULL) { fputs("X", written); fclose(written); }
	CHECK_STR(read_file(install + "/Data/INI/Weapon.ini").c_str(), "Xnstall weapon");
	CHECK_STR(read_file(overlay + "/data/ini/weapon.INI").c_str(), "overlay weapon");
	const int handle = zh_open("ReforgedTextures.big", O_WRONLY, 0);	// in the overlay only
	CHECK(handle < 0);
	if (handle >= 0) close(handle);
	FILE * created = zh_fopen("Data\\INI\\FXListReforged.ini", "wb");	// a create lands in the install
	CHECK(created != NULL);
	if (created != NULL) { fputs("install made", created); fclose(created); }
	CHECK_STR(read_file(install + "/Data/INI/FXListReforged.ini").c_str(), "install made");
	CHECK_STR(read_file(overlay + "/data/ini/FXListReforged.ini").c_str(), "overlay fxlist");
	CHECK_STR(resolved_contents("Data\\INI\\FXListReforged.ini").c_str(), "overlay fxlist");	// reads still see the overlay's

	// absolute paths are untouched
	CHECK_STR(resolved_contents(install + "/Data/INI/Weapon.ini").c_str(), "Xnstall weapon");

	PosixPath_Set_Overlays(std::vector<std::string>());
	PosixPath_Forget_All();
	CHECK_EQ(chdir(previous), 0);
	remove_tree(base);
}

// P1 step 2: with the roots read-only, every zh_* write of a RELATIVE path is refused with EROFS, and
// the install is left as it was; absolute paths and reads are untouched.  The control: the same calls
// with read-only off do write.
TEST(read_only_root_refuses_relative_writes_only)
{
	const std::string base = temp_root("test_posixpath_readonly");
	const std::string install = base + "/install", elsewhere = base + "/userdata";
	make_directory(base);
	make_directory(install);
	make_directory(install + "/Data");
	make_directory(install + "/Data/INI");
	make_directory(elsewhere);
	write_file(install + "/Data/INI/INIZH.big", "stray");
	write_file(install + "/Keep.txt", "keep");

	char previous[4096];
	CHECK(getcwd(previous, sizeof(previous)) != NULL);
	CHECK_EQ(chdir(install.c_str()), 0);
	PosixPath_Forget_All();
	PosixPath_Set_Root_Read_Only(true);
	CHECK(PosixPath_Root_Read_Only());

	errno = 0;
	CHECK(zh_unlink("Data\\INI\\INIZH.big") != 0);			// GameEngine::init's deletion
	CHECK_EQ(errno, EROFS);
	CHECK(zh_remove("Keep.txt") != 0);
	CHECK(zh_fopen("New.txt", "wb") == NULL);
	CHECK_EQ(errno, EROFS);
	CHECK(zh_fopen("Keep.txt", "r+b") == NULL);
	CHECK(zh_fopen("Keep.txt", "ab") == NULL);
	CHECK(zh_open("Keep.txt", O_WRONLY | O_TRUNC, 0) < 0);
	CHECK(zh_open("PatchAccessTest.txt", O_CREAT | O_RDWR, 0600) < 0);	// the patch check's probe
	CHECK(zh_mkdir("Stats") != 0);
	CHECK(zh_rename("Keep.txt", "Moved.txt") != 0);
	CHECK_STR(read_file(install + "/Data/INI/INIZH.big").c_str(), "stray");
	CHECK_STR(read_file(install + "/Keep.txt").c_str(), "keep");
	CHECK(!is_directory_here(install + "/Stats"));
	struct stat status;
	CHECK(stat((install + "/New.txt").c_str(), &status) != 0);
	CHECK(stat((install + "/PatchAccessTest.txt").c_str(), &status) != 0);

	// reads go on as before
	FILE * read = zh_fopen("Keep.txt", "rb");
	CHECK(read != NULL);
	if (read != NULL) fclose(read);
	CHECK_EQ(zh_access("Data\\INI\\INIZH.big", F_OK), 0);

	// absolute paths - the user data directory - are the engine's deliberate destinations
	FILE * absolute = zh_fopen((elsewhere + "/Options.ini").c_str(), "wb");
	CHECK(absolute != NULL);
	if (absolute != NULL) { fputs("x", absolute); fclose(absolute); }
	CHECK_EQ(zh_mkdir((elsewhere + "/Replays").c_str()), 0);
	CHECK_EQ(zh_unlink((elsewhere + "/Options.ini").c_str()), 0);

	// the control: read-only off, the same relative calls write
	PosixPath_Set_Root_Read_Only(false);
	CHECK_EQ(zh_unlink("Data\\INI\\INIZH.big"), 0);
	FILE * made = zh_fopen("New.txt", "wb");
	CHECK(made != NULL);
	if (made != NULL) fclose(made);
	CHECK_EQ(zh_mkdir("Stats"), 0);
	CHECK(stat((install + "/Data/INI/INIZH.big").c_str(), &status) != 0);
	CHECK(is_directory_here(install + "/Stats"));

	PosixPath_Forget_All();
	CHECK_EQ(chdir(previous), 0);
	remove_tree(base);
}
