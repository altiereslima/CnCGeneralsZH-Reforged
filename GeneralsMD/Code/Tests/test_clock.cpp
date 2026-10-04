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
 * Lib/Clock.h - the two questions the engine asks a clock.
 *
 * What is worth pinning here is not "does it return a number".  It is the three
 * properties the 462 call sites B2 moved depend on, each of which would fail
 * silently rather than loudly if a future implementation got it wrong:
 *
 *   - the millisecond clocks are 32 bits wide, because interval arithmetic in
 *     this engine relies on their wrap (SysTimeClass::Reset writes
 *     `WrapAdd = 0 - StartTime`, which is only correct in 32-bit unsigned);
 *   - all of them go forwards and none of them goes backwards;
 *   - the tick clock and its rate describe the same clock, so that
 *     (later - earlier) / rate is a duration and not a number.
 *
 * This runs on macOS today, which is the point: the Windows half of the header
 * is a one-line forward to the call that was already there, and the macOS half
 * is the part nobody has ever run.
 */
#include "test_harness.h"
#include "Lib/Clock.h"

/* Long enough that the coarse clock (about 15.6ms on Windows) must move, short
   enough that nobody notices it in ctest. */
static void spin_at_least(unsigned milliseconds)
{
	const unsigned start = Clock_Milliseconds();
	while (Clock_Milliseconds() - start < milliseconds)
		; /* busy - sleeping would drag in a platform header this test does not want */
}

TEST(clock_milliseconds_is_thirty_two_bits)
{
	/* Not a style preference.  A wider return value would stop the callers'
	   wrap arithmetic wrapping, and it would stop it quietly, 49.7 days in. */
	CHECK_EQ(sizeof(Clock_Milliseconds()), (size_t)4);
	CHECK_EQ(sizeof(Clock_Milliseconds_Coarse()), (size_t)4);

	/* And unsigned, which is the half of it that makes the wrap defined
	   behaviour rather than something the compiler may assume never happens. */
	const unsigned int all_ones = (unsigned int)-1;
	CHECK(all_ones > 0u);
	CHECK_EQ((unsigned int)(all_ones + 1u), 0u);
}

TEST(clock_milliseconds_goes_forwards)
{
	const unsigned first = Clock_Milliseconds();
	spin_at_least(3);
	const unsigned second = Clock_Milliseconds();
	/* Unsigned subtraction, so this is still true across the wrap. */
	CHECK(second - first >= 3);
	CHECK(second - first < 5000);
}

TEST(clock_coarse_milliseconds_goes_forwards)
{
	const unsigned first = Clock_Milliseconds_Coarse();
	spin_at_least(40);
	const unsigned second = Clock_Milliseconds_Coarse();
	CHECK(second - first >= 1);
	CHECK(second - first < 5000);
}

TEST(clock_ticks_go_forwards_and_never_back)
{
	long long previous = Clock_Ticks();
	for (int i = 0; i < 2000; ++i)
	{
		const long long now = Clock_Ticks();
		CHECK(now >= previous);
		previous = now;
	}
}

TEST(clock_rate_is_positive_and_plausible)
{
	const long long rate = Clock_Ticks_Per_Second();
	CHECK(rate > 0);
	/* Every real implementation of this lands between a kilohertz and a
	   terahertz.  A rate outside that is a units mistake, which is the mistake
	   that turns a profile into nonsense rather than into an error. */
	CHECK(rate >= 1000LL);
	CHECK(rate <= 1000000000000LL);
}

TEST(clock_ticks_and_rate_describe_the_same_clock)
{
	/* The whole contract of the pair: (later - earlier) / rate is seconds.
	   Measured against the millisecond clock, which is independent of it. */
	const long long rate = Clock_Ticks_Per_Second();
	CHECK(rate > 0);
	if (rate <= 0)
		return;

	const long long tick_start = Clock_Ticks();
	spin_at_least(50);
	const long long tick_end = Clock_Ticks();

	const double seconds = (double)(tick_end - tick_start) / (double)rate;
	/* Wide bounds on purpose - this is a busy loop on a shared machine, and
	   the assertion is about units, not about scheduling. */
	CHECK(seconds >= 0.04);
	CHECK(seconds <= 2.0);
}

TEST(clock_fine_resolution_is_paired_and_answers)
{
	/* On macOS both are no-ops that answer true; on Windows they are
	   timeBeginPeriod/timeEndPeriod, which two callers assert on. */
	CHECK(Clock_Begin_Fine_Resolution());
	CHECK(Clock_End_Fine_Resolution());
}
