/*
**	Copyright 2026 İlyas Akın
**	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
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

/*
 * A3e-asm: Microsoft's own D3DXAssembleShader as the oracle for FFReference's ps.1.1 assembler.
 *
 * Built for x86_64 and run under Rosetta by run_assemble_oracle.sh.  It loads the genuine d3dx9_43.dll
 * and D3DCompiler_43.dll (the June 2010 redistributable every Steam install of the game carries; both
 * sha256-pinned by the script) as PE images: the sections mapped, the base relocations applied, every
 * import bound to a small Microsoft-x64-ABI stub over the C library or, for anything not listed here, to
 * a trap that names the import and stops.  Then both DLLs' own entry points run, and the game's four
 * water programs (read out of W3DWater.cpp) go through d3dx9_43's D3DXAssembleShader exactly as the game
 * calls it on Windows.  It loads D3DCompiler_43 itself (LoadLibraryA, answered with the image loaded
 * here) and forwards to its D3DAssemble.
 *
 * THE ONE DEPARTURE FROM MICROSOFT'S CODE, in each DLL: three C-runtime sites that read the Windows
 * thread block through the gs segment, which on x86_64 macOS holds the pthread slots instead.  They are
 * the stack probe __chkstk (the stack limit at gs:0x10), patched to a plain return since this stack is
 * committed, and the startup lock's two reads of the thread block at gs:0x30, pointed at a small block
 * of our own whose stack-base field is a non-zero id.  Each site's bytes are checked before it is
 * patched.  No assembler code is touched.
 *
 * What it compares, for each program: Microsoft's tokens against FFRef::assemblePixelProgram's, word by
 * word (comment tokens, where Microsoft adds any, are reported and set aside), and Microsoft's tokens
 * through FFRef::decodeProgram.  An armed control flips one word of our tokens, which must be found.
 *
 *   assemble_oracle <d3dx9_43.dll> <D3DCompiler_43.dll> <W3DWater.cpp>    exit 0 when all agree
 */

#if !defined(__x86_64__)
#error "assemble_oracle runs Microsoft's x64 code; build it for x86_64 (run_assemble_oracle.sh)"
#endif

#include "ffreference/ffprogram.h"

#include <ctype.h>
#include <math.h>
#include <mach/mach_time.h>
#include <malloc/malloc.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <map>
#include <string>
#include <vector>

#define MSABI __attribute__((ms_abi))

