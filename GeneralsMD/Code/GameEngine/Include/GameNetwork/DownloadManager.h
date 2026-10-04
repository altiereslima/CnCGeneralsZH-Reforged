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

// FILE: DownloadManager.h //////////////////////////////////////////////////////
// Generals download class definitions
// Author: Matthew D. Campbell, July 2002

#pragma once

#ifndef __DOWNLOADMANAGER_H__
#define __DOWNLOADMANAGER_H__

#if defined(_WIN32)
#include "WWDownload/downloaddefs.h"
#include "WWDownload/download.h"
#endif

class CDownload;
class QueuedDownload
{
public:
	AsciiString server;
	AsciiString userName;
	AsciiString password;
	AsciiString file;
	AsciiString localFile;
	AsciiString regKey;
	Bool tryResume;
};

/////////////////////////////////////////////////////////////////////////////
// DownloadManager

#if defined(_WIN32)
class DownloadManager : public IDownload
{
public:
	DownloadManager();
	virtual ~DownloadManager();
	
public:
	void init( void );
	HRESULT update( void );
	void reset( void );

	virtual HRESULT OnError( Int error );
	virtual HRESULT OnEnd();
	virtual HRESULT OnQueryResume();
	virtual HRESULT OnProgressUpdate( Int bytesread, Int totalsize, Int timetaken, Int timeleft );
	virtual HRESULT OnStatusUpdate( Int status );

	virtual HRESULT downloadFile( AsciiString server, AsciiString username, AsciiString password, AsciiString file, AsciiString localfile, AsciiString regkey, Bool tryResume );
	AsciiString getLastLocalFile( void );

	Bool isDone( void ) { return m_sawEnd || m_wasError; }
	Bool isOk( void ) { return m_sawEnd; }
	Bool wasError( void ) { return m_wasError; }

	UnicodeString getStatusString( void ) { return m_statusString; }
	UnicodeString getErrorString( void ) { return m_errorString; }

	void queueFileForDownload( AsciiString server, AsciiString username, AsciiString password, AsciiString file, AsciiString localfile, AsciiString regkey, Bool tryResume );
	Bool isFileQueuedForDownload( void ) { return !m_queuedDownloads.empty(); }
	HRESULT downloadNextQueuedFile( void );

private:
	Bool m_winsockInit;
	CDownload *m_download;
	Bool m_wasError;
	Bool m_sawEnd;
	UnicodeString m_errorString;
	UnicodeString m_statusString;

protected:
	std::list<QueuedDownload> m_queuedDownloads;
};

#else
/* Off Windows there is no downloader: WWDownload is FTP over winsock, for patches from a dead
	 service.  Only DownloadMenuInit creates TheDownloadManager, and only on Windows, so here it stays
	 NULL (DownloadManagerPosix.cpp) and every caller already tests it before use.  These are the
	 members callers name, doing nothing; no caller reads what update or downloadNextQueuedFile
	 return, which is an HRESULT on Windows. */
class DownloadManager
{
public:
	virtual ~DownloadManager() {}

	void update( void ) {}
	Bool isDone( void ) { return TRUE; }
	void queueFileForDownload( AsciiString, AsciiString, AsciiString, AsciiString, AsciiString, AsciiString, Bool ) {}
	void downloadNextQueuedFile( void ) {}
};
#endif

extern DownloadManager *TheDownloadManager;

#endif // __DOWNLOADMANAGER_H__
