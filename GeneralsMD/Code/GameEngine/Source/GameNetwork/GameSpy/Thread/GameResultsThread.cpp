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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: PingThread.cpp //////////////////////////////////////////////////////
// Ping thread
// Author: Matthew D. Campbell, August 2002

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#if defined(_WIN32)
#include <winsock.h>	// This one has to be here. Prevents collisions with winsock2.h
#else
#include <arpa/inet.h>		// inet_addr, inet_ntoa
#include <netdb.h>				// gethostbyname
#include <netinet/in.h>
typedef struct hostent HOSTENT;		// winsock's name for it
#endif

#include "GameNetwork/GameSpy/GameResultsThread.h"
#include "mutex.h"
#include "thread.h"

#include "Common/StackDump.h"
#include "Common/SubsystemInterface.h"

#include <condition_variable>
#include <mutex>

//-------------------------------------------------------------------------

static const Int NumWorkerThreads = 1;

typedef std::queue<GameResultsRequest> RequestQueue;
typedef std::queue<GameResultsResponse> ResponseQueue;
class GameResultsThreadClass;

class GameResultsQueue : public GameResultsInterface
{
public:
	virtual ~GameResultsQueue();
	GameResultsQueue();

	virtual void init() {}
	virtual void reset() {}
	virtual void update() {}

	virtual void startThreads( void );
	virtual void endThreads( void );
	virtual Bool areThreadsRunning( void );

	virtual void addRequest( const GameResultsRequest& req );
	virtual Bool getRequest( GameResultsRequest& resp );

	virtual void addResponse( const GameResultsResponse& resp );
	virtual Bool getResponse( GameResultsResponse& resp );

	virtual Bool areGameResultsBeingSent( void );

	/// The worker's wait: the next request, blocking until there is one; FALSE once endThreads has begun.
	Bool waitForRequest( GameResultsRequest& req );

private:
	MutexClass m_requestMutex;
	MutexClass m_responseMutex;
	RequestQueue m_requests;
	ResponseQueue m_responses;
	Int m_requestCount;
	Int m_responseCount;

	/* The worker used to poll getRequest with a 1 ms sleep between tries: a thousand wakeups a second for
		 the whole session, for a queue that is written at most once, after an online ladder game.  It now
		 sleeps on m_wake.  m_wakeLock orders a worker's check of the queue before its wait against
		 addRequest's notify, so a request is never left waiting; m_stopping, under the same lock, is what
		 endThreads wakes it with. */
	std::mutex m_wakeLock;
	std::condition_variable m_wake;
	Bool m_stopping;

	GameResultsThreadClass *m_workerThreads[NumWorkerThreads];
};

GameResultsInterface* GameResultsInterface::createNewGameResultsInterface( void )
{
	return NEW GameResultsQueue;
}

GameResultsInterface *TheGameResultsQueue;

//-------------------------------------------------------------------------

class GameResultsThreadClass : public ThreadClass
{

public:
	/* The queue it serves.  The worker read the global TheGameResultsQueue instead, which GameEngine::init
		 assigns only after this thread is already running: a data race on every start (TSan). */
	explicit GameResultsThreadClass( GameResultsQueue *queue ) : ThreadClass(), m_queue(queue) {}

	void Thread_Function();

private:
	Int sendGameResults( UnsignedInt IP, UnsignedShort port, const std::string& results );

	GameResultsQueue *m_queue;
};


//-------------------------------------------------------------------------

GameResultsQueue::GameResultsQueue() : m_requestCount(0), m_responseCount(0), m_stopping(FALSE)
{
	for (Int i=0; i<NumWorkerThreads; ++i)
	{
		m_workerThreads[i] = NULL;
	}

	startThreads();
}

GameResultsQueue::~GameResultsQueue()
{
	endThreads();
}

void GameResultsQueue::startThreads( void )
{
	endThreads();
	{
		std::lock_guard<std::mutex> wake(m_wakeLock);
		m_stopping = FALSE;
	}
	for (Int i=0; i<NumWorkerThreads; ++i)
	{
		m_workerThreads[i] = NEW GameResultsThreadClass(this);
		m_workerThreads[i]->Execute();
	}
}

void GameResultsQueue::endThreads( void )
{
	// A worker asleep in waitForRequest never looks at ThreadClass::running, so wake it to leave before
	// ~ThreadClass stops and joins it.
	{
		std::lock_guard<std::mutex> wake(m_wakeLock);
		m_stopping = TRUE;
	}
	m_wake.notify_all();
	for (Int i=0; i<NumWorkerThreads; ++i)
	{
		if (m_workerThreads[i])
		{
			delete m_workerThreads[i];
			m_workerThreads[i] = NULL;
		}
	}
}

