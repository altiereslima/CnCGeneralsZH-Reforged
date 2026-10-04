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

// The user's folders off Windows (C1 (e), decision D5): where saves, replays and Options.ini live,
// and where the replay menu's "copy to Desktop" puts a replay.  EarlyOptions.h's POSIX half.
//
// Nothing here writes to the real home directory: the user data directory comes from
// ZH_USER_DATA_DIR, set to a temporary folder before the first call, and HOME is pointed at a
// temporary folder for the Desktop checks.

#include "test_harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

#include "Common/EarlyOptions.h"

namespace {

std::string temp_root(const char * name)
{
	const char * temp = getenv("TMPDIR");
	std::string base = (temp != NULL && *temp) ? temp : "/tmp";
	if (!base.empty() && base[base.size() - 1] == '/') base.erase(base.size() - 1);
	char unique[64];
	snprintf(unique, sizeof(unique), "/%s_%d", name, (int)getpid());
	return base + unique;
}

bool is_directory(const std::string & path)
{
	struct stat status;
	return stat(path.c_str(), &status) == 0 && S_ISDIR(status.st_mode);
}

std::string composed(UserFolderConvention convention, const char * override, const char * home, const char * dataHome)
{
	char out[4096];
	return composeUserDataDirectory(convention, override, home, dataHome, out, sizeof(out)) ? out : "(none)";
}

std::string desktop(UserFolderConvention convention, const std::string & home, const char * configHome)
{
	char out[4096];
	return composeDesktopDirectory(convention, home.c_str(), configHome, out, sizeof(out)) ? out : "(none)";
}

std::string xdg_value(const char * contents, const char * key, const char * home)
{
	FILE * fp = tmpfile();
	fputs(contents, fp);
	rewind(fp);
	char out[4096];
	const bool found = findXdgUserDirIn(fp, key, home, out, sizeof(out));
	fclose(fp);
	return found ? out : "(none)";
}

} // namespace

// Each convention's place, the override, and what is ignored - both conventions, on any machine.
TEST(user_data_directory_is_the_platforms_place_for_application_data)
{
	const std::string leaf = "Command and Conquer Generals Zero Hour Data";
	const UserFolderConvention APPLE = USER_FOLDERS_APPLE;
	const UserFolderConvention XDG = USER_FOLDERS_XDG;
	CHECK_STR(composed(APPLE, NULL, "/Users/player", NULL).c_str(), ("/Users/player/Library/Application Support/" + leaf).c_str());
	CHECK_STR(composed(APPLE, NULL, "/Users/player", "/elsewhere").c_str(),			// XDG is not Apple's
		("/Users/player/Library/Application Support/" + leaf).c_str());
	CHECK_STR(composed(XDG, NULL, "/home/player", NULL).c_str(), ("/home/player/.local/share/" + leaf).c_str());
	CHECK_STR(composed(XDG, NULL, "/home/player", "/data").c_str(), ("/data/" + leaf).c_str());
	CHECK_STR(composed(XDG, NULL, "/home/player", "relative").c_str(),				// ignored, as the specification says
		("/home/player/.local/share/" + leaf).c_str());
	CHECK_STR(composed(XDG, "/tmp/zh-data/", "/home/player", "/data").c_str(), "/tmp/zh-data");		// the override wins
	CHECK_STR(composed(APPLE, "/tmp/zh-data\\", "/Users/player", NULL).c_str(), "/tmp/zh-data");
	CHECK_STR(composed(XDG, "", "/home/player", NULL).c_str(), composed(XDG, NULL, "/home/player", NULL).c_str());
	CHECK_STR(composed(APPLE, NULL, NULL, NULL).c_str(), "(none)");
	CHECK_STR(composed(XDG, NULL, "relative/home", NULL).c_str(), "(none)");
#if defined(__APPLE__)
	CHECK(PLATFORM_USER_FOLDERS == USER_FOLDERS_APPLE);
#else
	CHECK(PLATFORM_USER_FOLDERS == USER_FOLDERS_XDG);
#endif
}

// The process's own answer: made, with its parents, ending in '\' as Windows' does, and fixed after
// the first call.
TEST(user_data_directory_is_made_and_ends_as_windows_does)
{
	const std::string root = temp_root("test_userfolders");
	const std::string wanted = root + "/nested/User Data";
	setenv("ZH_USER_DATA_DIR", (wanted + "/").c_str(), 1);

	char out[4096];
	CHECK(findUserDataDirectory(out, sizeof(out)));
	CHECK_STR(out, (wanted + "\\").c_str());
	CHECK(is_directory(wanted));

	setenv("ZH_USER_DATA_DIR", (root + "/other").c_str(), 1);
	CHECK(findUserDataDirectory(out, sizeof(out)));
	CHECK_STR(out, (wanted + "\\").c_str());				// decided once a process
	CHECK(!is_directory(root + "/other"));
	char small[8];
	CHECK(!findUserDataDirectory(small, sizeof(small)));	// too long for the buffer: no answer, not a cut one
	CHECK_STR(small, "");

	// The files in it, by the engine's spelling "<dir>\\Options.ini": read through zh_fopen, which a
	// raw fopen could not do - it takes the '\\' as part of the file's name (C1 (d)).
	FILE * options = fopen((wanted + "/Options.ini").c_str(), "w");
	if (options != NULL) {
		fputs("Resolution = 1920 1080\nWindowMode = Borderless\n", options);
		fclose(options);
	}
	char value[64];
	CHECK(findEarlyOptionValue("WindowMode", value, sizeof(value)));
	CHECK_STR(value, "Borderless");
	CHECK(!findEarlyOptionValue("Absent", value, sizeof(value)));
	char registry[4096];
	CHECK(findRegistryFile(registry, sizeof(registry)));
	CHECK_STR(registry, (wanted + "\\Registry.ini").c_str());
	CHECK(!findRegistryFile(small, sizeof(small)));

	unsetenv("ZH_USER_DATA_DIR");
	const std::string command = "rm -rf '" + root + "'";
	if (system(command.c_str()) != 0) printf("  could not remove %s\n", root.c_str());
}

