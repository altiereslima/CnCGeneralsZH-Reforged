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

// FILE: AsciiString.h 
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
// File name:  AsciiString.h
//
// Created:    Steven Johnson, October 2001
//
// Desc:       General-purpose string classes
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef ASCIISTRING_H
#define ASCIISTRING_H

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "Lib/BaseType.h"
#include "Common/Debug.h"
#include "Common/Errors.h"

class UnicodeString;

#include <atomic>
#include <new>

// -----------------------------------------------------
/**
	AsciiString is the fundamental single-byte string type used in the Generals
	code base, and should be preferred over all other string constructions
	(e.g., array of char, STL string<>, WWVegas StringClass, etc.)

	Of course, other string setups may be used when necessary or appropriate!

	AsciiString is modeled after the MFC CString class, with some minor
	syntactic differences to keep in line with our coding conventions.

	Basically, AsciiString allows you to treat a string as an intrinsic
	type, rather analogous to 'int' -- when passed by value, a new string
	is created, and modifying the new string doesn't modify the original.
	This is done fairly efficiently, so that no new memory allocation is done
	unless the string is actually modified. 

	Naturally, AsciiString handles all memory issues, so there's no need
	to do anything to free memory... just allow the AsciiString's
	destructor to run.

	AsciiStrings are suitable for use as automatic, member, or static variables.
*/

class AsciiString
{
private:
	
	// Note, this is a Plain Old Data Structure... don't
	// add a ctor/dtor, 'cuz they won't ever be called.
	struct AsciiStringData
	{
#if defined(_DEBUG) || defined(_INTERNAL)
		const char* m_debugptr;	// just makes it easier to read in the debugger
#endif
		/* B13: std::atomic, and deliberately still 16 bits - the struct's size and every offset in
			 it are unchanged, which matters because these are pool-allocated and the block sizes are
			 computed from sizeof(AsciiStringData).  Verified identical on arm64 and x86-64, in both
			 the debug layout (with m_debugptr) and the release one.

			 What it replaces was InterlockedIncrement((long *)&m_refCount): a THIRTY-TWO bit atomic
			 read-modify-write on a SIXTEEN bit field.  m_numCharsAllocated is at offset 2 and long is
			 4 bytes on both Win32 and Win64, so every refcount operation was also reading and writing
			 back its neighbour.  The old comment - "yes, I know it's not a DWord but we're
			 incrementing so we're safe" - is the author noting that a count which never wraps never
			 carries out of its low half, and that part is true.

			 Being accurate about how bad this was, because the fix is free either way and overclaiming
			 it would be worse than not finding it: it is type-punning UB, but it was not corrupting
			 anything reachable.  The 32-bit access was correctly aligned (MEM_BOUND_ALIGNMENT is 4),
			 and m_numCharsAllocated is written in exactly one place - AsciiString.cpp:148, on a buffer
			 that has just been allocated and is not shared yet - so no thread can be writing it while
			 another increments the count.  A 16-bit atomic touches two bytes, the pun goes away, and
			 nothing has to depend on that argument staying true.  std::atomic<unsigned short> is
			 lock-free everywhere this builds (ATOMIC_SHORT_LOCK_FREE == 2). */
		std::atomic<unsigned short>	m_refCount;		// reference count
		unsigned short	m_numCharsAllocated;  // length of data allocated
		// char m_stringdata[];

		inline char* peek() { return (char*)(this+1); }
	};

	#ifdef _DEBUG
	void validate() const;
	#else
	inline void validate() const { }
	#endif

  void freeBytes(void);

protected:
	AsciiStringData* m_data;   // pointer to ref counted string data

	char* peek() const;
	void releaseBuffer();
	void ensureUniqueBufferOfSize(int numCharsNeeded, Bool preserveData, const char* strToCpy, const char* strToCat);

public:

	enum 
	{ 
		MAX_FORMAT_BUF_LEN = 2048,		///< max total len of string created by format/format_va
		MAX_LEN = 32767							///< max total len of any AsciiString, in chars
	};


	/**
		This is a convenient global used to indicate the empty
		string, so we don't need to construct temporaries
		for such a common thing.
	*/
	static AsciiString TheEmptyString;

	/**
		Default constructor -- construct a new, empty AsciiString.
	*/
	AsciiString();
	/**
		Copy constructor -- make this AsciiString identical to the
		other AsciiString. (This is actually quite efficient, because
		they will simply share the same string and increment the
		refcount.)
	*/
	AsciiString(const AsciiString& stringSrc);
	/**
		Constructor -- from a literal string. Constructs an AsciiString
		with the given string. Note that a copy of the string is made;
		the input ptr is not saved. 
		Note that this is no longer explicit, as the conversion is almost
		always wanted, anyhow.
	*/
	AsciiString(const char* s);

