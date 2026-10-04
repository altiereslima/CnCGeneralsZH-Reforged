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

//----------------------------------------------------------------------------
//                                                                          
//                       Westwood Studios Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright(C) 2001 - All Rights Reserved                  
//                                                                          
//----------------------------------------------------------------------------
//
// Project:   WSYS Library
//
// Module:    IO_
//
// File name: IO_LocalFile.cpp
//
// Created:   4/23/01
//
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
//         Includes                                                      
//----------------------------------------------------------------------------

#include "PreRTS.h"

#include <stdio.h>
#include <fcntl.h>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#include "zhio.h"
#include "Platform/LoadTiming.h"
#endif
#include <string.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>

#include "Common/LocalFile.h"
#include "Common/RAMFile.h"
#include "Lib/BaseType.h"
#include "Common/PerfTimer.h"
			

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

//----------------------------------------------------------------------------
//         Externals                                                     
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Defines                                                         
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Private Types                                                     
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Private Data                                                     
//----------------------------------------------------------------------------

static Int s_totalOpen = 0;

//----------------------------------------------------------------------------
//         Public Data                                                      
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Private Prototypes                                               
//----------------------------------------------------------------------------



//----------------------------------------------------------------------------
//         Private Functions                                               
//----------------------------------------------------------------------------

// The C runtime's unbuffered file calls, as LocalFile uses them.  On Windows each is the call it
// names.  Off Windows (C1, decisions D4 and D6):
// - the file name is resolved against the disk (zhio.h), since the engine spells it the Windows way;
// - a TEXT file reads as Windows' text mode reads it, "\r\n" becoming "\n" (zh_read_text).  Writes
//   are not translated, so text written here ends lines with "\n" alone, which every reader in the
//   engine accepts.
#if defined(_WIN32)

enum
{
	OPEN_CREATE = _O_CREAT,
	OPEN_TRUNCATE = _O_TRUNC,
	OPEN_APPEND = _O_APPEND,
	OPEN_TEXT = _O_TEXT,
	OPEN_BINARY = _O_BINARY,
	OPEN_READ_WRITE = _O_RDWR,
	OPEN_WRITE_ONLY = _O_WRONLY,
	OPEN_READ_ONLY = _O_RDONLY
};

static inline int openFile(const Char *filename, int flags) { return _open(filename, flags, _S_IREAD | _S_IWRITE); }
static inline int readFile(int handle, void *buffer, Int bytes, Bool) { return _read(handle, buffer, bytes); }
static inline int writeFile(int handle, const void *buffer, Int bytes) { return _write(handle, buffer, bytes); }
static inline long seekFile(int handle, long offset, int origin) { return _lseek(handle, offset, origin); }
static inline int closeFile(int handle) { return _close(handle); }

#else

enum
{
	OPEN_CREATE = O_CREAT,
	OPEN_TRUNCATE = O_TRUNC,
	OPEN_APPEND = O_APPEND,
	OPEN_TEXT = 0,				// the mode is LocalFile's to apply, in readFile
	OPEN_BINARY = 0,
	OPEN_READ_WRITE = O_RDWR,
	OPEN_WRITE_ONLY = O_WRONLY,
	OPEN_READ_ONLY = O_RDONLY
};

static int openFile(const Char *filename, int flags)
{
	if (!zhLoadTimingAsked())
		return zh_open(filename, flags, 0666);
	const double start = zhLoadNowMs();
	const int handle = zh_open(filename, flags, 0666);
	const double took = zhLoadNowMs() - start;
	zhLoadReadMs() += took;
	if (took > ZH_LOAD_TIMING_REPORT_MS)
		fprintf(stderr, "LOAD open  t %10.1f ms  %7.1f ms  %s thread  %s\n", start, took, zhLoadThread(), filename);
	return handle;
}

static int readFile(int handle, void *buffer, Int bytes, Bool text)
{
	if (!zhLoadTimingAsked())
		return text ? zh_read_text(handle, buffer, bytes) : (int)::read(handle, buffer, bytes);
	const double start = zhLoadNowMs();
	const int got = text ? zh_read_text(handle, buffer, bytes) : (int)::read(handle, buffer, bytes);
	const double took = zhLoadNowMs() - start;
	zhLoadReadMs() += took;
	if (took > ZH_LOAD_TIMING_REPORT_MS)
		fprintf(stderr, "LOAD read  t %10.1f ms  %7.1f ms  %s thread  %d bytes\n", start, took, zhLoadThread(), bytes);
	return got;
}

