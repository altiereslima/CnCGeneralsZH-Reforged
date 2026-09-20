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

// FILE: ChromaKeyboard.cpp ///////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include <wininet.h>
#include <math.h>

#include "GameClient/ChromaKeyboard.h"
#include "GameClient/Color.h"
#include "GameClient/HotKey.h"
#include "Common/GameCommon.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameLogic/GameLogic.h"

//-----------------------------------------------------------------------------
// The Chroma grid is fixed at six rows of twenty-two, whatever keyboard is
// underneath; a device with fewer keys drops what it has no lamp for.
//-----------------------------------------------------------------------------
static const Int CHROMA_ROWS = 6;
static const Int CHROMA_COLUMNS = 22;
static const Int CHROMA_CELLS = CHROMA_ROWS * CHROMA_COLUMNS;

static const char *CHROMA_HOST = "localhost";
static const INTERNET_PORT CHROMA_PORT = 54235;
static const char *CHROMA_INIT_PATH = "/razer/chromasdk";
static const char *CHROMA_INIT_BODY =
	"{\"title\":\"Zero Hour Reforged\","
	"\"description\":\"Command bar and base state on the keyboard\","
	"\"author\":{\"name\":\"Zero Hour Reforged\",\"contact\":\"https://github.com/olcayseygan/CnCGeneralsZH-Reforged\"},"
	"\"device_supported\":[\"keyboard\"],"
	"\"category\":\"application\"}";

static const DWORD CHROMA_TIMEOUT_MS = 500;
static const DWORD CHROMA_SEND_INTERVAL_MS = 100;
/// The session expires after ten idle seconds, so a still frame is resent well inside that.
static const DWORD CHROMA_KEEPALIVE_MS = 4000;

/// Rows two to four of an ANSI board, in the order their keys sit on it.  Every
/// command bar hotkey the game hands out is a letter or a digit, so this is the
/// whole map rather than a subset of one.
static const char *CHROMA_KEY_ROWS[] = { "1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm" };
static const Int CHROMA_KEY_ROW_COUNT = 4;
/// Column zero is the logo strip and column one is escape, tab, caps and shift,
/// so every one of these four rows starts at two.  Row zero is the function keys.
static const Int CHROMA_KEY_FIRST_COLUMN = 2;
static const Int CHROMA_KEY_FIRST_ROW = 1;

static const Real AMBIENT_SCALE = 0.22f;			///< the unlit bed of player colour
static const Real PRESSABLE_SCALE = 1.0f;			///< a command key that would fire if pressed
static const Int UNDER_ATTACK_FRAMES = LOGICFRAMES_PER_SECOND * 4;
static const Real UNDER_ATTACK_PULSE_HZ = 2.5f;
static const Real UNDER_ATTACK_DEPTH = 0.75f;		///< how far the pulse drags the board to red

//-----------------------------------------------------------------------------
// The main thread writes s_pendingGrid, the worker reads it.  One lock over the
// whole grid: it is 528 bytes copied ten times a second.
//-----------------------------------------------------------------------------
static CRITICAL_SECTION s_gridLock;
static Int s_pendingGrid[ CHROMA_CELLS ];
static Bool s_workerRunning = FALSE;
static volatile LONG s_workerShouldStop = 0;
static HANDLE s_workerThread = NULL;

//-----------------------------------------------------------------------------
Int chromaCellForKey( char key )
{
	for( Int row = 0; row < CHROMA_KEY_ROW_COUNT; ++row )
	{
		const char *keys = CHROMA_KEY_ROWS[ row ];
		for( Int column = 0; keys[ column ] != 0; ++column )
		{
			if( keys[ column ] == key )
				return (CHROMA_KEY_FIRST_ROW + row) * CHROMA_COLUMNS
						 + CHROMA_KEY_FIRST_COLUMN + column;
		}
	}
	return -1;
}

//-----------------------------------------------------------------------------
static Int chromaColorFromComponents( Real red, Real green, Real blue )
{
	const Int r = (Int)(red * 255.0f + 0.5f);
	const Int g = (Int)(green * 255.0f + 0.5f);
	const Int b = (Int)(blue * 255.0f + 0.5f);
	// Chroma wants BGR, not the RGB the rest of the engine speaks.
	return (b << 16) | (g << 8) | r;
}

