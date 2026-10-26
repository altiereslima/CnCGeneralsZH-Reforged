/*
 * The air, room, duck and limiter of the Miles surface on miniaudio (MSS.h's AIL_ex_set_3D_air,
 * AIL_ex_set_reverb, AIL_ex_duck and AIL_ex_limit), measured in the mix a -wav run records.
 *
 * miles_miniaudio.cpp is compiled into this test on its own, without the XAudio2 half, and run on its
 * offline mix: AIL_ex_offline_mix, then AIL_ex_mix_to_frame moves the mix by logic frames, so every
 * figure below is the same on every run and on a machine with no sound card.  What is heard comes
 * back through AIL_ex_start_capture, the tap -wav writes its file from.  The expected numbers are the
 * physics the helpers in MSS.h claim, worked out here, not read back from them.
 *
 * The room is MSS/mss_ex_room.h, which miles_xaudio2 runs too, so the room tests hold for what a
 * player on Windows hears.  Not covered: XAudio2's own filter and its wall-clock duck, which need an
 * audio device, and the delay a world sound starts with, which is the game's request list and not
 * this library.
 */
#include "test_harness.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "MSS/MSS.h"

static const unsigned TONE_RATE = 22050;
static const double TONE_PI = 3.14159265358979323846;	// MSVC's math.h gives M_PI only on request
static const double TONE_AMPLITUDE = 8000.0;
static const int LOGIC_FRAMES_PER_SECOND = 60;	// the logic clock AIL_ex_mix_to_frame counts
static const char CAPTURE_PATH[] = "test_miles_effects.wav";

static void put16(unsigned char *p, unsigned value) { p[0] = (unsigned char)value; p[1] = (unsigned char)(value >> 8); }
static void put32(unsigned char *p, unsigned value) { put16(p, value & 0xffff); put16(p + 2, value >> 16); }
static unsigned get16(const unsigned char *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }
static unsigned get32(const unsigned char *p) { return get16(p) | (get16(p + 2) << 16); }

// A mono 16-bit sine as a WAV image, the shape of the game's effects.
static std::vector<unsigned char> makeTone(double hz, unsigned milliseconds)
{
	const unsigned frames = TONE_RATE * milliseconds / 1000;
	std::vector<unsigned char> image(44 + frames * 2);
	memcpy(&image[0], "RIFF", 4);
	put32(&image[4], 36 + frames * 2);
	memcpy(&image[8], "WAVEfmt ", 8);
	put32(&image[16], 16);
	put16(&image[20], 1);
	put16(&image[22], 1);
	put32(&image[24], TONE_RATE);
	put32(&image[28], TONE_RATE * 2);
	put16(&image[32], 2);
	put16(&image[34], 16);
	memcpy(&image[36], "data", 4);
	put32(&image[40], frames * 2);
	for (unsigned frame = 0; frame < frames; ++frame) {
		put16(&image[44 + frame * 2], (unsigned short)(short)(sin(2.0 * TONE_PI * hz * frame / TONE_RATE) * TONE_AMPLITUDE));
	}
	return image;
}

// The device once, offline; a listener at the origin facing +Y with +Z up, so -X is the right speaker.
static HPROVIDER g_provider = 0;
static int g_logicFrame = 0;

