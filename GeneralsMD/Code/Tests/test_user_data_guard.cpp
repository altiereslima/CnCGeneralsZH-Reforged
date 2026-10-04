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
 * The one guard every POSIX test binary carries: no ZH_USER_DATA_DIR, no run.
 *
 * Off Windows the engine's user data directory is ZH_USER_DATA_DIR when that is set, and otherwise
 * this user's real one (EarlyOptions.h, findUserDataDirectory) - where the game keeps Options.ini,
 * Registry.ini and the saves.  A test that reaches the engine without the variable would make that
 * folder and could write into it; test_wwdownload_registry, run by hand, once did.  So rather than a
 * check in each test that remembers to have one, this file is compiled into every executable that
 * links test_harness (an INTERFACE source, CMakeLists.txt) and stops the process from a static
 * initialiser, before main and before the engine's own libraries' initialisers.  ctest gives every
 * test its own folder in the build tree; a hand run must name one:
 *
 *   ZH_USER_DATA_DIR=/tmp/zh-user test_gameengine
 *
 * It fails rather than skips: a test that did not run must not read as one that passed.
 *
 * What it cannot see: a static initialiser in a source compiled into the same executable ahead of
 * this file (Mach-O runs one image's initialisers in link order, and ELF's constructor priority is
 * honoured here but not relied on); none of the engine's reaches the user data directory today.  A
 * test that sets the variable itself is still covered - the guard only asks that the caller did.
 * Windows is not covered: there the engine ignores the variable and uses Documents, as it always has.
 */

#if !defined(_WIN32)

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

namespace {

__attribute__((constructor(101))) void require_user_data_directory(void)
{
	const char *dir = getenv("ZH_USER_DATA_DIR");
	if (dir != NULL && dir[0] != 0)
		return;
	fprintf(stderr, "FAIL: ZH_USER_DATA_DIR is not set.  Without it the engine would use this user's real "
		"data directory (Options.ini, Registry.ini, saves).  Run through ctest, or set it to a scratch folder.\n");
	fflush(stderr);
	_exit(1);	// nothing else runs: no atexit handlers, no static destructors
}

}  // namespace

#endif
