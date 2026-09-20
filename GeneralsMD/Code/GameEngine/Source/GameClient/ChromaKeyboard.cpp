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
#include "Common/Energy.h"
#include "Common/GameCommon.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/SpecialPowerModule.h"

//-----------------------------------------------------------------------------
// The devices, and where each one's cells sit in the single flat array the main
// thread fills and the worker sends.  The keyboard grid is fixed at six rows of
// twenty-two whatever board is underneath, the mouse at nine of seven, and the
// mousepad is one strip of fifteen.
//-----------------------------------------------------------------------------
struct ChromaDevice
{
	const char *endpoint;
	const char *effect;
	Int rows;
	Int columns;
	Int firstCell;
};

static const Int KEYBOARD_ROWS = 6;
static const Int KEYBOARD_COLUMNS = 22;
static const Int KEYBOARD_CELLS = KEYBOARD_ROWS * KEYBOARD_COLUMNS;
static const Int MOUSE_CELLS = 9 * 7;
static const Int MOUSEPAD_CELLS = 15;

static const Int KEYBOARD_FIRST_CELL = 0;
static const Int MOUSE_FIRST_CELL = KEYBOARD_CELLS;
static const Int MOUSEPAD_FIRST_CELL = MOUSE_FIRST_CELL + MOUSE_CELLS;
static const Int CHROMA_CELLS = MOUSEPAD_FIRST_CELL + MOUSEPAD_CELLS;

static const ChromaDevice CHROMA_DEVICES[] =
{
	{ "keyboard", "CHROMA_CUSTOM",  KEYBOARD_ROWS, KEYBOARD_COLUMNS, KEYBOARD_FIRST_CELL },
	// The mouse takes CUSTOM2, which is the nine by seven grid; plain CUSTOM on a
	// mouse is the old seven-lamp effect and ignores most of the device.
	{ "mouse",    "CHROMA_CUSTOM2", 9,             7,                MOUSE_FIRST_CELL    },
	{ "mousepad", "CHROMA_CUSTOM",  1,             MOUSEPAD_CELLS,   MOUSEPAD_FIRST_CELL },
};
static const Int CHROMA_DEVICE_COUNT = sizeof( CHROMA_DEVICES ) / sizeof( CHROMA_DEVICES[ 0 ] );

static const char *CHROMA_HOST = "localhost";
static const INTERNET_PORT CHROMA_PORT = 54235;
static const char *CHROMA_INIT_PATH = "/razer/chromasdk";
static const char *CHROMA_INIT_BODY =
	"{\"title\":\"Zero Hour Reforged\","
	"\"description\":\"Command bar, power and superweapons on the hardware\","
	"\"author\":{\"name\":\"Zero Hour Reforged\",\"contact\":\"https://github.com/olcayseygan/CnCGeneralsZH-Reforged\"},"
	"\"device_supported\":[\"keyboard\",\"mouse\",\"mousepad\"],"
	"\"category\":\"application\"}";

static const DWORD CHROMA_TIMEOUT_MS = 500;
static const DWORD CHROMA_SEND_INTERVAL_MS = 100;
/// The session expires after ten idle seconds, so a still frame is resent well inside that.
static const DWORD CHROMA_KEEPALIVE_MS = 4000;

/// The four rows of an ANSI board, in the order their keys sit on it.  Every
/// command bar hotkey the game hands out is a letter, and the digits carry the
/// power meter instead, so this is the whole map rather than a subset of one.
static const char *CHROMA_KEY_ROWS[] = { "1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm" };
static const Int CHROMA_KEY_ROW_COUNT = 4;
/// Row zero of the table is the digits, which the command bar does not get.
static const Int CHROMA_POWER_ROW = 0;
static const Int CHROMA_FIRST_HOTKEY_ROW = 1;
/// Column zero is the logo strip and column one is escape, tab, caps and shift,
/// so every one of these four rows starts at two.  Row zero is the function keys.
static const Int CHROMA_KEY_FIRST_COLUMN = 2;
static const Int CHROMA_KEY_FIRST_ROW = 1;

/// The numpad is its own block on the right of the same grid.
static const Int NUMPAD_FIRST_ROW = 1;
static const Int NUMPAD_LAST_ROW = 5;
static const Int NUMPAD_FIRST_COLUMN = 18;
static const Int NUMPAD_LAST_COLUMN = 21;