static void startAudio()
{
	static bool started = false;
	if (started) {
		return;
	}
	started = true;
	AIL_ex_offline_mix();
	AIL_startup();
	AIL_quick_startup(1, 0, 44100, 16, 2);
	HPROENUM next = HPROENUM_FIRST;
	char *name = NULL;
	AIL_enumerate_3D_providers(&next, &g_provider, &name);
	H3DPOBJECT listener = AIL_open_3D_listener(g_provider);
	AIL_set_3D_orientation(listener, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
	AIL_set_3D_position(listener, 0.0f, 0.0f, 0.0f);
	AIL_ex_mix_to_frame(g_logicFrame);	// places the mix
}

static void runSeconds(double seconds)
{
	const int frames = (int)(seconds * LOGIC_FRAMES_PER_SECOND);
	for (int i = 0; i < frames; ++i) {
		AIL_ex_mix_to_frame(++g_logicFrame);
	}
}

// A 3D sample of `tone` on the X axis, minimum distance 10 and a maximum out of the way.
static H3DSAMPLE place(const std::vector<unsigned char> &tone, float x, float velocityX, float volume)
{
	H3DSAMPLE sample = AIL_allocate_3D_sample_handle(g_provider);
	AIL_set_3D_sample_file(sample, &tone[0]);
	AIL_set_3D_sample_volume(sample, volume);
	AIL_set_3D_sample_distances(sample, 100000.0f, 10.0f);
	AIL_set_3D_velocity_vector(sample, velocityX, 0.0f, 0.0f);
	AIL_set_3D_position(sample, x, 0.0f, 0.0f);
	return sample;
}

struct Capture
{
	std::vector<double> mono;	// left plus right over two
	unsigned rate;
	double peak;	// the loudest sample of either side, full scale 1
};

static Capture readCapture()
{
	Capture capture;
	capture.rate = 0;
	capture.peak = 0.0;
	FILE *file = fopen(CAPTURE_PATH, "rb");
	if (file == NULL) {
		return capture;
	}
	std::vector<unsigned char> bytes;
	unsigned char buffer[65536];
	size_t got;
	while ((got = fread(buffer, 1, sizeof(buffer), file)) > 0) {
		bytes.insert(bytes.end(), buffer, buffer + got);
	}
	fclose(file);
	remove(CAPTURE_PATH);
	if (bytes.size() < 44) {
		return capture;
	}
	capture.rate = get32(&bytes[24]);
	const size_t frames = (bytes.size() - 44) / 4;
	capture.mono.resize(frames);
	for (size_t i = 0; i < frames; ++i) {
		const double left = (double)(short)get16(&bytes[44 + i * 4]) / 32767.0;
		const double right = (double)(short)get16(&bytes[44 + i * 4 + 2]) / 32767.0;
		capture.mono[i] = (left + right) / 2.0;
		capture.peak = fabs(left) > capture.peak ? fabs(left) : capture.peak;
		capture.peak = fabs(right) > capture.peak ? fabs(right) : capture.peak;
	}
	return capture;
}

// Plays the samples given for `seconds` and returns what came out.
static Capture listen(const std::vector<H3DSAMPLE> &samples, double seconds)
{
	AIL_ex_start_capture(CAPTURE_PATH);
	for (size_t i = 0; i < samples.size(); ++i) {
		AIL_start_3D_sample(samples[i]);
	}
	runSeconds(seconds);
	AIL_ex_stop_capture();
	for (size_t i = 0; i < samples.size(); ++i) {
		AIL_release_3D_sample_handle(samples[i]);
	}
	return readCapture();
}

static double rms(const Capture &capture, double fromSeconds, double toSeconds)
{
	const size_t from = (size_t)(fromSeconds * capture.rate);
	const size_t to = (size_t)(toSeconds * capture.rate) < capture.mono.size() ? (size_t)(toSeconds * capture.rate) : capture.mono.size();
	double sum = 0.0;
	for (size_t i = from; i < to; ++i) {
		sum += capture.mono[i] * capture.mono[i];
	}
	return to > from ? sqrt(sum / (double)(to - from)) : 0.0;
}

static double frequency(const Capture &capture, double fromSeconds, double toSeconds)
{
	const size_t from = (size_t)(fromSeconds * capture.rate);
	const size_t to = (size_t)(toSeconds * capture.rate);
	unsigned crossings = 0;
	for (size_t i = from + 1; i < to && i < capture.mono.size(); ++i) {
		if ((capture.mono[i - 1] < 0.0) != (capture.mono[i] < 0.0)) {
			++crossings;
		}
	}
	return (double)crossings / 2.0 / (toSeconds - fromSeconds);
}

// The level of an 8 kHz tone at a distance, with the 1/distance falloff taken back out.
static double brightnessAt(float x)
{
	std::vector<unsigned char> tone = makeTone(8000.0, 500);
	std::vector<H3DSAMPLE> samples(1, place(tone, x, 0.0f, 1.0f));
	const Capture capture = listen(samples, 0.6);
	return rms(capture, 0.1, 0.4) / (10.0 / fabs(x));
}

TEST(air_takes_the_top_off_a_far_sound)
{
	startAudio();
	// No air: distance changes the level and nothing else.
	AIL_ex_set_3D_air(0.0f, 0.0f);
	const double nearOpen = brightnessAt(-20.0f);
	const double farOpen = brightnessAt(-1600.0f);
	// all of it on the right, half of the mono average: 0.17263 / 2 at full volume
	CHECK(nearOpen > 0.05);
	CHECK(fabs(farOpen / nearOpen - 1.0) < 0.05);

	// Air at 400: 1600 out is a cutoff of 20 kHz / 17, 1.2 kHz, where a one-pole leaves an 8 kHz
	// tone under a fifth of itself; 20 out the cutoff is still near 20 kHz and the tone passes.
	AIL_ex_set_3D_air(0.0f, 400.0f);
	const double nearAir = brightnessAt(-20.0f);
	const double farAir = brightnessAt(-1600.0f);
	AIL_ex_set_3D_air(0.0f, 0.0f);
	CHECK(nearAir / nearOpen > 0.9);
	CHECK(farAir / nearAir < 0.2);
}

TEST(doppler_raises_what_comes_and_lowers_what_goes)
{
	startAudio();
	// A quarter of the speed of sound towards the listener is 1700 / 1275, a third up; the same away
	// is 1700 / 2125, a fifth down.
	AIL_ex_set_3D_air(1700.0f, 0.0f);
	std::vector<unsigned char> tone = makeTone(440.0, 500);
	std::vector<H3DSAMPLE> coming(1, place(tone, -500.0f, 425.0f, 1.0f));
	const Capture up = listen(coming, 0.6);
	std::vector<H3DSAMPLE> going(1, place(tone, -500.0f, -425.0f, 1.0f));
	const Capture down = listen(going, 0.6);
	std::vector<H3DSAMPLE> standing(1, place(tone, -500.0f, 0.0f, 1.0f));
	const Capture still = listen(standing, 0.6);
	AIL_ex_set_3D_air(0.0f, 0.0f);

	CHECK_NEAR(frequency(still, 0.05, 0.25), 440.0, 440.0 * 0.02);
	CHECK_NEAR(frequency(up, 0.05, 0.25), 440.0 * 4.0 / 3.0, 440.0 * 0.02);
	CHECK_NEAR(frequency(down, 0.05, 0.25), 440.0 * 0.8, 440.0 * 0.02);
}

TEST(the_room_rings_on_after_the_sound_has_stopped)
{
	startAudio();
	// A tenth of a second of tone, then listen past it: dry is silence, a room is a tail.
	std::vector<unsigned char> tone = makeTone(440.0, 100);
	AIL_ex_set_reverb(0.0f, 1.5f, 0.3f, 0.0f, 0.0f);
	std::vector<H3DSAMPLE> dry(1, place(tone, -20.0f, 0.0f, 1.0f));
	const Capture dryCapture = listen(dry, 1.0);

	AIL_ex_set_reverb(0.5f, 1.5f, 0.3f, 0.0f, 0.0f);
	std::vector<H3DSAMPLE> wet(1, place(tone, -20.0f, 0.0f, 1.0f));
	const Capture wetCapture = listen(wet, 1.0);

	// An echo holds the room back: after a 30 ms blip the room answers within a comb's length, 25 ms,
	// and with a 90 ms echo nothing comes back before the echo does.
	// Each waits out the last one's tail first, four seconds being 160 dB at this decay.
	std::vector<unsigned char> blip = makeTone(440.0, 30);
	runSeconds(4.0);
	std::vector<H3DSAMPLE> prompt(1, place(blip, -20.0f, 0.0f, 1.0f));
	const Capture promptCapture = listen(prompt, 0.3);
	runSeconds(4.0);
	AIL_ex_set_reverb(0.5f, 1.5f, 0.3f, 0.09f, 0.35f);
	std::vector<H3DSAMPLE> echoed(1, place(blip, -20.0f, 0.0f, 1.0f));
	const Capture echoCapture = listen(echoed, 0.3);
	AIL_ex_set_reverb(0.0f, 1.0f, 0.0f, 0.0f, 0.0f);
	runSeconds(3.0);	// let the tail die before the next test listens

	CHECK(rms(dryCapture, 0.0, 0.1) > 0.02);
	CHECK(rms(dryCapture, 0.3, 0.6) < 0.0002);
	CHECK(rms(wetCapture, 0.3, 0.6) > 0.001);
	CHECK(rms(wetCapture, 0.3, 0.6) < rms(wetCapture, 0.0, 0.1));
	CHECK(rms(promptCapture, 0.035, 0.085) > 0.001);
	CHECK(rms(echoCapture, 0.035, 0.085) < 0.0002);
	CHECK(rms(echoCapture, 0.095, 0.12) > 0.002);
}

TEST(a_dry_sample_takes_no_air_and_no_room)
{
	startAudio();
	// A unit's reply: 1600 out with air at 400 keeps its 8 kHz, and a room at half send adds no tail.
	AIL_ex_set_3D_air(0.0f, 400.0f);
	AIL_ex_set_reverb(0.5f, 1.5f, 0.3f, 0.0f, 0.0f);
	std::vector<unsigned char> bright = makeTone(8000.0, 500);
	std::vector<H3DSAMPLE> farReply(1, place(bright, -1600.0f, 0.0f, 1.0f));
	AIL_ex_set_3D_sample_dry(farReply[0], 1);
	const double farLevel = rms(listen(farReply, 0.6), 0.1, 0.4) / (10.0 / 1600.0);
	std::vector<H3DSAMPLE> nearReply(1, place(bright, -20.0f, 0.0f, 1.0f));
	AIL_ex_set_3D_sample_dry(nearReply[0], 1);
	const double nearLevel = rms(listen(nearReply, 0.6), 0.1, 0.4) / (10.0 / 20.0);
	runSeconds(4.0);
	std::vector<unsigned char> blip = makeTone(440.0, 100);
	std::vector<H3DSAMPLE> reply(1, place(blip, -20.0f, 0.0f, 1.0f));
	AIL_ex_set_3D_sample_dry(reply[0], 1);
	const Capture replyCapture = listen(reply, 1.0);
	AIL_ex_set_3D_air(0.0f, 0.0f);
	AIL_ex_set_reverb(0.0f, 1.0f, 0.0f, 0.0f, 0.0f);
	runSeconds(3.0);

	CHECK(nearLevel > 0.05);
	CHECK(fabs(farLevel / nearLevel - 1.0) < 0.05);
	CHECK(rms(replyCapture, 0.0, 0.1) > 0.02);
	CHECK(rms(replyCapture, 0.3, 0.6) < 0.0002);
}

TEST(a_duck_dips_the_rest_and_spares_the_blast)
{
	startAudio();
	// The rest is a steady tone, low enough that the duck's muffle leaves it be; the blast is a silent
	// sample, since only its exemption matters.
	std::vector<unsigned char> steady = makeTone(110.0, 4000);
	std::vector<unsigned char> blastTone = makeTone(880.0, 4000);
	std::vector<H3DSAMPLE> samples;
	samples.push_back(place(steady, -20.0f, 0.0f, 1.0f));
	samples.push_back(place(blastTone, -20.0f, 0.0f, 0.0f));
	AIL_ex_duck(samples[1], 0.25f, 2.0f);
	const Capture capture = listen(samples, 3.0);

	// Held at a quarter for the first half second (the hold is a quarter of the two), back by two.
	const double full = rms(capture, 2.2, 2.9);
	CHECK(full > 0.02);
	CHECK_NEAR(rms(capture, 0.1, 0.45) / full, 0.25, 0.03);
	CHECK(rms(capture, 1.2, 1.3) / full > 0.4);
	CHECK(rms(capture, 1.2, 1.3) / full < 0.9);
}

TEST(a_duck_muffles_what_it_dips)
{
	startAudio();
	// A duck to a quarter is a 1250 Hz ceiling, AIL_ex_duck_cutoff: an 8 kHz tone under it loses far
	// more than the quarter the gain takes.
	std::vector<unsigned char> bright = makeTone(8000.0, 3000);
	std::vector<unsigned char> blastTone = makeTone(880.0, 3000);
	std::vector<H3DSAMPLE> samples;
	samples.push_back(place(bright, -20.0f, 0.0f, 1.0f));
	samples.push_back(place(blastTone, -20.0f, 0.0f, 0.0f));
	AIL_ex_duck(samples[1], 0.25f, 2.0f);
	const Capture capture = listen(samples, 2.5);

	const double full = rms(capture, 2.1, 2.4);
	CHECK(full > 0.02);
	CHECK(rms(capture, 0.1, 0.45) / full < 0.1);
}

TEST(a_loud_mix_never_reaches_full_scale)
{
	startAudio();
	// Eight tones at full volume inside their minimum distance, all on the right: four times full
	// scale before the limiter, and nothing past its ceiling after it.
	const unsigned TONES = 8;
	std::vector<unsigned char> tones[TONES];
	std::vector<H3DSAMPLE> samples;
	for (unsigned i = 0; i < TONES; ++i) {
		tones[i] = makeTone(110.0 * (i + 1), 1000);
		samples.push_back(place(tones[i], -5.0f, 0.0f, 1.0f));
	}
	const Capture capture = listen(samples, 1.2);

	CHECK(capture.peak <= AIL_EX_LIMITER_CEILING + 0.001);
	CHECK(capture.peak > AIL_EX_LIMITER_CEILING - 0.05);
	CHECK(rms(capture, 1.1, 1.2) < 0.0002);
}

TEST(headphones_hear_a_sound_on_the_right_later_quieter_and_duller_in_the_left_ear)
{
	// MSS.h's ears alone, both backends' code.  A click straight to the right of a listener facing +y
	// (right is up x face, -x here, as the test above places its tones): Woodworth's 0.66 ms is 31.6
	// frames at 48 kHz, and the far ear's shadow both lowers and spreads what arrives.
	const float RATE = 48000.0f;
	const int FRAMES = 96;
	float side = 0.0f;
	float front = 0.0f;
	AIL_ex_3D_bearing(-10.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, &side, &front);
	CHECK_NEAR(side, 1.0f, 0.0001f);
	CHECK_NEAR(front, 0.0f, 0.0001f);

	AIL_EX_HRTF_TARGET ears;
	AIL_ex_hrtf_target(side, front, RATE, &ears);
	CHECK_NEAR(ears.delay[0], AIL_EX_HRTF_HEAD_SECONDS * (TONE_PI / 2.0 + 1.0) * RATE, 0.01);
	CHECK_EQ(ears.delay[1], 0.0f);
	CHECK(ears.gain[0] < ears.gain[1]);
	CHECK(ears.coefficient[0] < ears.coefficient[1]);

	float click[FRAMES] = { 1.0f };
	float heard[FRAMES * 2];
	AIL_EX_HRTF head;
	AIL_ex_hrtf_reset(&head);
	AIL_ex_hrtf_process(&head, &ears, click, heard, FRAMES);

	int loudest[2] = { 0, 0 };
	float leftSum = 0.0f;
	float rightSum = 0.0f;
	for (int frame = 0; frame < FRAMES; ++frame) {
		for (int ear = 0; ear < 2; ++ear) {
			if (fabsf(heard[frame * 2 + ear]) > fabsf(heard[loudest[ear] * 2 + ear])) {
				loudest[ear] = frame;
			}
		}
		leftSum += heard[frame * 2];
		rightSum += heard[frame * 2 + 1];
	}
	CHECK(loudest[1] <= 1);
	CHECK(loudest[0] >= 31);
	CHECK(fabsf(heard[30 * 2]) < 1e-6f);	// nothing reaches the left ear before the delay
	CHECK(leftSum < rightSum);

	// straight ahead the two ears are one: no delay, the stereo pan's centre gain
	AIL_ex_hrtf_target(0.0f, 1.0f, RATE, &ears);
	CHECK_EQ(ears.delay[0], ears.delay[1]);
	CHECK_NEAR(ears.gain[0], AIL_EX_HRTF_CENTRE, 0.0001f);
	CHECK_NEAR(ears.gain[1], AIL_EX_HRTF_CENTRE, 0.0001f);
}