Bool GameResultsQueue::areThreadsRunning( void )
{
	for (Int i=0; i<NumWorkerThreads; ++i)
	{
		if (m_workerThreads[i])
		{
			if (m_workerThreads[i]->Is_Running())
				return true;
		}
	}
	return false;
}

void GameResultsQueue::addRequest( const GameResultsRequest& req )
{
	{
		MutexClass::LockClass m(m_requestMutex);

		++m_requestCount;
		m_requests.push(req);
	}
	// Under m_wakeLock, so this lands before the worker looks at the queue or after it is waiting.
	std::lock_guard<std::mutex> wake(m_wakeLock);
	m_wake.notify_one();
}

Bool GameResultsQueue::waitForRequest( GameResultsRequest& req )
{
	std::unique_lock<std::mutex> wake(m_wakeLock);
	for (;;)
	{
		if (m_stopping)
			return FALSE;
		{
			// A blocking lock, where getRequest tries once: a try that lost to areGameResultsBeingSent
			// would go back to sleep with the request still queued.
			MutexClass::LockClass m(m_requestMutex);
			if (!m_requests.empty())
			{
				req = m_requests.front();
				m_requests.pop();
				return TRUE;
			}
		}
		m_wake.wait(wake);
	}
}

Bool GameResultsQueue::getRequest( GameResultsRequest& req )
{
	MutexClass::LockClass m(m_requestMutex, 0);
	if (m.Failed())
		return false;

	if (m_requests.empty())
		return false;
	req = m_requests.front();
	m_requests.pop();
	return true;
}

void GameResultsQueue::addResponse( const GameResultsResponse& resp )
{
	{
		MutexClass::LockClass m(m_responseMutex);

		++m_responseCount;
		m_responses.push(resp);
	}
}

Bool GameResultsQueue::getResponse( GameResultsResponse& resp )
{
	MutexClass::LockClass m(m_responseMutex, 0);
	if (m.Failed())
		return false;

	if (m_responses.empty())
		return false;
	resp = m_responses.front();
	m_responses.pop();
	return true;
}

Bool GameResultsQueue::areGameResultsBeingSent( void )
{
	MutexClass::LockClass m(m_requestMutex, 0);
	if (m.Failed())
		return true;

	return m_requestCount > 0;
}

//-------------------------------------------------------------------------

void GameResultsThreadClass::Thread_Function()
{
	try {
	InstallThreadExceptionTranslator(); // Hook that allows stack trace.
	GameResultsRequest req;

#if defined(_WIN32)
	WSADATA wsaData;

	// Fire up winsock (prob already done, but doesn't matter)
	WORD wVersionRequested = MAKEWORD(1, 1);
	WSAStartup( wVersionRequested, &wsaData );
#endif

	while ( running )
	{
		// deal with requests: asleep until there is one, and out when the queue shuts down
		if (!m_queue->waitForRequest(req))
			break;
		{
			// resolve the hostname
			const char *hostnameBuffer = req.hostname.c_str();
			UnsignedInt IP = 0xFFFFFFFF;
			if (isdigit(hostnameBuffer[0]))
			{
				IP = inet_addr(hostnameBuffer);
				in_addr hostNode;
				hostNode.s_addr = IP;
				DEBUG_LOG(("sending game results to %s - IP = %s\n", hostnameBuffer, inet_ntoa(hostNode) ));
			}
			else
			{
				HOSTENT *hostStruct;
				in_addr *hostNode;
				hostStruct = gethostbyname(hostnameBuffer);
				if (hostStruct == NULL)
				{
					DEBUG_LOG(("sending game results to %s - host lookup failed\n", hostnameBuffer));
					
					// Even though this failed to resolve IP, still need to send a
					//   callback.
					IP = 0xFFFFFFFF;   // flag for IP resolve failed
				}
				else
				{
					// The lookup's failure used to fall through to here and read h_addr through NULL.
					hostNode = (in_addr *) hostStruct->h_addr;
					IP = hostNode->s_addr;
					DEBUG_LOG(("sending game results to %s IP = %s\n", hostnameBuffer, inet_ntoa(*hostNode) ));
				}
			}

			int result = sendGameResults( IP, req.port, req.results );
			GameResultsResponse resp;
			resp.hostname = req.hostname;
			resp.port = req.port;
			resp.sentOk = (result == req.results.length());

		}
	}

#if defined(_WIN32)
	WSACleanup();
#endif
	} catch ( ... ) {
		DEBUG_CRASH(("Exception in results thread!"));
	}
}

//-------------------------------------------------------------------------