namespace {

// ---- the images ---------------------------------------------------------------------------------------
struct Module
{
	std::string name;
	uint8_t *base;
	uint32_t size;
	uint32_t entry;
	std::map<std::string, void *> exports;
};
std::vector<Module *> modules;

uint16_t u16( const uint8_t *p ) { uint16_t v; memcpy( &v, p, 2 ); return v; }
uint32_t u32( const uint8_t *p ) { uint32_t v; memcpy( &v, p, 4 ); return v; }
uint64_t u64( const uint8_t *p ) { uint64_t v; memcpy( &v, p, 8 ); return v; }

bool sameName( const std::string &a, const std::string &b ) { return strcasecmp( a.c_str(), b.c_str() ) == 0; }

Module *moduleNamed( const char *name )
{
	for (size_t i = 0; i < modules.size(); ++i)
		if (sameName( modules[i]->name, name ))
			return modules[i];
	return NULL;
}

// ---- the stubs: the Windows functions the two DLLs call, over the C library -------------------------
uint32_t lastError = 0;
void *tls[ 64 ];
unsigned tlsNext = 0;
std::map<void *, size_t> virtualSizes;

MSABI uint32_t k_GetLastError( void ) { return lastError; }
MSABI void k_SetLastError( uint32_t e ) { lastError = e; }
MSABI void *k_GetProcessHeap( void ) { return (void *)0x1000; }
MSABI void *k_HeapCreate( uint32_t, size_t, size_t ) { return (void *)0x1000; }
MSABI int k_HeapDestroy( void * ) { return 1; }
MSABI void *k_HeapAlloc( void *, uint32_t flags, size_t n ) { return (flags & 0x8) ? calloc( 1, n ? n : 1 ) : malloc( n ? n : 1 ); }	// HEAP_ZERO_MEMORY
MSABI void *k_HeapReAlloc( void *, uint32_t, void *p, size_t n ) { return realloc( p, n ); }
MSABI int k_HeapFree( void *, uint32_t, void *p ) { free( p ); return 1; }
MSABI size_t k_HeapSize( void *, uint32_t, const void *p ) { return malloc_size( p ); }
MSABI void k_GetSystemInfo( uint8_t *info )
{
	memset( info, 0, 48 );		// SYSTEM_INFO, x64
	const uint32_t page = 4096, granularity = 65536, processors = 1;
	memcpy( info + 4, &page, 4 );
	memcpy( info + 32, &processors, 4 );
	memcpy( info + 40, &granularity, 4 );
}
MSABI void *k_VirtualAlloc( void *address, size_t n, uint32_t, uint32_t )
{
	if (address != NULL)
		return address;		// a commit inside a reservation of ours: already mapped
	void *p = mmap( NULL, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0 );
	if (p == MAP_FAILED)
		return NULL;
	virtualSizes[p] = n;
	return p;
}
MSABI int k_VirtualFree( void *p, size_t, uint32_t type )
{
	if ((type & 0x8000) && virtualSizes.count( p ))		// MEM_RELEASE
	{
		munmap( p, virtualSizes[p] );
		virtualSizes.erase( p );
	}
	return 1;
}
MSABI uint32_t k_TlsAlloc( void ) { return tlsNext < 64 ? tlsNext++ : 0xFFFFFFFFu; }
MSABI int k_TlsFree( uint32_t ) { return 1; }
MSABI void *k_TlsGetValue( uint32_t i ) { lastError = 0; return i < 64 ? tls[i] : NULL; }
MSABI int k_TlsSetValue( uint32_t i, void *v ) { if (i >= 64) return 0; tls[i] = v; return 1; }
std::vector<std::string> librariesAsked;		// what the DLLs loaded: proof the real path ran
MSABI void *k_LoadLibraryA( const char *name )
{
	librariesAsked.push_back( name );
	const char *slash = strrchr( name, '\\' );
	Module *m = moduleNamed( slash ? slash + 1 : name );
	return m ? m->base : NULL;
}
MSABI void *k_GetModuleHandleA( const char *name ) { return name ? k_LoadLibraryA( name ) : modules[0]->base; }
MSABI int k_FreeLibrary( void * ) { return 1; }
MSABI void *k_GetProcAddress( void *handle, const char *name )
{
	for (size_t i = 0; i < modules.size(); ++i)
		if (modules[i]->base == handle)
		{
			if ((uintptr_t)name < 0x10000)
				return NULL;		// by ordinal: not used here
			std::map<std::string, void *>::iterator it = modules[i]->exports.find( name );
			return it == modules[i]->exports.end() ? NULL : it->second;
		}
	return NULL;
}
MSABI int k_DisableThreadLibraryCalls( void * ) { return 1; }
MSABI uint32_t k_GetCurrentProcessId( void ) { return (uint32_t)getpid(); }
MSABI uint32_t k_GetCurrentThreadId( void ) { return 1; }
MSABI void k_GetSystemTimeAsFileTime( uint64_t *t ) { *t = 116444736000000000ull + (uint64_t)time( NULL ) * 10000000ull; }
MSABI uint32_t k_GetTickCount( void ) { return (uint32_t)(mach_absolute_time() / 1000000); }
MSABI int k_QueryPerformanceCounter( int64_t *c ) { *c = (int64_t)mach_absolute_time(); return 1; }
MSABI int k_QueryPerformanceFrequency( int64_t *f ) { *f = 1000000000; return 1; }
MSABI void k_Sleep( uint32_t ms ) { usleep( ms * 1000 ); }
MSABI void k_OutputDebugStringA( const char *s ) { fprintf( stderr, "[d3dcompiler] %s", s ); }
MSABI int k_lstrcmpiA( const char *a, const char *b ) { return strcasecmp( a, b ); }
MSABI int k_CloseHandle( void * ) { return 1; }
MSABI int k_UnmapViewOfFile( const void * ) { return 1; }
MSABI uint32_t k_GetFullPathNameA( const char *name, uint32_t n, char *out, char **part )
{
	const size_t length = strlen( name );
	if (length + 1 > n)
		return (uint32_t)(length + 1);
	memcpy( out, name, length + 1 );
	if (part)
		*part = out;
	return (uint32_t)length;
}
MSABI int k_MultiByteToWideChar( uint32_t, uint32_t, const char *s, int n, uint16_t *w, int wn )
{
	if (n < 0)
		n = (int)strlen( s ) + 1;
	if (wn == 0)
		return n;
	for (int i = 0; i < n && i < wn; ++i)
		w[i] = (uint8_t)s[i];
	return n < wn ? n : wn;
}
MSABI int k_WideCharToMultiByte( uint32_t, uint32_t, const uint16_t *w, int n, char *s, int sn, const char *, int * )
{
	if (n < 0)
	{
		n = 0;
		while (w[n])
			++n;
		++n;
	}
	if (sn == 0)
		return n;
	for (int i = 0; i < n && i < sn; ++i)
		s[i] = (char)(w[i] < 0x80 ? w[i] : '?');
	return n < sn ? n : sn;
}
MSABI void k_InitializeCriticalSection( void * ) {}
MSABI int k_InitializeCriticalSectionAndSpinCount( void *, uint32_t ) { return 1; }
MSABI void k_EnterCriticalSection( void * ) {}
MSABI void k_LeaveCriticalSection( void * ) {}
MSABI void k_DeleteCriticalSection( void * ) {}
MSABI void *k_EncodePointer( void *p ) { return p; }
MSABI void *k_DecodePointer( void *p ) { return p; }
MSABI int g_DeleteObject( void * ) { return 1; }

// msvcrt
MSABI void *c_malloc( size_t n ) { return malloc( n ); }
MSABI void *c_calloc( size_t a, size_t b ) { return calloc( a, b ); }
MSABI void *c_realloc( void *p, size_t n ) { return realloc( p, n ); }
MSABI void c_free( void *p ) { free( p ); }
MSABI void *c_new( size_t n ) { return malloc( n ? n : 1 ); }
MSABI void c_delete( void *p ) { free( p ); }
MSABI void *c_memcpy( void *d, const void *s, size_t n ) { return memcpy( d, s, n ); }
MSABI void *c_memmove( void *d, const void *s, size_t n ) { return memmove( d, s, n ); }
MSABI void *c_memset( void *d, int c, size_t n ) { return memset( d, c, n ); }
MSABI int c_memcmp( const void *a, const void *b, size_t n ) { return memcmp( a, b, n ); }
MSABI size_t c_strlen( const char *s ) { return strlen( s ); }
MSABI size_t c_mbstrlen( const char *s ) { return strlen( s ); }
MSABI int c_strcmp( const char *a, const char *b ) { return strcmp( a, b ); }
MSABI int c_strncmp( const char *a, const char *b, size_t n ) { return strncmp( a, b, n ); }
MSABI int c_stricmp( const char *a, const char *b ) { return strcasecmp( a, b ); }
MSABI int c_strnicmp( const char *a, const char *b, size_t n ) { return strncasecmp( a, b, n ); }
MSABI char *c_strchr( const char *s, int c ) { return (char *)strchr( s, c ); }
MSABI char *c_strrchr( const char *s, int c ) { return (char *)strrchr( s, c ); }
MSABI char *c_strstr( const char *s, const char *t ) { return (char *)strstr( s, t ); }
MSABI char *c_strcpy( char *d, const char *s ) { return strcpy( d, s ); }
MSABI char *c_strncpy( char *d, const char *s, size_t n ) { return strncpy( d, s, n ); }
MSABI char *c_strdup( const char *s ) { return strdup( s ); }
MSABI double c_atof( const char *s ) { return atof( s ); }
MSABI int c_atoi( const char *s ) { return atoi( s ); }
MSABI long c_strtol( const char *s, char **e, int b ) { return (long)(int)strtol( s, e, b ); }
MSABI double c_strtod( const char *s, char **e ) { return strtod( s, e ); }
MSABI int c_isalnum( int c ) { return isalnum( c ); }
MSABI int c_isalpha( int c ) { return isalpha( c ); }
MSABI int c_isdigit( int c ) { return isdigit( c ); }
MSABI int c_isxdigit( int c ) { return isxdigit( c ); }
MSABI int c_isspace( int c ) { return isspace( c ); }
MSABI int c_tolower( int c ) { return tolower( c ); }
MSABI int c_toupper( int c ) { return toupper( c ); }
MSABI const char *c_setlocale( int, const char * ) { return "C"; }
MSABI int c_finite( double x ) { return isfinite( x ); }
MSABI uint32_t c_clearfp( void ) { return 0; }
MSABI void c_initterm( void (MSABI **begin)( void ), void (MSABI **end)( void ) )
{
	for (; begin < end; ++begin)
		if (*begin)
			(*begin)();
}
int (MSABI *qsortCompare)( const void *, const void * ) = NULL;
int qsortBridge( const void *a, const void *b ) { return qsortCompare( a, b ); }
MSABI void c_qsort( void *base, size_t n, size_t size, int (MSABI *compare)( const void *, const void * ) )
{
	int (MSABI *saved)( const void *, const void * ) = qsortCompare;
	qsortCompare = compare;
	qsort( base, n, size, qsortBridge );
	qsortCompare = saved;
}
[[noreturn]] void stop( const char *why );
void stopHere( const char *why ) { stop( why ); }

// the C library's arithmetic and small queries, one to one
MSABI float c_sqrtf( float x ) { return sqrtf( x ); }
MSABI double c_sqrt( double x ) { return sqrt( x ); }
MSABI float c_sinf( float x ) { return sinf( x ); }
MSABI double c_sin( double x ) { return sin( x ); }
MSABI float c_cosf( float x ) { return cosf( x ); }
MSABI double c_cos( double x ) { return cos( x ); }
MSABI double c_tan( double x ) { return tan( x ); }
MSABI double c_atan( double x ) { return atan( x ); }
MSABI double c_atan2( double y, double x ) { return atan2( y, x ); }
MSABI float c_atan2f( float y, float x ) { return atan2f( y, x ); }
MSABI double c_asin( double x ) { return asin( x ); }
MSABI float c_asinf( float x ) { return asinf( x ); }
MSABI double c_acos( double x ) { return acos( x ); }
MSABI float c_acosf( float x ) { return acosf( x ); }
MSABI double c_exp( double x ) { return exp( x ); }
MSABI double c_log( double x ) { return log( x ); }
MSABI float c_logf( float x ) { return logf( x ); }
MSABI double c_pow( double x, double y ) { return pow( x, y ); }
MSABI float c_powf( float x, float y ) { return powf( x, y ); }
MSABI double c_floor( double x ) { return floor( x ); }
MSABI float c_floorf( float x ) { return floorf( x ); }
MSABI double c_ceil( double x ) { return ceil( x ); }
MSABI float c_ceilf( float x ) { return ceilf( x ); }
MSABI double c_fmod( double x, double y ) { return fmod( x, y ); }
MSABI float c_fmodf( float x, float y ) { return fmodf( x, y ); }
MSABI double c_frexp( double x, int *e ) { return frexp( x, e ); }
MSABI double c_ldexp( double x, int e ) { return ldexp( x, e ); }
MSABI double c_modf( double x, double *i ) { return modf( x, i ); }
MSABI float c_modff( float x, float *i ) { return modff( x, i ); }
MSABI double c_sinh( double x ) { return sinh( x ); }
MSABI double c_cosh( double x ) { return cosh( x ); }
MSABI double c_tanh( double x ) { return tanh( x ); }
MSABI int c_isnan( double x ) { return isnan( x ); }
int errnoValue = 0;
MSABI int *c_errno( void ) { return &errnoValue; }
MSABI uint32_t c_controlfp( uint32_t, uint32_t ) { return 0x0009001Fu; }		// _CW_DEFAULT on x64
MSABI long c_atol( const char *s ) { return (long)(int)atol( s ); }
MSABI int c_rand( void ) { return rand() & 0x7FFF; }
MSABI void c_srand( unsigned seed ) { srand( seed ); }
MSABI const char *c_getenv( const char * ) { return NULL; }
MSABI int c_isleadbyte( int ) { return 0; }
MSABI int c_iswalpha( uint16_t c ) { return c < 0x80 && isalpha( c ); }
MSABI int c_iswdigit( uint16_t c ) { return c < 0x80 && isdigit( c ); }
MSABI int c_iswspace( uint16_t c ) { return c < 0x80 && isspace( c ); }
MSABI int c_iswpunct( uint16_t c ) { return c < 0x80 && ispunct( c ); }
MSABI void c_abort( void ) { stopHere( "the DLL called abort" ); }
MSABI uint32_t k_GetACP( void ) { return 1252; }
MSABI void *k_GetCurrentProcess( void ) { return (void *)-1; }
MSABI void *k_SetUnhandledExceptionFilter( void * ) { return NULL; }
MSABI int k_IsProcessorFeaturePresent( uint32_t feature ) { return feature == 6 || feature == 10; }	// SSE, SSE2
MSABI int k_IsBadPtr( const void *, size_t ) { return 0; }
MSABI int k_IsDBCSLeadByte( uint8_t ) { return 0; }
MSABI int k_GetVersionExA( uint8_t *info )
{
	uint32_t size;
	memcpy( &size, info, 4 );
	const uint32_t major = 6, minor = 1, build = 7601, platform = 2;	// Windows 7 SP1, VER_PLATFORM_WIN32_NT
	memcpy( info + 4, &major, 4 ); memcpy( info + 8, &minor, 4 ); memcpy( info + 12, &build, 4 ); memcpy( info + 16, &platform, 4 );
	return 1;
}

MSABI void c_lock( int ) {}
MSABI void c_unlock( int ) {}
/// __dllonexit and _onexit register a DLL's destructors; the process ends without unloading, so they
/// are recorded and never run
MSABI void *c_dllonexit( void *function, void ***, void *** ) { return function; }
MSABI void *c_onexit( void *function ) { return function; }
[[noreturn]] void stop( const char *why )
{
	fprintf( stderr, "[assemble-oracle] ERROR: %s\n", why );
	_exit( 3 );
}
MSABI void c_amsg_exit( int code ) { char b[ 64 ]; snprintf( b, sizeof( b ), "the C runtime stopped (_amsg_exit %d)", code ); stop( b ); }
MSABI void c_purecall( void ) { stop( "a pure virtual call" ); }
MSABI void c_CxxThrowException( void *, void * ) { stop( "the DLL threw a C++ exception (_CxxThrowException); not modelled here" ); }

/// _vsnprintf over a Microsoft va_list: the conversions an assembler's messages use
int formatMs( char *out, size_t n, const char *f, __builtin_ms_va_list ap )
{
	std::string s;
	for (; *f; ++f)
	{
		if (*f != '%')
		{
			s += *f;
			continue;
		}
		std::string spec = "%";
		++f;
		while (*f && strchr( "-+ #0123456789.", *f ))
			spec += *f++;
		bool wide = false;
		while (*f == 'l' || *f == 'h' || *f == 'I' || *f == '6' || *f == '4' || *f == 'z')
		{
			if (*f == 'l' || *f == 'I' || *f == 'z')
				wide = wide || *f == 'I' || *f == 'z';
			++f;
		}
		char buffer[ 512 ];
		switch (*f)
		{
			case 'd': case 'i':
				spec += "lld";
				snprintf( buffer, sizeof( buffer ), spec.c_str(), wide ? (long long)__builtin_va_arg( ap, int64_t ) : (long long)__builtin_va_arg( ap, int ) );
				break;
			case 'u': case 'x': case 'X': case 'o':
				spec += "ll"; spec += *f;
				snprintf( buffer, sizeof( buffer ), spec.c_str(), wide ? (unsigned long long)__builtin_va_arg( ap, uint64_t ) : (unsigned long long)__builtin_va_arg( ap, unsigned ) );
				break;
			case 'c':
				spec += 'c';
				snprintf( buffer, sizeof( buffer ), spec.c_str(), __builtin_va_arg( ap, int ) );
				break;
			case 's':
				spec += 's';
				snprintf( buffer, sizeof( buffer ), spec.c_str(), __builtin_va_arg( ap, const char * ) );
				break;
			case 'p':
				snprintf( buffer, sizeof( buffer ), "%p", __builtin_va_arg( ap, void * ) );
				break;
			case 'f': case 'g': case 'e': case 'G': case 'E':
				spec += *f;
				snprintf( buffer, sizeof( buffer ), spec.c_str(), __builtin_va_arg( ap, double ) );
				break;
			case '%':
				buffer[0] = '%'; buffer[1] = 0;
				break;
			default:
				stop( "a printf conversion this stub does not know" );
		}
		s += buffer;
	}
	if (n == 0)
		return (int)s.size();
	const size_t copied = s.size() < n ? s.size() : n;
	memcpy( out, s.data(), copied );
	if (copied < n)
		out[copied] = 0;
	return s.size() < n ? (int)s.size() : -1;		// Microsoft's _vsnprintf: -1 when it did not fit
}
MSABI int c_vsnprintf( char *out, size_t n, const char *f, __builtin_ms_va_list ap ) { return formatMs( out, n, f, ap ); }
MSABI int c_snprintf( char *out, size_t n, const char *f, ... )
{
	__builtin_ms_va_list ap;
	__builtin_ms_va_start( ap, f );
	const int r = formatMs( out, n, f, ap );
	__builtin_ms_va_end( ap );
	return r;
}
MSABI int c_sprintf( char *out, const char *f, ... )
{
	__builtin_ms_va_list ap;
	__builtin_ms_va_start( ap, f );
	const int r = formatMs( out, (size_t)1 << 20, f, ap );
	__builtin_ms_va_end( ap );
	return r;
}

// The C runtime's data: zeroed stand-ins (read, never needed for a result)
uint8_t data_iob[ 3 * 64 ], data_pioinfo[ 64 * 8 ], data_badioinfo[ 64 ];

struct Stub { const char *dll, *name; void *address; };
const Stub STUBS[] = {
	{ "KERNEL32.dll", "GetLastError", (void *)k_GetLastError }, { "KERNEL32.dll", "SetLastError", (void *)k_SetLastError },
	{ "KERNEL32.dll", "GetProcessHeap", (void *)k_GetProcessHeap }, { "KERNEL32.dll", "HeapCreate", (void *)k_HeapCreate },
	{ "KERNEL32.dll", "HeapDestroy", (void *)k_HeapDestroy }, { "KERNEL32.dll", "HeapAlloc", (void *)k_HeapAlloc },
	{ "KERNEL32.dll", "HeapReAlloc", (void *)k_HeapReAlloc }, { "KERNEL32.dll", "HeapFree", (void *)k_HeapFree },
	{ "KERNEL32.dll", "HeapSize", (void *)k_HeapSize }, { "KERNEL32.dll", "GetSystemInfo", (void *)k_GetSystemInfo },
	{ "KERNEL32.dll", "VirtualAlloc", (void *)k_VirtualAlloc }, { "KERNEL32.dll", "VirtualFree", (void *)k_VirtualFree },
	{ "KERNEL32.dll", "TlsAlloc", (void *)k_TlsAlloc }, { "KERNEL32.dll", "TlsFree", (void *)k_TlsFree },
	{ "KERNEL32.dll", "TlsGetValue", (void *)k_TlsGetValue }, { "KERNEL32.dll", "TlsSetValue", (void *)k_TlsSetValue },
	{ "KERNEL32.dll", "LoadLibraryA", (void *)k_LoadLibraryA }, { "KERNEL32.dll", "GetModuleHandleA", (void *)k_GetModuleHandleA },
	{ "KERNEL32.dll", "FreeLibrary", (void *)k_FreeLibrary }, { "KERNEL32.dll", "GetProcAddress", (void *)k_GetProcAddress },
	{ "KERNEL32.dll", "DisableThreadLibraryCalls", (void *)k_DisableThreadLibraryCalls },
	{ "KERNEL32.dll", "GetCurrentProcessId", (void *)k_GetCurrentProcessId }, { "KERNEL32.dll", "GetCurrentThreadId", (void *)k_GetCurrentThreadId },
	{ "KERNEL32.dll", "GetSystemTimeAsFileTime", (void *)k_GetSystemTimeAsFileTime }, { "KERNEL32.dll", "GetTickCount", (void *)k_GetTickCount },
	{ "KERNEL32.dll", "QueryPerformanceCounter", (void *)k_QueryPerformanceCounter },
	{ "KERNEL32.dll", "QueryPerformanceFrequency", (void *)k_QueryPerformanceFrequency },
	{ "KERNEL32.dll", "Sleep", (void *)k_Sleep }, { "KERNEL32.dll", "OutputDebugStringA", (void *)k_OutputDebugStringA },
	{ "KERNEL32.dll", "lstrcmpiA", (void *)k_lstrcmpiA }, { "KERNEL32.dll", "CloseHandle", (void *)k_CloseHandle },
	{ "KERNEL32.dll", "UnmapViewOfFile", (void *)k_UnmapViewOfFile }, { "KERNEL32.dll", "GetFullPathNameA", (void *)k_GetFullPathNameA },
	{ "KERNEL32.dll", "MultiByteToWideChar", (void *)k_MultiByteToWideChar }, { "KERNEL32.dll", "WideCharToMultiByte", (void *)k_WideCharToMultiByte },
	{ "KERNEL32.dll", "InitializeCriticalSection", (void *)k_InitializeCriticalSection },
	{ "KERNEL32.dll", "InitializeCriticalSectionAndSpinCount", (void *)k_InitializeCriticalSectionAndSpinCount },
	{ "KERNEL32.dll", "EnterCriticalSection", (void *)k_EnterCriticalSection }, { "KERNEL32.dll", "LeaveCriticalSection", (void *)k_LeaveCriticalSection },
	{ "KERNEL32.dll", "DeleteCriticalSection", (void *)k_DeleteCriticalSection },
	{ "KERNEL32.dll", "EncodePointer", (void *)k_EncodePointer }, { "KERNEL32.dll", "DecodePointer", (void *)k_DecodePointer },
	{ "GDI32.dll", "DeleteObject", (void *)g_DeleteObject },
	{ "msvcrt.dll", "malloc", (void *)c_malloc }, { "msvcrt.dll", "calloc", (void *)c_calloc }, { "msvcrt.dll", "realloc", (void *)c_realloc },
	{ "msvcrt.dll", "free", (void *)c_free }, { "msvcrt.dll", "??2@YAPEAX_K@Z", (void *)c_new }, { "msvcrt.dll", "??3@YAXPEAX@Z", (void *)c_delete },
	{ "msvcrt.dll", "memcpy", (void *)c_memcpy }, { "msvcrt.dll", "memmove", (void *)c_memmove }, { "msvcrt.dll", "memset", (void *)c_memset },
	{ "msvcrt.dll", "memcmp", (void *)c_memcmp }, { "msvcrt.dll", "strlen", (void *)c_strlen }, { "msvcrt.dll", "_mbstrlen", (void *)c_mbstrlen },
	{ "msvcrt.dll", "strcmp", (void *)c_strcmp }, { "msvcrt.dll", "strncmp", (void *)c_strncmp }, { "msvcrt.dll", "_stricmp", (void *)c_stricmp },
	{ "msvcrt.dll", "_strnicmp", (void *)c_strnicmp }, { "msvcrt.dll", "strchr", (void *)c_strchr }, { "msvcrt.dll", "strrchr", (void *)c_strrchr },
	{ "msvcrt.dll", "strstr", (void *)c_strstr }, { "msvcrt.dll", "strcpy", (void *)c_strcpy }, { "msvcrt.dll", "strncpy", (void *)c_strncpy },
	{ "msvcrt.dll", "_strdup", (void *)c_strdup }, { "msvcrt.dll", "atof", (void *)c_atof }, { "msvcrt.dll", "atoi", (void *)c_atoi },
	{ "msvcrt.dll", "strtol", (void *)c_strtol }, { "msvcrt.dll", "strtod", (void *)c_strtod },
	{ "msvcrt.dll", "isalnum", (void *)c_isalnum }, { "msvcrt.dll", "isalpha", (void *)c_isalpha }, { "msvcrt.dll", "isdigit", (void *)c_isdigit },
	{ "msvcrt.dll", "isxdigit", (void *)c_isxdigit }, { "msvcrt.dll", "isspace", (void *)c_isspace },
	{ "msvcrt.dll", "tolower", (void *)c_tolower }, { "msvcrt.dll", "toupper", (void *)c_toupper },
	{ "msvcrt.dll", "setlocale", (void *)c_setlocale }, { "msvcrt.dll", "_finite", (void *)c_finite }, { "msvcrt.dll", "_clearfp", (void *)c_clearfp },
	{ "msvcrt.dll", "_initterm", (void *)c_initterm },
	{ "msvcrt.dll", "sqrtf", (void *)c_sqrtf }, { "msvcrt.dll", "sqrt", (void *)c_sqrt }, { "msvcrt.dll", "sinf", (void *)c_sinf },
	{ "msvcrt.dll", "sin", (void *)c_sin }, { "msvcrt.dll", "cosf", (void *)c_cosf }, { "msvcrt.dll", "cos", (void *)c_cos },
	{ "msvcrt.dll", "tan", (void *)c_tan }, { "msvcrt.dll", "atan", (void *)c_atan }, { "msvcrt.dll", "atan2", (void *)c_atan2 },
	{ "msvcrt.dll", "atan2f", (void *)c_atan2f }, { "msvcrt.dll", "asin", (void *)c_asin }, { "msvcrt.dll", "asinf", (void *)c_asinf },
	{ "msvcrt.dll", "acos", (void *)c_acos }, { "msvcrt.dll", "acosf", (void *)c_acosf }, { "msvcrt.dll", "exp", (void *)c_exp },
	{ "msvcrt.dll", "log", (void *)c_log }, { "msvcrt.dll", "logf", (void *)c_logf }, { "msvcrt.dll", "pow", (void *)c_pow },
	{ "msvcrt.dll", "powf", (void *)c_powf }, { "msvcrt.dll", "floor", (void *)c_floor }, { "msvcrt.dll", "floorf", (void *)c_floorf },
	{ "msvcrt.dll", "ceil", (void *)c_ceil }, { "msvcrt.dll", "ceilf", (void *)c_ceilf }, { "msvcrt.dll", "fmod", (void *)c_fmod },
	{ "msvcrt.dll", "fmodf", (void *)c_fmodf }, { "msvcrt.dll", "frexp", (void *)c_frexp }, { "msvcrt.dll", "ldexp", (void *)c_ldexp },
	{ "msvcrt.dll", "modf", (void *)c_modf }, { "msvcrt.dll", "modff", (void *)c_modff }, { "msvcrt.dll", "sinh", (void *)c_sinh },
	{ "msvcrt.dll", "cosh", (void *)c_cosh }, { "msvcrt.dll", "tanh", (void *)c_tanh }, { "msvcrt.dll", "_isnan", (void *)c_isnan },
	{ "msvcrt.dll", "_errno", (void *)c_errno }, { "msvcrt.dll", "_controlfp", (void *)c_controlfp }, { "msvcrt.dll", "atol", (void *)c_atol },
	{ "msvcrt.dll", "rand", (void *)c_rand }, { "msvcrt.dll", "srand", (void *)c_srand }, { "msvcrt.dll", "getenv", (void *)c_getenv },
	{ "msvcrt.dll", "isleadbyte", (void *)c_isleadbyte }, { "msvcrt.dll", "iswalpha", (void *)c_iswalpha },
	{ "msvcrt.dll", "iswdigit", (void *)c_iswdigit }, { "msvcrt.dll", "iswspace", (void *)c_iswspace },
	{ "msvcrt.dll", "iswpunct", (void *)c_iswpunct }, { "msvcrt.dll", "abort", (void *)c_abort },
	{ "KERNEL32.dll", "GetACP", (void *)k_GetACP }, { "KERNEL32.dll", "GetCurrentProcess", (void *)k_GetCurrentProcess },
	{ "KERNEL32.dll", "SetUnhandledExceptionFilter", (void *)k_SetUnhandledExceptionFilter },
	{ "KERNEL32.dll", "IsProcessorFeaturePresent", (void *)k_IsProcessorFeaturePresent },
	{ "KERNEL32.dll", "IsBadReadPtr", (void *)k_IsBadPtr }, { "KERNEL32.dll", "IsBadWritePtr", (void *)k_IsBadPtr },
	{ "KERNEL32.dll", "IsBadCodePtr", (void *)k_IsBadPtr }, { "KERNEL32.dll", "IsDBCSLeadByte", (void *)k_IsDBCSLeadByte },
	{ "KERNEL32.dll", "GetVersionExA", (void *)k_GetVersionExA }, { "msvcrt.dll", "_lock", (void *)c_lock }, { "msvcrt.dll", "_unlock", (void *)c_unlock },
	{ "msvcrt.dll", "__dllonexit", (void *)c_dllonexit }, { "msvcrt.dll", "_onexit", (void *)c_onexit }, { "msvcrt.dll", "qsort", (void *)c_qsort },
	{ "msvcrt.dll", "_amsg_exit", (void *)c_amsg_exit }, { "msvcrt.dll", "_purecall", (void *)c_purecall },
	{ "msvcrt.dll", "_CxxThrowException", (void *)c_CxxThrowException },
	{ "msvcrt.dll", "_vsnprintf", (void *)c_vsnprintf }, { "msvcrt.dll", "_snprintf", (void *)c_snprintf },
	{ "msvcrt.dll", "sprintf", (void *)c_sprintf },
	{ "msvcrt.dll", "_iob", (void *)data_iob }, { "msvcrt.dll", "__pioinfo", (void *)data_pioinfo },
	{ "msvcrt.dll", "__badioinfo", (void *)data_badioinfo } };

// ---- the traps: every other import, by name --------------------------------------------------------
std::vector<std::string> trapNames;
MSABI void trapped( uint64_t index )
{
	fprintf( stderr, "[assemble-oracle] ERROR: the DLL called %s, which this loader does not provide\n",
		index < trapNames.size() ? trapNames[index].c_str() : "?" );
	_exit( 3 );
}
uint8_t *trapPage = NULL;
size_t trapUsed = 0;
void *trapFor( const std::string &name )
{
	if (trapPage == NULL)
	{
		trapPage = (uint8_t *)mmap( NULL, 1 << 16, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0 );
		if (trapPage == MAP_FAILED)
			stop( "no executable page for the traps" );
	}
	const uint64_t index = trapNames.size();
	trapNames.push_back( name );
	uint8_t *p = trapPage + trapUsed;
	const uint64_t handler = (uint64_t)(void *)trapped;
	p[0] = 0x48; p[1] = 0xB9; memcpy( p + 2, &index, 8 );		// movabs rcx, index
	p[10] = 0x48; p[11] = 0xB8; memcpy( p + 12, &handler, 8 );	// movabs rax, trapped
	p[20] = 0xFF; p[21] = 0xE0;									// jmp rax
	trapUsed += 32;
	return p;
}

// ---- loading ----------------------------------------------------------------------------------------
std::vector<uint8_t> readFile( const char *path )
{
	std::vector<uint8_t> b;
	FILE *f = fopen( path, "rb" );
	if (f == NULL)
		return b;
	fseek( f, 0, SEEK_END );
	b.resize( (size_t)ftell( f ) );
	fseek( f, 0, SEEK_SET );
	if (fread( b.data(), 1, b.size(), f ) != b.size())
		b.clear();
	fclose( f );
	return b;
}

const uint32_t FAKE_TEB = 0x2000;		// bytes after the image: our stand-in for the thread block

Module *load( const char *path, const char *name )
{
	const std::vector<uint8_t> file = readFile( path );
	if (file.size() < 0x400 || file[0] != 'M' || file[1] != 'Z')
		stop( "not a PE file" );
	const uint8_t *nt = &file[u32( &file[0x3C] )];
	if (memcmp( nt, "PE\0\0", 4 ) != 0 || u16( nt + 4 ) != 0x8664)
		stop( "not an x64 PE image" );
	const uint16_t sections = u16( nt + 6 ), optionalSize = u16( nt + 20 );
	const uint8_t *opt = nt + 24;
	if (u16( opt ) != 0x20B)
		stop( "not PE32+" );
	Module *m = new Module;
	m->name = name;
	m->entry = u32( opt + 16 );
	const uint64_t preferred = u64( opt + 24 );
	m->size = u32( opt + 56 );
	m->base = (uint8_t *)mmap( NULL, m->size + FAKE_TEB, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0 );
	if (m->base == MAP_FAILED)
		stop( "could not map the image" );
	memcpy( m->base, file.data(), u32( opt + 60 ) );		// the headers
	const uint8_t *section = opt + optionalSize;
	for (int i = 0; i < sections; ++i, section += 40)
	{
		const uint32_t virtualAddress = u32( section + 12 ), rawSize = u32( section + 16 ), rawOffset = u32( section + 20 );
		if (rawSize)
			memcpy( m->base + virtualAddress, &file[rawOffset], rawSize );
	}
	const uint8_t *directory = opt + 112;
	// base relocations (IMAGE_REL_BASED_DIR64)
	const int64_t delta = (int64_t)((uint64_t)m->base - preferred);
	uint32_t at = u32( directory + 5 * 8 ), end = at + u32( directory + 5 * 8 + 4 );
	while (at < end)
	{
		const uint32_t page = u32( m->base + at ), blockSize = u32( m->base + at + 4 );
		for (uint32_t k = 8; k + 2 <= blockSize; k += 2)
		{
			const uint16_t e = u16( m->base + at + k );
			if ((e >> 12) == 10)
			{
				uint64_t v = u64( m->base + page + (e & 0xFFF) );
				v += (uint64_t)delta;
				memcpy( m->base + page + (e & 0xFFF), &v, 8 );
			}
			else if ((e >> 12) != 0)
				stop( "a base relocation other than DIR64" );
		}
		at += blockSize;
	}
	// imports
	for (uint32_t d = u32( directory + 8 ); u32( m->base + d + 12 ) != 0; d += 20)
	{
		const std::string dll = (const char *)(m->base + u32( m->base + d + 12 ));
		const uint32_t lookup = u32( m->base + d ) ? u32( m->base + d ) : u32( m->base + d + 16 );
		const uint32_t iat = u32( m->base + d + 16 );
		for (uint32_t k = 0; u64( m->base + lookup + 8 * k ) != 0; ++k)
		{
			const uint64_t thunk = u64( m->base + lookup + 8 * k );
			if (thunk >> 63)
				stop( "an import by ordinal" );
			const std::string fn = (const char *)(m->base + (uint32_t)thunk + 2);
			void *address = NULL;
			for (size_t s = 0; s < sizeof( STUBS ) / sizeof( STUBS[0] ) && address == NULL; ++s)
				if (sameName( dll, STUBS[s].dll ) && fn == STUBS[s].name)
					address = STUBS[s].address;
			if (address == NULL)
				address = trapFor( dll + "!" + fn );
			memcpy( m->base + iat + 8 * k, &address, 8 );
		}
	}
	// exports
	const uint32_t exportDir = u32( directory );
	const uint32_t names = u32( m->base + exportDir + 24 ), functions = u32( m->base + exportDir + 28 );
	const uint32_t namePointers = u32( m->base + exportDir + 32 ), ordinals = u32( m->base + exportDir + 36 );
	for (uint32_t k = 0; k < names; ++k)
	{
		const char *exportName = (const char *)(m->base + u32( m->base + namePointers + 4 * k ));
		const uint16_t ordinal = u16( m->base + ordinals + 2 * k );
		m->exports[exportName] = m->base + u32( m->base + functions + 4 * ordinal );
	}
	modules.push_back( m );
	return m;
}

/// The departure (see the top): the C runtime's reads of the thread block, checked and then patched
void patch( Module *m, uint32_t chkstk, const uint32_t tebReads[2] )
{
	static const uint8_t CHKSTK_START[] = { 0x48, 0x83, 0xEC, 0x10, 0x4C, 0x89, 0x14, 0x24 };		// sub rsp,10h; mov [rsp],r10
	static const uint8_t TEB_READ[] = { 0x65, 0x48, 0x8B, 0x04, 0x25, 0x30, 0x00, 0x00, 0x00 };		// mov rax, gs:[30h]
	if (memcmp( m->base + chkstk, CHKSTK_START, sizeof( CHKSTK_START ) ) != 0)
		stop( "__chkstk is not where this DLL's build keeps it" );
	m->base[chkstk] = 0xC3;		// ret: the stack is committed, nothing to probe
	uint8_t *teb = m->base + m->size;
	uint64_t id = (uint64_t)teb;
	memcpy( teb + 8, &id, 8 );		// its stack base: a non-zero id for the startup lock
	for (int k = 0; k < 2; ++k)
	{
		uint8_t *site = m->base + tebReads[k];
		if (memcmp( site, TEB_READ, sizeof( TEB_READ ) ) != 0)
			stop( "a thread-block read is not where this DLL's build keeps it" );
		const int32_t rel = (int32_t)(teb - (site + 7));
		site[0] = 0x48; site[1] = 0x8D; site[2] = 0x05; memcpy( site + 3, &rel, 4 );	// lea rax, [rip + rel]
		site[7] = 0x66; site[8] = 0x90;													// nop
	}
}

bool runEntry( Module *m )
{
	typedef int (MSABI *DllMain)( void *, uint32_t, void * );
	return ((DllMain)(m->base + m->entry))( m->base, 1, NULL ) != 0;	// DLL_PROCESS_ATTACH
}

// ---- the water's programs, as the compiler reads W3DWater.cpp's literals ---------------------------
std::vector<std::string> pixelProgramsIn( const char *path )
{
	std::vector<std::string> programs;
	const std::vector<uint8_t> bytes = readFile( path );
	const std::string source( bytes.begin(), bytes.end() );
	size_t at = 0;
	while ((at = source.find( "\"ps.1.1", at )) != std::string::npos)
	{
		std::string text;
		size_t i = at + 1;
		for (; i < source.size() && source[i] != '"'; ++i)
		{
			if (source[i] != '\\') { text += source[i]; continue; }
			++i;
			if (source[i] == 'n') text += '\n';
			else if (source[i] == '\n') {}
			else if (source[i] == '\r' && source[i + 1] == '\n') ++i;
			else text += source[i];
		}
		programs.push_back( text );
		at = i;
	}
	return programs;
}

struct Buffer;		// ID3DXBuffer, called through its vtable
typedef long (MSABI *AssembleShader)( const char *, uint32_t, const void *, void *, uint32_t, Buffer **, Buffer ** );

void *bufferPointer( Buffer *b ) { return ((void *(MSABI *)( Buffer * ))(*(void ***)b)[3])( b ); }
uint32_t bufferSize( Buffer *b ) { return ((uint32_t (MSABI *)( Buffer * ))(*(void ***)b)[4])( b ); }
void bufferRelease( Buffer *b ) { ((uint32_t (MSABI *)( Buffer * ))(*(void ***)b)[2])( b ); }

}	// namespace

