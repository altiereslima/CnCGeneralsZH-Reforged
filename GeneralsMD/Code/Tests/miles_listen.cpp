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
 * miles_listen - plays one of the game's sounds on the real audio device, for a person to hear.
 *
 * Not a test: nothing here can tell whether it sounded right.  It exists so that a human can put the
 * Mac build's audio next to the Windows build's by ear.  The sound goes through the same Miles
 * surface MilesAudioManager uses: a WAV as a sample from memory (IMA ADPCM through
 * AIL_decompress_ADPCM first, as the game does), an MP3 as a stream read through file callbacks.
 *
 *   miles_listen <archive.big> <entry> [volume 0..1] [pan 0..1]
 *   miles_listen <file.wav|file.mp3> [volume 0..1] [pan 0..1]
 *   miles_listen --list <archive.big>                         entry names, to pick one
 *
 * ZH_AUDIO_BACKEND=null plays to nothing, which is only useful for checking the tool itself.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "MSS/MSS.h"

static volatile int g_done = 0;
static void AILCALLBACK sampleDone(HSAMPLE) { g_done = 1; }
static void AILCALLBACK streamDone(HSTREAM) { g_done = 1; }

static unsigned be32(const unsigned char *p) { return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3]; }

struct Entry { std::string name; long offset; long size; };

static std::vector<Entry> readBig(const char *path)
{
	std::vector<Entry> entries;
	FILE *file = fopen(path, "rb");
	if (file == NULL) return entries;
	unsigned char header[16];
	if (fread(header, 1, 16, file) == 16 && memcmp(header, "BIGF", 4) == 0) {
		const unsigned count = be32(header + 8);
		for (unsigned i = 0; i < count; ++i) {
			unsigned char pair[8];
			if (fread(pair, 1, 8, file) != 8) break;
			Entry entry;
			entry.offset = (long)be32(pair);
			entry.size = (long)be32(pair + 4);
			int c;
			while ((c = fgetc(file)) != EOF && c != 0) entry.name += (char)c;
			entries.push_back(entry);
		}
	}
	fclose(file);
	return entries;
}

// The one file being played, for the stream callbacks: a byte range of a file on disk.
static std::string g_path;
static long g_base = 0, g_size = 0;
struct Open { FILE *file; long at; };

static U32 AILCALLBACK fileOpen(const char *, AILFILEHANDLE *handle)
{
	FILE *file = fopen(g_path.c_str(), "rb");
	if (file == NULL) return 0;
	Open *open = new Open;
	open->file = file;
	open->at = 0;
	*handle = (AILFILEHANDLE)open;
	return 1;
}
static void AILCALLBACK fileClose(AILFILEHANDLE handle) { fclose(((Open *)handle)->file); delete (Open *)handle; }
static S32 AILCALLBACK fileSeek(AILFILEHANDLE handle, S32 offset, U32 whence)
{
	Open *open = (Open *)handle;
	long target = whence == SEEK_CUR ? open->at + offset : (whence == SEEK_END ? g_size + offset : offset);
	open->at = target < 0 ? 0 : (target > g_size ? g_size : target);
	return (S32)open->at;
}
static U32 AILCALLBACK fileRead(AILFILEHANDLE handle, void *buffer, U32 bytes)
{
	Open *open = (Open *)handle;
	long wanted = g_size - open->at < (long)bytes ? g_size - open->at : (long)bytes;
	if (wanted <= 0) return 0;
	fseek(open->file, g_base + open->at, SEEK_SET);
	const size_t got = fread(buffer, 1, (size_t)wanted, open->file);
	open->at += (long)got;
	return (U32)got;
}