/* The results go to GameSpy's server over a winsock TCP connection, with winsock's error names in the
	 log.  GameSpy is a dead service and only has to compile and link (the B5 survey: stub, do not port),
	 so off Windows sendGameResults answers -1, which the thread reports as not sent. */
#if defined(_WIN32)
#ifdef DEBUG_LOGGING
#define CASE(x) case (x): return #x;

static const char *getWSAErrorString( Int error )
{
	switch (error)
	{
		CASE(WSABASEERR)
		CASE(WSAEINTR)
		CASE(WSAEBADF)
		CASE(WSAEACCES)
		CASE(WSAEFAULT)
		CASE(WSAEINVAL)
		CASE(WSAEMFILE)
		CASE(WSAEWOULDBLOCK)
		CASE(WSAEINPROGRESS)
		CASE(WSAEALREADY)
		CASE(WSAENOTSOCK)
		CASE(WSAEDESTADDRREQ)
		CASE(WSAEMSGSIZE)
		CASE(WSAEPROTOTYPE)
		CASE(WSAENOPROTOOPT)
		CASE(WSAEPROTONOSUPPORT)
		CASE(WSAESOCKTNOSUPPORT)
		CASE(WSAEOPNOTSUPP)
		CASE(WSAEPFNOSUPPORT)
		CASE(WSAEAFNOSUPPORT)
		CASE(WSAEADDRINUSE)
		CASE(WSAEADDRNOTAVAIL)
		CASE(WSAENETDOWN)
		CASE(WSAENETUNREACH)
		CASE(WSAENETRESET)
		CASE(WSAECONNABORTED)
		CASE(WSAECONNRESET)
		CASE(WSAENOBUFS)
		CASE(WSAEISCONN)
		CASE(WSAENOTCONN)
		CASE(WSAESHUTDOWN)
		CASE(WSAETOOMANYREFS)
		CASE(WSAETIMEDOUT)
		CASE(WSAECONNREFUSED)
		CASE(WSAELOOP)
		CASE(WSAENAMETOOLONG)
		CASE(WSAEHOSTDOWN)
		CASE(WSAEHOSTUNREACH)
		CASE(WSAENOTEMPTY)
		CASE(WSAEPROCLIM)
		CASE(WSAEUSERS)
		CASE(WSAEDQUOT)
		CASE(WSAESTALE)
		CASE(WSAEREMOTE)
		CASE(WSAEDISCON)
		CASE(WSASYSNOTREADY)
		CASE(WSAVERNOTSUPPORTED)
		CASE(WSANOTINITIALISED)
		CASE(WSAHOST_NOT_FOUND)
		CASE(WSATRY_AGAIN)
		CASE(WSANO_RECOVERY)
		CASE(WSANO_DATA)
		default:
			return "Not a Winsock error";
	}
}

#undef CASE

#endif
//-------------------------------------------------------------------------

Int GameResultsThreadClass::sendGameResults( UnsignedInt IP, UnsignedShort port, const std::string& results )
{
	int error = 0;

	// create the socket
	Int sock = socket( AF_INET, SOCK_STREAM, 0 );
	if (sock < 0)
	{
		DEBUG_LOG(("GameResultsThreadClass::sendGameResults() - socket() returned %d(%s)\n", sock, getWSAErrorString(sock)));
		return sock;
	}

	// fill in address info
	struct sockaddr_in sockAddr;
	memset( &sockAddr, 0, sizeof( sockAddr ) );
	sockAddr.sin_family = AF_INET;
	sockAddr.sin_addr.s_addr = IP;
	sockAddr.sin_port = htons(port);

	// Start the connection process....
	if( connect( sock, (struct sockaddr *)&sockAddr, sizeof( sockAddr ) ) == -1 )
	{
		error = WSAGetLastError();
		DEBUG_LOG(("GameResultsThreadClass::sendGameResults() - connect() returned %d(%s)\n", error, getWSAErrorString(error)));
		if( ( error == WSAEWOULDBLOCK ) || ( error == WSAEINVAL ) || ( error == WSAEALREADY ) )
		{
			return( -1 );
		}

		if( error != WSAEISCONN )
		{
			closesocket( sock );
			return( -1 );
		}
	}

	if (send( sock, results.c_str(), results.length(), 0 ) == SOCKET_ERROR)
	{
		error = WSAGetLastError();
		DEBUG_LOG(("GameResultsThreadClass::sendGameResults() - send() returned %d(%s)\n", error, getWSAErrorString(error)));
		closesocket(sock);
		return WSAGetLastError();
	}

	closesocket(sock);

	return results.length();
}
#else
Int GameResultsThreadClass::sendGameResults( UnsignedInt, UnsignedShort, const std::string& )
{
	return -1;
}
#endif


//-------------------------------------------------------------------------
