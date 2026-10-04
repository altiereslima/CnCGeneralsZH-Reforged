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
// threadwake.h: a worker thread asleep until it has work, or until it is told to leave.
//
// WW3D's texture loader thread looked at its queue and slept a millisecond (ThreadClass::Switch_Thread),
// for the whole session: about a thousand wakeups a second for a queue the shipped game never fills
// (textureloader.cpp).  A worker waits here instead, and whoever gives it work wakes it.  Header-only so
// every target that already builds WWLib's threads gets it without a new source in any list.

#ifndef THREADWAKE_H
#define THREADWAKE_H

#include <condition_variable>
#include <mutex>

class ThreadWakeClass
{
public:
	ThreadWakeClass() : Stopping(false) {}

	/// The worker: sleeps until has_work() is true or Stop() is called; true to go on, false to leave.
	/// has_work() is asked under the lock that Wake() takes, so work made visible before a Wake() is either
	/// seen here or wakes the wait - never lost between the two.
	template <typename HasWork> bool Wait(HasWork has_work)
	{
		std::unique_lock<std::mutex> lock(Lock);
		while (!Stopping && !has_work()) {
			Condition.wait(lock);
		}
		return !Stopping;
	}

	/// A producer, after its work is visible to has_work(): wakes the worker.
	void Wake()
	{
		std::lock_guard<std::mutex> lock(Lock);
		Condition.notify_one();
	}

	/// Every Wait() returns false from now on, the one asleep included: call before joining the worker.
	void Stop()
	{
		{
			std::lock_guard<std::mutex> lock(Lock);
			Stopping = true;
		}
		Condition.notify_all();
	}

	/// Undoes Stop() for a worker started again.
	void Restart()
	{
		std::lock_guard<std::mutex> lock(Lock);
		Stopping = false;
	}

private:
	std::mutex Lock;
	std::condition_variable Condition;
	bool Stopping;
};

#endif // THREADWAKE_H
