/*
 * The rank ladder's arithmetic (RankLadder in GlobalData.h, docs/plans/RANK-LADDER.md in the
 * wrapper): the ratio each side's length and top give, the thresholds, the EA level on the pins, the
 * one-grade clip and the stat split.
 */
/* crc.h pulls winsock2.h, so it has to come before anything that drags in windows.h */
#include "Common/crc.h"

#include "test_harness.h"

#include <math.h>

#include "Common/GlobalData.h"

/* BalanceReforged.ini's Veterancy block, one side at a time */
static RankLadder makeLadder( Int grades, Int veteran, Int elite, Int heroic, Int heroStart,
															Real rateOfFire, Real damage, Real health )
{
	RankLadder ladder;
	ladder.clear();
	ladder.m_grades = grades;
	ladder.m_top = 120.0f;
	ladder.m_pin[LEVEL_VETERAN] = veteran;
	ladder.m_pin[LEVEL_ELITE] = elite;
	ladder.m_pin[LEVEL_HEROIC] = heroic;
	ladder.m_heroStart = heroStart;
	ladder.m_statTop[RANK_STAT_RATE_OF_FIRE] = rateOfFire;
	ladder.m_statTop[RANK_STAT_DAMAGE] = damage;
	ladder.m_statTop[RANK_STAT_HEALTH] = health;
	ladder.finish( 1.4f );
	return ladder;
}

static RankLadder america() { return makeLadder( 28, 4, 13, 22, 23, 1.42f, 1.19f, 1.42f ); }
static RankLadder china() { return makeLadder( 19, 4, 10, 15, 15, 1.30f, 1.19f, 1.55f ); }
static RankLadder gla() { return makeLadder( 20, 4, 10, 15, 15, 1.48f, 1.14f, 1.42f ); }

TEST(rank_ladder_ratio_puts_every_top_at_120_kills)
{
	/* grade 0 is unranked on every side, so China's Private and GLA's Recruit are grade 1 */
	CHECK_NEAR( america().m_ratio, 1.0935f, 0.0005f );
	CHECK_NEAR( china().m_ratio, 1.1775f, 0.0005f );
	CHECK_NEAR( gla().m_ratio, 1.1633f, 0.0005f );

	/* a Crusader, E 100: 12,000 to General of the Army, one kill to E-2 */
	const RankLadder usa = america();
	CHECK_NEAR( usa.threshold( 100, 28 ), 12000, 1 );
	CHECK_EQ( usa.threshold( 100, 1 ), 100 );
	CHECK_EQ( usa.threshold( 100, 0 ), 0 );
	/* the doc's table: Corporal 4.6 E, Warrant Officer 23.5 E, Lieutenant Colonel 65.7 E */
	CHECK_NEAR( usa.threshold( 100, 4 ), 460, 1 );
	CHECK_NEAR( usa.threshold( 100, 13 ), 2350, 3 );
	CHECK_NEAR( usa.threshold( 100, 22 ), 6570, 5 );
	/* the Heroic pins: China's Colonel 59.7 E, GLA's Warlord 53.1 E */
	CHECK_NEAR( china().threshold( 100, 15 ), 5972, 5 );
	CHECK_NEAR( gla().threshold( 100, 15 ), 5308, 5 );
	CHECK_EQ( china().threshold( 100, 1 ), 100 );
	CHECK_EQ( gla().threshold( 100, 1 ), 100 );
}

TEST(rank_ladder_small_values_round_once_and_never_fall_back)
{
	/* the Red Guard's E of 5: each threshold is rounded from the sum, never step by step */
	const RankLadder ladder = china();
	for( Int grade = 1; grade <= ladder.m_grades; ++grade )
		CHECK( ladder.threshold( 5, grade ) >= ladder.threshold( 5, grade - 1 ) );
	CHECK_NEAR( ladder.threshold( 5, 19 ), 600, 1 );
}