static const Real AMBIENT_SCALE = 0.22f;			///< the unlit bed of player colour
static const Real PRESSABLE_SCALE = 1.0f;			///< a command key that would fire if pressed
static const Int UNDER_ATTACK_FRAMES = LOGICFRAMES_PER_SECOND * 4;
static const Real UNDER_ATTACK_PULSE_HZ = 2.5f;
static const Real UNDER_ATTACK_DEPTH = 0.75f;		///< how far the pulse drags the board to red

/// Half of this many frames lit, half dark.
static const Int BLINK_PERIOD_FRAMES = 16;
/// Walking every building the player owns is not a per-frame job, and a
/// superweapon's charge does not move fast enough to need one.
static const Int SUPERWEAPON_SCAN_INTERVAL_FRAMES = 15;

//-----------------------------------------------------------------------------
// The main thread writes s_pendingCells, the worker reads it.  One lock over the
// whole array: it is 840 bytes copied ten times a second.
//-----------------------------------------------------------------------------
static CRITICAL_SECTION s_cellLock;
static Int s_pendingCells[ CHROMA_CELLS ];
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
				return (CHROMA_KEY_FIRST_ROW + row) * KEYBOARD_COLUMNS
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
static Bool chromaBlinkIsOn( UnsignedInt frame )
{
	return ((frame / (BLINK_PERIOD_FRAMES / 2)) & 1) == 0;
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
/** Ask the Chroma server for a session, and read back the address it wants the
	* frames on.  That address is not the one the request went to and not a path
	* under it either: Synapse hands out its own port and its own root, so it has
	* to be parsed rather than assembled. */
static Bool chromaOpenSession( HINTERNET connection, char *host, Int hostBytes,
															 INTERNET_PORT *port, char *path, Int pathBytes )
{
	char reply[ 512 ];
	if( !chromaRequest( connection, "POST", CHROMA_INIT_PATH, CHROMA_INIT_BODY, reply, sizeof( reply ) ) )
		return FALSE;

	const char *uri = strstr( reply, "http://" );
	if( uri == NULL )
		return FALSE;
	uri += strlen( "http://" );

	char parsedHost[ 64 ];
	char parsedPath[ 128 ];
	UnsignedShort parsedPort = 0;
	if( sscanf( uri, "%63[^:/]:%hu%127[^\"]", parsedHost, &parsedPort, parsedPath ) == 3 )
		*port = (INTERNET_PORT)parsedPort;
	else if( sscanf( uri, "%63[^:/]%127[^\"]", parsedHost, parsedPath ) == 2 )
		*port = INTERNET_DEFAULT_HTTP_PORT;
	else
		return FALSE;

	_snprintf( host, hostBytes, "%s", parsedHost );
	host[ hostBytes - 1 ] = 0;
	_snprintf( path, pathBytes, "%s", parsedPath );
	path[ pathBytes - 1 ] = 0;
	return TRUE;
}

//-----------------------------------------------------------------------------
/** A one-row device takes a flat array of colours, a taller one an array of rows. */
static void chromaBuildDeviceBody( const ChromaDevice &device, const Int *cells,
																	 char *body, Int bodyBytes )
{
	const Bool nested = device.rows > 1;
	Int used = _snprintf( body, bodyBytes, "{\"effect\":\"%s\",\"param\":[", device.effect );
	for( Int row = 0; row < device.rows && used > 0 && used < bodyBytes; ++row )
	{
		if( nested )
			used += _snprintf( body + used, bodyBytes - used, row == 0 ? "[" : ",[" );
		for( Int column = 0; column < device.columns && used > 0 && used < bodyBytes; ++column )
		{
			const Bool first = row == 0 && column == 0;
			used += _snprintf( body + used, bodyBytes - used,
												 (nested ? column == 0 : first) ? "%d" : ",%d",
												 cells[ row * device.columns + column ] );
		}
		if( nested )
			used += _snprintf( body + used, bodyBytes - used, "]" );
	}
	_snprintf( body + used, bodyBytes - used, "]}" );
	body[ bodyBytes - 1 ] = 0;
}

//-----------------------------------------------------------------------------
static DWORD WINAPI chromaWorkerMain( LPVOID )
{
	// Big enough for the largest device's cells as ten-digit numbers, plus the
	// punctuation and the effect name.
	char body[ KEYBOARD_CELLS * 12 + 128 ];
	char sessionHost[ 64 ];
	char sessionRoot[ 128 ];
	char devicePath[ CHROMA_DEVICE_COUNT ][ 160 ];
	INTERNET_PORT sessionPort = 0;
	Int sent[ CHROMA_CELLS ];
	Int cells[ CHROMA_CELLS ];

	HINTERNET internet = InternetOpenA( "ZeroHourReforged", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0 );
	HINTERNET handshake = NULL;
	if( internet != NULL )
	{
		DWORD timeout = CHROMA_TIMEOUT_MS;
		InternetSetOptionA( internet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof( timeout ) );
		InternetSetOptionA( internet, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof( timeout ) );
		InternetSetOptionA( internet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof( timeout ) );
		handshake = InternetConnectA( internet, CHROMA_HOST, CHROMA_PORT, NULL, NULL,
																	INTERNET_SERVICE_HTTP, 0, 0 );
	}

	// ponytail: one attempt at startup.  Synapse started after the game is not
	// picked up; retrying on a timer would mean a second piece of state to own.
	Bool opened = handshake != NULL
							&& chromaOpenSession( handshake, sessionHost, sizeof( sessionHost ),
																		&sessionPort, sessionRoot, sizeof( sessionRoot ) );
	if( handshake != NULL )
		InternetCloseHandle( handshake );

	HINTERNET connection = NULL;
	if( opened )
		connection = InternetConnectA( internet, sessionHost, sessionPort, NULL, NULL,
																	 INTERNET_SERVICE_HTTP, 0, 0 );
	if( connection == NULL )
	{
		DEBUG_LOG(( "Chroma: no Razer server on %s:%d, hardware lighting is off for this run\n",
								CHROMA_HOST, (Int)CHROMA_PORT ));
		if( internet != NULL )
			InternetCloseHandle( internet );
		return 0;
	}
	for( Int device = 0; device < CHROMA_DEVICE_COUNT; ++device )
	{
		_snprintf( devicePath[ device ], sizeof( devicePath[ device ] ), "%s/%s",
							 sessionRoot, CHROMA_DEVICES[ device ].endpoint );
		devicePath[ device ][ sizeof( devicePath[ device ] ) - 1 ] = 0;
	}
	DEBUG_LOG(( "Chroma: session open on %s:%d%s\n", sessionHost, (Int)sessionPort, sessionRoot ));

	memset( sent, 0, sizeof( sent ) );
	DWORD lastSendMs = 0;
	Bool answersLogged = FALSE;
	while( InterlockedCompareExchange( &s_workerShouldStop, 0, 0 ) == 0 )
	{
		EnterCriticalSection( &s_cellLock );
		memcpy( cells, s_pendingCells, sizeof( cells ) );
		LeaveCriticalSection( &s_cellLock );

		const DWORD nowMs = timeGetTime();
		const Bool keepalive = lastSendMs == 0 || nowMs - lastSendMs >= CHROMA_KEEPALIVE_MS;
		Bool sentAnything = FALSE;
		for( Int device = 0; device < CHROMA_DEVICE_COUNT; ++device )
		{
			const ChromaDevice &info = CHROMA_DEVICES[ device ];
			const Int deviceBytes = (Int)sizeof( Int ) * info.rows * info.columns;
			if( !keepalive && memcmp( cells + info.firstCell, sent + info.firstCell, deviceBytes ) == 0 )
				continue;

			chromaBuildDeviceBody( info, cells + info.firstCell, body, sizeof( body ) );
			// The server answers every frame with a result code, and a device it does
			// not have is refused rather than ignored, so the answer is worth a line
			// the first time round.
			char reply[ 64 ];
			const Bool sentOk = chromaRequest( connection, "PUT", devicePath[ device ],
																				 body, reply, sizeof( reply ) );
			if( !answersLogged )
			{
				DEBUG_LOG(( "Chroma: %s %s, answer %s\n", info.endpoint,
										sentOk ? "took the frame" : "refused it", sentOk ? reply : "none" ));
			}
			memcpy( sent + info.firstCell, cells + info.firstCell, deviceBytes );
			sentAnything = TRUE;
		}
		answersLogged = TRUE;
		if( sentAnything )
			lastSendMs = nowMs;

		Sleep( CHROMA_SEND_INTERVAL_MS );
	}

	// Without this the hardware holds the last frame sent until the session times
	// out, so the board stays lit for ten seconds after the game is gone.
	chromaRequest( connection, "DELETE", sessionRoot, "", NULL, 0 );
	InternetCloseHandle( connection );
	InternetCloseHandle( internet );
	return 0;
}

//-----------------------------------------------------------------------------
// The game state side.  Everything below here runs on the main thread.
//-----------------------------------------------------------------------------
struct SuperweaponScan
{
	Real bestCharge;			///< 0 to 1, the nearest one to firing
	Bool anyReady;
};

//-----------------------------------------------------------------------------
static void scanOneSuperweapon( Object *obj, void *userData )
{
	SuperweaponScan *scan = (SuperweaponScan *)userData;

	if( !obj->isKindOf( KINDOF_FS_SUPERWEAPON )
			|| obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION )
			|| obj->testStatus( OBJECT_STATUS_SOLD )
			|| obj->isEffectivelyDead() )
		return;

	for( BehaviorModule **module = obj->getBehaviorModules(); module && *module; ++module )
	{
		SpecialPowerModuleInterface *power = (*module)->getSpecialPower();
		if( power == NULL || power->isScriptOnly() )
			continue;

		const Real charge = power->getPercentReady();
		if( charge > scan->bestCharge )
			scan->bestCharge = charge;
		if( power->isReady() )
			scan->anyReady = TRUE;
	}
}