static int writeFile(int handle, const void *buffer, Int bytes) { return (int)::write(handle, buffer, bytes); }
static long seekFile(int handle, long offset, int origin) { return (long)lseek(handle, offset, origin); }
static int closeFile(int handle) { return ::close(handle); }

#endif

/* A read or a seek that failed because the file's device went away while it was open: the drive holding
	 the game's data disconnected, ejected, or put to sleep (the lid-close crash; PORTING.md, "Defects found while porting").
	 Off Windows the call fails with EIO (measured on macOS 26, HFS+ and exFAT disk images detached under an
	 open file); ENXIO and ENODEV are the same loss on other devices and systems.  On Windows the CRT's
	 _read and _lseek leave the system's error in _doserrno; the codes below are the device-gone ones.  On
	 Windows 11 an exFAT VHD detached under the running game gave one of them (W2: exit status 3). */
#if defined(_WIN32)
static inline int lastFileError(void) { return (int)_doserrno; }
static Bool errorMeansDeviceGone(int error)
{
	return error == ERROR_DEVICE_NOT_CONNECTED || error == ERROR_NOT_READY || error == ERROR_DEV_NOT_EXIST ||
		error == ERROR_FILE_INVALID;
}
#else
static inline int lastFileError(void) { return errno; }
static Bool errorMeansDeviceGone(int error) { return error == EIO || error == ENXIO || error == ENODEV; }
#endif

//=================================================================
// LocalFile::LocalFile
//=================================================================

LocalFile::LocalFile()
#ifdef USE_BUFFERED_IO
	: m_file(NULL)
#else
	: m_handle(-1)
#endif
	, m_lastError(0)
{
}


//----------------------------------------------------------------------------
//         Public Functions                                                
//----------------------------------------------------------------------------


//=================================================================
// LocalFile::~LocalFile	
//=================================================================

LocalFile::~LocalFile()
{
#ifdef USE_BUFFERED_IO
	if (m_file)
	{
		fclose(m_file);
		m_file = NULL;
		--s_totalOpen;
	}
#else
	if( m_handle != -1 )
	{
		closeFile( m_handle );
		m_handle = -1;
		--s_totalOpen;
	}
#endif

	File::close();

}

//=================================================================
// LocalFile::open	
//=================================================================
/**
  *	This function opens a file using the standard C open() call. Access flags
	* are mapped to the appropriate open flags. Returns true if file was opened
	* successfully.
	*/
//=================================================================

//DECLARE_PERF_TIMER(LocalFile)
Bool LocalFile::open( const Char *filename, Int access )
{
	//USE_PERF_TIMER(LocalFile)
	if( !File::open( filename, access) )
	{
		return FALSE;
	}
	m_lastError = 0;

	/* here we translate WSYS file access to the std C equivalent */
#ifdef USE_BUFFERED_IO
	char mode[32];
	char* m = mode;

	if (m_access & APPEND)
	{
		DEBUG_CRASH(("not yet supported by buffered mode"));
	}

	if (m_access & TRUNCATE)
	{
		DEBUG_CRASH(("not yet supported by buffered mode"));
	}

	if((m_access & READWRITE ) == READWRITE )
	{
		if (m_access & CREATE)
		{
			*m++ = 'w';
			*m++ = '+';
		}
		else
		{
			*m++ = 'r';
			*m++ = '+';
		}
	}
	else if(m_access & WRITE)
	{
		*m++ = 'w';
	}
	else
	{
		*m++ = 'r';
		DEBUG_ASSERTCRASH(!(m_access & TRUNCATE), ("cannot truncate with read-only"));
	}

	if (m_access & TEXT)
	{
		*m++ = 't';
	}
	if (m_access & BINARY)
	{
		*m++ = 'b';
	}

	*m++ = 0;

	m_file = fopen(filename, mode);
	if (m_file == NULL)
	{
		goto error;
	}
	
	//setvbuf(m_file, m_vbuf, _IOFBF, sizeof(BUF_SIZE));

#else

	int flags = 0;

	if (m_access & CREATE)
	{
		flags |= OPEN_CREATE;
	}
	if (m_access & TRUNCATE)
	{
		flags |= OPEN_TRUNCATE;
	}
	if (m_access & APPEND)
	{
		flags |= OPEN_APPEND;
	}
	if (m_access & TEXT)
	{
		flags |= OPEN_TEXT;
	}
	if (m_access & BINARY)
	{
		flags |= OPEN_BINARY;
	}

	if((m_access & READWRITE )== READWRITE )
	{
		flags |= OPEN_READ_WRITE;
	}
	else if(m_access & WRITE)
	{
		flags |= OPEN_WRITE_ONLY;
		flags |= OPEN_CREATE;
	}
	else
	{
		flags |= OPEN_READ_ONLY;
	}

	m_handle = openFile( filename, flags );

	if( m_handle == -1 )
	{
		goto error;
	}

#endif

	++s_totalOpen;
///	DEBUG_LOG(("LocalFile::open %s (total %d)\n",filename,s_totalOpen));
	if ( m_access & APPEND )
	{
		if ( seek ( 0, END ) < 0 )
		{
			goto error;
		}
	}

	return TRUE;

error:

	close();

	return FALSE;
}