TEST(rank_ladder_grade_and_level_follow_experience_and_pins)
{
	const RankLadder usa = america();
	const Int corporal = usa.threshold( 100, 4 );
	CHECK_EQ( usa.gradeOf( 100, corporal - 1 ), 3 );
	CHECK_EQ( usa.gradeOf( 100, corporal ), 4 );
	CHECK_EQ( usa.gradeOf( 100, 0 ), 0 );
	CHECK_EQ( usa.gradeOf( 100, 1000000 ), 28 );

	CHECK_EQ( (Int)usa.levelOf( 3 ), (Int)LEVEL_REGULAR );
	CHECK_EQ( (Int)usa.levelOf( 4 ), (Int)LEVEL_VETERAN );
	CHECK_EQ( (Int)usa.levelOf( 12 ), (Int)LEVEL_VETERAN );
	CHECK_EQ( (Int)usa.levelOf( 13 ), (Int)LEVEL_ELITE );
	CHECK_EQ( (Int)usa.levelOf( 21 ), (Int)LEVEL_ELITE );
	CHECK_EQ( (Int)usa.levelOf( 22 ), (Int)LEVEL_HEROIC );
	CHECK_EQ( (Int)usa.levelOf( 28 ), (Int)LEVEL_HEROIC );
	CHECK_EQ( (Int)china().levelOf( 0 ), (Int)LEVEL_REGULAR );
	CHECK_EQ( (Int)china().levelOf( 3 ), (Int)LEVEL_REGULAR );
	CHECK_EQ( (Int)china().levelOf( 4 ), (Int)LEVEL_VETERAN );
	CHECK_EQ( (Int)china().levelOf( 14 ), (Int)LEVEL_ELITE );
	CHECK_EQ( (Int)china().levelOf( 15 ), (Int)LEVEL_HEROIC );
	CHECK_EQ( (Int)gla().levelOf( 9 ), (Int)LEVEL_VETERAN );
	CHECK_EQ( (Int)gla().levelOf( 10 ), (Int)LEVEL_ELITE );
}

TEST(rank_ladder_one_gain_lifts_one_grade_at_most)
{
	const RankLadder usa = america();

	/* a Ranger (E 20) at grade 0 kills a Crusader worth 100: he stops one point short of grade 2 */
	const Int ranger = usa.clipGain( 20, 0, 100 );
	CHECK_EQ( ranger, usa.threshold( 20, 2 ) - 1 );
	CHECK_EQ( usa.gradeOf( 20, ranger ), 1 );

	/* a gain that fits is untouched */
	CHECK_EQ( usa.clipGain( 100, 0, 150 ), 150 );

	/* halfway through a grade the cap is still the second threshold above it */
	const Int mid = usa.threshold( 100, 10 ) + 50;
	CHECK_EQ( usa.clipGain( 100, mid, 100000 ), usa.threshold( 100, 12 ) - 1 - mid );
	CHECK_EQ( usa.gradeOf( 100, mid + usa.clipGain( 100, mid, 100000 ) ), 11 );

	/* from the next-to-last grade on nothing is clipped, and experience keeps growing past the top */
	CHECK_EQ( usa.clipGain( 100, usa.threshold( 100, 27 ), 100000 ), 100000 );
	CHECK_EQ( usa.clipGain( 100, usa.threshold( 100, 28 ) + 5, 777 ), 777 );
}

TEST(rank_ladder_stats_split_one_worth_curve)
{
	const RankLadder ladders[] = { america(), china(), gla() };
	for( Int side = 0; side < 3; ++side )
	{
		const RankLadder &ladder = ladders[ side ];
		const Real *bottom = ladder.m_stat[ 0 ];
		CHECK_NEAR( bottom[RANK_STAT_RATE_OF_FIRE] * bottom[RANK_STAT_DAMAGE] * bottom[RANK_STAT_HEALTH], 1.0f, 0.0001f );
		const Real *top = ladder.m_stat[ ladder.m_grades ];
		CHECK_NEAR( top[RANK_STAT_RATE_OF_FIRE] * top[RANK_STAT_DAMAGE] * top[RANK_STAT_HEALTH], 2.40f, 0.01f );
		/* worth = 1 + 1.4 sqrt(total / 120) at every grade */
		for( Int grade = 1; grade <= ladder.m_grades; ++grade )
		{
			const Real *stat = ladder.m_stat[ grade ];
			const Real worth = stat[RANK_STAT_RATE_OF_FIRE] * stat[RANK_STAT_DAMAGE] * stat[RANK_STAT_HEALTH];
			CHECK_NEAR( worth, 1.0f + 1.4f * sqrtf( ladder.m_total[ grade ] / 120.0f ), 0.01f );
			CHECK( stat[RANK_STAT_HEALTH] >= ladder.m_stat[ grade - 1 ][RANK_STAT_HEALTH] );
		}
	}
	/* the hand-rolled ln and e agree with the CRT's: each stat is its top value to ln(worth) / ln(2.4) */
	const RankLadder &usa = ladders[ 0 ];
	for( Int grade = 0; grade <= usa.m_grades; ++grade )
	{
		const double share = log( 1.0 + 1.4 * sqrt( ( usa.m_total[ grade ] < 120.0f ? usa.m_total[ grade ] : 120.0f ) / 120.0 ) ) / log( 2.4 );
		CHECK_NEAR( usa.m_stat[ grade ][RANK_STAT_HEALTH], pow( 1.42, share ), 0.00001 );
		CHECK_NEAR( usa.m_stat[ grade ][RANK_STAT_RANGE], 1.0, 0.00001 );
	}
	CHECK_NEAR( usa.m_stat[ usa.m_grades ][RANK_STAT_RATE_OF_FIRE], 1.42f, 0.0005f );

	/* the first kill is worth 1.13 everywhere */
	const Real *first = ladders[ 0 ].m_stat[ 1 ];
	CHECK_NEAR( first[RANK_STAT_RATE_OF_FIRE] * first[RANK_STAT_DAMAGE] * first[RANK_STAT_HEALTH], 1.13f, 0.005f );
}

