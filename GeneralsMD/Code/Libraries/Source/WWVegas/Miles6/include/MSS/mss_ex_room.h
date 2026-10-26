/*
 * The room (AIL_ex_set_reverb): the reverb every 3D sample sends to, shared by both backends so that
 * what a player hears on Windows and what a -wav run records are the same room.
 *
 * Not Miles.  A small reverb of Freeverb's shape, cut to four combs and two all-passes a side.  The
 * send goes through the echo first, a delay that is also heard once on its own (a street of
 * buildings answering), then into the combs, damped in their feedback, and out through the
 * all-passes that smear them.  Mono in, stereo out; the right side's lines are 23 samples longer
 * than the left's, Freeverb's spread, for width.
 *
 * XAudio2's own reverb was the Windows room until 2026-10-25, through an I3DL2 mapping of these
 * five numbers.  Driven offline against this one it answered an impulse 12 dB quieter on open
 * ground, 20 dB in a town and 31 dB in snow, with the top end of its tail 19 dB further down, so a
 * player heard next to no room at all; and it takes 20 to 48 kHz only, so a device at 96 kHz had
 * none.  miles_xaudio2 runs this as an effect on its room submix instead.
 *
 * C++ only: the two backends are.  The caller keeps it to one thread at a time.
 */

#ifndef MSS_EX_ROOM_H
#define MSS_EX_ROOM_H

#include <math.h>
#include <stddef.h>
#include <vector>

const unsigned ROOM_COMBS = 4;
const unsigned ROOM_ALLPASSES = 2;
const unsigned ROOM_COMB_LENGTHS[ROOM_COMBS] = { 1116, 1188, 1277, 1356 };	// Freeverb's, at 44.1 kHz
const unsigned ROOM_ALLPASS_LENGTHS[ROOM_ALLPASSES] = { 556, 441 };
const unsigned ROOM_STEREO_SPREAD = 23;
const unsigned ROOM_OUTPUT_CHANNELS = 2;
const float ROOM_TUNING_RATE = 44100.0f;
const float ROOM_ALLPASS_FEEDBACK = 0.5f;
const float ROOM_INPUT_GAIN = 0.25f;		// four combs at a second-long decay answer at about the send's level
const float ROOM_MAX_DAMPING = 0.7f;
const float ROOM_MAX_ECHO_SECONDS = 0.3f;	// the longest echo MilesAudioManager asks for is 0.26
const float ROOM_DENORMAL_GUARD = 1e-18f;	// keeps a dying tail out of the denormals, which x64 does slowly

struct DelayLine
{
	DelayLine() : at(0), feedback(0.0f), store(0.0f) {}

	std::vector<float> line;
	size_t at;
	float feedback;
	float store;	// a comb's damping filter
};

struct Room
{
	Room() : ready(false), rate(0), damping(0.0f), slap(0.0f), echoLength(0) {}

	bool ready;
	unsigned rate;
	float damping;
	float slap;	// the echo's own level, AIL_ex_set_reverb
	DelayLine echo;
	size_t echoLength;
	DelayLine combs[2][ROOM_COMBS];
	DelayLine allpasses[2][ROOM_ALLPASSES];
};

inline void initRoom(Room *room, unsigned rate)
{
	const float scale = (float)rate / ROOM_TUNING_RATE;
	for (unsigned side = 0; side < 2; ++side) {
		for (unsigned i = 0; i < ROOM_COMBS; ++i) {
			room->combs[side][i].line.assign((size_t)((ROOM_COMB_LENGTHS[i] + side * ROOM_STEREO_SPREAD) * scale), 0.0f);
		}
		for (unsigned i = 0; i < ROOM_ALLPASSES; ++i) {
			room->allpasses[side][i].line.assign((size_t)((ROOM_ALLPASS_LENGTHS[i] + side * ROOM_STEREO_SPREAD) * scale), 0.0f);
		}
	}
	room->echo.line.assign((size_t)(ROOM_MAX_ECHO_SECONDS * rate) + 1, 0.0f);
	room->rate = rate;
	room->ready = true;
}

inline void setRoom(Room *room, float decay, float damping, float echo, float slap)
{
	if (!room->ready) {
		return;
	}
	// each comb loses 60 dB in `decay` seconds: a gain per trip of 0.001 to the power of trips that long
	for (unsigned side = 0; side < 2; ++side) {
		for (unsigned i = 0; i < ROOM_COMBS; ++i) {
			DelayLine &comb = room->combs[side][i];
			comb.feedback = powf(0.001f, (float)comb.line.size() / (decay * (float)room->rate));
		}
	}
	room->damping = damping * ROOM_MAX_DAMPING;
	room->slap = slap;
	const size_t echoLength = echo > 0.0f ? (size_t)(echo * (float)room->rate) : 0;
	room->echoLength = echoLength < room->echo.line.size() ? echoLength : room->echo.line.size() - 1;
	room->echo.at = room->echoLength > 0 ? room->echo.at % room->echoLength : 0;
}

// Adds the room's answer to `input` (mono, the sends of one mix pass) into the stereo `output`.
inline void processRoom(Room *room, const float *input, float *output, unsigned frames)
{
	for (unsigned frame = 0; frame < frames; ++frame) {
		float in = input[frame];
		float echoed = 0.0f;
		if (room->echoLength > 0) {
			DelayLine &echo = room->echo;
			echoed = echo.line[echo.at];
			echo.line[echo.at] = in;
			echo.at = (echo.at + 1) % room->echoLength;
			in = echoed;
		}
		for (unsigned side = 0; side < 2; ++side) {
			float sum = 0.0f;
			for (unsigned i = 0; i < ROOM_COMBS; ++i) {
				DelayLine &comb = room->combs[side][i];
				const float out = comb.line[comb.at];
				comb.store = out + room->damping * (comb.store - out);
				comb.line[comb.at] = in * ROOM_INPUT_GAIN + comb.store * comb.feedback + ROOM_DENORMAL_GUARD;
				comb.at = (comb.at + 1) % comb.line.size();
				sum += out;
			}
			for (unsigned i = 0; i < ROOM_ALLPASSES; ++i) {
				DelayLine &pass = room->allpasses[side][i];
				const float held = pass.line[pass.at];
				pass.line[pass.at] = sum + held * ROOM_ALLPASS_FEEDBACK;
				pass.at = (pass.at + 1) % pass.line.size();
				sum = held - sum;
			}
			output[frame * ROOM_OUTPUT_CHANNELS + side] += sum + echoed * room->slap;
		}
	}
}

#endif /* MSS_EX_ROOM_H */
