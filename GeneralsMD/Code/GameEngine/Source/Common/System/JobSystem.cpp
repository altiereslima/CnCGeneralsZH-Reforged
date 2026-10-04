/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
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
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

// FILE: JobSystem.cpp ////////////////////////////////////////////////////////////////////////////
// Desc:   THREADING-ROADMAP.md 3.1 - a fork-join pool, and nothing else.
////////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"
#include "Common/JobSystem.h"
#include "Common/EarlyCommandLine.h"
#include "GameLogic/FPUControl.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <system_error>	// std::system_error, caught below: libc++ brings it in with <thread>, libstdc++ does not
#include <thread>

/*
	This pool was CreateThread, a Win32 semaphore, an auto-reset event and Interlocked* over
	`volatile LONG`.  It is the standard library now (B8), because macOS has none of those and
	because the semantics below are the whole of what the pool needs - there was never anything
	Windows-specific about a fork-join pool.

	The two wakeups are the part worth reading before changing anything.  They are not the same
	primitive, and neither of them is a bare condition_variable notify - which is how a pool like
	this usually gets broken:

	  s_workReady was CreateSemaphore(0, MAX_WORKERS), released N-at-a-time per fork.  What makes
	  it a *counting* semaphore rather than an event is that its permits are **stored**.  A worker
	  that finished the previous fork is not necessarily back at the wait yet - it is somewhere
	  between decrementing s_workersBusy and looping round - and the next fork can start before it
	  gets there.  A permit released into that gap has to still be there when the worker arrives.
	  A plain notify_all has no memory: it wakes whoever is waiting at that instant and is lost on
	  everyone else, so that worker would sleep through a fork it had been counted into,
	  s_workersBusy would never reach zero, and parallel_for would hang forever.

	  So: an integer permit count under the mutex, with the wait keyed on a predicate over it.
	  notify_all then only ever wakes threads up to re-test that predicate; the permit is the
	  state, and the state is what survives the gap.  (C++20's std::counting_semaphore is exactly
	  this and would be the one-liner, but the tree builds as C++17 - see the gameengine target's
	  CXX_STANDARD.)

	  s_allDone was CreateEvent(auto-reset), set by whichever worker decremented s_workersBusy to
	  zero, and waited on by the forking thread alone.  An auto-reset event also stores its signal:
	  if the last worker finishes before the forking thread reaches the wait, the wait returns at
	  once instead of blocking forever.  A predicate wait has that property for free, because the
	  predicate *is* the condition - s_workersBusy == 0 is already true, so the forking thread does
	  not block.  One waiter, so notify_one.

	Ordering, stated once so nobody has to derive it again:

	  - s_fn, s_context, s_count, s_grain and the reset of s_nextIndex are written by the forking
	    thread under s_lock, before the permits are published.  A worker reads them after it has
	    taken s_lock and claimed a permit.  That unlock/lock pair is what publishes them, which is
	    the role ReleaseSemaphore/WaitForSingleObject played before.
	  - s_workersBusy is a plain Int under s_lock although it was an InterlockedDecrement.  It is a
	    condition_variable predicate now, and a predicate has to be read and written under the
	    mutex the waiter holds.  Making it an atomic as well would not remove that requirement; it
	    would only make the code read as though the lock were optional.
	  - s_nextIndex is the one genuinely lock-free thing here, and the only ordering that came out
	    *weaker* than the Win32 original - see the comment inside runChunks.
*/

namespace
{

	enum { MAX_WORKERS = 31 };		// nothing here scales past it, and the pool is not the bottleneck

	/** One lock, three conditions, each with a named predicate.  Nothing runs a job while holding
		* it: it is taken to hand out a permit, to report a worker done, and to report one exited. */
	std::mutex								s_lock;
	std::condition_variable		s_workReadyCv;			// predicate: s_permits > 0 || s_quitting
	std::condition_variable		s_allDoneCv;				// predicate: s_workersBusy == 0
	std::condition_variable		s_workerExitedCv;		// predicate: s_workersExited >= s_numWorkers

	Int		s_permits = 0;					// stored wakeups; see above for why they have to be counted
	Int		s_workersBusy = 0;			// workers still inside this fork
	Int		s_workersExited = 0;		// workers that have left workerMain for good
	Bool	s_quitting = FALSE;

	std::thread		s_threads[ MAX_WORKERS ];
	Int						s_numWorkers = 0;
	Bool					s_running = FALSE;