//-----------------------------------------------------------------------------
// The HTTP side.  Everything below here runs on the worker thread.
//-----------------------------------------------------------------------------
static Bool chromaRequest( HINTERNET connection, const char *verb, const char *path,
													 const char *body, char *reply, Int replyBytes )
{
	static const char *ACCEPT_TYPES[] = { "application/json", NULL };
	static const char *CONTENT_TYPE_HEADER = "Content-Type: application/json\r\n";

	HINTERNET request = HttpOpenRequestA( connection, verb, path, NULL, NULL, ACCEPT_TYPES,
																				INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0 );
	if( request == NULL )
		return FALSE;

	Bool ok = HttpSendRequestA( request, CONTENT_TYPE_HEADER, (DWORD)strlen( CONTENT_TYPE_HEADER ),
															(LPVOID)body, (DWORD)strlen( body ) ) ? TRUE : FALSE;
	if( ok && reply != NULL )
	{
		DWORD read = 0;
		ok = InternetReadFile( request, reply, (DWORD)(replyBytes - 1), &read ) ? TRUE : FALSE;
		reply[ ok ? read : 0 ] = 0;
	}

	InternetCloseHandle( request );
	return ok;
}

//-----------------------------------------------------------------------------
/** Ask the Chroma server for a session and write its path into sessionPath.
	* The host and port are ours, so only the id out of the reply is needed. */
static Bool chromaOpenSession( HINTERNET connection, char *sessionPath, Int sessionPathBytes )
{
	char reply[ 512 ];
	if( !chromaRequest( connection, "POST", CHROMA_INIT_PATH, CHROMA_INIT_BODY, reply, sizeof( reply ) ) )
		return FALSE;

	const char *idField = strstr( reply, "\"sessionid\"" );
	Int sessionId = 0;
	if( idField == NULL || sscanf( idField, "\"sessionid\" : %d", &sessionId ) != 1 )
	{
		if( idField == NULL || sscanf( idField, "\"sessionid\":%d", &sessionId ) != 1 )
			return FALSE;
	}
	if( sessionId <= 0 )
		return FALSE;

	_snprintf( sessionPath, sessionPathBytes, "%s/%d/keyboard", CHROMA_INIT_PATH, sessionId );
	sessionPath[ sessionPathBytes - 1 ] = 0;
	return TRUE;
}

//-----------------------------------------------------------------------------
static void chromaBuildKeyboardBody( const Int *grid, char *body, Int bodyBytes )
{
	Int used = _snprintf( body, bodyBytes, "{\"effect\":\"CHROMA_CUSTOM\",\"param\":[" );
	for( Int row = 0; row < CHROMA_ROWS && used > 0 && used < bodyBytes; ++row )
	{
		used += _snprintf( body + used, bodyBytes - used, row == 0 ? "[" : ",[" );
		for( Int column = 0; column < CHROMA_COLUMNS && used > 0 && used < bodyBytes; ++column )
			used += _snprintf( body + used, bodyBytes - used, column == 0 ? "%d" : ",%d",
												 grid[ row * CHROMA_COLUMNS + column ] );
		used += _snprintf( body + used, bodyBytes - used, "]" );
	}
	_snprintf( body + used, bodyBytes - used, "]}" );
	body[ bodyBytes - 1 ] = 0;
}

//-----------------------------------------------------------------------------
static DWORD WINAPI chromaWorkerMain( LPVOID )
{
	// Big enough for six rows of twenty-two ten-digit numbers and the punctuation.
	char body[ CHROMA_CELLS * 12 + 64 ];
	char sessionPath[ 128 ];
	Int sent[ CHROMA_CELLS ];
	Int grid[ CHROMA_CELLS ];

	HINTERNET internet = InternetOpenA( "ZeroHourReforged", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0 );
	HINTERNET connection = NULL;
	if( internet != NULL )
	{
		DWORD timeout = CHROMA_TIMEOUT_MS;
		InternetSetOptionA( internet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof( timeout ) );
		InternetSetOptionA( internet, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof( timeout ) );
		InternetSetOptionA( internet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof( timeout ) );
		connection = InternetConnectA( internet, CHROMA_HOST, CHROMA_PORT, NULL, NULL,
																	 INTERNET_SERVICE_HTTP, 0, 0 );
	}

	// ponytail: one attempt at startup.  Synapse started after the game is not
	// picked up; retrying on a timer would mean a second piece of state to own.
	if( connection == NULL || !chromaOpenSession( connection, sessionPath, sizeof( sessionPath ) ) )
	{
		DEBUG_LOG(( "Chroma: no Razer server on %s:%d, keyboard lighting is off for this run\n",
								CHROMA_HOST, (Int)CHROMA_PORT ));
		if( connection != NULL )
			InternetCloseHandle( connection );
		if( internet != NULL )
			InternetCloseHandle( internet );
		return 0;
	}

	memset( sent, 0, sizeof( sent ) );
	DWORD lastSendMs = 0;
	while( InterlockedCompareExchange( &s_workerShouldStop, 0, 0 ) == 0 )
	{
		EnterCriticalSection( &s_gridLock );
		memcpy( grid, s_pendingGrid, sizeof( grid ) );
		LeaveCriticalSection( &s_gridLock );

		const DWORD nowMs = timeGetTime();
		const Bool changed = memcmp( grid, sent, sizeof( grid ) ) != 0;
		if( changed || nowMs - lastSendMs >= CHROMA_KEEPALIVE_MS )
		{
			chromaBuildKeyboardBody( grid, body, sizeof( body ) );
			chromaRequest( connection, "PUT", sessionPath, body, NULL, 0 );
			memcpy( sent, grid, sizeof( sent ) );
			lastSendMs = nowMs;
		}

		Sleep( CHROMA_SEND_INTERVAL_MS );
	}

	InternetCloseHandle( connection );
	InternetCloseHandle( internet );
	return 0;
}