int main(int argc, char **argv)
{
	if (argc >= 3 && strcmp(argv[1], "--list") == 0) {
		std::vector<Entry> entries = readBig(argv[2]);
		for (size_t i = 0; i < entries.size(); ++i) printf("%s\n", entries[i].name.c_str());
		return 0;
	}
	if (argc < 2) {
		fprintf(stderr, "usage: miles_listen <archive.big> <entry> [volume] [pan]\n"
			"       miles_listen <file.wav|file.mp3> [volume] [pan]\n"
			"       miles_listen --list <archive.big>\n");
		return 2;
	}

	std::string name;
	int next = 2;
	const size_t length = strlen(argv[1]);
	if (length > 4 && strcasecmp(argv[1] + length - 4, ".big") == 0) {
		if (argc < 3) { fprintf(stderr, "which entry?\n"); return 2; }
		std::vector<Entry> entries = readBig(argv[1]);
		for (size_t i = 0; i < entries.size(); ++i) {
			if (strcasecmp(entries[i].name.c_str(), argv[2]) == 0) {
				g_path = argv[1]; g_base = entries[i].offset; g_size = entries[i].size; name = entries[i].name;
			}
		}
		if (name.empty()) { fprintf(stderr, "no entry %s in %s\n", argv[2], argv[1]); return 1; }
		next = 3;
	} else {
		g_path = argv[1];
		FILE *file = fopen(g_path.c_str(), "rb");
		if (file == NULL) { fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
		fseek(file, 0, SEEK_END);
		g_size = ftell(file);
		fclose(file);
		name = g_path;
	}
	const float volume = argc > next ? (float)atof(argv[next]) : 1.0f;
	const float pan = argc > next + 1 ? (float)atof(argv[next + 1]) : 0.5f;

	AIL_startup();
	if (!AIL_quick_startup(1, 0, 44100, 16, 2)) { fprintf(stderr, "no audio device\n"); return 1; }
	HDIGDRIVER driver = NULL;
	AIL_quick_handles(&driver, NULL, NULL);
	char version[128];
	AIL_MSS_version(version, sizeof(version));
	printf("%s: %s at volume %.2f, pan %.2f\n", version, name.c_str(), volume, pan);

	const size_t nameLength = name.size();
	if (nameLength > 4 && strcasecmp(name.c_str() + nameLength - 4, ".wav") == 0) {
		std::vector<unsigned char> bytes((size_t)g_size);
		FILE *file = fopen(g_path.c_str(), "rb");
		fseek(file, g_base, SEEK_SET);
		bytes.resize(fread(&bytes[0], 1, bytes.size(), file));
		fclose(file);

		AILSOUNDINFO info;
		memset(&info, 0, sizeof(info));
		if (!AIL_WAV_info(&bytes[0], &info)) { fprintf(stderr, "not a WAV\n"); return 1; }
		void *image = &bytes[0];
		void *decoded = NULL;
		U32 decodedBytes = 0;
		if (info.format == 0x11 && AIL_decompress_ADPCM(&info, &decoded, &decodedBytes)) image = decoded;
		printf("  %ld Hz, %ld channel(s), %lu frames%s\n", (long)info.rate, (long)info.channels,
			(unsigned long)info.samples, decoded != NULL ? ", IMA ADPCM" : "");

		HSAMPLE sample = AIL_allocate_sample_handle(driver);
		AIL_init_sample(sample);
		AIL_register_EOS_callback(sample, sampleDone);
		AIL_set_sample_file(sample, image, 0);
		AIL_set_sample_volume_pan(sample, volume, pan);
		AIL_start_sample(sample);
		while (!g_done) usleep(10000);
		AIL_release_sample_handle(sample);
		if (decoded != NULL) AIL_mem_free_lock(decoded);
	} else {
		AIL_set_file_callbacks(fileOpen, fileClose, fileSeek, fileRead);
		HSTREAM stream = AIL_open_stream(driver, name.c_str(), 0);
		if (stream == NULL) { fprintf(stderr, "could not open the stream\n"); return 1; }
		S32 total = 0;
		AIL_stream_ms_position(stream, &total, NULL);
		printf("  stream, %ld ms\n", (long)total);
		AIL_register_stream_callback(stream, streamDone);
		AIL_set_stream_volume_pan(stream, volume, pan);
		AIL_start_stream(stream);
		while (!g_done) usleep(10000);
		AIL_close_stream(stream);
	}
	AIL_shutdown();
	return 0;
}
