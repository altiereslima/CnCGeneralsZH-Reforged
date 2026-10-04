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

#define _WIN32_WINNT 0x0400

#include "thread.h"
#include "Except.h"
#include "wwdebug.h"

#ifdef _WIN32
// Still needed here, and only here: the SEH wrapper around Thread_Function, the crash handler's
// thread registry, the real thread id the registry matches on, and SetThreadPriority.  None of
// them is a synchronisation primitive and none of them exists off Windows.
#include <windows.h>
#endif

#include <chrono>
#include <thread>

#pragma warning ( push )
#pragma warning ( disable : 4201 )
#include "systimer.h"
#pragma warning ( pop )


ThreadClass::ThreadClass(const char *thread_name, ExceptionHandlerType exception_handler)
	: running(false), ThreadID(0), ExceptionHandler(NULL), ThreadIsRunning(false), thread_priority(0)
{
	if (thread_name) {
		assert(strlen(thread_name) < sizeof(ThreadName) - 1);
		strcpy(ThreadName, thread_name);
	} else {
		strcpy(ThreadName, "No name");;
	}

	ExceptionHandler = exception_handler;
}

ThreadClass::~ThreadClass()
{
	Stop();

	/* Stop() gives up after its timeout and leaves the thread running - see the note there.  If
	   that happened we are now about to destroy the object the thread is still using, which is
	   the one thing that must not happen, so this blocks until it is gone.  Windows used to
	   TerminateThread instead, which returns immediately and leaves every lock the thread held
	   locked forever; blocking here is worse to watch and better to live with. */
	if (Thread.joinable()) {
		Thread.join();
	}
}

void ThreadClass::Internal_Thread_Function()
{
	running=true;
	ThreadID = _Get_Current_Thread_ID();

#ifdef _WIN32
	Register_Thread_ID(ThreadID, ThreadName);

	if (ExceptionHandler != NULL) {
		__try {
			Thread_Function();
		} __except(ExceptionHandler(GetExceptionCode(), GetExceptionInformation())) {};
	} else {
		Thread_Function();
	}

	Unregister_Thread_ID(ThreadID, ThreadName);
#else //_WIN32
	// There is no structured exception handling off Windows.  ExceptionHandler is a
	// _EXCEPTION_POINTERS callback and nothing portable can raise one, so a thread that sets one
	// simply runs without it; the handlers in this tree write a crash dump, which C5 owns.
	Thread_Function();
#endif //_WIN32

	ThreadID = 0;
	ThreadIsRunning.store(false, std::memory_order_release);
}

void ThreadClass::Execute()
{
	WWASSERT(!Thread.joinable());	// Only one thread at a time!

	/* Set before the thread starts, not inside it.  _beginthread's return value landed in `handle`
	   after the new thread was already running, so a thread that finished immediately zeroed the
	   handle and then had it overwritten with a stale value - Is_Running() answered true forever.
	   Racy then, not racy now. */
	ThreadIsRunning.store(true, std::memory_order_release);

	Thread = std::thread(&ThreadClass::Internal_Thread_Function, this);
	Set_Priority(thread_priority);

	WWDEBUG_SAY(("ThreadClass::Execute: Started thread %s\n", ThreadName));
}

void ThreadClass::Set_Priority(int priority)
{
	thread_priority=priority;

#ifdef _WIN32
	if (Thread.joinable()) {
		SetThreadPriority((HANDLE)Thread.native_handle(), THREAD_PRIORITY_NORMAL+thread_priority);
	}
#else
	/* WHAT IS LOST: thread priority.  The standard library has no equivalent, and the POSIX one -
	   pthread_setschedparam - needs a scheduling policy the process is usually not allowed to ask
	   for, so a naive port silently fails rather than setting anything.  macOS would want
	   pthread_set_qos_class_self_np, which is a different model and belongs with whoever tunes
	   the frame.  The value is stored either way, so nothing loses the caller's intent; two
	   callers in this tree set one, both to raise a loader thread. */
#endif
}

void ThreadClass::Stop(unsigned ms)
{
	running=false;

	if (!Thread.joinable()) {
		return;
	}

	unsigned time=TIMEGETTIME();
	while (ThreadIsRunning.load(std::memory_order_acquire)) {
		if ((TIMEGETTIME()-time)>ms) {
			/* WHAT IS LOST: TerminateThread.  Windows killed the thread here and cleared the
			   handle, and there is no portable equivalent - nor a safe one, because a killed
			   thread never releases what it holds.  The test in test_wwlib.cpp that pins this
			   path (threadclass_stop_deadlocks_if_the_caller_holds_the_workers_lock) asks only
			   that Stop runs out the whole timeout and then returns, which it still does.

			   The thread is left running.  ~ThreadClass joins it, so the object cannot be
			   destroyed underneath it; what changes is that a thread which never returns now
			   hangs the shutdown instead of corrupting it. */
			WWDEBUG_SAY(("ThreadClass::Stop: thread %s did not exit within %u ms\n", ThreadName, ms));
			return;
		}
		Sleep_Ms(0);
	}

	Thread.join();
}

void ThreadClass::Sleep_Ms(unsigned ms)
{
	/* Sleep(0) on Windows means "give up the rest of my slice to another ready thread", which is
	   yield(); sleep_for(0) is allowed to return immediately without doing that, and Stop()'s
	   wait loop is a spin that depends on the difference. */
	if (ms==0) {
		std::this_thread::yield();
	}
	else {
		std::this_thread::sleep_for(std::chrono::milliseconds(ms));
	}
}

void ThreadClass::Switch_Thread()
{
	/* This was ::WaitForSingleObject on a process-global event that nothing ever signalled: a
	   one millisecond sleep with an event handle attached to it, and the event existed only
	   because SwitchToThread and Sleep(1) had both been tried and commented out above it.  The
	   surviving comment - "Parameter can not be 0 (or the thread switch doesn't occur)" - is
	   about Sleep, not about the event.

	   One millisecond is a long time to back off a spinlock, and this is the backoff
	   FastCriticalSectionClass uses.  It is kept exactly as it was: changing how long the
	   engine's hottest lock waits is a performance change, and this task is a port. */
	std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

// Return calling thread's unique thread id
unsigned ThreadClass::_Get_Current_Thread_ID()
{
#ifdef _WIN32
	/* Unchanged on Windows on purpose.  Register_Thread_ID hands this value to the crash handler's
	   thread registry, and a dump is only readable if the number in it is the one the debugger
	   shows. */
	return GetCurrentThreadId();
#else
	/* Everywhere else: a number handed out once per thread, first come first served.  That is
	   enough because every use of this in the tree is an equality test against one stored
	   earlier - DX8_THREAD_ASSERT (dx8wrapper.h:155), TextureLoader's three main-thread asserts
	   and its Is_Main_Thread, wwmemlog's per-thread category stack.  Nothing reads it as an OS
	   handle and nothing prints it expecting to find it elsewhere.

	   Never zero: _UNIX used to return 0 from here for every thread, which made every one of
	   those equality tests true and every main-thread assert pass from any thread. */
	static std::atomic<unsigned> next_id(1);
	static thread_local unsigned id = next_id.fetch_add(1, std::memory_order_relaxed);
	return id;
#endif
}

bool ThreadClass::Is_Running()
{
	return ThreadIsRunning.load(std::memory_order_acquire);
}