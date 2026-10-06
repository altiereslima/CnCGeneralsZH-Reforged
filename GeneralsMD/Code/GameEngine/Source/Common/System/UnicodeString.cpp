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

// FILE: UnicodeString.cpp 
//-----------------------------------------------------------------------------
//                                                                          
//                       Westwood Studios Pacific.                          
//                                                                          
//                       Confidential Information					         
//                Copyright (C) 2001 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
// Project:    RTS3
//
// File name:  UnicodeString.cpp
//
// Created:    Steven Johnson, October 2001
//
// Desc:       General-purpose string classes
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Lib/WideCharFns.h"


#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

// -----------------------------------------------------

/*static*/ UnicodeString UnicodeString::TheEmptyString;

// -----------------------------------------------------
#ifdef _DEBUG
void UnicodeString::validate() const
{
	if (!m_data) return;
	DEBUG_ASSERTCRASH(m_data->m_refCount.load(std::memory_order_relaxed) > 0, ("m_refCount is zero"));
	DEBUG_ASSERTCRASH(m_data->m_numCharsAllocated > 0, ("m_numCharsAllocated is zero"));
	DEBUG_ASSERTCRASH(WideCharLen(m_data->peek())+1 <= m_data->m_numCharsAllocated,("str is too long for storage"));
}
#endif

// -----------------------------------------------------
UnicodeString::UnicodeString(const UnicodeString& stringSrc) : m_data(stringSrc.m_data)
{
	/* The refcount is atomic, with AsciiString's orderings (AsciiString.h, the copy constructor and
		 releaseBuffer), so TheUnicodeStringCriticalSection is no longer taken here or anywhere.  That
		 lock only ever covered the count: a race on one UnicodeString object itself - one thread
		 copying it while another reassigns it - read m_data outside the lock before, and still does. */
	if (m_data)
		m_data->m_refCount.fetch_add(1, std::memory_order_relaxed);
	validate();
}

// -----------------------------------------------------
void UnicodeString::ensureUniqueBufferOfSize(int numCharsNeeded, Bool preserveData, const WideChar* strToCopy, const WideChar* strToCat)
{
	validate();

	if (m_data &&
			m_data->m_refCount.load(std::memory_order_acquire) == 1 &&	// acquire: the last other holder's reads of the buffer happen before this thread writes it
			m_data->m_numCharsAllocated >= numCharsNeeded)
	{
		// no buffer manhandling is needed (it's already large enough, and unique to us)
		// memmove, as AsciiString's: nextToken sets the string to the rest of itself, and a string can
		// be concatenated onto itself; an overlapping wcscpy or wcscat is undefined, and the loop off
		// Windows ran past the end of a self-concatenation.
		if (strToCopy)
			memmove(m_data->peek(), strToCopy, (WideCharLen(strToCopy) + 1) * sizeof(WideChar));
		if (strToCat)
		{
			WideChar *end = m_data->peek() + WideCharLen(m_data->peek());
			memmove(end, strToCat, (WideCharLen(strToCat) + 1) * sizeof(WideChar));
		}
		return;
	}

	int minBytes = sizeof(UnicodeStringData) + numCharsNeeded*sizeof(WideChar);
	if (minBytes > MAX_LEN)
		throw ERROR_OUT_OF_MEMORY;

	if (TheDynamicMemoryAllocator == NULL)
		preMainInitMemoryManager();	// a string built by a static constructor, before main (GameMemory.h)
	int actualBytes = TheDynamicMemoryAllocator->getActualAllocationSize(minBytes);
	UnicodeStringData* newData = (UnicodeStringData*)TheDynamicMemoryAllocator->allocateBytesDoNotZero(actualBytes, "STR_UnicodeString::ensureUniqueBufferOfSize");
	new (&newData->m_refCount) std::atomic<unsigned short>(1);	// raw bytes, no constructor ran: start the atomic's lifetime
	newData->m_numCharsAllocated = (actualBytes - sizeof(UnicodeStringData))/sizeof(WideChar);
#if defined(_DEBUG) || defined(_INTERNAL)
	newData->m_debugptr = newData->peek();	// just makes it easier to read in the debugger
#endif

	if (m_data && preserveData)
		WideCharCpy(newData->peek(), m_data->peek());
	else
		newData->peek()[0] = 0;

	// do these BEFORE releasing the old buffer, so that self-copies
	// or self-cats will work correctly.
	if (strToCopy)
		WideCharCpy(newData->peek(), strToCopy);
	if (strToCat)
		WideCharCat(newData->peek(), strToCat);

	releaseBuffer();
	m_data = newData;

	validate();
}


// -----------------------------------------------------
void UnicodeString::releaseBuffer()
{
	validate();
	if (m_data)
	{
		if (m_data->m_refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)	// exactly one releaser sees 1
		{
			TheDynamicMemoryAllocator->freeBytes(m_data);
		}
		m_data = 0;
	}
}

// -----------------------------------------------------
UnicodeString::UnicodeString(const WideChar* s) : m_data(0)
{
	int len = WideCharLen(s);
	if (len)
	{
		ensureUniqueBufferOfSize(len + 1, false, s, NULL);
	}
	validate();
}

