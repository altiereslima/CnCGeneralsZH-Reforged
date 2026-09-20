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
// $File: //depot/GeneralsMD/Staging/code/Libraries/Source/debug/debug_stack.cpp $
// $Author: KMorness $
// $Revision: #2 $
// $DateTime: 2005/01/19 15:02:33 $
//
// �2003 Electronic Arts
//
// Stack walker
//////////////////////////////////////////////////////////////////////////////
#include "_pch.h"
#include "dbghelp.h"
// x64 dbghelp.h maps StackWalk onto StackWalk64, which renames DebugStackwalk's own method too.
#undef StackWalk

// Definitions to allow run-time linking to the dbghelp.dll functions.

#define DBGHELP(name,ret,par) typedef ret (WINAPI *name##Type) par;
#include "debug_stack.inl"
#undef DBGHELP

#define DBGHELP(name,ret,par) name##Type _##name;
static union 
{
  struct  
  {
#include "debug_stack.inl"
  };
  // The union walks the struct above as a plain array of function pointers, so the array's element
  // has to be pointer sized: as "unsigned" it wrote half of each address on x64 and the first call
  // through one of them faulted.
  FARPROC funcPtr[1];
} gDbg;
#undef DBGHELP

#define DBGHELP(name,ret,par) #name,
static char const *DebughelpFunctionNames[] =
{
#include "debug_stack.inl"
	NULL
};
#undef DBGHELP

// local dbghelp.dll module handle
static HMODULE g_dbghelp;

// local flag that is true if we're using an old dbghelp.dll version
static bool g_oldDbghelp;

static void InitDbghelp(void)
{
  // already called?
  if (g_dbghelp)
    return;

	// firstly check for dbghelp.dll in the EXE directory
	char dbgHelpPath[256];
	if (GetModuleFileName(NULL,dbgHelpPath,sizeof(dbgHelpPath)))
	{
		char *slash=strrchr(dbgHelpPath,'\\');
		if (slash)
		{
			strcpy(slash+1,"DBGHELP.DLL");
			g_dbghelp=::LoadLibrary(dbgHelpPath);
		}
	}
	if (!g_dbghelp)
		// load any version we can
		g_dbghelp=::LoadLibrary("DBGHELP.DLL");
  
  if (!g_dbghelp)
    return;

  // Get function addresses
  FARPROC *funcptr=gDbg.funcPtr;
  unsigned k;
  for (k=0;DebughelpFunctionNames[k];++k,++funcptr)
  {
    *funcptr=GetProcAddress(g_dbghelp,DebughelpFunctionNames[k]);
    if (!*funcptr)
      break;
  }
  if (DebughelpFunctionNames[k])
  {
    // not all functions found -> clear them all
    while (funcptr!=gDbg.funcPtr)
      *--funcptr=NULL;
  }
  else
  {
    // Set options
    gDbg._SymSetOptions(gDbg._SymGetOptions()|SYMOPT_DEFERRED_LOADS|SYMOPT_LOAD_LINES);

    // Init module.  This used to pass (HANDLE)GetCurrentProcessId() here and in
    // every Sym* call below, while StackWalk got GetCurrentProcess() -- dbghelp
    // keys its per-process state by whatever it is handed, so the walk ran with
    // callbacks that had no symbol context, returned the frame it was seeded with
    // and stopped: every crash report in this port read "1 addresses:".  The real
    // handle is what dbghelp documents and what GameEngine's StackDump.cpp already
    // uses, so the two agree now.  Re-initializing an already-initialized handle
    // just fails harmlessly.
    gDbg._SymInitialize(GetCurrentProcess(),NULL,TRUE);

    // Check: are we using a newer version of dbghelp.dll?
    // (older versions have some serious issues.. err... bugs)
    if (!GetProcAddress(g_dbghelp,"SymEnumSymbolsForAddr"))
      g_oldDbghelp=true;
  }
}

//////////////////////////////////////////////////////////////////////////////

DebugStackwalk::Signature::Signature(const Signature &src)
{
  *this=src;
}

DebugStackwalk::Signature& DebugStackwalk::Signature::operator=(const Signature& src)
{
  if (&src!=this)
  {
    m_numAddr=src.m_numAddr;
    memcpy(m_addr,src.m_addr,m_numAddr*sizeof(*m_addr));
  }
  return *this;
}

size_t DebugStackwalk::Signature::GetAddress(int n) const
{
  DFAIL_IF_MSG(n<0||n>=MAX_ADDR,n << "/" << MAX_ADDR) return 0;
  return m_addr[n];
}

