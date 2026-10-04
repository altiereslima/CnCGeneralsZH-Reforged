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

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : WWSaveLoad                                                   *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/wwsaveload/persistfactory.h                  $*
 *                                                                                             *
 *                       Author:: Greg Hjelstrom                                               *
 *                                                                                             *
 *                     $Modtime:: 5/04/01 8:42p                                               $*
 *                                                                                             *
 *                    $Revision:: 11                                                          $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */


#if defined(_MSC_VER)
#pragma once
#endif




#ifndef PERSISTFACTORY_H
#define PERSISTFACTORY_H

#include "always.h"
#include "bittype.h"
#include "chunkio.h"
#include "wwdebug.h"
#include "saveload.h"
/*
**	The whole class, not a forward declaration.  SimplePersistFactoryClass::Save calls obj->Save()
**	on a PersistClass *, which does not depend on T or CHUNKID, so standard two-phase lookup checks
**	it where the template is defined and needs PersistClass complete there.  MSVC's /permissive
**	defers the check to instantiation, which is why this compiled there.  persist.h includes only
**	always.h, refcount.h and postloadable.h, and defines nothing but its include guard.
*/
#include "persist.h"

class PersistClass;

/*
** PersistFactoryClass
** Create a PersistFactoryClass for each concrete derived PersistClass.  These
** factories automatically register with the SaveLoadSystem in their constructors
** and should be accessible through the virtual Get_Factory method of any
** derived PersistClass.
*/

class PersistFactoryClass
{
public:

	PersistFactoryClass(void);
	virtual ~PersistFactoryClass(void);

	virtual uint32				Chunk_ID(void) const												= 0;
	virtual PersistClass *	Load(ChunkLoadClass & cload) const	 						= 0;
	virtual void				Save(ChunkSaveClass & csave,PersistClass * obj)	const	= 0;

private:

	PersistFactoryClass * NextFactory;
	friend class SaveLoadSystemClass;
};




/*
** SimplePersistFactoryClass
** This template automates the creation of a PersistFactory for any type of Persist
** object.  Simply instantiate a single static instance of this template with the
** type and chunkid in the .cpp file of your class.
*/
template <class T,int CHUNKID> class SimplePersistFactoryClass : public PersistFactoryClass
{
public:

	virtual uint32				Chunk_ID(void) const										{ return CHUNKID; }
	virtual PersistClass *	Load(ChunkLoadClass & cload) const;
	virtual void				Save(ChunkSaveClass & csave,PersistClass * obj) const;

	/*
	** Internal chunk id's
	*/
	enum 
	{
		SIMPLEFACTORY_CHUNKID_OBJPOINTER		=	 0x00100100,
		SIMPLEFACTORY_CHUNKID_OBJDATA
	};
};


template<class T, int CHUNKID> PersistClass * 
SimplePersistFactoryClass<T,CHUNKID>::Load(ChunkLoadClass & cload) const 
{
	T * new_obj = W3DNEW T;
	T * old_obj = NULL;

	cload.Open_Chunk();
	WWASSERT(cload.Cur_Chunk_ID() == SIMPLEFACTORY_CHUNKID_OBJPOINTER);
	cload.Read(&old_obj,sizeof(T *));
	cload.Close_Chunk();

	cload.Open_Chunk();
	WWASSERT(cload.Cur_Chunk_ID() == SIMPLEFACTORY_CHUNKID_OBJDATA);
	new_obj->Load(cload);
	cload.Close_Chunk();

	SaveLoadSystemClass::Register_Pointer(old_obj,new_obj);
	return new_obj;
}


template<class T, int CHUNKID> void
SimplePersistFactoryClass<T,CHUNKID>::Save(ChunkSaveClass & csave,PersistClass * obj) const 
{
	/* The object's address, written as an identity token: Load() reads it back and hands it to
	** SaveLoadSystemClass::Register_Pointer so that pointers held by other objects in the same
	** stream can be remapped onto the newly constructed instance.  It is only ever a key into
	** PointerRemapClass's table - never dereferenced - but it has to be the WHOLE address.
	**
	** This used to be `uint32 objptr = (uint32)obj` with a matching `sizeof(uint32)`.  That was
	** right on Win32, where a pointer is four bytes.  It has been wrong since the x64 port, and
	** not subtly: Load() has always read `sizeof(T *)`, so on a 64-bit build Save writes 4 bytes
	** into the chunk and Load asks for 8.  ChunkLoadClass::Read refuses a read that would run past
	** the end of the chunk and returns 0 WITHOUT touching the buffer, so old_obj keeps its NULL
	** initialiser and every object in the stream registers under the key NULL.  Measured, not
	** reasoned: with the real ChunkSaveClass and ChunkLoadClass, Read returns 0 and old_obj is
	** NULL.  The remapping is not degraded on x64, it is entirely absent.
	**
	** Writing the pointer at its own width also puts this in line with how the rest of the engine
	** stores the same token - AudibleSoundClass saves VARID_THIS_PTR with WRITE_MICRO_CHUNK, which
	** is sizeof(the pointer variable), and reads it back with sizeof(old_ptr).  This template was
	** the only place that narrowed it.
	**
	** `obj` is a PersistClass *, and that is deliberate: Win32 wrote the bits of the PersistClass
	** subobject pointer, so keying on it preserves the behaviour that build had.  Under multiple
	** inheritance that address can differ from the T * Load() reads it into, which was equally
	** true before this change and is not introduced by it. */
	PersistClass * objptr = obj;
	csave.Begin_Chunk(SIMPLEFACTORY_CHUNKID_OBJPOINTER);
	csave.Write(&objptr,sizeof(objptr));
	csave.End_Chunk();

	csave.Begin_Chunk(SIMPLEFACTORY_CHUNKID_OBJDATA);
	obj->Save(csave);
	csave.End_Chunk();
}


#endif
