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

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Command & Conquer                                            *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/wwlib/Except.cpp                             $*
 *                                                                                             *
 *                      $Author:: Steve_t                                                     $*
 *                                                                                             *
 *                     $Modtime:: 2/07/02 12:28p                                              $*
 *                                                                                             *
 *                    $Revision:: 14                                                          $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *                                                                                             *
 * Exception_Proc -- Windows dialog callback for the exception dialog                          *
 * Exception_Dialog -- Brings up the exception options dialog.                                 *
 * Add_Txt -- Add the given text to the machine state dump buffer.                             *
 * Dump_Exception_Info -- Dump machine state information into a buffer                         *
 * Exception_Handler -- Exception handler filter function                                      *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#ifdef _MSC_VER

#include	"always.h"
#include <windows.h>
#include	"assert.h"
#include "stringex.h"
#include "cpudetect.h"
#include	"except.h"
//#include "debug.h"
#include "mpu.h"
//#include "commando\nat.h"
#include "thread.h"
#include "wwdebug.h"
#include "wwmemlog.h"

#include	<conio.h>
#include	<imagehlp.h>
#include <crtdbg.h>
#include	<stdio.h>

#ifdef WWDEBUG
#define DebugString 	WWDebug_Printf
#else
void DebugString(char const *, ...){};
#endif //WWDEBUG

/*
** Enable this define to get the 'demo timed out' message on a crash or assert failure.
*/
//#define DEMO_TIME_OUT

/*
** Buffer to dump machine state information to. We don't want to allocate this at run-time
** in case the exception was caused by a malfunction in the memory system.
*/
static char ExceptionText [65536];

bool SymbolsAvailable = false;
HINSTANCE ImageHelp = (HINSTANCE) -1;

void (*AppCallback)(void) = NULL;
char *(*AppVersionCallback)(void) = NULL;

/*
** Flag to indicate we should exit when an exception occurs.
*/
bool ExitOnException = false;
bool TryingToExit = false;

/*
** Register dump variables. These are used to allow the game to restart from an arbitrary
** position after an exception occurs.
*/
unsigned long ExceptionReturnStack = 0;
unsigned long ExceptionReturnAddress = 0;
unsigned long ExceptionReturnFrame = 0;

/*
** Number of times the exception handler has recursed. Recursions are bad.
*/
int ExceptionRecursions = -1;

/*
** List of threads that the exception handler knows about.
*/
DynamicVectorClass<ThreadInfoType*> ThreadList;

/*
** Definitions to allow run-time linking to the Imagehlp.dll functions.
**
*/
typedef BOOL  (WINAPI *SymCleanupType) (HANDLE hProcess);
typedef BOOL  (WINAPI *SymGetSymFromAddrType) (HANDLE hProcess, DWORD Address, LPDWORD Displacement, PIMAGEHLP_SYMBOL Symbol);
typedef BOOL  (WINAPI *SymInitializeType) (HANDLE hProcess, LPSTR UserSearchPath, BOOL fInvadeProcess);
typedef BOOL  (WINAPI *SymLoadModuleType) (HANDLE hProcess, HANDLE hFile, LPSTR ImageName, LPSTR ModuleName, DWORD BaseOfDll, DWORD SizeOfDll);
typedef DWORD (WINAPI *SymSetOptionsType) (DWORD SymOptions);
typedef BOOL  (WINAPI *SymUnloadModuleType) (HANDLE hProcess, DWORD BaseOfDll);
typedef BOOL  (WINAPI *StackWalkType) (DWORD MachineType, HANDLE hProcess, HANDLE hThread, LPSTACKFRAME StackFrame, LPVOID ContextRecord, PREAD_PROCESS_MEMORY_ROUTINE ReadMemoryRoutine, PFUNCTION_TABLE_ACCESS_ROUTINE FunctionTableAccessRoutine, PGET_MODULE_BASE_ROUTINE GetModuleBaseRoutine, PTRANSLATE_ADDRESS_ROUTINE TranslateAddress);
typedef LPVOID (WINAPI *SymFunctionTableAccessType) (HANDLE hProcess, DWORD AddrBase);
typedef DWORD (WINAPI *SymGetModuleBaseType) (HANDLE hProcess, DWORD dwAddr);