//-----------------------------------------------------------------------------
/** The most-charged superweapon the local player owns, re-counted twice a second
	* rather than every frame. */
static const SuperweaponScan &chromaSuperweaponState( Player *localPlayer, UnsignedInt frame )
{
	static SuperweaponScan scan = { 0.0f, FALSE };
	static UnsignedInt lastScanFrame = 0;

	if( frame >= lastScanFrame && frame - lastScanFrame < (UnsignedInt)SUPERWEAPON_SCAN_INTERVAL_FRAMES )
		return scan;

	lastScanFrame = frame;
	scan.bestCharge = 0.0f;
	scan.anyReady = FALSE;
	if( localPlayer != NULL )
		localPlayer->iterateObjects( scanOneSuperweapon, &scan );
	return scan;
}

//-----------------------------------------------------------------------------
Int chromaPowerSegments( Int production, Int consumption )
{
	if( production <= 0 )
		return 0;
	if( production < consumption )
		return CHROMA_POWER_BROWNOUT;

	const Real headroom = 1.0f - (Real)consumption / (Real)production;
	const Int segments = (Int)(headroom * CHROMA_POWER_SEGMENTS + 0.999f);
	return segments > CHROMA_POWER_SEGMENTS ? CHROMA_POWER_SEGMENTS : segments;
}