// xdg-user-dirs' file, as Linux desktops write it.
TEST(xdg_user_dirs_file_names_the_desktop)
{
	const char * file =
		"# This file is written by xdg-user-dirs-update\n"
		"XDG_DOWNLOAD_DIR=\"$HOME/Downloads\"\n"
		"XDG_DESKTOP_DIR=\"$HOME/Schreibtisch\"\n";
	CHECK_STR(xdg_value(file, "XDG_DESKTOP_DIR", "/home/p").c_str(), "/home/p/Schreibtisch");
	CHECK_STR(xdg_value(file, "XDG_DOWNLOAD_DIR", "/home/p").c_str(), "/home/p/Downloads");
	CHECK_STR(xdg_value("XDG_DESKTOP_DIR=\"/srv/desk\"\n", "XDG_DESKTOP_DIR", "/home/p").c_str(), "/srv/desk");
	CHECK_STR(xdg_value("XDG_DESKTOP_DIR=\"$HOME\"\n", "XDG_DESKTOP_DIR", "/home/p").c_str(), "/home/p");
	CHECK_STR(xdg_value("XDG_DESKTOP_DIR=\"Desk\"\n", "XDG_DESKTOP_DIR", "/home/p").c_str(), "(none)");	// not a form the spec allows
	CHECK_STR(xdg_value("XDG_DESKTOP_DIRX=\"/x\"\n", "XDG_DESKTOP_DIR", "/home/p").c_str(), "(none)");
	CHECK_STR(xdg_value("XDG_DESKTOP_DIR=\"/a\"\nXDG_DESKTOP_DIR=\"/b\"\n", "XDG_DESKTOP_DIR", "/home/p").c_str(), "/b");
	CHECK_STR(xdg_value("", "XDG_DESKTOP_DIR", "/home/p").c_str(), "(none)");
}

// The Desktop the replay menu copies to: both conventions, with a home of the test's own.
TEST(desktop_directory_is_the_platforms_desktop)
{
	const std::string home = temp_root("test_userfolders_home");
	mkdir(home.c_str(), 0777);

	CHECK_STR(desktop(USER_FOLDERS_APPLE, home, NULL).c_str(), (home + "/Desktop").c_str());
	CHECK_STR(desktop(USER_FOLDERS_XDG, home, NULL).c_str(), (home + "/Desktop").c_str());	// no user-dirs file yet
	mkdir((home + "/.config").c_str(), 0777);
	FILE * fp = fopen((home + "/.config/user-dirs.dirs").c_str(), "w");
	if (fp != NULL) {
		fputs("XDG_DESKTOP_DIR=\"$HOME/Bureau\"\n", fp);
		fclose(fp);
	}
	CHECK_STR(desktop(USER_FOLDERS_XDG, home, NULL).c_str(), (home + "/Bureau").c_str());
	CHECK_STR(desktop(USER_FOLDERS_APPLE, home, NULL).c_str(), (home + "/Desktop").c_str());	// Apple has no such file
	CHECK_STR(desktop(USER_FOLDERS_XDG, home, (home + "/elsewhere").c_str()).c_str(), (home + "/Desktop").c_str());	// XDG_CONFIG_HOME moves the file
	CHECK_STR(desktop(USER_FOLDERS_XDG, "relative", NULL).c_str(), "(none)");

	// The process's own call, with HOME pointed at the test's folder.
	const char * savedHome = getenv("HOME");
	const std::string previousHome = savedHome != NULL ? savedHome : "";
	setenv("HOME", home.c_str(), 1);
	char out[4096];
	CHECK(findDesktopDirectory(out, sizeof(out)));
	CHECK_STR(out, desktop(PLATFORM_USER_FOLDERS, home, getenv("XDG_CONFIG_HOME")).c_str());
	if (savedHome != NULL) setenv("HOME", previousHome.c_str(), 1); else unsetenv("HOME");

	const std::string command = "rm -rf '" + home + "'";
	if (system(command.c_str()) != 0) printf("  could not remove %s\n", home.c_str());
}
