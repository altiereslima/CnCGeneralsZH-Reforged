/*
 * miles_smoke - plays sound through the x64 audio backend and checks that it was consumed.
 *
 * The x64 build has no retail mss32.dll to bind to, so MilesAudioManager talks to the XAudio2 and
 * FFmpeg implementation in Libraries/Source/WWVegas/Miles6/xaudio2/.  This drives that
 * implementation the way the game does and waits for the end-of-sample callbacks: a callback only
 * arrives once a voice has actually played a buffer out, so it is evidence that samples reached
 * the mixer rather than evidence that the calls returned.
 *
 * Two paths, because the game has two: a whole PCM image handed over for a sound effect, and a
 * file decoded as it plays for music and speech, read through the game's own file callbacks.
 *
 * On a machine with no audio device at all the mixer cannot start; that prints "skip" and passes,
 * the same way bink_smoke does without game data.
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "MSS/MSS.h"

static int failures = 0;

#define CHECK(cond, what)                                                      \
	do {                                                                       \
		if (!(cond)) {                                                         \
			printf("FAIL %s\n", (what));                                       \
			++failures;                                                        \
		}                                                                      \
	} while (0)

static const unsigned SAMPLE_RATE = 22050;
static const unsigned SAMPLE_MILLISECONDS = 250;
static const unsigned CALLBACK_TIMEOUT_MS = 5000;
static const double TONE_HZ = 440.0;

static volatile LONG g_sampleFinished;
static volatile LONG g_streamFinished;

static void AILCALLBACK onSampleDone(HSAMPLE) { InterlockedExchange(&g_sampleFinished, 1); }
static void AILCALLBACK onStreamDone(HSTREAM) { InterlockedExchange(&g_streamFinished, 1); }

/* ---- the game's file callbacks, as the plain filesystem ------------------------------------ */

static U32 AILCALLBACK fileOpen(const char *name, AILFILEHANDLE *handle)
{
	FILE *file = fopen(name, "rb");
	*handle = (AILFILEHANDLE)file;
	return file != NULL;
}

static void AILCALLBACK fileClose(AILFILEHANDLE handle) { fclose((FILE *)handle); }

static S32 AILCALLBACK fileSeek(AILFILEHANDLE handle, S32 offset, U32 whence)
{
	fseek((FILE *)handle, offset, (int)whence);
	return (S32)ftell((FILE *)handle);
}

static U32 AILCALLBACK fileRead(AILFILEHANDLE handle, void *buffer, U32 bytes)
{
	return (U32)fread(buffer, 1, bytes, (FILE *)handle);
}

/* ---- a WAV image to play -------------------------------------------------------------------- */

static void writeU32(unsigned char *p, unsigned int value)
{
	p[0] = (unsigned char)(value & 0xff);
	p[1] = (unsigned char)((value >> 8) & 0xff);
	p[2] = (unsigned char)((value >> 16) & 0xff);
	p[3] = (unsigned char)((value >> 24) & 0xff);
}

static void writeU16(unsigned char *p, unsigned short value)
{
	p[0] = (unsigned char)(value & 0xff);
	p[1] = (unsigned char)((value >> 8) & 0xff);
}

/* Returns a malloc'd mono 16-bit PCM WAV image of a sine tone, and its size. */
static unsigned char *makeToneWav(unsigned milliseconds, unsigned *imageBytes)
{
	const unsigned frames = SAMPLE_RATE * milliseconds / 1000;
	const unsigned payload = frames * 2;
	unsigned char *image = (unsigned char *)malloc(44 + payload);
	if (image == NULL) {
		return NULL;
	}

	memcpy(image, "RIFF", 4);
	writeU32(image + 4, 44 + payload - 8);
	memcpy(image + 8, "WAVEfmt ", 8);
	writeU32(image + 16, 16);
	writeU16(image + 20, 1);				/* PCM */
	writeU16(image + 22, 1);				/* mono */
	writeU32(image + 24, SAMPLE_RATE);
	writeU32(image + 28, SAMPLE_RATE * 2);
	writeU16(image + 32, 2);
	writeU16(image + 34, 16);
	memcpy(image + 36, "data", 4);
	writeU32(image + 40, payload);

	for (unsigned frame = 0; frame < frames; ++frame) {
		const double phase = 2.0 * 3.14159265358979 * TONE_HZ * frame / SAMPLE_RATE;
		const short value = (short)(sin(phase) * 8000.0);
		writeU16(image + 44 + frame * 2, (unsigned short)value);
	}

	*imageBytes = 44 + payload;
	return image;
}

