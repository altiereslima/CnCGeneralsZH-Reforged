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

// The replay header's strings (C1, PR (g)).  WideCharFileWrite, WideCharFilePut and WideCharFileGet
// must put on disk, and read back, what MSVC's fwprintf(L"%ls"), fputwc and fgetwc did on the
// binary stream Recorder.cpp opens - each code unit as two bytes, low first, nothing translated -
// while the same FILE* carries fwrite, fprintf and fread.
//
// The expected bytes below are MSVC's documented binary-mode behaviour, written out.  C1
// made a Wine run of Tests/replay_wide_oracle.cpp, which writes the same strings with the real
// wide calls through Wine's msvcrt and gets these bytes.
//
// Built without wide or UTF-16 literals, so the file does not depend on WideChar's width or on how a
// compiler reads non-ASCII source.

#include "test_harness.h"

#include <stdio.h>
#include <string.h>
#if !defined(_WIN32)
#include <wchar.h>
#endif

#include <string>

#include "Lib/BaseType.h"
#include "Lib/WideCharFns.h"

namespace {

// A NUL-terminated WideChar string from code units.
std::basic_string<WideChar> units(std::initializer_list<unsigned> list)
{
	std::basic_string<WideChar> s;
	for (unsigned u : list) s += (WideChar)u;
	return s;
}

std::string bytes_of(FILE * f)
{
	std::string out;
	rewind(f);
	int c;
	while ((c = fgetc(f)) != EOF) out += (char)c;
	return out;
}

std::string hex(const std::string & s)
{
	std::string out;
	char b[4];
	for (size_t i = 0; i < s.size(); ++i) {
		snprintf(b, sizeof(b), "%02x", (unsigned char)s[i]);
		out += b;
	}
	return out;
}

} // namespace

// Units that text mode or a multibyte conversion would change - non-ASCII, a surrogate pair, CR, LF,
// Ctrl-Z - go through untouched, two bytes each, low first.
TEST(widechar_file_writes_each_unit_as_two_bytes_low_first)
{
	FILE * f = tmpfile();
	CHECK(f != NULL);
	if (f == NULL) return;
	// "Ü" "日本" U+1F600 (a surrogate pair) CR LF Ctrl-Z "A"
	const std::basic_string<WideChar> s = units({ 0x00DC, 0x65E5, 0x672C, 0xD83D, 0xDE00, 0x000D, 0x000A, 0x001A, 0x0041 });
	CHECK_EQ(WideCharFileWrite(f, s.c_str()), 9);
	CHECK_EQ(WideCharFilePut(f, 0), 0);
	CHECK_STR(hex(bytes_of(f)).c_str(), "dc00e5652c673dd800de0d000a001a0041000000");
	fclose(f);
}

// The header's shape: byte calls and these, interleaved on one stream, written and read back.
TEST(widechar_file_shares_a_stream_with_byte_calls)
{
	FILE * f = tmpfile();
	CHECK(f != NULL);
	if (f == NULL) return;
	fprintf(f, "GENREP");
	const unsigned int number = 0x01020304;
	fwrite(&number, sizeof(number), 1, f);
	const std::basic_string<WideChar> name = units({ 'L', 'a', 's', 't', ' ', 0x00FC });
	CHECK_EQ(WideCharFileWrite(f, name.c_str()), 6);
	CHECK_EQ(WideCharFilePut(f, 0), 0);
	fprintf(f, "%d", 7);
	fputc(0, f);
	CHECK_STR(hex(bytes_of(f)).c_str(), "47454e524550" "04030201" "4c00610073007400" "2000" "fc00" "0000" "3700");

	rewind(f);
	char genrep[6];
	CHECK_EQ((int)fread(genrep, 1, 6, f), 6);
	unsigned int back = 0;
	CHECK_EQ((int)fread(&back, sizeof(back), 1, f), 1);
	CHECK_EQ(back, number);
	std::basic_string<WideChar> read;
	Int c;
	while ((c = WideCharFileGet(f)) != 0 && c != WIDECHAR_FILE_EOF) read += (WideChar)c;
	CHECK(read == name);
	CHECK_EQ(fgetc(f), '7');
	CHECK_EQ(fgetc(f), 0);
	CHECK_EQ(WideCharFileGet(f), (Int)WIDECHAR_FILE_EOF);		// at the end: WEOF, as fgetwc gives it
#if !defined(_WIN32)
	CHECK(fwide(f, 0) <= 0);		// never made wide-oriented: every call above was a byte call
#endif
	fclose(f);
}

// fgetwc's end of file, as MSVC has it: 0xFFFF, not -1, and also when only one byte is left.
TEST(widechar_file_get_at_the_end_is_weof)
{
	FILE * f = tmpfile();
	CHECK(f != NULL);
	if (f == NULL) return;
	fputc(0x41, f);
	rewind(f);
	CHECK_EQ(WideCharFileGet(f), 0xFFFF);
	CHECK(WideCharFileGet(f) != EOF);
	fclose(f);
	CHECK_EQ(WideCharFileGet(NULL), 0xFFFF);
	CHECK_EQ(WideCharFilePut(NULL, 0x41), 0xFFFF);
	CHECK_EQ(WideCharFileWrite(NULL, units({ 0x41 }).c_str()), -1);
}

#if !defined(_WIN32)
// The defect these replace, as this C library shows it.  Mixing is undefined, and macOS's libc
// (measured) neither fails nor writes the replay format: a wide call on a byte-oriented stream
// goes through the locale's multibyte encoding, one byte for an ASCII unit.  A replay header written
// that way reads back on the same machine and is unreadable anywhere else.  Asserted loosely - not
// the two bytes the format needs - so a C library that fails the call instead passes too.
TEST(control_a_wide_call_on_a_byte_stream_does_not_write_the_format_here)
{
	FILE * f = tmpfile();
	CHECK(f != NULL);
	if (f == NULL) return;
	fwrite("GENREP", 1, 6, f);
	CHECK(fwide(f, 0) < 0);
	fputwc(L'A', f);
	CHECK(hex(bytes_of(f)) != "47454e524550" "4100");
	fclose(f);
}
#endif