	// The job in flight.  Written by the forking thread before the permits are released and read
	// by the workers after they take one, which is the pairing that publishes them.
	JobSystem::JobFunc	s_fn = NULL;
	void								*s_context = NULL;
	std::atomic<Int>		s_nextIndex( 0 );
	Int									s_count = 0;
	Int									s_grain = 1;

	// Per-thread, so the allocator can tell whose hand is in the pool without a lock of its own.
	thread_local Bool		s_isWorker = FALSE;
	std::atomic<Int>		s_workerAllocations( 0 );

	/** Claim chunks of the current job until there are none left.  Every thread in the fork runs
		* this, the forking one included - a pool of N-1 workers plus the caller is N threads of
		* work, and the caller blocking while the workers run would waste the best one of them. */
	void runChunks( void )
	{
		for( ;; )
		{
			/* relaxed, and this is the one place the file is weaker than the Win32 original, where
				 InterlockedExchangeAdd was a full barrier.  It is sound because the claim carries no
				 data: read-modify-write operations on a single atomic are totally ordered whatever
				 memory order is asked for, so no index is handed out twice and none is skipped, and
				 every other ordering this pool needs comes from somewhere else - the job's inputs from
				 the permit handover, the job's outputs from the join.  It does rely on JobSystem.h's
				 rule 3, that an item writes only its own slot.  A job that breaks that rule was already
				 broken under the barrier; it is only less likely to have got away with it. */
			const Int start = s_nextIndex.fetch_add( s_grain, std::memory_order_relaxed );
			if( start >= s_count )
				break;

			Int end = start + s_grain;
			if( end > s_count )
				end = s_count;

			for( Int i = start; i < end; ++i )
				s_fn( i, s_context );
		}
	}

	void workerMain( void )
	{
		s_isWorker = TRUE;

		/* The FPU control word is per-thread and a new thread starts on the CRT default, not on
			 what setFPMode() left on the main thread.  A job that computes a float with a different
			 precision or rounding mode than the rest of the game is the kind of bug that shows up as
			 a rare visual difference and never as a crash, so this is not optional even for work
			 that never touches the simulation. */
		setFPMode();

		for( ;; )
		{
			{
				std::unique_lock<std::mutex> lock( s_lock );
				s_workReadyCv.wait( lock, []{ return s_permits > 0 || s_quitting; } );
				if( s_quitting )
					break;
				--s_permits;
			}

			runChunks();

			Bool lastOut = FALSE;
			{
				std::lock_guard<std::mutex> lock( s_lock );
				lastOut = (--s_workersBusy == 0);
			}
			if( lastOut )
				s_allDoneCv.notify_one();			// exactly one waiter, and it is the forking thread
		}

		{
			std::lock_guard<std::mutex> lock( s_lock );
			++s_workersExited;
		}
		s_workerExitedCv.notify_one();
	}

}  // anonymous namespace

//-------------------------------------------------------------------------------------------------
void JobSystem::init( Int workers )
{
	if( s_running )
		return;

	if( workers < 0 )
	{
		/* -jobthreads <n>, read here rather than from CommandLine.cpp's table because the pool is
			 started before that table runs and because 0 has to be reachable - which is the whole
			 point: it is the single-threaded baseline to measure a threading change against. */
		char value[ 32 ];
		if( findEarlyCommandLineValue( L"-jobthreads", value, sizeof(value) ) )
		{
			workers = atoi( value );
		}
		else
		{
			/* The forking thread works too, so the pool wants one fewer than the machine has.
				 hardware_concurrency() is allowed to answer 0 when it cannot tell, which lands on -1
				 and is clamped to no pool below - the supported single-threaded configuration. */
			workers = (Int)std::thread::hardware_concurrency() - 1;
		}
	}
	if( workers > MAX_WORKERS )
		workers = MAX_WORKERS;
	if( workers < 0 )
		workers = 0;

	/* There is no "could not create the sync objects" path any more.  The Win32 version had to
		 cope with CreateSemaphore or CreateEvent failing and fall back to running inline; a mutex
		 and a condition variable with static storage duration cannot fail to exist, so that branch
		 is gone rather than ported. */
	{
		std::lock_guard<std::mutex> lock( s_lock );
		s_quitting = FALSE;
		s_permits = 0;
		s_workersBusy = 0;
		s_workersExited = 0;
	}
	s_numWorkers = 0;

	for( Int i = 0; i < workers; ++i )
	{
		try
		{
			s_threads[ i ] = std::thread( workerMain );
		}
		catch( const std::system_error & )
		{
			break;						// take what we got; the pool is allowed to be smaller than asked for
		}
		++s_numWorkers;
	}

	s_running = TRUE;
	DEBUG_LOG(("JobSystem: %d worker threads\n", s_numWorkers));
}

