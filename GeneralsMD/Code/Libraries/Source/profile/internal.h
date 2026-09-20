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

/////////////////////////////////////////////////////////////////////////EA-V1
// $File: //depot/GeneralsMD/Staging/code/Libraries/Source/profile/internal.h $
// $Author: mhoffe $
// $Revision: #3 $
// $DateTime: 2003/07/09 10:57:23 $
//
// �2003 Electronic Arts
//
// Internal header
//////////////////////////////////////////////////////////////////////////////
#ifdef _MSC_VER
#  pragma once
#endif
#ifndef INTERNAL_H // Include guard
#define INTERNAL_H

#include "../debug/debug.h"
#include "internal_funclevel.h"
#include "internal_highlevel.h"
#include "internal_cmd.h"
#include "internal_result.h"

class ProfileFastCS
{
  ProfileFastCS(const ProfileFastCS&);
  ProfileFastCS& operator=(const ProfileFastCS&);

	volatile unsigned m_Flag;
  static HANDLE testEvent;

	void ThreadSafeSetFlag()
	{
		volatile unsigned& nFlag=m_Flag;

		// EA's "lock bts" spin, written with the intrinsic that compiles to the same instruction.
		while (_interlockedbittestandset((volatile long *)&nFlag, 0))
		{
			if (testEvent)
				::WaitForSingleObject(testEvent,1);
		}
		return;
	}

	void ThreadSafeClearFlag()
	{
		m_Flag=0;
	}

public:
	ProfileFastCS(void):
    m_Flag(0) 
  {
  }

	class Lock
	{
    Lock(const Lock&);
    Lock& operator=(const Lock&);

		ProfileFastCS& CriticalSection;

	public:
		Lock(ProfileFastCS& cs): 
      CriticalSection(cs)
		{
			CriticalSection.ThreadSafeSetFlag();
		}

		~Lock()
		{
			CriticalSection.ThreadSafeClearFlag();
		}
	};

	friend class Lock;
};

void *ProfileAllocMemory(unsigned numBytes);
void *ProfileReAllocMemory(void *oldPtr, unsigned newSize);
void ProfileFreeMemory(void *ptr);

__forceinline void ProfileGetTime(__int64 &t)
{
  t = (__int64)__rdtsc();
}

#endif // INTERNAL_H
