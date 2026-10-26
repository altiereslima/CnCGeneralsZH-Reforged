/*
 * The arithmetic of MilesAudioManager's battle mix (MilesAudioDevice/AudioMixPolicy.h): which sound
 * gives way when the voices run out, how much louder a folded shot plays, how deep a hill stands in
 * the line from the microphone, and how the muffle moves.  The manager itself, its lists and the
 * terrain it reads are measured in the -wav mix (experiments/proofs/2026-10-25/audio-mix).
 */
#include "test_harness.h"

#include "MilesAudioDevice/AudioMixPolicy.h"

using namespace AudioMixPolicy;

TEST(importance_puts_priority_first_and_the_quieter_sound_below_its_equal)
{
	CHECK(importance(2, 0.9f) < importance(3, 0.0f));
	CHECK(importance(2, 0.1f) < importance(2, 0.8f));
	CHECK_NEAR(importance(1, 3.0f), 2.0f, 1e-6f);	// loudness is held to 0..1
	CHECK_NEAR(importance(1, -1.0f), 1.0f, 1e-6f);
}

TEST(a_folded_shot_grows_a_step_each_and_stops_at_the_ceiling)
{
	CHECK_NEAR(coalescedGain(0, 0.12f, 1.5f), 1.0f, 1e-6f);
	CHECK_NEAR(coalescedGain(2, 0.12f, 1.5f), 1.24f, 1e-6f);
	CHECK_NEAR(coalescedGain(19, 0.12f, 1.5f), 1.5f, 1e-6f);
}

// A ridge 100 high between x = 400 and x = 600 on flat ground at 0.
static float ridge(float x, float)
{
	return x >= 400.0f && x <= 600.0f ? 100.0f : 0.0f;
}

TEST(a_ridge_between_microphone_and_sound_stands_in_the_line_by_its_height)
{
	// microphone at 50 over the ground at x 0, the sound at 10 over it at x 1000: the line is near 30 at the ridge
	const float depth = rayObstruction(0.0f, 0.0f, 50.0f, 1000.0f, 0.0f, 10.0f, 20.0f, 25.0f, 64, ridge);
	CHECK(depth > 65.0f && depth < 75.0f);
	CHECK_NEAR(occlusionAmount(depth, 30.0f), 1.0f, 1e-6f);

	// the same ridge seen from high above clears the line
	CHECK_NEAR(rayObstruction(0.0f, 0.0f, 400.0f, 1000.0f, 0.0f, 10.0f, 20.0f, 25.0f, 64, ridge), 0.0f, 1e-6f);
}

TEST(the_ground_under_either_end_is_not_a_hill_between_them)
{
	// flat ground at 0 and a sound on it at 0: only the margins touch the line, and they are left out
	struct Flat { float operator()(float, float) const { return 0.0f; } };
	CHECK_NEAR(rayObstruction(0.0f, 0.0f, 50.0f, 300.0f, 0.0f, 0.0f, 20.0f, 25.0f, 64, Flat()), 0.0f, 1e-6f);
	// a sound closer than both margins is never looked through
	CHECK_NEAR(rayObstruction(0.0f, 0.0f, 0.0f, 40.0f, 0.0f, 0.0f, 20.0f, 25.0f, 64, ridge), 0.0f, 1e-6f);
}

TEST(a_long_line_is_sampled_no_more_than_the_cap)
{
	int calls = 0;
	rayObstruction(0.0f, 0.0f, 50.0f, 5000.0f, 0.0f, 0.0f, 20.0f, 25.0f, 48,
		[&calls](float, float) { ++calls; return 0.0f; });
	CHECK_EQ(calls, 48);
}

TEST(the_muffle_moves_towards_what_was_seen_and_stands_still_without_time)
{
	CHECK_NEAR(approach(0.0f, 1.0f, 0.0f, 250.0f), 0.0f, 1e-6f);
	CHECK_NEAR(approach(0.0f, 1.0f, 250.0f, 250.0f), 0.6321f, 1e-3f);
	CHECK_NEAR(approach(0.3f, 1.0f, 16.0f, 0.0f), 1.0f, 1e-6f);
	// two half steps land where one whole step does, so the frame rate does not change how fast it opens
	CHECK_NEAR(approach(approach(1.0f, 0.0f, 125.0f, 250.0f), 0.0f, 125.0f, 250.0f), approach(1.0f, 0.0f, 250.0f, 250.0f), 1e-5f);
}