int main( int argc, char **argv )
{
	if (argc < 4)
	{
		fprintf( stderr, "usage: assemble_oracle <d3dx9_43.dll> <D3DCompiler_43.dll> <W3DWater.cpp>\n" );
		return 2;
	}
	// D3DCompiler_43.dll's build (sha256 44c3a7e3...850e8a): __chkstk, and the startup lock's reads
	static const uint32_t COMPILER_TEB[ 2 ] = { 0x23ba76, 0x23bb5b };
	static const uint32_t D3DX_TEB[ 2 ] = { 0x215936, 0x215a1b };
	Module *compiler = load( argv[2], "d3dcompiler_43.dll" );
	patch( compiler, 0x23c680, COMPILER_TEB );
	Module *d3dx = load( argv[1], "d3dx9_43.dll" );
	patch( d3dx, 0x2178d0, D3DX_TEB );
	if (!runEntry( compiler ) || !runEntry( d3dx ))
		stop( "a DLL's entry point refused to attach" );
	AssembleShader assemble = (AssembleShader)d3dx->exports["D3DXAssembleShader"];
	if (assemble == NULL)
		stop( "d3dx9_43 exports no D3DXAssembleShader" );
	printf( "[assemble-oracle] both DLLs loaded and attached; %zu imports trapped unless called\n", trapNames.size() );

	const std::vector<std::string> programs = pixelProgramsIn( argv[3] );
	int failures = 0;
	if (programs.size() != 4)
	{
		printf( "[assemble-oracle] FAIL: W3DWater.cpp holds %zu ps.1.1 texts, not the census's 4\n", programs.size() );
		++failures;
	}
	std::vector<uint32_t> firstOurs;
	for (size_t k = 0; k < programs.size(); ++k)
	{
		Buffer *shader = NULL, *errors = NULL;
		const long hr = assemble( programs[k].c_str(), (uint32_t)programs[k].size(), NULL, NULL, 0, &shader, &errors );
		if (hr != 0 || shader == NULL)
		{
			printf( "[assemble-oracle] FAIL: program %zu: D3DXAssembleShader returned 0x%08lx: %s\n", k, hr & 0xFFFFFFFFl,
				errors ? (const char *)bufferPointer( errors ) : "(no message)" );
			++failures;
			continue;
		}
		std::vector<uint32_t> microsoft( bufferSize( shader ) / 4 );
		memcpy( microsoft.data(), bufferPointer( shader ), microsoft.size() * 4 );
		bufferRelease( shader );
		if (errors)
			bufferRelease( errors );
		// Microsoft's comment tokens, reported and set aside
		std::vector<uint32_t> tokens;
		for (size_t i = 0; i < microsoft.size(); ++i)
		{
			if (i > 0 && (microsoft[i] & 0xFFFF) == FFRef::Token::COMMENT && !(microsoft[i] & 0x80000000u))
			{
				const uint32_t words = (microsoft[i] >> 16) & 0x7FFF;
				printf( "[assemble-oracle]   program %zu: Microsoft's output carries a %u-word comment, set aside\n", k, words );
				i += words;
				continue;
			}
			tokens.push_back( microsoft[i] );
		}
		std::vector<uint32_t> ours;
		std::string error;
		if (!FFRef::assemblePixelProgram( programs[k], ours, error ))
		{
			printf( "[assemble-oracle] FAIL: program %zu: FFReference's assembler: %s\n", k, error.c_str() );
			++failures;
			continue;
		}
		if (k == 0)
			firstOurs = ours;
		FFRef::Program decoded;
		const bool decodes = FFRef::decodeProgram( tokens.data(), tokens.size(), decoded );
		if (tokens == ours && decodes)
			printf( "[assemble-oracle] ok: program %zu: %zu tokens, Microsoft's and FFReference's identical; they decode (%zu instructions)\n",
				k, tokens.size(), decoded.code.size() );
		else
		{
			++failures;
			printf( "[assemble-oracle] FAIL: program %zu: %s%s\n", k, tokens == ours ? "" : "the tokens differ",
				decodes ? "" : (std::string( "; Microsoft's do not decode: " ) + decoded.refusals[0]).c_str() );
			for (size_t i = 0; i < tokens.size() || i < ours.size(); ++i)
				if (i >= tokens.size() || i >= ours.size() || tokens[i] != ours[i])
					printf( "    word %zu: Microsoft %08x, FFReference %08x\n", i, i < tokens.size() ? tokens[i] : 0u,
						i < ours.size() ? ours[i] : 0u );
		}
	}
	// the armed control: one word of ours changed must be found
	if (!firstOurs.empty() && programs.size() > 0)
	{
		Buffer *shader = NULL, *errors = NULL;
		assemble( programs[0].c_str(), (uint32_t)programs[0].size(), NULL, NULL, 0, &shader, &errors );
		std::vector<uint32_t> microsoft( shader ? bufferSize( shader ) / 4 : 0 );
		if (shader)
			memcpy( microsoft.data(), bufferPointer( shader ), microsoft.size() * 4 );
		std::vector<uint32_t> spoiled = firstOurs;
		spoiled[spoiled.size() / 2] ^= 1u << 16;		// one bit of a write mask or a swizzle
		std::vector<uint32_t> tokens;
		for (size_t i = 0; i < microsoft.size(); ++i)
			if (!(i > 0 && (microsoft[i] & 0xFFFF) == FFRef::Token::COMMENT && !(microsoft[i] & 0x80000000u)))
				tokens.push_back( microsoft[i] );
			else
				i += (microsoft[i] >> 16) & 0x7FFF;
		if (tokens != spoiled)
			printf( "[assemble-oracle] ok: the control, one bit of FFReference's tokens changed, is found\n" );
		else
		{
			printf( "[assemble-oracle] FAIL: the control was not found, so this comparison proves nothing\n" );
			++failures;
		}
	}
	std::string asked;
	for (size_t i = 0; i < librariesAsked.size(); ++i)
		asked += (i ? ", " : "") + librariesAsked[i];
	printf( "[assemble-oracle] D3DXAssembleShader loaded: %s\n", asked.empty() ? "(nothing)" : asked.c_str() );
	if (librariesAsked.empty() || !sameName( librariesAsked[0], "d3dcompiler_43.dll" ))
	{
		printf( "[assemble-oracle] FAIL: D3DXAssembleShader did not load D3DCompiler_43, so its path was not the one Windows takes\n" );
		++failures;
	}
	printf( "[assemble-oracle] %s\n", failures ? "FAILED" : "Microsoft's assembler and FFReference's agree on every water program" );
	return failures ? 1 : 0;
}