	/**
		Destructor. Not too exciting... clean up the works and such.
	*/
	~AsciiString();

	/**
		Return the length, in characters (not bytes!), of the string.
	*/
	int getLength() const;
	/**
		Return true iff the length of the string is zero. Equivalent
		to (getLength() == 0) but slightly more efficient.
	*/
	Bool isEmpty() const;
	/**
		Make the string empty. Equivalent to (str = "") but slightly more efficient.
	*/
	void clear();

	/**
		Return the character and the given (zero-based) index into the string.
		No range checking is done (except in debug mode).
	*/
	char getCharAt(int index) const;
	/**
		Return a pointer to the (null-terminated) string. Note that this is 
		a const pointer: do NOT change this! It is imperative that it be 
		impossible (or at least, really difficuly) for someone to change our
		private data, since it might be shared amongst other AsciiStrings.
	*/
	const char* str() const;

	/**
		Makes sure there is room for a string of len+1 characters, and
		returns a pointer to the string buffer.  This ensures that the
		string buffer is NOT shared.  This is intended for the file reader, 
		that is reading new strings in from a file. jba.
	*/
	char* getBufferForRead(Int len);

	/**
		Replace the contents of self with the given string.
		(This is actually quite efficient, because
		they will simply share the same string and increment the
		refcount.)
	*/
	void set(const AsciiString& stringSrc);
	/**
		Replace the contents of self with the given string.
		Note that a copy of the string is made; the input ptr is not saved.
	*/
	void set(const char* s);

	/**
		replace contents of self with the given string. Note the
		nomenclature is translate rather than set; this is because
		not all single-byte strings translate one-for-one into
		UnicodeStrings, so some data manipulation may be necessary,
		and the resulting strings may not be equivalent.
	*/
	void translate(const UnicodeString& stringSrc);

	/**
		Concatenate the given string onto self.
	*/
	void concat(const AsciiString& stringSrc);
	/**
		Concatenate the given string onto self.
	*/
	void concat(const char* s);
	/**
		Concatenate the given character onto self.
	*/
	void concat(const char c);

	/**
	  Remove leading and trailing whitespace from the string.
	*/
	void trim( void );

	/**
	  Make the string lowercase
	*/
	void toLower( void );

	/**
		Remove the final character in the string. If the string is empty,
		do nothing. (This is a rather dorky method, but used a lot in 
		text editing, thus its presence here.)
	*/
	void removeLastChar();

	/**
		Analogous to sprintf() -- this formats a string according to the
		given sprintf-style format string (and the variable argument list)
		and stores the result in self.
	*/
	void format(AsciiString format, ...);
	void format(const char* format, ...);
	/**
		Identical to format(), but takes a va_list rather than
		a variable argument list. (i.e., analogous to vsprintf.)
	*/
	void format_va(const AsciiString& format, va_list args);
	void format_va(const char* format, va_list args);

	/**
		Conceptually identical to strcmp().
	*/
	int compare(const AsciiString& stringSrc) const;
	/**
		Conceptually identical to strcmp().
	*/
	int compare(const char* s) const;
	/**
		Conceptually identical to strcasecmp().
	*/
	int compareNoCase(const AsciiString& stringSrc) const;
	/**
		Conceptually identical to strcasecmp().
	*/
	int compareNoCase(const char* s) const;

	/**
		Conceptually identical to strchr().
	*/
	const char* find(char c) const;

	/**
		Conceptually identical to strrchr().
	*/
	const char* reverseFind(char c) const;

	/**
		return true iff self starts with the given string.
	*/
	Bool startsWith(const char* p) const;
	inline Bool startsWith(const AsciiString& stringSrc) const { return startsWith(stringSrc.str()); }

	/**
		return true iff self starts with the given string. (case insensitive)
	*/
	Bool startsWithNoCase(const char* p) const;
	inline Bool startsWithNoCase(const AsciiString& stringSrc) const { return startsWithNoCase(stringSrc.str()); }

	/**
		return true iff self ends with the given string.
	*/
	Bool endsWith(const char* p) const;
	Bool endsWith(const AsciiString& stringSrc) const { return endsWith(stringSrc.str()); }