static SymCleanupType							_SymCleanup = NULL;
static SymGetSymFromAddrType				_SymGetSymFromAddr = NULL;
static SymInitializeType						_SymInitialize = NULL;
static SymLoadModuleType						_SymLoadModule = NULL;
static SymSetOptionsType						_SymSetOptions = NULL;
static SymUnloadModuleType					_SymUnloadModule = NULL;
static StackWalkType								_StackWalk = NULL;
static SymFunctionTableAccessType	_SymFunctionTableAccess = NULL;
static SymGetModuleBaseType				_SymGetModuleBase = NULL;

static char const *ImagehelpFunctionNames[] =
{
	"SymCleanup",
	"SymGetSymFromAddr",
	"SymInitialize",
	"SymLoadModule",
	"SymSetOptions",
	"SymUnloadModule",
	"StackWalk",
	"SymFunctionTableAccess",
	"SymGetModuleBaseType",
	NULL
};



/***********************************************************************************************
 * _purecall -- This function overrides the C library Pure Virtual Function Call error         *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    Nothing                                                                           *
 *                                                                                             *
 * OUTPUT:   0 = no error                                                                      *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   8/22/00 11:42AM ST : Created                                                              *
 *=============================================================================================*/
// The debug library replaces _purecall too (debug_purecall.cpp, which reports the
// call through DCRASH_RELEASE instead of an int 3), and the exe links both libs -
// two definitions of a CRT symbol is a hard link error.  The debug one wins: it
// says something in a Release build, this one only breaks into a debugger that is
// not attached.
#if 0
int __cdecl _purecall(void)
{
	int return_code = 0;

#ifdef WWDEBUG
	/*
	** Use int3 to cause an exception.
	*/
	WWDEBUG_SAY(("Pure Virtual Function call. Oh No!\n"));
	__debugbreak();
#endif	//_DEBUG_ASSERT

	return(return_code);
}
#endif



/***********************************************************************************************
 * Last_Error_Text -- Get the system error text for GetLastError                                *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    Nothing                                                                           *
 *                                                                                             *
 * OUTPUT:   Ptr to error string                                                               *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   8/14/98 11:11AM ST : Created                                                              *
 *=============================================================================================*/
char const * Last_Error_Text(void)
{
	static char message_buffer[256];
	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), &message_buffer[0], 256, NULL);
	return (message_buffer);
}



/***********************************************************************************************
 * Add_Txt -- Add the given text to the machine state dump buffer.                             *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    Text                                                                              *
 *                                                                                             *
 * OUTPUT:   Nothing                                                                           *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *    7/22/97 12:21PM ST : Created                                                             *
 *=============================================================================================*/
static void Add_Txt (char const *txt)
{
	// The guard was right, but it carried the buffer's size as a literal beside the buffer.
	strlcat(ExceptionText, txt, ARRAY_SIZE(ExceptionText));
#if (0)
	/*
	** Log to debug output too.
	*/
	static char _debug_output_txt[512];
	const char *in = txt;
	char *out = _debug_output_txt;
	bool done = false;

	if (strlen(txt) < sizeof(_debug_output_txt)) {
		for (int i=0 ; i<sizeof(_debug_output_txt) ; i++) {

			switch (*in) {
				case '\r':
					in++;
					continue;

				case 0:
					done = true;
					// fall through

				default:
					*out++ = *in++;
					break;
			}

			if (done) {
				break;
			}
		}

		DebugString(_debug_output_txt);
	}
#endif //(0)
}



/***********************************************************************************************
 * Dump_Exception_Info -- Dump machine state information into a buffer                         *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    ptr to exception information                                                      *
 *                                                                                             *
 * OUTPUT:   Nothing                                                                           *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *    7/22/97 12:21PM ST : Created                                                             *
 *=============================================================================================*/
void Dump_Exception_Info(EXCEPTION_POINTERS *e_info)
{
	// Westwood's own dump read the x86 CONTEXT register by register and was 32-bit assembly below
	// this line.  Nothing in the game reaches this handler: WinMain installs its own, and
	// StackDump.cpp writes the crash log.
	Add_Txt("Westwood exception dump is not implemented.\r\n");
}





/***********************************************************************************************
 * Exception_Handler -- Exception handler filter function                                      *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    exception code                                                                    *
 *           pointer to exception information pointers                                         *
 *                                                                                             *
 * OUTPUT:   EXCEPTION_EXECUTE_HANDLER -- Excecute the body of the __except construct          *
 *        or EXCEPTION_CONTINUE_SEARCH -- Pass this exception down to the debugger             *
 *        or EXCEPTION_CONTINUE_EXECUTION -- Continue to execute at the fault address          *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *    7/22/97 12:29PM ST : Created                                                              *
 *=============================================================================================*/
