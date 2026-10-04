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
 * replay_wide_oracle - what the Windows C runtime's wide stream calls put on disk in a replay header,
 * for test_widechar_file.cpp's expected bytes (C1, PR (g)).  Windows only: built with mingw-w64 and
 * run under Wine, or on Windows.  It writes exactly as Recorder.cpp did before PR (g) - fwprintf with
 * "%ls" and fputwc, mixed with fwrite and fprintf on one FILE* opened "wb" - then reads it back as the
 * replay reader did, with fread and fgetwc on "rb", and prints both as hex.
 *
 *   replay_wide_oracle <scratch file>
 *
 * Build with -D__USE_MINGW_ANSI_STDIO=0, so fwprintf is the C runtime's own and not mingw's.
 */
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static void print_file(const char *path, const char *label)
{
	FILE *f = fopen(path, "rb");
	printf("%s:", label);
	int c;
	while (f != NULL && (c = fgetc(f)) != EOF) printf("%02x", c);
	printf("\n");
	if (f != NULL) fclose(f);
}

int main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: replay_wide_oracle <scratch file>\n");
		return 2;
	}
	const char *path = argv[1];

	// test_widechar_file.cpp's first case: units a conversion or text mode would change.
	const wchar_t awkward[] = { 0x00DC, 0x65E5, 0x672C, 0xD83D, 0xDE00, 0x000D, 0x000A, 0x001A, 0x0041, 0 };
	FILE *f = fopen(path, "wb");
	if (f == NULL) return 1;
	fwprintf(f, L"%ls", awkward);
	fputwc(0, f);
	fclose(f);
	print_file(path, "units");

	// The second case: the header's shape, byte and wide calls interleaved.
	const wchar_t name[] = { 'L', 'a', 's', 't', ' ', 0x00FC, 0 };
	f = fopen(path, "wb");
	if (f == NULL) return 1;
	fprintf(f, "GENREP");
	const unsigned int number = 0x01020304;
	fwrite(&number, sizeof(number), 1, f);
	fwprintf(f, L"%ls", name);
	fputwc(0, f);
	fprintf(f, "%d", 7);
	fputc(0, f);
	fclose(f);
	print_file(path, "header");

	// Read back as RecorderClass::readReplayHeader and readUnicodeString did.
	f = fopen(path, "rb");
	if (f == NULL) return 1;
	char genrep[6];
	unsigned int back = 0;
	size_t got = fread(genrep, 1, 6, f);
	got += fread(&back, sizeof(back), 1, f);
	printf("read: %u items, number %08x, units", (unsigned)got, back);
	wint_t c;
	while ((c = fgetwc(f)) != 0 && c != WEOF) printf(" %04x", (unsigned)c);
	const int seven = fgetc(f);
	const int terminator = fgetc(f);
	const wint_t end = fgetwc(f);
	printf(", then '%c' %d, then fgetwc %04x\n", seven, terminator, (unsigned)end);
	fclose(f);

	// fgetwc with one byte left.
	f = fopen(path, "wb");
	if (f == NULL) return 1;
	fputc(0x41, f);
	fclose(f);
	f = fopen(path, "rb");
	if (f == NULL) return 1;
	printf("one byte left: fgetwc %04x, WEOF is %04x\n", (unsigned)fgetwc(f), (unsigned)WEOF);
	fclose(f);
	return 0;
}
