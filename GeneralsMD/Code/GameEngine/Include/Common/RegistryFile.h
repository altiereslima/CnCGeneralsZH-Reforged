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

// FILE: RegistryFile.h ///////////////////////////////////////////////////////////////////////////
// Desc:   Registry.ini, the registry off Windows: its keys, and reading and writing one value.
///////////////////////////////////////////////////////////////////////////////////////////////////

/* Off Windows the registry is one file, Registry.ini in the user data directory (EarlyOptions.h's
	 findRegistryFile), in Options.ini's "key = value" form.  registry.cpp reads the engine's values
	 from it; WWDownload's registry functions read and write theirs through here too, so every reader
	 and writer of the file shares one parser and one idea of which line is a key's.

	 The protocol (C1, "Registry.ini: the protocol"):
	 - A key is registryFileKey's: "Generals\\" for original-Generals values or "" for Zero Hour's,
		 then the registry path below the game's key without its leading backslashes, then '\\', then
		 the value name.  Keys match in any case, and the last line that sets one wins.
	 - A string is its text; a REG_DWORD is decimal text.
	 - A write replaces the last line that sets the key, or appends one, and keeps every other line.
		 It goes to "<file>.<pid>" and is renamed over the file, so a reader sees the old file or the
		 new one.  There is no locking: the last writer wins.
	 - A write the reader would not read back as written is refused: a line ending in the value, a
		 value over 255 bytes, or blanks at either end of it (the reader trims them).
	 - An empty value is written as "name =", which the reader takes as missing.  Windows stores an
		 empty string, and a read of it succeeds with ""; every reader of the one key the game writes
		 empty, the HTTP proxy (cleared in Options), takes "" and missing alike as "no proxy", so the
		 two behave the same.

	 Windows has the registry, and none of this. */

#pragma once

#if !defined(_WIN32)

#include "Common/AsciiString.h"

/** The Registry.ini key for a registry value: `tree` ("" for Zero Hour, "Generals\\" for the original
	* game), then `path` below the game's key without its leading backslashes, then `key`. */
AsciiString registryFileKey( const char *tree, const AsciiString &path, const AsciiString &key );

/** The value of `name` in the Registry.ini at `file` (a path spelled as the engine spells paths);
	* FALSE if the file or the key is not there. */
Bool readRegistryFileAt( const char *file, const AsciiString &name, AsciiString &val );

/** Sets `name` to `val` in the Registry.ini at `file`, creating the file if it is not there; FALSE if
	* the value would not read back as written (an empty one: would not read as missing), or any step
	* fails, and the file is then unchanged. */
Bool writeRegistryFileAt( const char *file, const AsciiString &name, const AsciiString &val );

/** readRegistryFileAt and writeRegistryFileAt on this user's Registry.ini (findRegistryFile). */
Bool readRegistryFile( const AsciiString &name, AsciiString &val );
Bool writeRegistryFile( const AsciiString &name, const AsciiString &val );

#endif