//=================================================================
// LocalFile::close 	
//=================================================================
/**
	* Closes the current file if it is open.
  * Must call LocalFile::close() for each successful LocalFile::open() call.
	*/
//=================================================================

void LocalFile::close( void )
{
	File::close();
}

//=================================================================
// LocalFile::read 
//=================================================================

Int LocalFile::read( void *buffer, Int bytes )
{
	//USE_PERF_TIMER(LocalFile)
	if( !m_open )
	{
		return -1;
	}

	if (buffer == NULL) 
	{
#ifdef USE_BUFFERED_IO
		fseek(m_file, bytes, SEEK_CUR);
#else
		seekFile(m_handle, bytes, SEEK_CUR);
#endif
		return bytes;
	}

#ifdef USE_BUFFERED_IO
	Int ret = fread(buffer, 1, bytes, m_file);
#else
	Int ret = readFile( m_handle, buffer, bytes, (m_access & TEXT) != 0 );
	if (ret < 0)
		m_lastError = lastFileError();
#endif

	return ret;
}

//=================================================================
// LocalFile::write 
//=================================================================

Int LocalFile::write( const void *buffer, Int bytes )
{

	if( !m_open || !buffer )
	{
		return -1;
	}

#ifdef USE_BUFFERED_IO
	Int ret = fwrite(buffer, 1, bytes, m_file);
#else
	Int ret = writeFile( m_handle, buffer, bytes );
#endif
	return ret;
}

//=================================================================
// LocalFile::seek 
//=================================================================

Int LocalFile::seek( Int pos, seekMode mode)
{
	int lmode;

	switch( mode )
	{
		case START:
			lmode = SEEK_SET;
			break;
		case CURRENT:
			lmode = SEEK_CUR;
			break;
		case END:
			DEBUG_ASSERTCRASH(pos <= 0, ("LocalFile::seek - pos should be <= 0 for a seek starting at the end of the file"));
			lmode = SEEK_END;
			break;
		default:
			// bad seek mode
			return -1;
	}

#ifdef USE_BUFFERED_IO
	Int ret = fseek(m_file, pos, lmode);
	if (ret == 0)
		return ftell(m_file);
	else
		return -1;
#else
	Int ret = seekFile( m_handle, pos, lmode );
	if (ret < 0)
		m_lastError = lastFileError();
#endif
	return ret;
}

//=================================================================
// LocalFile::deviceGone
//=================================================================

Bool LocalFile::deviceGone( void ) const
{
	return m_lastError != 0 && errorMeansDeviceGone( m_lastError );
}

//=================================================================
// LocalFile::scanInt
//=================================================================
// skips preceding whitespace and stops at the first non-number
// or at EOF
Bool LocalFile::scanInt(Int &newInt)
{
	newInt = 0;
	AsciiString tempstr;
	Char c;
	Int val;

	// skip preceding non-numeric characters
	do {
#ifdef USE_BUFFERED_IO
		val = fread(&c, 1, 1, m_file);
#else
		val = readFile( m_handle, &c, 1, (m_access & TEXT) != 0 );
#endif
	} while ((val != 0) && (((c < '0') || (c > '9')) && (c != '-')));

	if (val == 0) {
		return FALSE;
	}

	do {
		tempstr.concat(c);
#ifdef USE_BUFFERED_IO
		val = fread(&c, 1, 1, m_file);
#else
		val = readFile( m_handle, &c, 1, (m_access & TEXT) != 0 );
#endif
	} while ((val != 0) && ((c >= '0') && (c <= '9')));

	// put the last read char back, since we didn't use it.
	if (val != 0) {
#ifdef USE_BUFFERED_IO
		fseek(m_file, -1, SEEK_CUR);
#else
		seekFile(m_handle, -1, SEEK_CUR);
#endif
	}

	newInt = atoi(tempstr.str());
	return TRUE;
}

