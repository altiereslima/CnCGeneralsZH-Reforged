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
// Modified 2026 by Olcay Seygan for Zero Hour Reforged; see the git history.

// FILE: FileWriteQueue.h /////////////////////////////////////////////////////////////////////////
// Desc:   A file written by a thread of its own: the debug log and the replay.
////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdio>
#include <condition_variable>
#include <mutex>
#if !defined(_WIN32)
#include <pthread.h>
#endif

/* The thread that has something to write never waits for the disk.  D: here is a spinning disk, and
	 with other copies of the game, a build and a video dump writing to it Windows holds a writer back
	 for seconds: a flush inside a logic frame froze a 2v2 skirmish for 7.8 s.  write() copies the bytes
	 into a queue under a lock held for that copy; the writer thread takes the queue once a second and
	 writes and flushes it with the lock released.  The file's bytes and their order are what the same
	 calls made directly on the FILE would have left; only when they reach the disk changes.

	 Whatever reads the file back first calls flush() (or stop()), which writes the queue out on the
	 calling thread and returns once it is on disk.  Crash paths call flushAll() before they _exit.

	 Nothing in here calls operator new: the debug log opens from inside the first operator new,
	 before the memory manager exists, and builds its queue in place in static storage.  The queue's
	 bytes are malloc'd and the thread is the platform's own, not std::thread. */
class FileWriteQueue
{
public:
	FileWriteQueue();
	~FileWriteQueue();		///< stop()

	/// Takes writes for file and starts the writer.  afterWrite, if given, runs on whichever thread
	/// wrote a batch, right after its flush, with fileLock held.
	void start(FILE *file, void (*afterWrite)(FILE *file) = NULL);
	/// The writer joined and the queue written out; the file is the caller's again, to close.  A
	/// stopped queue drops what it is given.
	void stop();

	void write(const void *data, size_t length);							///< at the end of the file
	void writeAt(long offset, const void *data, size_t length);	///< over earlier bytes, as fseek, fwrite and a seek back did

	/// Everything queued so far into the file and flushed, on the caller's thread.
	void flush();
	/// flush() on every started queue: crash paths, before _exit skips the C library's own flush.
	static void flushAll();

	/// Held while a batch is written.  ReleaseCrash takes the debug log's for good, so that no other
	/// thread's lines cut into its report.  Recursive: a crash on the writer thread flushes from
	/// inside a flush.
	std::recursive_mutex fileLock;

	void runWriter();		///< the writer thread's loop

private:
	void queue(long offset, const void *data, size_t length);

	std::mutex m_queueLock;
	std::condition_variable m_wake;
	FILE *m_file;
	void (*m_afterWrite)(FILE *file);
	char *m_text;					///< records: a RecordHeader, then its bytes
	size_t m_used;
	size_t m_size;
	size_t m_lastAppend;	///< where the last record starts when it appends, so the next append joins it
	char *m_batch;				///< what flush() writes from, swapped with m_text; fileLock's
	size_t m_batchSize;
#if defined(_WIN32)
	void *m_thread;
#else
	pthread_t m_thread;
#endif
	bool m_running;
	bool m_quit;
};