TEST(rank_ladder_hero_insignia_runs_from_its_start_to_the_top)
{
	const RankLadder usa = america();
	CHECK_EQ( usa.insigniaIndex( 0, TRUE ), 23 );
	CHECK_EQ( usa.insigniaIndex( 28, TRUE ), 28 );
	CHECK_EQ( usa.insigniaIndex( 14, TRUE ), 25 );
	CHECK_EQ( usa.insigniaIndex( 14, FALSE ), 14 );
	CHECK_EQ( gla().insigniaIndex( 20, TRUE ), 20 );
	CHECK_EQ( china().insigniaIndex( 0, TRUE ), 15 );
	CHECK_EQ( china().insigniaIndex( 19, TRUE ), 19 );
}

TEST(rank_ladder_perks_carry_up_and_the_strongest_of_a_kind_holds)
{
	/* Turkiye's Perk_ lines: sight 110%, 120%, 130% at 3, 8, 18; the rate-of-fire aura 5% at 10, 10% at 16 */
	RankLadder ladder;
	ladder.clear();
	ladder.m_grades = 21;
	ladder.m_top = 120.0f;
	ladder.m_pin[LEVEL_VETERAN] = 4;
	ladder.m_pin[LEVEL_ELITE] = 11;
	ladder.m_pin[LEVEL_HEROIC] = 16;
	ladder.m_auraRadius = 150.0f;
	ladder.m_perkAt[ 3 ][ RANK_PERK_SIGHT ] = 1.10f;
	ladder.m_perkAt[ 8 ][ RANK_PERK_SIGHT ] = 1.20f;
	ladder.m_perkAt[ 18 ][ RANK_PERK_SIGHT ] = 1.30f;
	ladder.m_perkAt[ 10 ][ RANK_PERK_AURA_RATE_OF_FIRE ] = 0.05f;
	ladder.m_perkAt[ 16 ][ RANK_PERK_AURA_RATE_OF_FIRE ] = 0.10f;
	ladder.m_perkAt[ 17 ][ RANK_PERK_DEATH_RALLY ] = 0.15f;
	ladder.m_perkAt[ 21 ][ RANK_PERK_AURA_RADIUS ] = 2.0f;
	ladder.finish( 1.4f );

	CHECK_EQ( ladder.perk( 2, RANK_PERK_SIGHT ), 0.0f );
	CHECK_NEAR( ladder.perk( 3, RANK_PERK_SIGHT ), 1.10f, 1e-6f );
	CHECK_NEAR( ladder.perk( 7, RANK_PERK_SIGHT ), 1.10f, 1e-6f );
	CHECK_NEAR( ladder.perk( 8, RANK_PERK_SIGHT ), 1.20f, 1e-6f );
	CHECK_NEAR( ladder.perk( 21, RANK_PERK_SIGHT ), 1.30f, 1e-6f );

	/* the Albay's 10% replaces the Bascavus's 5%, it does not add to it */
	CHECK_EQ( ladder.aura( 9, RANK_AURA_RATE_OF_FIRE ), 0.0f );
	CHECK_NEAR( ladder.aura( 10, RANK_AURA_RATE_OF_FIRE ), 0.05f, 1e-6f );
	CHECK_NEAR( ladder.aura( 16, RANK_AURA_RATE_OF_FIRE ), 0.10f, 1e-6f );
	CHECK_NEAR( ladder.aura( 17, RANK_AURA_RALLY ), 0.15f, 1e-6f );
	CHECK_EQ( ladder.aura( 16, RANK_AURA_RALLY ), 0.0f );

	/* the Maresal's double reach, and a grade past the top reads the top */
	CHECK_NEAR( ladder.auraRadius( 20 ), 150.0f, 1e-3f );
	CHECK_NEAR( ladder.auraRadius( 21 ), 300.0f, 1e-3f );
	CHECK_NEAR( ladder.auraRadius( 40 ), 300.0f, 1e-3f );
}