//=================================================================
// LocalFile::scanReal
//=================================================================
// skips preceding whitespace and stops at the first non-number
// or at EOF
Bool LocalFile::scanReal(Real &newReal) 
{
	newReal = 0.0;
	AsciiString tempstr;
	Char c;
	Int val;
	Bool sawDec = FALSE;

	// skip the preceding white space
	do {
#ifdef USE_BUFFERED_IO
		val = fread(&c, 1, 1, m_file);
#else
		val = readFile( m_handle, &c, 1, (m_access & TEXT) != 0 );
#endif
	} while ((val != 0) && (((c < '0') || (c > '9')) && (c != '-') && (c != '.')));

	if (val == 0) {
		return FALSE;
	}

	do {
		tempstr.concat(c);
		if (c == '.') {
			sawDec = TRUE;
		}
#ifdef USE_BUFFERED_IO
		val = fread(&c, 1, 1, m_file);
#else
		val = readFile(m_handle, &c, 1, (m_access & TEXT) != 0);
#endif
	} while ((val != 0) && (((c >= '0') && (c <= '9')) || ((c == '.') && !sawDec)));

	if (val != 0) {
#ifdef USE_BUFFERED_IO
		fseek(m_file, -1, SEEK_CUR);
#else
		seekFile(m_handle, -1, SEEK_CUR);
#endif
	}

	newReal = atof(tempstr.str());
	return TRUE;
}

//=================================================================
// LocalFile::scanString
//=================================================================
// skips preceding whitespace and stops at the first whitespace
// or at EOF
Bool LocalFile::scanString(AsciiString &newString) 
{
	Char c;
	Int val;

	newString.clear();

	// skip the preceding whitespace
	do {
#ifdef USE_BUFFERED_IO
		val = fread(&c, 1, 1, m_file);
#else
		val = readFile(m_handle, &c, 1, (m_access & TEXT) != 0);
#endif
	} while ((val != 0) && (isspace(c)));

	if (val == 0) {
		return FALSE;
	}

	do {
		newString.concat(c);
#ifdef USE_BUFFERED_IO
		val = fread(&c, 1, 1, m_file);
#else
		val = readFile(m_handle, &c, 1, (m_access & TEXT) != 0);
#endif
	} while ((val != 0) && (!isspace(c)));

	if (val != 0) {
#ifdef USE_BUFFERED_IO
		fseek(m_file, -1, SEEK_CUR);
#else
		seekFile(m_handle, -1, SEEK_CUR);
#endif
	}

	return TRUE;
}

//=================================================================
// LocalFile::nextLine
//=================================================================
// scans to the first character after a new-line or at EOF
void LocalFile::nextLine(Char *buf, Int bufSize) 
{
	Char c = 0;
	Int val;
	Int i = 0;

	// seek to the next new-line.
	do {
		if ((buf == NULL) || (i >= (bufSize-1))) {
#ifdef USE_BUFFERED_IO
			val = fread(&c, 1, 1, m_file);
#else
			val = readFile(m_handle, &c, 1, (m_access & TEXT) != 0);
#endif
		} else {
#ifdef USE_BUFFERED_IO
			val = fread(buf + i, 1, 1, m_file);
#else
			val = readFile(m_handle, buf + i, 1, (m_access & TEXT) != 0);
#endif
			c = buf[i];
		}
		++i;
	} while ((val != 0) && (c != '\n'));

	if (buf != NULL) {
		if (i < bufSize) {
			buf[i] = 0;
		} else {
			buf[bufSize] = 0;
		}
	}
}

//=================================================================
//=================================================================
File* LocalFile::convertToRAMFile()
{
	RAMFile *ramFile = newInstance( RAMFile );
	if (ramFile->open(this)) 
	{
		if (this->m_deleteOnClose)
		{
			ramFile->deleteOnClose();
			this->close(); // is deleteonclose, so should delete.
		}
		else
		{
			this->close();
			this->deleteInstance();
		}
		return ramFile;
	}	
	else 
	{
		ramFile->close();
		ramFile->deleteInstance();
		return this;
	}
}

//=================================================================
// LocalFile::readEntireAndClose
//=================================================================
/**
	Allocate a buffer large enough to hold entire file, read 
	the entire file into the buffer, then close the file.
	the buffer is owned by the caller, who is responsible
	for freeing is (via delete[]). This is a Good Thing to
	use because it minimizes memory copies for BIG files.
*/
char* LocalFile::readEntireAndClose()
{
	UnsignedInt fileSize = size();
	char* buffer = NEW char[fileSize];

	read(buffer, fileSize);

	close();

	return buffer;
}