void DebugStackwalk::Signature::GetSymbol(size_t addr, char *buf, unsigned bufSize)
{
  DFAIL_IF(!buf) return;
  DFAIL_IF(bufSize<64||bufSize>=0x80000000) return;

  InitDbghelp();

  char *bufEnd=buf+bufSize;
  *buf=0;
  buf+=wsprintf(buf,"%p",(void *)addr);

  // determine module
  DWORD64 modBase=gDbg._SymGetModuleBase64(GetCurrentProcess(),addr);
  if (!modBase)
	{
		strcpy(buf," (unknown module)");
    return;
	}

  // illegal code ptr?
	if (IsBadReadPtr((void *)addr,4)||IsBadCodePtr((FARPROC)addr))
	{
		strcpy(buf," (invalid code addr)");
		return;
	}

  char symbolBuffer[512];
  GetModuleFileName((HMODULE)(size_t)modBase,symbolBuffer,sizeof(symbolBuffer));

  char *p=strrchr(symbolBuffer,'\\'); // use filename only, strip off path
  p=p?p+1:symbolBuffer;
  *buf++=' ';
  strcpy(buf,p);
  buf+=strlen(buf);
  if (bufEnd-buf<32)
    return;
  buf+=wsprintf(buf,"+0x%x",(unsigned)(addr-(size_t)modBase));

  // determine symbol
  PIMAGEHLP_SYMBOL64 symPtr=(PIMAGEHLP_SYMBOL64)symbolBuffer;
  memset(symPtr,0,sizeof(symbolBuffer));
  symPtr->SizeOfStruct=sizeof(IMAGEHLP_SYMBOL64);
  symPtr->MaxNameLength=sizeof(symbolBuffer)-sizeof(IMAGEHLP_SYMBOL64);
  DWORD64 displacement;
  if (!gDbg._SymGetSymFromAddr64(GetCurrentProcess(),addr,&displacement,symPtr))
    return;
  if ((unsigned int)(bufEnd-buf)<strlen(symPtr->Name)+16)
    return;
  buf+=wsprintf(buf,", %s+0x%x",symPtr->Name,(unsigned)displacement);

  // and line number
  IMAGEHLP_LINE64 line;
  memset(&line,0,sizeof(line));
  line.SizeOfStruct=sizeof(line);
  DWORD lineDisplacement;
  if (!gDbg._SymGetLineFromAddr64(GetCurrentProcess(),addr,&lineDisplacement,&line))
    return;

  p=strrchr(line.FileName,'\\'); // use filename only, strip off path
  p=p?p+1:line.FileName;

  if ((unsigned int)(bufEnd-buf)<strlen(p)+16)
    return;
  buf+=wsprintf(buf,", %s:%i+0x%x",p,line.LineNumber,lineDisplacement);
}

