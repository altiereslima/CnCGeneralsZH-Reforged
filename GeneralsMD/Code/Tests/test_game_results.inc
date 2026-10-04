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
// The game-results queue's one worker (GameResultsThread.cpp) sleeps until addRequest wakes it, where it
// used to wake every millisecond to look.  A request it is never woken for would sit in the queue until the
// next one, so this checks that one is served, and that ending the queue wakes the worker and joins it.  The
// request's host is a literal address, so nothing is looked up; off Windows the send itself is a stub, and
// on Windows it is refused by the local machine.
//
// Both are checked by what happened, not by how long it took.  Windows takes about two seconds to refuse a
// connection to a closed port, even on 127.0.0.1 (measured: 2.0-2.1 s on the Windows CI build's machine), so
// endThreads can find the worker still inside connect(), which no wake can shorten; a one-second bound on
// endThreads failed there for that reason alone.  A worker never woken, on the other hand, sleeps for good,
// and the join in endThreads never returns: that fails as a hang, under the test's timeout.

#include "GameNetwork/GameSpy/GameResultsThread.h"

#include <chrono>
#include <thread>

TEST(game_results_worker_serves_a_request_and_leaves_when_woken)
{
	CHECK( bootOnce() );
	GameResultsInterface *queue = GameResultsInterface::createNewGameResultsInterface();
	CHECK( queue != NULL );
	if (queue == NULL)
		return;

	// Give the worker time to be asleep, so the request has to wake it.
	std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
	CHECK( queue->areThreadsRunning() );

	GameResultsRequest request;
	request.hostname = "127.0.0.1";
	request.port = 9;
	request.results = "results";
	queue->addRequest( request );
	std::this_thread::sleep_for( std::chrono::seconds( 1 ) );

	const std::chrono::steady_clock::time_point before = std::chrono::steady_clock::now();
	queue->endThreads();
	const long long endMs = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - before ).count();
	// Returned, so the worker was woken and joined; the time is for the record (the refusal, on Windows).
	printf( "game_results: endThreads returned after %lld ms\n", endMs );
	CHECK( !queue->areThreadsRunning() );

	// No worker now, so the queue holds still: the request is gone only if the worker took it.
	GameResultsRequest left;
	CHECK( !queue->getRequest( left ) );

	delete queue;
}