int Exception_Handler(int exception_code, EXCEPTION_POINTERS *e_info)
{
	DebugString("Exception!\n");

#ifdef DEMO_TIME_OUT
	if ( !WindowedMode ) {
		Load_Title_Page("TITLE.PCX", true);
		MouseCursor->Release_Mouse();
		MessageBox(MainWindow, "This demo has timed out. Thank you for playing Red Alert 2.","Byeee!", MB_ICONEXCLAMATION|MB_OK);
		return (EXCEPTION_EXECUTE_HANDLER);
	}
#endif	//DEMO_TIME_OUT

	/*
	** If we were trying to quit and we got another exception then just shut down the whole shooting match right here.
	*/
	if (TryingToExit) {
		ExitProcess(0);
	}

	/*
	** Track recursions because we need to know if something here is failing.
	*/
	ExceptionRecursions++;

	if (ExceptionRecursions > 1) {
		return (EXCEPTION_CONTINUE_SEARCH);
	}

	/*
	** If there was a breakpoint then chances are it was set by a debugger. In _DEBUG mode
	** we probably should ignore breakpoints. Breakpoints become more significant in release
	** mode since there probably isn't a debugger present.
	*/
#ifdef _DEBUG
	if (exception_code == EXCEPTION_BREAKPOINT) {
		return (EXCEPTION_CONTINUE_SEARCH);
	}
#else
	exception_code = exception_code;
#endif	//_DEBUG

#ifdef WWDEBUG
	//CONTEXT *context;
#endif WWDEBUG

	if (ExceptionRecursions == 0) {

		/*
		** Create a dump of the exception info.
		*/
		Dump_Exception_Info(e_info);

		/*
		** Log the machine state to disk
		*/
		HANDLE debug_file;
		DWORD	actual;
		debug_file = CreateFile("_except.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (debug_file != INVALID_HANDLE_VALUE){
			WriteFile(debug_file, ExceptionText, strlen(ExceptionText), &actual, NULL);
			CloseHandle (debug_file);

#if (0)
#ifdef _DEBUG_PRINT
#ifndef _DEBUG
			/*
			** Copy the exception debug file to the network. No point in doing this for the debug version
			** since symbols are not normally available.
			*/
			DebugString ("About to copy debug file\n");
			char filename[512];
			if (Get_Global_Output_File_Name ("EXCEPT", filename, 512)) {
				DebugString ("Copying DEBUG.TXT to %s\n", filename);
				int result = CopyFile("debug.txt", filename, false);
				if (result == 0) {
					DebugString ("CopyFile failed with error code %d - %s\n", GetLastError(), Last_Error_Text());
				}
			}
			DebugString ("Debug file copied\n");
#endif	//_DEBUG
#endif	//_DEBUG_PRINT
#endif	//(0)

		}
	}

	/*
	** Call the apps callback function.
	*/
	if (AppCallback) {
		AppCallback();
	}

	/*
	** If an exit is required then turn of memory leak reporting (there will be lots of them) and use
	** EXCEPTION_EXECUTE_HANDLER to let us fall out of winmain.
	*/
	if (ExitOnException) {
#ifdef _DEBUG
		_CrtSetDbgFlag(0);
#endif //_DEBUG
		TryingToExit = true;

		unsigned long id = Get_Main_Thread_ID();
		if (id != GetCurrentThreadId()) {
			DebugString("Exiting due to exception in sub thread\n");
			ExitProcess(EXIT_SUCCESS);
		}

		return(EXCEPTION_EXECUTE_HANDLER);

	}
	return (EXCEPTION_CONTINUE_SEARCH);
}




/***********************************************************************************************
 * Register_Thread_ID -- Let the exception handler know about a thread                         *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    Thread ID                                                                         *
 *           Thread name                                                                       *
 *                                                                                             *
 * OUTPUT:   Nothing                                                                           *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   8/30/2001 3:04PM ST : Created                                                             *
 *=============================================================================================*/