	/**
		return true iff self ends with the given string. (case insensitive)
	*/
	Bool endsWithNoCase(const char* p) const;
	Bool endsWithNoCase(const AsciiString& stringSrc) const { return endsWithNoCase(stringSrc.str()); }

	/**
		conceptually similar to strtok():

		extract the next seps-delimited token from the front
		of 'this' and copy it into 'token', returning true if a nonempty
		token was found. (note that this modifies 'this' as well, stripping
		the token off!)
	*/
	Bool nextToken(AsciiString* token, const char* seps = NULL);

	/**
		return true iff the string is "NONE" (case-insensitive).
		Hey, hokey, but we use it a ton.
	*/
	Bool isNone() const;

	Bool isNotEmpty() const { return !isEmpty(); }
	Bool isNotNone() const { return !isNone(); }

//
// You might think it would be a good idea to overload the * operator
// to allow for an implicit conversion to an char*. This is
// (in theory) a good idea, but in practice, there's lots of code
// that assumes it should check text fields for null, which
// is meaningless for us, since we never return a null ptr.
//
//	operator const char*() const { return str(); }
//

	AsciiString& operator=(const AsciiString& stringSrc);	///< the same as set()
	AsciiString& operator=(const char* s);				///< the same as set()

	void debugIgnoreLeaks();

};

// -----------------------------------------------------
inline char* AsciiString::peek() const
{
	DEBUG_ASSERTCRASH(m_data, ("null string ptr"));
	validate();
	return m_data->peek();
}

// -----------------------------------------------------
inline AsciiString::AsciiString() : m_data(0)
{
	validate();
}

// -----------------------------------------------------
inline AsciiString::AsciiString(const char* s) : m_data(0)
{
	//DEBUG_ASSERTCRASH(isMemoryManagerOfficiallyInited(), ("Initializing AsciiStrings prior to main (ie, as static vars) can cause memory leak reporting problems. Are you sure you want to do this?\n"));
	int len = (s)?strlen(s):0;
	if (len)
	{
		ensureUniqueBufferOfSize(len + 1, false, s, NULL);
	}
	validate();
}

// -----------------------------------------------------
inline AsciiString::AsciiString(const AsciiString& stringSrc) : m_data(stringSrc.m_data)
{
	if (m_data)
		/* relaxed: the caller already holds a reference to stringSrc, so the buffer cannot be
			 destroyed underneath this and no memory is being published by the increment.  This is
			 the same ordering every standard library uses for a shared_ptr's use count.  The
			 decrement is the one that needs ordering - see releaseBuffer. */
		m_data->m_refCount.fetch_add(1, std::memory_order_relaxed);
	validate();
}