void DebugStackwalk::Signature::GetSymbol(size_t addr,
                                          char *bufMod, unsigned sizeMod, unsigned *relMod,
                                          char *bufSym, unsigned sizeSym, unsigned *relSym,
                                          char *bufFile, unsigned sizeFile, unsigned *linePtr, unsigned *relLine)
{
  InitDbghelp();

  if (bufMod) *bufMod=0;
  if (relMod) *relMod=0;
  if (bufSym) *bufSym=0;
  if (relSym) *relSym=0;
  
  if (bufFile) *bufFile=0;
  if (linePtr) *linePtr=0;
  if (relLine) *relLine=0;

  DFAIL_IF(bufMod&&sizeMod<16) return;
  DFAIL_IF(bufSym&&sizeSym<16) return;
  DFAIL_IF(bufFile&&sizeFile<16) return;

  // determine module
  DWORD64 modBase=gDbg._SymGetModuleBase64(GetCurrentProcess(),addr);
  if (!modBase)
	{
    if (bufMod)
		  strcpy(bufMod,"(unknown mod)");
    if (bufSym)
      strcpy(bufSym,"(unknown)");
    return;
	}

  // illegal code ptr?
	if (IsBadReadPtr((void *)addr,4)||IsBadCodePtr((FARPROC)addr))
	{
    if (bufMod)
		  strcpy(bufMod,"(inv code addr)");
    if (bufSym)
      strcpy(bufSym,"(unknown)");
		return;
	}

  char symbolBuffer[512];
  if (bufMod)
  {
    GetModuleFileName((HMODULE)(size_t)modBase,symbolBuffer,sizeof(symbolBuffer));

    char *p=strrchr(symbolBuffer,'\\'); // use filename only, strip off path
    p=p?p+1:symbolBuffer;
    strncpy(bufMod,p,sizeMod);
    bufMod[sizeMod-1]=0;
  }
  if (relMod)
    *relMod=(unsigned)(addr-(size_t)modBase);

  // determine symbol
  if (bufSym)
  {
    PIMAGEHLP_SYMBOL64 symPtr=(PIMAGEHLP_SYMBOL64)symbolBuffer;
    memset(symPtr,0,sizeof(symbolBuffer));
    symPtr->SizeOfStruct=sizeof(IMAGEHLP_SYMBOL64);
    symPtr->MaxNameLength=sizeof(symbolBuffer)-sizeof(IMAGEHLP_SYMBOL64);
    DWORD64 displacement;
    if (gDbg._SymGetSymFromAddr64(GetCurrentProcess(),addr,&displacement,symPtr))
    {
      strncpy(bufSym,symPtr->Name,sizeSym);
      bufSym[sizeSym-1]=0;
      if (relSym)
        *relSym=(unsigned)displacement;
    }
    else 
      strcpy(bufSym,"(unknown)");
  }

  // and line number
  if (bufFile)
  {
    IMAGEHLP_LINE64 line;
    memset(&line,0,sizeof(line));
    line.SizeOfStruct=sizeof(line);
    DWORD displacement;
    if (!gDbg._SymGetLineFromAddr64(GetCurrentProcess(),addr,&displacement,&line))
      strcpy(bufFile,"(unknown)");
    else
    {
      char *p=strrchr(line.FileName,'\\'); // use filename only, strip off path
      p=p?p+1:line.FileName;
      strncpy(bufFile,p,sizeFile);
      bufFile[sizeFile-1]=0;
      if (linePtr)
        *linePtr=line.LineNumber;
      if (relLine)
        *relLine=displacement;
    }
  }
}

Debug& operator<<(Debug &dbg, const DebugStackwalk::Signature &sig)
{
  dbg << sig.Size() << " addresses:\n";

  for (unsigned k=0;k<sig.Size();k++)
  {
    char buf[512];
    sig.GetSymbol(sig.GetAddress(k),buf,sizeof(buf));
    dbg << buf << "\n";
  }

  return dbg;
}

//////////////////////////////////////////////////////////////////////////////

DebugStackwalk::DebugStackwalk(void)
{
  // it doesn't harm to do this here
  InitDbghelp();
}

DebugStackwalk::~DebugStackwalk()
{
}

void *DebugStackwalk::GetDbghelpHandle(void)
{
  return g_dbghelp;
}

bool DebugStackwalk::IsOldDbghelp(void)
{
  return g_oldDbghelp;
}

int DebugStackwalk::StackWalk(Signature &sig, struct _CONTEXT *ctx)
{
  InitDbghelp();

  sig.m_numAddr=0;

  // bail out if no stack walk available
  if (!gDbg._StackWalk64)
    return 0;

  // The unwind comes from the exception tables, which means StackWalk64 needs a whole register
  // set: seeding three addresses and passing no context walks nowhere.
  CONTEXT walkContext;
  if (ctx)
  {
    walkContext=*ctx;
  }
  else
  {
    memset(&walkContext,0,sizeof(walkContext));
    walkContext.ContextFlags=CONTEXT_FULL;
    RtlCaptureContext(&walkContext);
  }

  STACKFRAME64 frame;
  memset(&frame,0,sizeof(frame));
  frame.AddrPC.Offset=walkContext.Rip;
  frame.AddrPC.Mode=AddrModeFlat;
  frame.AddrStack.Offset=walkContext.Rsp;
  frame.AddrStack.Mode=AddrModeFlat;
  frame.AddrFrame.Offset=walkContext.Rbp;
  frame.AddrFrame.Mode=AddrModeFlat;

  // The frame this function is standing in is not worth reporting when the caller did not hand
  // over a context of its own.
  bool skipFirst=!ctx;
  while (sig.m_numAddr<Signature::MAX_ADDR&&
         gDbg._StackWalk64(IMAGE_FILE_MACHINE_AMD64,GetCurrentProcess(),GetCurrentThread(),
                           &frame,&walkContext,NULL,gDbg._SymFunctionTableAccess64,
                           gDbg._SymGetModuleBase64,NULL))
  {
    if (skipFirst)
      skipFirst=false;
    else
      sig.m_addr[sig.m_numAddr++]=(size_t)frame.AddrPC.Offset;
  }

  return sig.m_numAddr;
}