void Register_Thread_ID(unsigned long thread_id, char *thread_name, bool main_thread)
{
	WWMEMLOG(MEM_GAMEDATA);
	if (thread_name) {

		/*
		** See if we already know about this thread. Maybe just the thread_id changed.
		*/
		for (int i=0 ; i<ThreadList.Count() ; i++) {
			if (strcmp(thread_name, ThreadList[i]->ThreadName) == 0) {
				ThreadList[i]->ThreadID = thread_id;
				return;
			}
		}

		ThreadInfoType *thread = new ThreadInfoType;
		thread->ThreadID = thread_id;
		strcpy(thread->ThreadName, thread_name);
		thread->Main = main_thread;
		thread->ThreadHandle = INVALID_HANDLE_VALUE;
		ThreadList.Add(thread);
	}
}


#if (0)
/***********************************************************************************************
 * Register_Thread_Handle -- Keep a copy of the thread handle that matches this thread ID      *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    Thread ID                                                                         *
 *           Thread handle                                                                     *
 *                                                                                             *
 * OUTPUT:   True if thread ID was matched                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   2/6/2002 9:40PM ST : Created                                                              *
 *=============================================================================================*/
bool Register_Thread_Handle(unsigned long thread_id, HANDLE thread_handle)
{
	for (int i=0 ; i<ThreadList.Count() ; i++) {
		if (ThreadList[i]->ThreadID == thread_id) {
			ThreadList[i]->ThreadHandle = thread_handle;
			return(true);
			break;
		}
	}
	return(false);
}



/***********************************************************************************************
 * Get_Num_Threads -- Get the number of threads being tracked.                                 *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    Nothing                                                                           *
 *                                                                                             *
 * OUTPUT:   Number of threads we know about                                                   *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   2/6/2002 9:43PM ST : Created                                                              *
 *=============================================================================================*/
int Get_Num_Threads(void)
{
	return(ThreadList.Count());
}


/***********************************************************************************************
 * Get_Thread_Handle -- Get the handle for the given thread index                              *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    Thread index                                                                      *
 *                                                                                             *
 * OUTPUT:   Thread handle                                                                     *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   2/6/2002 9:46PM ST : Created                                                              *
 *=============================================================================================*/
HANDLE Get_Thread_Handle(int thread_index)
{
	if (thread_index < ThreadList.Count()) {
		return(ThreadList[thread_index]->ThreadHandle);
	}
}
#endif //(0)

/***********************************************************************************************
 * Unregister_Thread_ID -- Remove a thread entry from the thread list                          *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    Thread ID                                                                         *
 *           Thread name                                                                       *
 *                                                                                             *
 * OUTPUT:   Nothing                                                                           *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   8/30/2001 3:10PM ST : Created                                                             *
 *=============================================================================================*/
void Unregister_Thread_ID(unsigned long thread_id, char *thread_name)
{
	for (int i=0 ; i<ThreadList.Count() ; i++) {
		if (strcmp(thread_name, ThreadList[i]->ThreadName) == 0) {
			assert(ThreadList[i]->ThreadID == thread_id);
			delete ThreadList[i];
			ThreadList.Delete(i);
			return;
		}
	}
}



/***********************************************************************************************
 * Get_Main_Thread_ID -- Get the ID of the processes main thread                               *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    Nothing                                                                           *
 *                                                                                             *
 * OUTPUT:   Thread ID                                                                         *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   12/6/2001 12:20PM ST : Created                                                            *
 *=============================================================================================*/
unsigned long Get_Main_Thread_ID(void)
{
	for (int i=0 ; i<ThreadList.Count() ; i++) {
		if (ThreadList[i]->Main) {
			return(ThreadList[i]->ThreadID);
		}
	}
	return(0);
}






/***********************************************************************************************
 * Load_Image_Helper -- Load imagehlp.dll and retrieve the programs symbols                    *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    Nothing                                                                           *
 *                                                                                             *
 * OUTPUT:   Nothing                                                                           *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   6/12/2001 4:27PM ST : Created                                                             *
 *=============================================================================================*/
