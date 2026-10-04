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
 * Registry.ini's reader and writer (Common/RegistryFile.h), linked from the real registry.cpp: the
 * writer replaces the line the reader takes for a key's, keeps every other byte, and refuses what the
 * reader would not read back as written, leaving the file as it was.  Every file is spelled as
 * findRegistryFile spells it, the directory and "\Registry.ini", so zh_fopen's resolver is on the path.
 *
 * Built with test_posixlocalfilesystem's engine sources (CMakeLists.txt).  Writes only under $TMPDIR.
 */
#include "test_harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

#include "Common/AsciiString.h"
#include "Common/GameMemory.h"
#include "Common/RegistryFile.h"

namespace {

void boot_memory()
{
	static bool booted = false;
	if (!booted) {
		initMemoryManager();
		booted = true;
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

void remove_tree(const std::string & root)
{
	const std::string command = "rm -rf '" + root + "'";
	if (system(command.c_str()) != 0) printf("  could not remove %s\n", root.c_str());
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

bool exists(const std::string & path)
{
	struct stat status;
	return stat(path.c_str(), &status) == 0;
}

// A fresh directory; `posix` is Registry.ini's POSIX path, `engine` the same file as the engine spells it.
struct Scratch
{
	std::string root, posix, engine, temporary;
	explicit Scratch(const char * name)
	{
		boot_memory();
		root = temp_root(name);
		remove_tree(root);
		mkdir(root.c_str(), 0777);
		posix = root + "/Registry.ini";
		engine = root + "\\Registry.ini";
		char pid[32];
		snprintf(pid, sizeof(pid), ".%d", (int)getpid());
		temporary = posix + pid;
	}
	~Scratch() { remove_tree(root); }
};

std::string read_value(const std::string & file, const char * name)
{
	AsciiString value;
	if (!readRegistryFileAt(file.c_str(), AsciiString(name), value)) return "<missing>";
	return value.str();
}

} // namespace

TEST(registry_file_keys_are_the_path_below_the_game_key_then_the_name)
{
	boot_memory();
	CHECK_STR(registryFileKey("", AsciiString(""), AsciiString("Language")).str(), "Language");
	CHECK_STR(registryFileKey("", AsciiString("\\ergc"), AsciiString("Proxy")).str(), "ergc\\Proxy");
	CHECK_STR(registryFileKey("", AsciiString("\\\\a\\b"), AsciiString("c")).str(), "a\\b\\c");
	CHECK_STR(registryFileKey("Generals\\", AsciiString(""), AsciiString("InstallPath")).str(), "Generals\\InstallPath");
}

TEST(registry_file_write_creates_the_file_and_reads_back)
{
	Scratch s("test_registryfile_create");
	CHECK(!exists(s.posix));
	CHECK(writeRegistryFileAt(s.engine.c_str(), AsciiString("Language"), AsciiString("german")));
	CHECK_STR(read_file(s.posix).c_str(), "Language = german\n");
	CHECK_STR(read_value(s.engine, "language").c_str(), "german");
	CHECK(!exists(s.temporary));
}

TEST(registry_file_write_replaces_the_last_line_the_reader_takes_and_keeps_the_rest)
{
	Scratch s("test_registryfile_replace");
	write_file(s.posix,
		"; written by hand\r\n"
		"Language = english\n"
		"ergc\\Proxy = 1\n"
		"LanguageX = not this one\n"
		"  LANGUAGE\t=  polish  \n"			// the reader's: the last, in any case, blanks around
		"Version = 65540");						// no line ending on the last line
	CHECK_STR(read_value(s.engine, "Language").c_str(), "polish");

	CHECK(writeRegistryFileAt(s.engine.c_str(), AsciiString("Language"), AsciiString("french")));
	CHECK_STR(read_file(s.posix).c_str(),
		"; written by hand\r\n"
		"Language = english\n"
		"ergc\\Proxy = 1\n"
		"LanguageX = not this one\n"
		"Language = french\n"
		"Version = 65540");
	CHECK_STR(read_value(s.engine, "Language").c_str(), "french");

	// A key that is not there goes at the end, on a line of its own.
	CHECK(writeRegistryFileAt(s.engine.c_str(), AsciiString("ergc\\Serial"), AsciiString("1234-5678")));
	CHECK_STR(read_file(s.posix).c_str(),
		"; written by hand\r\n"
		"Language = english\n"
		"ergc\\Proxy = 1\n"
		"LanguageX = not this one\n"
		"Language = french\n"
		"Version = 65540\n"
		"ergc\\Serial = 1234-5678\n");
	CHECK_STR(read_value(s.engine, "ergc\\Serial").c_str(), "1234-5678");
	CHECK_STR(read_value(s.engine, "Version").c_str(), "65540");
	CHECK(!exists(s.temporary));
}

TEST(registry_file_write_refuses_what_would_not_read_back_and_leaves_the_file)
{
	Scratch s("test_registryfile_refuse");
	const std::string original = "Language = english\n";
	write_file(s.posix, original);

	const std::string longest(255, 'x');
	const char * refused[] = { "two\nlines", "cr\r", " leading", "trailing\t" };
	for (size_t i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i) {
		CHECK(!writeRegistryFileAt(s.engine.c_str(), AsciiString("Language"), AsciiString(refused[i])));
	}
	CHECK(!writeRegistryFileAt(s.engine.c_str(), AsciiString("Language"), AsciiString((longest + "x").c_str())));
	CHECK(!writeRegistryFileAt(s.engine.c_str(), AsciiString(""), AsciiString("value")));
	CHECK(!writeRegistryFileAt(s.engine.c_str(), AsciiString(" Language"), AsciiString("value")));	// the reader skips the blank and misses it
	CHECK_STR(read_file(s.posix).c_str(), original.c_str());
	CHECK(!exists(s.temporary));

	CHECK(writeRegistryFileAt(s.engine.c_str(), AsciiString("Language"), AsciiString(longest.c_str())));
	CHECK_STR(read_value(s.engine, "Language").c_str(), longest.c_str());
}

// Windows stores an empty string; here an empty value is "name =", which the reader takes as missing,
// and it replaces the key's line as any other value does.
TEST(registry_file_write_of_an_empty_value_makes_the_key_read_as_missing)
{
	Scratch s("test_registryfile_empty");
	write_file(s.posix, "Proxy = proxy.example:8080\nLanguage = english\n");
	CHECK(writeRegistryFileAt(s.engine.c_str(), AsciiString("proxy"), AsciiString("")));
	CHECK_STR(read_file(s.posix).c_str(), "proxy =\nLanguage = english\n");
	CHECK_STR(read_value(s.engine, "Proxy").c_str(), "<missing>");
	CHECK_STR(read_value(s.engine, "Language").c_str(), "english");

	// and a value written after it is read again
	CHECK(writeRegistryFileAt(s.engine.c_str(), AsciiString("Proxy"), AsciiString("other.example:3128")));
	CHECK_STR(read_file(s.posix).c_str(), "Proxy = other.example:3128\nLanguage = english\n");

	// On a file with no such key it is appended, and reads as missing.
	CHECK(writeRegistryFileAt(s.engine.c_str(), AsciiString("ergc\\Serial"), AsciiString("")));
	CHECK_STR(read_value(s.engine, "ergc\\Serial").c_str(), "<missing>");
	CHECK(!exists(s.temporary));
}

// The reader reads 1024 bytes at a time, so the tail of a longer line reaches it as a line of its own.
// Here that tail sets the key after the line the writer would replace, so the reader would still
// read the tail's value: the writer's read-back catches it and leaves the file alone.
TEST(registry_file_write_is_refused_when_the_reader_would_read_something_else)
{
	Scratch s("test_registryfile_readback");
	const std::string original = "Language = english\n#" + std::string(1022, 'a') + "Language = tail\n";
	write_file(s.posix, original);
	CHECK_STR(read_value(s.engine, "Language").c_str(), "tail");
	CHECK(!writeRegistryFileAt(s.engine.c_str(), AsciiString("Language"), AsciiString("french")));
	CHECK_STR(read_file(s.posix).c_str(), original.c_str());
	CHECK(!exists(s.temporary));
}

TEST(registry_file_write_does_not_replace_a_file_it_cannot_read)
{
	Scratch s("test_registryfile_unreadable");
	const std::string original = "Language = english\n";
	write_file(s.posix, original);
	chmod(s.posix.c_str(), 0200);
	FILE * probe = fopen(s.posix.c_str(), "r");
	if (probe != NULL) {
		fclose(probe);
		printf("  SKIPPED: a mode-0200 file is readable here (root?), so an unreadable file cannot be made\n");
	} else {
		CHECK(!writeRegistryFileAt(s.engine.c_str(), AsciiString("Language"), AsciiString("french")));
		chmod(s.posix.c_str(), 0600);
		CHECK_STR(read_file(s.posix).c_str(), original.c_str());
		CHECK(!exists(s.temporary));
	}
}
