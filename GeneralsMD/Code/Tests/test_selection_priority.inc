#include "GameClient/SelectionPriority.h"
#include "GameNetwork/GameMessageParser.h"

namespace
{
struct SelectionPriorityUnit
{
	KindOfMaskType kinds;
	Bool eligible;
	SelectionPriorityUnit(KindOfType first, KindOfType second, Bool canSelect = TRUE)
		: kinds(KindOfMaskType::kInit, first, second), eligible(canSelect) {}
	Bool isKindOf(KindOfType kind) const { return kinds.test(kind); }
};

Bool selectionPriorityMilitary(const SelectionPriorityUnit *unit)
{
	return unit && unit->eligible && isMilitaryBoxSelectionUnit(unit);
}
}

TEST(selection_priority_mixed_box_selects_all_military_in_original_order)
{
	SelectionPriorityUnit dozer(KINDOF_VEHICLE, KINDOF_DOZER);
	SelectionPriorityUnit ranger(KINDOF_INFANTRY, KINDOF_CAN_ATTACK);
	SelectionPriorityUnit truck(KINDOF_VEHICLE, KINDOF_HARVESTER);
	SelectionPriorityUnit tank(KINDOF_VEHICLE, KINDOF_CAN_ATTACK);
	SelectionPriorityUnit ambulance(KINDOF_VEHICLE, KINDOF_TRANSPORT);
	std::list<SelectionPriorityUnit *> selection;
	selection.push_back(&dozer);
	selection.push_back(&ranger);
	selection.push_back(&truck);
	selection.push_back(&tank);
	selection.push_back(&ambulance);
	prioritizeMilitaryBoxSelection(selection, selectionPriorityMilitary);
	CHECK(selection.size() == 3);
	CHECK(selection.front() == &ranger);
	selection.pop_front();
	CHECK(selection.front() == &tank);
	CHECK(selection.back() == &ambulance);
}

TEST(selection_priority_economy_only_box_preserves_every_candidate)
{
	SelectionPriorityUnit dozer(KINDOF_VEHICLE, KINDOF_DOZER);
	SelectionPriorityUnit truck(KINDOF_VEHICLE, KINDOF_HARVESTER);
	SelectionPriorityUnit worker(KINDOF_INFANTRY, KINDOF_DOZER);
	SelectionPriorityUnit chinook(KINDOF_AIRCRAFT, KINDOF_HARVESTER);
	SelectionPriorityUnit hacker(KINDOF_INFANTRY, KINDOF_MONEY_HACKER);
	hacker.kinds.set(KINDOF_CAN_ATTACK);
	std::list<SelectionPriorityUnit *> selection;
	selection.push_back(&dozer);
	selection.push_back(&truck);
	selection.push_back(&worker);
	selection.push_back(&chinook);
	selection.push_back(&hacker);
	const std::list<SelectionPriorityUnit *> before = selection;
	prioritizeMilitaryBoxSelection(selection, selectionPriorityMilitary);
	CHECK(selection == before);
}

TEST(selection_priority_ineligible_military_does_not_suppress_workers)
{
	SelectionPriorityUnit worker(KINDOF_INFANTRY, KINDOF_DOZER);
	// Foreign, dead, hidden or contained military fail the caller's eligibility test.
	SelectionPriorityUnit unavailableTank(KINDOF_VEHICLE, KINDOF_CAN_ATTACK, FALSE);
	SelectionPriorityUnit turret(KINDOF_STRUCTURE, KINDOF_CAN_ATTACK);
	std::list<SelectionPriorityUnit *> selection;
	selection.push_back(&worker);
	selection.push_back(&unavailableTank);
	selection.push_back(&turret);
	const std::list<SelectionPriorityUnit *> before = selection;
	prioritizeMilitaryBoxSelection(selection, selectionPriorityMilitary);
	CHECK(selection == before);
	SelectionPriorityUnit ranger(KINDOF_INFANTRY, KINDOF_CAN_ATTACK);
	selection.push_back(&ranger);
	prioritizeMilitaryBoxSelection(selection, selectionPriorityMilitary);
	CHECK(selection.size() == 1);
	CHECK(selection.front() == &ranger);
}

TEST(selection_priority_empty_box_and_military_support)
{
	std::list<SelectionPriorityUnit *> empty;
	prioritizeMilitaryBoxSelection(empty, selectionPriorityMilitary);
	CHECK(empty.empty());
	SelectionPriorityUnit lotus(KINDOF_INFANTRY, KINDOF_CAN_ATTACK);
	SelectionPriorityUnit aircraft(KINDOF_AIRCRAFT, KINDOF_VEHICLE);
	SelectionPriorityUnit cashUnit(KINDOF_INFANTRY, KINDOF_CASH_GENERATOR);
	CHECK(isMilitaryBoxSelectionUnit(&lotus));
	CHECK(isMilitaryBoxSelectionUnit(&aircraft));
	CHECK(!isMilitaryBoxSelectionUnit(&cashUnit));
	CHECK(!isMilitaryBoxSelectionUnit<SelectionPriorityUnit>(NULL));
}

TEST(selection_of_more_than_254_units_survives_the_one_byte_run_counts)
{
	CHECK(bootOnce());
	// a GameMessage stamps itself with the local player's index, and a Player resets its rank and
	// score from the global data and the rank store when it is built
	GlobalData *savedGlobal = TheWritableGlobalData;
	RankInfoStore *savedRanks = TheRankInfoStore;
	PlayerList *savedPlayers = ThePlayerList;
	GlobalData *global = NEW GlobalData;
	TheWritableGlobalData = global;
	static RankInfoStore *ranks = NEW RankInfoStore;
	TheRankInfoStore = ranks;
	static PlayerList *players = NEW PlayerList;
	ThePlayerList = players;
	// 255, 511, 767 and 1023 units used to arrive as none, 1000 as 232: the count was a byte, and so
	// is each run's count in the packet and the replay.
	const Int sizes[] = { 254, 255, 256, 511, 1000, 1023 };
	for (Int s = 0; s < 6; ++s)
	{
		GameMessage *msg = newInstance(GameMessage)(GameMessage::MSG_CREATE_SELECTED_GROUP);
		msg->appendBooleanArgument(TRUE);
		for (Int i = 0; i < sizes[s]; ++i)
			msg->appendObjectIDArgument((ObjectID)(i + 1));
		CHECK_EQ(msg->getArgumentCount(), sizes[s] + 1);

		GameMessageParser *parser = newInstance(GameMessageParser)(msg);
		CHECK(parser->getNumTypes() <= 255);
		Int read = 0;
		for (GameMessageParserArgumentType *run = parser->getFirstArgumentType(); run; run = run->getNext())
		{
			// what a reader gets back from the byte the writer stored
			const UnsignedByte count = (UnsignedByte)run->getArgCount();
			CHECK_EQ((Int)count, run->getArgCount());
			CHECK_EQ(run->getType(), read == 0 ? ARGUMENTDATATYPE_BOOLEAN : ARGUMENTDATATYPE_OBJECTID);
			read += count;
		}
		CHECK_EQ(read, sizes[s] + 1);
		parser->deleteInstance();
		msg->deleteInstance();
	}
	ThePlayerList = savedPlayers;
	TheRankInfoStore = savedRanks;
	delete global;					// while it is the current one: a Debug build's ~GlobalData reads TheWritableGlobalData
	TheWritableGlobalData = savedGlobal;
}