// -----------------------------------------------------
void UnicodeString::set(const UnicodeString& stringSrc)
{
	validate();
	if (&stringSrc != this)
	{
		// Release first is safe without the lock: if both share a buffer the count is at least 2,
		// so the release cannot free what stringSrc still holds.
		releaseBuffer();
		m_data = stringSrc.m_data;
		if (m_data)
			m_data->m_refCount.fetch_add(1, std::memory_order_relaxed);
	}
	validate();
}

// -----------------------------------------------------
void UnicodeString::set(const WideChar* s)
{
	validate();
	if (!m_data || s != peek())
	{
		int len = s ? WideCharLen(s) : 0;
		if (len)
		{
			ensureUniqueBufferOfSize(len + 1, false, s, NULL);
		}
		else
		{
			releaseBuffer();
		}
	}
	validate();
}

// -----------------------------------------------------
WideChar* UnicodeString::getBufferForRead(Int len)
{
	validate();
	DEBUG_ASSERTCRASH(len>0, ("No need to allocate 0 len strings."));
	ensureUniqueBufferOfSize(len + 1, false, NULL, NULL);
	validate();
	return peek();
}

// -----------------------------------------------------
void UnicodeString::translate(const AsciiString& stringSrc)
{
	validate();
	/// @todo srj put in a real translation here; this will only work for 7-bit ascii
	clear();
	Int len = stringSrc.getLength();
	for (Int i = 0; i < len; i++)
		concat((WideChar)stringSrc.getCharAt(i));
	validate();
}

// -----------------------------------------------------
void UnicodeString::concat(const WideChar* s)
{
	validate();
	int addlen = WideCharLen(s);
	if (addlen == 0)
		return;	// my, that was easy

	if (m_data)
	{
		ensureUniqueBufferOfSize(getLength() + addlen + 1, true, NULL, s);
	}
	else
	{
		set(s);
	}
	validate();
}

// -----------------------------------------------------
void UnicodeString::trim()
{
	validate();

	if (m_data)
	{
		const WideChar *c = peek();

		//	Strip leading white space from the string.
		while (c && WideCharIsSpace(*c))
		{
			c++;
		}
		if (c != peek())
		{
			set(c);
		}

		if (m_data) // another check, because the previous set() could erase m_data
		{
			//	Clip trailing white space from the string.
			int len = WideCharLen(peek());
			for (int index = len-1; index >= 0; index--)
			{
				if (WideCharIsSpace(getCharAt(index)))
				{
					removeLastChar();
				}
				else
				{
					break;
				}
			}
		}
	}
	validate();
}

// -----------------------------------------------------
void UnicodeString::removeLastChar()
{
	validate();
	if (m_data)
	{
		int len = WideCharLen(peek());
		if (len > 0)
		{
			ensureUniqueBufferOfSize(len+1, true, NULL, NULL);
			peek()[len - 1] = 0;
		}
	}
	validate();
}

// -----------------------------------------------------
void UnicodeString::format(UnicodeString format, ...)
{
	validate();
	va_list args;
  va_start(args, format);
	format_va(format, args);
  va_end(args);
	validate();
}

// -----------------------------------------------------
void UnicodeString::format(const WideChar* format, ...)
{
	validate();
	va_list args;
  va_start(args, format);
	format_va(format, args);
  va_end(args);
	validate();
}

// -----------------------------------------------------
void UnicodeString::format_va(const UnicodeString& format, va_list args)
{
	validate();
	WideChar buf[MAX_FORMAT_BUF_LEN];
	// WideCharFormatV, not _vsnwprintf: buf and format are WideChar and the C library has no
	// char16_t printf on any platform.  It keeps _vsnwprintf's contract - negative means the text
	// did not fit - which is what the throw below is written against.  See Lib/WideCharFns.h.
  if (WideCharFormatV(buf, sizeof(buf)/sizeof(WideChar)-1, format.str(), args) < 0)
			throw ERROR_OUT_OF_MEMORY;
	set(buf);
	validate();
}

// -----------------------------------------------------
void UnicodeString::format_va(const WideChar* format, va_list args)
{
	validate();
	WideChar buf[MAX_FORMAT_BUF_LEN];
  if (WideCharFormatV(buf, sizeof(buf)/sizeof(WideChar)-1, format, args) < 0)
			throw ERROR_OUT_OF_MEMORY;
	set(buf);
	validate();
}

//-----------------------------------------------------------------------------
Bool UnicodeString::nextToken(UnicodeString* tok, UnicodeString delimiters)
{
	if (this->isEmpty() || tok == this)
		return false;

	if (delimiters.isEmpty())
		delimiters = UnicodeString(u" \t\n\r");

	Int offset;

	offset = WideCharSpn(peek(), delimiters.str());
	WideChar* start = peek() + offset;

	offset = WideCharCSpn(start, delimiters.str());
	WideChar* end = start + offset;

	if (end > start)
	{
		Int len = end - start;
		WideChar* tmp = tok->getBufferForRead(len + 1);
		memcpy(tmp, start, len*sizeof(WideChar));	// was len*2: the same bytes while WideChar is two
		tmp[len] = 0;

		this->set(end);

		return true;
	}
	else
	{
		this->clear();
		tok->clear();
		return false;
	}
}