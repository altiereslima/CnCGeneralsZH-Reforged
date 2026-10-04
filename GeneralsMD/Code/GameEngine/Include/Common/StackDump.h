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
// Modified 2025-2026 by Olcay Seygan for Zero Hour Reforged; see the git history.
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __STACKDUMP_H_
#define __STACKDUMP_H_

#ifndef IG_DEGBUG_STACKTRACE
#define IG_DEBUG_STACKTRACE	1
#endif // Unsure about this one -ML 3/25/03
#if defined(_DEBUG) || defined(_INTERNAL) || defined(IG_DEBUG_STACKTRACE)

// Writes a stackdump (provide a callback : gets called per line)
// If callback is NULL then will write using OuputDebugString
void StackDump(void (*callback)(const char*));

#if defined(_WIN32)
// Writes a stackdump (provide a callback : gets called per line)
// If callback is NULL then will write using OuputDebugString
// The three are the instruction, stack and frame pointers of the context to walk, pointer-wide so
// the same call carries an x64 Rip/Rsp/Rbp.
void StackDumpFromContext(DWORD_PTR eip,DWORD_PTR esp,DWORD_PTR ebp, void (*callback)(const char*));
#endif

// Gets count* addresses from the current stack
void FillStackAddresses(void**addresses, unsigned int count, unsigned int skip = 0);

// Do full stack dump using an address array
void StackDumpFromAddresses(void**addresses, unsigned int count, void (*callback)(const char*));

void GetFunctionDetails(void *pointer, char*name, size_t nameSize, char*filename, size_t filenameSize, unsigned int* linenumber, unsigned int* address);

#if defined(_WIN32)
// Dumps out the exception info and stack trace.  Windows only: EXCEPTION_POINTERS is a structured
// exception's, and C5 owns what a crash reports elsewhere.  StackDumpPosix.cpp is the other body.
void DumpExceptionInfo( unsigned int u, EXCEPTION_POINTERS* e_info );
#endif

#else

__inline void StackDump(void (*callback)(const char*)) {};

// Gets count* addresses from the current stack
__inline void FillStackAddresses(void**addresses, unsigned int count, unsigned int skip = 0) {}

// Do full stack dump using an address array
__inline void StackDumpFromAddresses(void**addresses, unsigned int count, void (*callback)(const char*)) {}

__inline void GetFunctionDetails(void *pointer, char*name, size_t nameSize, char*filename, size_t filenameSize, unsigned int* linenumber, unsigned int* address) {}

#if defined(_WIN32)
// Dumps out the exception info and stack trace.
__inline void DumpExceptionInfo( unsigned int u, EXCEPTION_POINTERS* e_info ) {};
#endif

#endif

/* Hooks this thread's structured exceptions to DumpExceptionInfo, so that a crash in it leaves a stack
	 dump: _set_se_translator on Windows, as each GameSpy thread always called it first thing.  Elsewhere
	 a crash is a signal, whose handler is process-wide (Common/CrashHandler.h); what a thread needs of
	 its own is an alternate stack, so that a stack overflow in it is reported too. */
#if !defined(_WIN32)
#include "Common/CrashHandler.h"
#endif
inline void InstallThreadExceptionTranslator( void )
{
#if defined(_WIN32)
	_set_se_translator( DumpExceptionInfo );
#else
	installThreadCrashStack();
#endif
}

extern AsciiString g_LastErrorDump;
#endif // __STACKDUMP_H_
