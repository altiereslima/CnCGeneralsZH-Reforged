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

// FILE: FileWriteQueue.cpp ///////////////////////////////////////////////////////////////////////
// Desc:   A file written by a thread of its own; see FileWriteQueue.h.
////////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"
#include "Common/FileWriteQueue.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <utility>

/// A record in the queue: offset -1 appends, anything else is a writeAt.
struct RecordHeader
{
	long offset;
	size_t length;
};

static const size_t NO_APPEND = (size_t)-1;

// ponytail: a fixed table read without a lock by crash paths; there are two queues, the debug log
// and the replay, and a crash racing a start or stop at worst misses one flush.
static FileWriteQueue *theStartedQueues[ 4 ];

FileWriteQueue::FileWriteQueue() :
	m_file(NULL),
	m_afterWrite(NULL),
	m_text(NULL),
	m_used(0),
	m_size(0),
	m_lastAppend(NO_APPEND),
	m_batch(NULL),
	m_batchSize(0),
	m_running(false),
	m_quit(false)
{
}

FileWriteQueue::~FileWriteQueue()
{
	stop();
	free(m_text);
	free(m_batch);
}

#if defined(_WIN32)
static DWORD WINAPI writerThread(LPVOID queue)
{
	((FileWriteQueue *)queue)->runWriter();
	return 0;
}
#else
static void *writerThread(void *queue)
{
	((FileWriteQueue *)queue)->runWriter();
	return NULL;
}
#endif

void FileWriteQueue::start(FILE *file, void (*afterWrite)(FILE *file))
{
	{
		std::lock_guard<std::mutex> lock(m_queueLock);
		m_file = file;
		m_afterWrite = afterWrite;
		m_quit = false;
	}
#if defined(_WIN32)
	m_thread = CreateThread(NULL, 0, writerThread, this, 0, NULL);
	m_running = m_thread != NULL;
#else
	m_running = pthread_create(&m_thread, NULL, writerThread, this) == 0;
#endif
	for (size_t i = 0; i < ARRAY_SIZE(theStartedQueues); ++i)
	{
		if (theStartedQueues[i] == NULL)
		{
			theStartedQueues[i] = this;
			break;
		}
	}
}

void FileWriteQueue::stop()
{
	if (m_running)
	{
		{
			std::lock_guard<std::mutex> lock(m_queueLock);
			m_quit = true;
		}
		m_wake.notify_one();
#if defined(_WIN32)
		WaitForSingleObject((HANDLE)m_thread, INFINITE);
		CloseHandle((HANDLE)m_thread);
#else
		pthread_join(m_thread, NULL);
#endif
		m_running = false;
	}
	flush();
	for (size_t i = 0; i < ARRAY_SIZE(theStartedQueues); ++i)
	{
		if (theStartedQueues[i] == this)
			theStartedQueues[i] = NULL;
	}
	std::lock_guard<std::mutex> lock(m_queueLock);
	m_file = NULL;
	m_used = 0;		// a write that raced the flush above, never to lead the next file
	m_lastAppend = NO_APPEND;
}

void FileWriteQueue::queue(long offset, const void *data, size_t length)
{
	std::lock_guard<std::mutex> lock(m_queueLock);
	if (m_file == NULL)
		return;		// stopped
	const bool append = offset < 0;
	const size_t needed = m_used + sizeof(RecordHeader) + length;
	if (needed > m_size)
	{
		m_size = 2 * needed;
		m_text = (char *)realloc(m_text, m_size);
	}
	if (append && m_lastAppend != NO_APPEND)
	{
		RecordHeader header;
		memcpy(&header, m_text + m_lastAppend, sizeof(header));
		header.length += length;
		memcpy(m_text + m_lastAppend, &header, sizeof(header));
	}
	else
	{
		const RecordHeader header = { offset, length };
		m_lastAppend = append ? m_used : NO_APPEND;
		memcpy(m_text + m_used, &header, sizeof(header));
		m_used += sizeof(header);
	}
	memcpy(m_text + m_used, data, length);
	m_used += length;
}

void FileWriteQueue::write(const void *data, size_t length)
{
	queue(-1, data, length);
}

void FileWriteQueue::writeAt(long offset, const void *data, size_t length)
{
	queue(offset, data, length);
}

void FileWriteQueue::flush()
{
	std::lock_guard<std::recursive_mutex> file(fileLock);
	size_t used;
	FILE *f;
	{
		// swapped, so what is queued while this batch is written goes on the next one
		std::lock_guard<std::mutex> lock(m_queueLock);
		std::swap(m_batch, m_text);
		std::swap(m_batchSize, m_size);
		used = m_used;
		m_used = 0;
		m_lastAppend = NO_APPEND;
		f = m_file;
	}
	if (f == NULL || used == 0)
		return;
	for (size_t at = 0; at < used; )
	{
		RecordHeader header;
		memcpy(&header, m_batch + at, sizeof(header));
		at += sizeof(header);
		if (header.offset < 0)
			fwrite(m_batch + at, 1, header.length, f);
		else
		{
			const long end = ftell(f);
			if (fseek(f, header.offset, SEEK_SET) == 0)
				fwrite(m_batch + at, 1, header.length, f);
			fseek(f, end, SEEK_SET);
		}
		at += header.length;
	}
	fflush(f);
	if (m_afterWrite != NULL)
		m_afterWrite(f);
}

void FileWriteQueue::flushAll()
{
	for (size_t i = 0; i < ARRAY_SIZE(theStartedQueues); ++i)
	{
		FileWriteQueue *queue = theStartedQueues[i];
		if (queue != NULL)
			queue->flush();
	}
}

void FileWriteQueue::runWriter()
{
	std::unique_lock<std::mutex> lock(m_queueLock);
	while (!m_quit)
	{
		m_wake.wait_for(lock, std::chrono::seconds(1));
		lock.unlock();
		flush();
		lock.lock();
	}
}