// -----------------------------------------------------
inline void AsciiString::releaseBuffer()
{
	validate();
	if (m_data)
	{
		/* acq_rel, and the result is used rather than re-read.
		
			 The release half publishes this thread's writes to the buffer before anyone can free it;
			 the acquire half means the thread that observes the last reference going away sees every
			 other thread's writes before it runs freeBytes().  Without it the buffer can be handed
			 back to the allocator while another core still has stores to it in flight.
		
			 Using fetch_sub's return value is not a style preference either.  The old code decremented
			 and then read m_refCount back as a separate, unsynchronised load, so two threads dropping
			 the last two references could both observe zero and both call freeBytes() - a double free.
			 fetch_sub returns the value from before the subtraction, so exactly one caller sees 1. */
		if (m_data->m_refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
			freeBytes();
		m_data = 0;
	}
	validate();
}

// -----------------------------------------------------
inline AsciiString::~AsciiString()
{
	validate();
	releaseBuffer();
}

// -----------------------------------------------------
inline int AsciiString::getLength() const
{
	validate();
	return m_data ? strlen(peek()) : 0;
}

// -----------------------------------------------------
inline Bool AsciiString::isEmpty() const
{
	validate();
	return m_data == NULL || peek()[0] == 0;
}

// -----------------------------------------------------
inline void AsciiString::clear()
{
	validate();
	releaseBuffer();
	validate();
}

// -----------------------------------------------------
inline char AsciiString::getCharAt(int index) const
{
	DEBUG_ASSERTCRASH(index >= 0 && index < getLength(), ("bad index in getCharAt"));
	validate();
	return m_data ? peek()[index] : 0;
}

// -----------------------------------------------------
inline const char* AsciiString::str() const
{
	validate();
	static const char TheNullChr = 0;
	return m_data ? peek() : &TheNullChr;
}

// -----------------------------------------------------
inline void AsciiString::set(const AsciiString& stringSrc)
{
	validate();
	if (&stringSrc != this)
	{
		// This still does not call releaseBuffer().  The reason in the original comment was that
		// the FastCriticalSectionClass lock above it was not recursive; that lock is gone and the
		// reason is now simply that releaseBuffer() would also clear m_data, which is about to be
		// reassigned.  Same orderings as releaseBuffer and the copy constructor; see those.
		if (m_data)
		{
			if (m_data->m_refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
				freeBytes();
		}

		m_data = stringSrc.m_data;
		if (m_data)
			m_data->m_refCount.fetch_add(1, std::memory_order_relaxed);
	}
	validate();
}

// -----------------------------------------------------
inline void AsciiString::set(const char* s)
{
	validate();
	if (!m_data || s != peek())
	{
		int len = s ? strlen(s) : 0;
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
inline AsciiString& AsciiString::operator=(const AsciiString& stringSrc)
{
	validate();
	set(stringSrc);
	validate();
	return *this;
}

// -----------------------------------------------------
inline AsciiString& AsciiString::operator=(const char* s)
{
	validate();
	set(s);
	validate();
	return *this;
}

// -----------------------------------------------------
inline void AsciiString::concat(const char* s)
{
	validate();
	int addlen = strlen(s);
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
inline void AsciiString::concat(const AsciiString& stringSrc)
{
	validate();
	concat(stringSrc.str());
	validate();
}

// -----------------------------------------------------
inline void AsciiString::concat(const char c)
{
	validate();
	/// this can probably be made more efficient, if necessary
	char tmp[2] = { c, 0 };
	concat(tmp);
	validate();
}

// -----------------------------------------------------
inline int AsciiString::compare(const AsciiString& stringSrc) const
{
	validate();
	return strcmp(this->str(), stringSrc.str());
}

// -----------------------------------------------------
inline int AsciiString::compare(const char* s) const
{
	validate();
	return strcmp(this->str(), s);
}

// -----------------------------------------------------
inline int AsciiString::compareNoCase(const AsciiString& stringSrc) const
{
	validate();
	return strcasecmp(this->str(), stringSrc.str());
}

// -----------------------------------------------------
inline int AsciiString::compareNoCase(const char* s) const
{
	validate();
	return strcasecmp(this->str(), s);
}

// -----------------------------------------------------
inline const char* AsciiString::find(char c) const
{
	return strchr(this->str(), c);
}

// -----------------------------------------------------
inline const char* AsciiString::reverseFind(char c) const
{
	return strrchr(this->str(), c);
}

// -----------------------------------------------------
inline Bool operator==(const AsciiString& s1, const AsciiString& s2)
{
	return strcmp(s1.str(), s2.str()) == 0;
}

// -----------------------------------------------------
inline Bool operator!=(const AsciiString& s1, const AsciiString& s2)
{
	return strcmp(s1.str(), s2.str()) != 0;
}

// -----------------------------------------------------
inline Bool operator<(const AsciiString& s1, const AsciiString& s2)
{
	return strcmp(s1.str(), s2.str()) < 0;
}

// -----------------------------------------------------
inline Bool operator<=(const AsciiString& s1, const AsciiString& s2)
{
	return strcmp(s1.str(), s2.str()) <= 0;
}

// -----------------------------------------------------
inline Bool operator>(const AsciiString& s1, const AsciiString& s2)
{
	return strcmp(s1.str(), s2.str()) > 0;
}

// -----------------------------------------------------
inline Bool operator>=(const AsciiString& s1, const AsciiString& s2)
{
	return strcmp(s1.str(), s2.str()) >= 0;
}

// -----------------------------------------------------
inline Bool operator==(const AsciiString& s1, const char* s2)
{
	return strcmp(s1.str(), s2) == 0;
}

// -----------------------------------------------------
inline Bool operator!=(const AsciiString& s1, const char* s2)
{
	return strcmp(s1.str(), s2) != 0;
}

// -----------------------------------------------------
inline Bool operator<(const AsciiString& s1, const char* s2)
{
	return strcmp(s1.str(), s2) < 0;
}

// -----------------------------------------------------
inline Bool operator<=(const AsciiString& s1, const char* s2)
{
	return strcmp(s1.str(), s2) <= 0;
}

// -----------------------------------------------------
inline Bool operator>(const AsciiString& s1, const char* s2)
{
	return strcmp(s1.str(), s2) > 0;
}

// -----------------------------------------------------
inline Bool operator>=(const AsciiString& s1, const char* s2)
{
	return strcmp(s1.str(), s2) >= 0;
}

#endif // ASCIISTRING_H