static bool waitFor(volatile LONG *flag)
{
	for (unsigned waited = 0; waited < CALLBACK_TIMEOUT_MS; waited += 50) {
		if (InterlockedCompareExchange(flag, 0, 0)) {
			return true;
		}
		Sleep(50);
	}
	return false;
}

int main(void)
{
	unsigned imageBytes = 0;
	unsigned char *wav = makeToneWav(SAMPLE_MILLISECONDS, &imageBytes);
	if (wav == NULL) {
		printf("FAIL out of memory building the test tone\n");
		return 1;
	}

	AIL_startup();
	if (!AIL_quick_startup(1, 0, 44100, 16, 2)) {
		printf("skip: no audio device\n");
		free(wav);
		return 0;
	}

	HDIGDRIVER digitalDriver = NULL;
	AIL_quick_handles(&digitalDriver, NULL, NULL);
	CHECK(digitalDriver != NULL, "AIL_quick_handles gave no digital driver");

	/* The provider list the options screen shows, and the one name MilesAudioManager looks up by
	   hand before indexing its array with the result. */
	HPROENUM next = HPROENUM_FIRST;
	HPROVIDER provider = 0;
	char *name = NULL;
	bool sawFast2D = false;
	while (AIL_enumerate_3D_providers(&next, &provider, &name)) {
		if (name != NULL && strcmp(name, "Miles Fast 2D Positional Audio") == 0) {
			sawFast2D = true;
		}
	}
	CHECK(sawFast2D, "the provider list has no \"Miles Fast 2D Positional Audio\"");

	/* The WAV header reader the audio cache runs over every sound it loads. */
	AILSOUNDINFO info;
	memset(&info, 0, sizeof(info));
	CHECK(AIL_WAV_info(wav, &info) != 0, "AIL_WAV_info rejected a PCM WAV");
	CHECK(info.format == WAVE_FORMAT_PCM, "AIL_WAV_info reported the wrong format");
	CHECK(info.rate == SAMPLE_RATE, "AIL_WAV_info reported the wrong sample rate");
	CHECK(info.channels == 1, "AIL_WAV_info reported the wrong channel count");
	CHECK(info.samples == SAMPLE_RATE * SAMPLE_MILLISECONDS / 1000,
		"AIL_WAV_info reported the wrong sample count");

	/* Path one: a sound effect, played from an image already in memory. */
	HSAMPLE sample = AIL_allocate_sample_handle(digitalDriver);
	CHECK(sample != NULL, "no 2D sample handle");
	if (sample != NULL) {
		AIL_init_sample(sample);
		AIL_register_EOS_callback(sample, onSampleDone);
		AIL_set_sample_volume_pan(sample, 0.2f, 0.5f);
		CHECK(AIL_set_sample_file(sample, wav, 0) != 0, "AIL_set_sample_file rejected the tone");
		AIL_start_sample(sample);
		CHECK(waitFor(&g_sampleFinished), "the sample never reached its end-of-sample callback");
		AIL_release_sample_handle(sample);
	}

	/* Path two: a stream, decoded from a file through the callbacks the game installs. */
	char streamPath[MAX_PATH];
	char tempDirectory[MAX_PATH];
	GetTempPathA(sizeof(tempDirectory), tempDirectory);
	snprintf(streamPath, sizeof(streamPath), "%smiles_smoke_tone.wav", tempDirectory);
	FILE *out = fopen(streamPath, "wb");
	if (out != NULL) {
		fwrite(wav, 1, imageBytes, out);
		fclose(out);

		AIL_set_file_callbacks(fileOpen, fileClose, fileSeek, fileRead);
		HSTREAM stream = AIL_open_stream(digitalDriver, streamPath, 0);
		CHECK(stream != NULL, "AIL_open_stream could not open the tone");
		if (stream != NULL) {
			S32 total = 0;
			AIL_stream_ms_position(stream, &total, NULL);
			CHECK(total > (S32)SAMPLE_MILLISECONDS / 2 && total < (S32)SAMPLE_MILLISECONDS * 2,
				"the stream's reported length is not the tone's length");

			AIL_register_stream_callback(stream, onStreamDone);
			AIL_set_stream_volume_pan(stream, 0.2f, 0.5f);
			AIL_start_stream(stream);
			CHECK(waitFor(&g_streamFinished), "the stream never reached its end-of-stream callback");
			AIL_close_stream(stream);
		}
		remove(streamPath);
	}

	AIL_shutdown();
	free(wav);

	printf("miles_smoke: %d failure(s)\n", failures);
	return failures;
}
