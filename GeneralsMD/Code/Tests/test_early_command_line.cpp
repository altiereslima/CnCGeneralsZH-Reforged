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
 * EarlyCommandLine.h off Windows: the options read before CommandLine.cpp's parser exists come from
 * the process's own argv, as GetCommandLineW gives them on Windows - including before main, where
 * the debug log reads -logPrefix.  ctest runs this with a fixed command line (CMakeLists.txt):
 *   test_early_command_line early_command_line -logPrefix pre -headless -root /tmp/zh_<U+00E4> -jobthreads 3 -last
 * (the first argument is test_harness's name filter, which every test here matches).
 */
#include "test_harness.h"

#include "PreRTS.h"
#include "Common/EarlyCommandLine.h"

#include <stdlib.h>
#include <string.h>

namespace {

// What a static constructor sees, before main, as the memory manager's pre-main start does.
struct SeenBeforeMain
{
	bool headless;
	char logPrefix[64];
	SeenBeforeMain()
	{
		headless = findEarlyCommandLineOption( L"-headless" ) != NULL;
		if (!findEarlyCommandLineValue( L"-logPrefix", logPrefix, sizeof( logPrefix ) ))
			logPrefix[0] = 0;
	}
};
SeenBeforeMain s_beforeMain;

std::string value_of( const wchar_t *option )
{
	char value[256];
	return findEarlyCommandLineValue( option, value, sizeof( value ) ) ? std::string( value ) : std::string( "<none>" );
}

} // namespace

TEST(early_command_line_is_read_before_main)
{
	CHECK( s_beforeMain.headless );
	CHECK_STR( s_beforeMain.logPrefix, "pre" );
}

TEST(early_command_line_options_match_on_word_boundaries_in_any_case)
{
	CHECK( findEarlyCommandLineOption( L"-headless" ) != NULL );
	CHECK( findEarlyCommandLineOption( L"-HEADLESS" ) != NULL );
	CHECK( findEarlyCommandLineOption( L"-head" ) == NULL );
	CHECK( findEarlyCommandLineOption( L"-multiInstance" ) == NULL );
	CHECK( findEarlyCommandLineOption( L"-last" ) != NULL );
}

// ZH_UNATTENDED marks a scripted run that draws (-headless is the one that does not): set to anything but
// "" or "0".  This test's own command line carries -headless, so isUnattendedProcess() is true either way.
static void set_unattended( const char *value )
{
#if defined(_WIN32)
	_putenv_s( "ZH_UNATTENDED", value != NULL ? value : "" );
#else
	if (value != NULL)
		setenv( "ZH_UNATTENDED", value, 1 );
	else
		unsetenv( "ZH_UNATTENDED" );
#endif
}

TEST(early_command_line_zh_unattended_marks_a_scripted_run)
{
	set_unattended( NULL );
	CHECK( !unattendedByEnvironment() );
	set_unattended( "" );
	CHECK( !unattendedByEnvironment() );
	set_unattended( "0" );
	CHECK( !unattendedByEnvironment() );
	set_unattended( "1" );
	CHECK( unattendedByEnvironment() );
	set_unattended( "yes" );
	CHECK( unattendedByEnvironment() );
	set_unattended( NULL );
	CHECK( isUnattendedProcess() );		// -headless, from the command line
}

TEST(early_command_line_values_are_the_bytes_given)
{
	CHECK_STR( value_of( L"-logPrefix" ).c_str(), "pre" );
	CHECK_STR( value_of( L"-jobthreads" ).c_str(), "3" );
	CHECK_STR( value_of( L"-root" ).c_str(), "/tmp/zh_\xC3\xA4" );	// UTF-8 in, UTF-8 out
	CHECK_STR( value_of( L"-headless" ).c_str(), "<none>" );			// followed by an option, not a value
	CHECK_STR( value_of( L"-last" ).c_str(), "<none>" );				// at the end
}

TEST(early_command_line_starts_with_the_program_as_windows_does)
{
	const wchar_t *line = processCommandLineW();
	char first[1024];
	size_t i = 0;
	while (line[i] != 0 && line[i] != L' ' && i + 1 < sizeof( first ))
	{
		first[i] = (char)line[i];
		++i;
	}
	first[i] = 0;
	CHECK( strstr( first, "test_early_command_line" ) != NULL );
}