//-------------------------------------------------------------------------------------------------
void JobSystem::shutdown( void )
{
	if( !s_running )
		return;

	/* There is never work in flight here: parallel_for does not return until its fork has joined,
		 so by the time anything can call this every worker is parked on s_workReadyCv, or on its
		 way back to it with no permit waiting to be taken. */
	{
		std::lock_guard<std::mutex> lock( s_lock );
		s_quitting = TRUE;
	}
	s_workReadyCv.notify_all();

	/* Bounded, not an unconditional join().  This runs on the way out of a process that may be
		 quitting from anywhere, and a worker that somehow never wakes must not be able to hang the
		 exit - the Win32 version waited 5000ms per handle and leaked the handle on a timeout.
		 std::thread has no timed join, so the wait is on the workers' own exit count instead and
		 any that miss it are detached, which is the same trade: a leaked thread rather than a hang.

		 It should be unreachable.  A worker's only blocking point is s_workReadyCv, it is woken
		 above, and there is no work in flight by construction - so reaching this timeout means a
		 bug in this file rather than a slow machine. */
	Bool allOut = TRUE;
	{
		std::unique_lock<std::mutex> lock( s_lock );
		allOut = s_workerExitedCv.wait_for( lock, std::chrono::seconds( 5 ),
																				[]{ return s_workersExited >= s_numWorkers; } );
	}

	for( Int i = 0; i < s_numWorkers; ++i )
	{
		if( !s_threads[ i ].joinable() )
			continue;

		if( allOut )
		{
			s_threads[ i ].join();				// already out of workerMain, so this returns at once
		}
		else
		{
			DEBUG_LOG(("JobSystem: worker %d did not exit, detaching it\n", i));
			s_threads[ i ].detach();
		}
		s_threads[ i ] = std::thread();
	}
	s_numWorkers = 0;

	/* s_lock and the condition variables are not destroyed here, unlike the Win32 handles, which
		 shutdown() used to CloseHandle out from under anything still waiting on them.  A detached
		 worker stays parked on a live object until static destruction now, which narrows that
		 window rather than opening a new one. */
	s_running = FALSE;
}

//-------------------------------------------------------------------------------------------------
Int JobSystem::workerCount( void )
{
	return s_numWorkers;
}

//-------------------------------------------------------------------------------------------------
Bool JobSystem::isWorkerThread( void )
{
	return s_isWorker;
}

//-------------------------------------------------------------------------------------------------
Int JobSystem::workerAllocationCount( void )
{
	return s_workerAllocations.load( std::memory_order_relaxed );
}

//-------------------------------------------------------------------------------------------------
void JobSystem::noteWorkerAllocation( void )
{
	// A diagnostic counter on the allocator's path: it has to be atomic, and it orders nothing.
	s_workerAllocations.fetch_add( 1, std::memory_order_relaxed );
}

//-------------------------------------------------------------------------------------------------
void JobSystem::parallel_for( Int count, Int granularity, JobFunc fn, void *context )
{
	if( count <= 0 || fn == NULL )
		return;

	if( granularity < 1 )
		granularity = 1;

	/* Inline when there is nobody to hand work to, or when the whole range is one chunk anyway.
		 Same code path as the parallel case would take for a single chunk, so the single-threaded
		 machine is running tested code rather than a second implementation. */
	if( s_numWorkers <= 0 || count <= granularity )
	{
		for( Int i = 0; i < count; ++i )
			fn( i, context );
		return;
	}

	DEBUG_ASSERTCRASH( !s_isWorker, ("JobSystem::parallel_for from inside a job - this pool does not nest") );

	{
		std::lock_guard<std::mutex> lock( s_lock );
		s_fn = fn;
		s_context = context;
		s_count = count;
		s_grain = granularity;
		s_nextIndex.store( 0, std::memory_order_relaxed );
		s_workersBusy = s_numWorkers;
		s_permits += s_numWorkers;			// one per worker, and they keep until they are taken
	}
	s_workReadyCv.notify_all();

	runChunks();		// the forking thread is a worker too

	/* Wait for the workers, not for the work: the chunks can all be claimed by this thread while a
		 worker is still on its way out of the wait, and returning then would let the caller reuse
		 the output buffers under it. */
	{
		std::unique_lock<std::mutex> lock( s_lock );
		s_allDoneCv.wait( lock, []{ return s_workersBusy == 0; } );
		s_fn = NULL;
		s_context = NULL;
	}
}