//-----------------------------------------------------------------------------
// The game state side.  Everything below here runs on the main thread.
//-----------------------------------------------------------------------------
static void chromaFillGrid( Int *grid )
{
	Real red = 0.5f, green = 0.5f, blue = 0.5f;
	Player *localPlayer = ThePlayerList ? ThePlayerList->getLocalPlayer() : NULL;
	if( localPlayer != NULL )
	{
		UnsignedByte r, g, b, a;
		GameGetColorComponents( localPlayer->getPlayerColor(), &r, &g, &b, &a );
		red = r / 255.0f;
		green = g / 255.0f;
		blue = b / 255.0f;
	}

	// How hard the board is being dragged towards red, zero when nothing is
	// shooting at us.
	Real alarm = 0.0f;
	const Bool inGame = TheGameLogic && TheGameLogic->isInGame();
	if( inGame && localPlayer != NULL )
	{
		const UnsignedInt frame = TheGameLogic->getFrame();
		const UnsignedInt attackedFrame = localPlayer->getAttackedFrame();
		if( attackedFrame > 0 && frame - attackedFrame < (UnsignedInt)UNDER_ATTACK_FRAMES )
		{
			const Real seconds = (Real)frame / (Real)LOGICFRAMES_PER_SECOND;
			const Real phase = sinf( seconds * UNDER_ATTACK_PULSE_HZ * TWO_PI );
			alarm = UNDER_ATTACK_DEPTH * (0.5f + 0.5f * phase);
		}
	}

	const Int ambient = chromaColorFromComponents(
		red * AMBIENT_SCALE * (1.0f - alarm) + alarm,
		green * AMBIENT_SCALE * (1.0f - alarm),
		blue * AMBIENT_SCALE * (1.0f - alarm) );
	for( Int cell = 0; cell < CHROMA_CELLS; ++cell )
		grid[ cell ] = ambient;

	// Outside a match there is no command bar to read, so the flat bed is the
	// whole picture.
	if( !inGame || TheHotKeyManager == NULL )
		return;

	const Int pressable = chromaColorFromComponents(
		red * PRESSABLE_SCALE * (1.0f - alarm) + alarm,
		green * PRESSABLE_SCALE * (1.0f - alarm),
		blue * PRESSABLE_SCALE * (1.0f - alarm) );
	const Int unavailable = chromaColorFromComponents( alarm, 0.0f, 0.0f );

	for( Int row = 0; row < CHROMA_KEY_ROW_COUNT; ++row )
	{
		const char *keys = CHROMA_KEY_ROWS[ row ];
		for( Int column = 0; keys[ column ] != 0; ++column )
		{
			const char keyText[ 2 ] = { keys[ column ], 0 };
			Bool isPressable = FALSE;
			if( !TheHotKeyManager->findHotKey( AsciiString( keyText ), &isPressable ) )
				continue;

			grid[ chromaCellForKey( keys[ column ] ) ] = isPressable ? pressable : unavailable;
		}
	}
}

//-----------------------------------------------------------------------------
void updateChromaKeyboard( void )
{
	if( !s_workerRunning )
	{
		InitializeCriticalSection( &s_gridLock );
		DWORD threadId = 0;
		s_workerThread = ::CreateThread( NULL, 0, chromaWorkerMain, NULL, 0, &threadId );
		if( s_workerThread == NULL )
		{
			DeleteCriticalSection( &s_gridLock );
			return;
		}
		s_workerRunning = TRUE;
	}

	Int grid[ CHROMA_CELLS ];
	chromaFillGrid( grid );

	EnterCriticalSection( &s_gridLock );
	memcpy( s_pendingGrid, grid, sizeof( s_pendingGrid ) );
	LeaveCriticalSection( &s_gridLock );
}

//-----------------------------------------------------------------------------
void shutdownChromaKeyboard( void )
{
	if( !s_workerRunning )
		return;

	InterlockedExchange( &s_workerShouldStop, 1 );
	WaitForSingleObject( s_workerThread, 2000 );
	CloseHandle( s_workerThread );
	s_workerThread = NULL;
	DeleteCriticalSection( &s_gridLock );
	s_workerRunning = FALSE;
}