//-----------------------------------------------------------------------------
/** The digit row reads as a tank of power that empties: full green while there is
	* headroom, down to nothing as consumption catches production, and the whole row
	* blinking red once it has been passed.  A player with no power plant at all
	* gets a dark row rather than a full or an empty one. */
static void chromaFillPowerRow( Int *cells, const Energy *energy, UnsignedInt frame )
{
	static const Int POWER_LIT = 0x00FF00;			///< green, in the BGR the device takes
	static const Int POWER_SHORT = 0x0000FF;		///< red
	static const Int POWER_DARK = 0x000000;

	const Int litSegments = chromaPowerSegments( energy->getProduction(), energy->getConsumption() );
	const Bool blinkOn = chromaBlinkIsOn( frame );

	const char *digits = CHROMA_KEY_ROWS[ CHROMA_POWER_ROW ];
	for( Int segment = 0; segment < CHROMA_POWER_SEGMENTS; ++segment )
	{
		Int color = POWER_DARK;
		if( litSegments == CHROMA_POWER_BROWNOUT )
			color = blinkOn ? POWER_SHORT : POWER_DARK;
		else if( segment < litSegments )
			color = POWER_LIT;
		cells[ chromaCellForKey( digits[ segment ] ) ] = color;
	}
}

//-----------------------------------------------------------------------------
static void chromaFillCells( Int *cells )
{
	// Warm white for anything about a superweapon, so it is never mistaken for the
	// green of the power row or the red of the alarm.
	static const Int SUPERWEAPON_COLOR = 0x0078DCFF;

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

	const Bool inGame = TheGameLogic && TheGameLogic->isInGame();
	const UnsignedInt frame = TheGameLogic ? TheGameLogic->getFrame() : 0;

	// How hard the board is being dragged towards red, zero when nothing is
	// shooting at us.
	Real alarm = 0.0f;
	if( inGame && localPlayer != NULL )
	{
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
		cells[ cell ] = ambient;

	// Outside a match there is no command bar, no power and no superweapon to
	// read, so the flat bed of colour is the whole picture.
	if( !inGame || localPlayer == NULL || TheHotKeyManager == NULL )
		return;

	const Int pressable = chromaColorFromComponents(
		red * PRESSABLE_SCALE * (1.0f - alarm) + alarm,
		green * PRESSABLE_SCALE * (1.0f - alarm),
		blue * PRESSABLE_SCALE * (1.0f - alarm) );
	const Int unavailable = chromaColorFromComponents( alarm, 0.0f, 0.0f );

	for( Int row = CHROMA_FIRST_HOTKEY_ROW; row < CHROMA_KEY_ROW_COUNT; ++row )
	{
		const char *keys = CHROMA_KEY_ROWS[ row ];
		for( Int column = 0; keys[ column ] != 0; ++column )
		{
			Bool isPressable = FALSE;
			const char keyText[ 2 ] = { keys[ column ], 0 };
			if( !TheHotKeyManager->findHotKey( AsciiString( keyText ), &isPressable ) )
				continue;

			cells[ chromaCellForKey( keys[ column ] ) ] = isPressable ? pressable : unavailable;
		}
	}

	chromaFillPowerRow( cells, localPlayer->getEnergy(), frame );

	// The numpad blinks while anything of yours can fire, and the mousepad strip
	// fills up as the nearest one charges.
	const SuperweaponScan &superweapons = chromaSuperweaponState( localPlayer, frame );
	const Bool blinkOn = chromaBlinkIsOn( frame );
	if( superweapons.anyReady )
	{
		const Int numpadColor = blinkOn ? SUPERWEAPON_COLOR : 0;
		for( Int row = NUMPAD_FIRST_ROW; row <= NUMPAD_LAST_ROW; ++row )
			for( Int column = NUMPAD_FIRST_COLUMN; column <= NUMPAD_LAST_COLUMN; ++column )
				cells[ row * KEYBOARD_COLUMNS + column ] = numpadColor;
	}

	Int *mousepad = cells + MOUSEPAD_FIRST_CELL;
	if( superweapons.anyReady )
	{
		for( Int led = 0; led < MOUSEPAD_CELLS; ++led )
			mousepad[ led ] = blinkOn ? SUPERWEAPON_COLOR : 0;
	}
	else if( superweapons.bestCharge > 0.0f )
	{
		const Int litLeds = (Int)(superweapons.bestCharge * MOUSEPAD_CELLS + 0.999f);
		for( Int led = 0; led < MOUSEPAD_CELLS; ++led )
			mousepad[ led ] = led < litLeds ? pressable : 0;
	}
}

//-----------------------------------------------------------------------------
void updateChromaKeyboard( void )
{
	if( !s_workerRunning )
	{
		InitializeCriticalSection( &s_cellLock );
		DWORD threadId = 0;
		s_workerThread = ::CreateThread( NULL, 0, chromaWorkerMain, NULL, 0, &threadId );
		if( s_workerThread == NULL )
		{
			DeleteCriticalSection( &s_cellLock );
			return;
		}
		s_workerRunning = TRUE;
	}

	Int cells[ CHROMA_CELLS ];
	chromaFillCells( cells );

	EnterCriticalSection( &s_cellLock );
	memcpy( s_pendingCells, cells, sizeof( s_pendingCells ) );
	LeaveCriticalSection( &s_cellLock );
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
	DeleteCriticalSection( &s_cellLock );
	s_workerRunning = FALSE;
}