void Load_Image_Helper(void)
{
	/*
	** If this is the first time through then fix up the imagehelp function pointers since imagehlp.dll
	** can't be statically linked.
	*/
	if (ImageHelp == (HINSTANCE)-1) {
		ImageHelp = LoadLibrary("IMAGEHLP.DLL");

		if (ImageHelp != NULL) {
			char const *function_name = NULL;
			unsigned long *fptr = (unsigned long *) &_SymCleanup;
			int count = 0;

			do {
				function_name = ImagehelpFunctionNames[count];
				if (function_name) {
					*fptr = (unsigned long) GetProcAddress(ImageHelp, function_name);
					fptr++;
					count++;
				}
			}
			while (function_name);
		}

		/*
		** Retrieve the programs symbols if they are available. This can be a .pdb or a .dbg file.
		*/
		if (_SymSetOptions != NULL) {
			_SymSetOptions(SYMOPT_DEFERRED_LOADS);
		}

		int symload = 0;

		if (_SymInitialize != NULL && _SymInitialize(GetCurrentProcess(), NULL, FALSE)) {

			if (_SymSetOptions != NULL) {
				_SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
			}

			char exe_name[_MAX_PATH];
			GetModuleFileName(NULL, exe_name, sizeof(exe_name));

			if (_SymLoadModule != NULL) {
				symload = _SymLoadModule(GetCurrentProcess(), NULL, exe_name, NULL, 0, 0);
			}

			if (symload) {
				SymbolsAvailable = true;
			} else {
				//assert (_SymLoadModule != NULL);
				//DebugString ("SymLoad failed for module %s with code %d - %s\n", szModuleName, GetLastError(), Last_Error_Text());
			}
		}
	}
}







/***********************************************************************************************
 * Lookup_Symbol -- Get the symbol for a given code address                                    *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    Address of code to get symbol for                                                 *
 *           Ptr to buffer to return symbol in                                                 *
 *           Reference to int to return displacement                                           *
 *                                                                                             *
 * OUTPUT:   True if symbol found                                                              *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   6/12/2001 4:47PM ST : Created                                                             *
 *=============================================================================================*/
bool Lookup_Symbol(void *code_ptr, char *symbol, int &displacement)
{
	/*
	** Locals.
	*/
	char symbol_struct_buf[1024];
	IMAGEHLP_SYMBOL *symbol_struct_ptr = (IMAGEHLP_SYMBOL *)symbol_struct_buf;

	/*
	** Set default values in case of early exit.
	*/
	displacement = 0;
	*symbol = '\0';

	/*
	** Make sure symbols are available.
	*/
	if (!SymbolsAvailable || _SymGetSymFromAddr == NULL) {
		return(false);
	}

	/*
	** If it's a bad code pointer then there is no point in trying to match it with a symbol.
	*/
	if (IsBadCodePtr((FARPROC)code_ptr)) {
		strcpy(symbol, "Bad code pointer");
		return(false);
	}

	/*
	** Set up the parameters for the call to SymGetSymFromAddr
	*/
	memset (symbol_struct_ptr, 0, sizeof (symbol_struct_buf));
	symbol_struct_ptr->SizeOfStruct = sizeof (symbol_struct_buf);
	symbol_struct_ptr->MaxNameLength = sizeof(symbol_struct_buf)-sizeof (IMAGEHLP_SYMBOL);
	symbol_struct_ptr->Size = 0;
	symbol_struct_ptr->Address = (unsigned long)code_ptr;

	/*
	** See if we have the symbol for that address.
	*/
	if (_SymGetSymFromAddr(GetCurrentProcess(), (unsigned long)code_ptr, (unsigned long *)&displacement, symbol_struct_ptr)) {

		/*
		** Copy it back into the buffer provided.
		*/
		strcpy(symbol, symbol_struct_ptr->Name);
		return(true);
	}
	return(false);
}




/***********************************************************************************************
 * Stack_Walk -- Walk the stack and get the last n return addresses                            *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:    Ptr to return address list                                                        *
 *           Number of return addresses to fetch                                               *
 *           Ptr to optional context. NULL means use current                                   *
 *                                                                                             *
 * OUTPUT:   Number of return addresses found                                                  *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   6/12/2001 11:57AM ST : Created                                                            *
 *=============================================================================================*/
int Stack_Walk(unsigned long *return_addresses, int num_addresses, CONTEXT *context)
{
	// Westwood's walker was 32-bit assembly seeding an imagehlp STACKFRAME, and its addresses do
	// not fit the caller's unsigned longs any more.  Nothing calls it; StackDump.cpp is the walker.
	return 0;
}



void Register_Application_Exception_Callback(void (*app_callback)(void))
{
	AppCallback = app_callback;
}

void Register_Application_Version_Callback(char *(*app_ver_callback)(void))
{
	AppVersionCallback = app_ver_callback;
}



void Set_Exit_On_Exception(bool set)
{
	ExitOnException = true;
}

bool Is_Trying_To_Exit(void)
{
	return(TryingToExit);
}




#endif	//_MSC_VER




