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
#include "GameClient/ControlBar.h"
#include "GameClient/Drawable.h"
#include "GameClient/Gadget.h"
#include "GameClient/GameWindow.h"
#include "GameClient/HotKey.h"
#include "GameClient/InGameUI.h"
#include "Common/Energy.h"
#include "Common/GameCommon.h"
#include "Common/GlobalData.h"
#include "Common/Money.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/ScoreKeeper.h"
#include "Common/SpecialPower.h"
#include "Common/SpecialPowerType.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/VictoryConditions.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameNetwork/NetworkInterface.h"

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
	"\"description\":\"The state of the match on the hardware\","
	"\"author\":{\"name\":\"Zero Hour Reforged\",\"contact\":\"https://github.com/olcayseygan/CnCGeneralsZH-Reforged\"},"
	"\"device_supported\":[\"keyboard\",\"mouse\",\"mousepad\"],"
	"\"category\":\"application\"}";

static const DWORD CHROMA_TIMEOUT_MS = 500;
static const DWORD CHROMA_SEND_INTERVAL_MS = 100;
/// The session dies after about ten idle seconds - measured, not assumed - so a
/// still frame is resent well inside that.
static const DWORD CHROMA_KEEPALIVE_MS = 4000;

//-----------------------------------------------------------------------------
// Colours, packed the way the device takes them: blue in the high byte, red in
// the low one.
//-----------------------------------------------------------------------------
static const Int COLOR_OFF = 0x000000;
static const Int COLOR_GREEN = 0x00FF00;
static const Int COLOR_YELLOW = 0x00FFFF;
static const Int COLOR_RED = 0x0000FF;
static const Int COLOR_AMBER = 0x0080FF;
static const Int COLOR_WHITE = 0xFFFFFF;
static const Int COLOR_WARM_WHITE = 0x78DCFF;
static const Int COLOR_BLUE = 0xFF0000;
static const Int COLOR_GOLD = 0x00D7FF;

//-----------------------------------------------------------------------------
// The keyboard grid, zone by zone.  Column zero is the strip down the left of
// the board and column one is escape, tab, caps and shift, so the typing rows
// all start at two.  Row zero is the function keys.
//-----------------------------------------------------------------------------
static const char *CHROMA_KEY_ROWS[] = { "1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm" };
static const Int CHROMA_KEY_ROW_COUNT = 4;
static const Int CHROMA_POWER_ROW = 0;
static const Int CHROMA_FIRST_HOTKEY_ROW = 1;
static const Int CHROMA_KEY_FIRST_COLUMN = 2;
static const Int CHROMA_KEY_FIRST_ROW = 1;

static const Int MATCH_STATE_ROW = 0;
static const Int MATCH_STATE_COLUMN = 1;			///< escape

static const Int FKEY_ROW = 0;
static const Int FKEY_FIRST_COLUMN = 3;
static const Int FKEY_COUNT = 12;

static const Int STATUS_ROW = 0;
static const Int STATUS_NETWORK_COLUMN = 15;	///< print screen
static const Int STATUS_RADAR_COLUMN = 16;		///< scroll lock
static const Int STATUS_PAUSED_COLUMN = 17;		///< pause

/// The six lamps of the navigation cluster, insert to page down.
static const Int ALERT_FIRST_COLUMN = 15;
static const Int ALERT_TOP_ROW = 1;
static const Int ALERT_BOTTOM_ROW = 2;
static const Int ALERTS_PER_ROW = 3;

static const Int NUMPAD_FIRST_ROW = 1;
static const Int NUMPAD_LAST_ROW = 5;
static const Int NUMPAD_FIRST_COLUMN = 18;
static const Int NUMPAD_LAST_COLUMN = 21;
static const Int NUMPAD_CELLS = (NUMPAD_LAST_ROW - NUMPAD_FIRST_ROW + 1)
															* (NUMPAD_LAST_COLUMN - NUMPAD_FIRST_COLUMN + 1);

static const Int SELECTION_HEALTH_ROW = 4;		///< the up arrow
static const Int SELECTION_HEALTH_COLUMN = 16;
static const Int SELECTION_RANK_ROW = 5;			///< the down arrow
static const Int SELECTION_RANK_COLUMN = 16;

/// The addressable keys along the bottom row, skipping the ones the grid does
/// not give a lamp of their own.
static const Int PRODUCTION_ROW = 5;
static const Int PRODUCTION_COLUMNS[] = { 1, 2, 3, 7, 11, 12, 13, 14 };
static const Int PRODUCTION_LAMPS = sizeof( PRODUCTION_COLUMNS ) / sizeof( PRODUCTION_COLUMNS[ 0 ] );

//-----------------------------------------------------------------------------
static const Real AMBIENT_SCALE = 0.22f;			///< the unlit bed of player colour
static const Real IDLE_PRODUCER_SCALE = 0.15f;	///< a factory that is building nothing
static const Int UNDER_ATTACK_FRAMES = LOGICFRAMES_PER_SECOND * 4;
static const Real UNDER_ATTACK_PULSE_HZ = 2.5f;
/// How far the alarm drags a zone towards red.  The bed, the mouse and anything
/// carrying no number take the full depth; a gauge takes a quarter of it, so a
/// brownout stays countable while the base is being shelled.
static const Real ALARM_DEPTH_EMPTY = 0.75f;
static const Real ALARM_DEPTH_DATA = 0.25f;

/// Half of this many frames lit, half dark.
static const Int BLINK_PERIOD_FRAMES = 16;
/// A power announces itself by blinking and then holds steady.  A key that blinks
/// for the rest of the match stops being news and starts being an irritation.
static const Int READY_BLINK_FRAMES = LOGICFRAMES_PER_SECOND * 3;
/// The general's star on screen flashes on a one second cycle; the promotion
/// lamp keeps to the same one rather than inventing a second rhythm.
static const Int STAR_PERIOD_FRAMES = LOGICFRAMES_PER_SECOND;
static const Int ALERT_HOLD_FRAMES = LOGICFRAMES_PER_SECOND * 3;
/// Walking every object the player owns is not a per-frame job, and neither a
/// build clock nor a superweapon's charge moves fast enough to need one.
static const Int WALK_INTERVAL_FRAMES = 15;

static const UnsignedInt MONEY_PER_SEGMENT = 1000;

//-----------------------------------------------------------------------------
// The main thread writes s_pendingCells, the worker reads it.  One lock over the
// whole array: it is 840 bytes copied ten times a second.
//-----------------------------------------------------------------------------
static CRITICAL_SECTION s_cellLock;
static Int s_pendingCells[ CHROMA_CELLS ];
static Bool s_workerRunning = FALSE;
static Bool s_disabled = FALSE;
static volatile LONG s_workerShouldStop = 0;
static HANDLE s_workerThread = NULL;

//-----------------------------------------------------------------------------
// Pure helpers.
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
Int chromaBarSegments( Real fraction, Int segments )
{
	if( fraction <= 0.0f )
		return 0;
	const Int lit = (Int)(fraction * segments + 0.999f);
	return lit > segments ? segments : lit;
}

//-----------------------------------------------------------------------------
Int chromaMoneySegments( UnsignedInt money, Int segments )
{
	const UnsignedInt lit = money / MONEY_PER_SEGMENT;
	return lit > (UnsignedInt)segments ? segments : (Int)lit;
}

//-----------------------------------------------------------------------------
static Int chromaColor( Real red, Real green, Real blue )
{
	const Int r = (Int)(red * 255.0f + 0.5f);
	const Int g = (Int)(green * 255.0f + 0.5f);
	const Int b = (Int)(blue * 255.0f + 0.5f);
	// Chroma wants BGR, not the RGB the rest of the engine speaks.
	return (b << 16) | (g << 8) | r;
}

//-----------------------------------------------------------------------------
static Int chromaScale( Int color, Real scale )
{
	const Int b = (Int)(((color >> 16) & 0xFF) * scale);
	const Int g = (Int)(((color >> 8) & 0xFF) * scale);
	const Int r = (Int)((color & 0xFF) * scale);
	return (b << 16) | (g << 8) | r;
}

//-----------------------------------------------------------------------------
/** Drag a colour towards red by the alarm depth, keeping whatever of it survives. */
static Int chromaAlarmed( Int color, Real alarm )
{
	if( alarm <= 0.0f )
		return color;

	const Real keep = 1.0f - alarm;
	const Int b = (Int)(((color >> 16) & 0xFF) * keep);
	const Int g = (Int)(((color >> 8) & 0xFF) * keep);
	Int r = (Int)((color & 0xFF) * keep + alarm * 255.0f);
	if( r > 255 )
		r = 255;
	return (b << 16) | (g << 8) | r;
}

//-----------------------------------------------------------------------------
static Bool chromaBlinkIsOn( UnsignedInt frame, Int periodFrames )
{
	return ((frame / (periodFrames / 2)) & 1) == 0;
}

//-----------------------------------------------------------------------------
static Int chromaKeyboardCell( Int row, Int column )
{
	return row * KEYBOARD_COLUMNS + column;
}

//-----------------------------------------------------------------------------
/** Ready holds the eye for a few seconds and then stops asking for it. */
static Int chromaReadyColor( UnsignedInt frame, UnsignedInt readySince, Int color )
{
	if( readySince == 0 || frame < readySince )
		return color;
	if( frame - readySince >= (UnsignedInt)READY_BLINK_FRAMES )
		return color;
	return chromaBlinkIsOn( frame, BLINK_PERIOD_FRAMES ) ? color : COLOR_OFF;
}

/// When each power last came ready, so the key can announce itself and then stop
/// shouting.  Zero means it is not ready at all.
static UnsignedInt s_powerReadySince[ SPECIALPOWER_COUNT ];
static UnsignedInt s_superweaponReadySince = 0;

//-----------------------------------------------------------------------------
// The superweapon effects.  A launch takes the whole board for a few seconds:
// nothing else on it matters while a nuke is in the air.
//-----------------------------------------------------------------------------
enum ChromaEffect
{
	EFFECT_NONE = 0,
	EFFECT_NUKE,
	EFFECT_LASER,
	EFFECT_SCUD
};

static const Int NUKE_EFFECT_FRAMES = LOGICFRAMES_PER_SECOND * 4;
static const Int LASER_EFFECT_FRAMES = LOGICFRAMES_PER_SECOND * 3;
static const Int SCUD_EFFECT_FRAMES = LOGICFRAMES_PER_SECOND * 4;
// Both the nuke and the scud storm are ripples: rings behind rings, running out
// from where the thing landed.  What separates them is size and how many.  The
// nuke is one ripple from the middle of the board with long wavelength, so the
// rings are wide and reach the corners.  The scud storm is a lot of small ones,
// each no bigger than a few keys, scattered and staggered.
//
// Distances are in half-boards: a device is two of these across and about 1.4
// from its centre to its corner.
/// Long and fast is what makes it feel heavy: a ring nearly a whole half-board
/// wide, moving quickly, so the board swells rather than flickers.
static const Real NUKE_RIPPLE_SPEED = 2.0f;			///< how fast the outermost ring travels
static const Real NUKE_RIPPLE_WAVELENGTH = 0.75f;	///< ring to ring
static const Real NUKE_RIPPLE_REACH = 1.5f;			///< where the rings have died away
/// The detonation, before the first ring has gone anywhere.
static const Real NUKE_FLASH_SECONDS = 0.45f;

/// Twenty landings across the four seconds, one after another rather than at
/// random moments, each one quick and a little wider than a key cluster.
static const Int SCUD_RIPPLE_COUNT = 20;
static const Real SCUD_RIPPLE_LIFE = 0.8f;
static const Real SCUD_RIPPLE_SPEED = 1.8f;
static const Real SCUD_RIPPLE_WAVELENGTH = 0.26f;
static const Real SCUD_RIPPLE_REACH = 0.75f;
/// How far off its slot a landing may drift, as a fraction of the gap between
/// slots.  Without it the salvo arrives like a metronome.
static const Real SCUD_LANDING_JITTER = 0.8f;

static ChromaEffect s_effect = EFFECT_NONE;
static UnsignedInt s_effectStartFrame = 0;
static UnsignedInt s_effectSeed = 0;

//-----------------------------------------------------------------------------
/** A cheap integer hash, for the flame's flicker and the scud's landing spots.
	* Not the game's random number generator: that one is logic state, and drawing
	* from it here would desync every machine that has a keyboard. */
static UnsignedInt chromaHash( UnsignedInt value )
{
	value ^= value >> 16;
	value *= 0x7feb352du;
	value ^= value >> 15;
	value *= 0x846ca68bu;
	value ^= value >> 16;
	return value;
}

//-----------------------------------------------------------------------------
static Real chromaHashUnit( UnsignedInt value )
{
	return (Real)(chromaHash( value ) & 0xFFFF) / 65535.0f;
}

//-----------------------------------------------------------------------------
static Int chromaEffectFrames( ChromaEffect effect )
{
	switch( effect )
	{
		case EFFECT_NUKE:		return NUKE_EFFECT_FRAMES;
		case EFFECT_LASER:	return LASER_EFFECT_FRAMES;
		case EFFECT_SCUD:		return SCUD_EFFECT_FRAMES;
		default:						return 0;
	}
}

//-----------------------------------------------------------------------------
/** How bright one ripple is at this distance from where it landed.  Nothing is
	* lit ahead of the outermost ring, the crests behind it are a cosine in the
	* distance the ring has already covered, and the whole thing dies off towards
	* the ripple's reach. */
static Real chromaRippleIntensity( Real distance, Real radius, Real wavelength, Real reach )
{
	if( radius <= 0.0f || distance > radius || distance >= reach )
		return 0.0f;

	// The plain cosine, not a sharpened one.  Squaring it narrows the crests until
	// the rings read as a row of dots rather than as water.
	const Real phase = (radius - distance) / wavelength;
	const Real crest = 0.5f + 0.5f * cosf( phase * TWO_PI );
	return crest * (1.0f - distance / reach);
}

//-----------------------------------------------------------------------------
static Bool chromaEffectIsLive( UnsignedInt frame )
{
	if( s_effect == EFFECT_NONE )
		return FALSE;

	const Int duration = chromaEffectFrames( s_effect );
	if( frame < s_effectStartFrame || frame - s_effectStartFrame >= (UnsignedInt)duration )
	{
		s_effect = EFFECT_NONE;
		return FALSE;
	}
	return TRUE;
}

//-----------------------------------------------------------------------------
/** The colour a cell takes while an effect is running.  The position comes in
	* normalised so one set of maths covers a keyboard, a mouse and a fifteen lamp
	* strip without knowing what shape any of them is. */
static Int chromaEffectColor( Real across, Real down, Int cellIndex, UnsignedInt frame )
{
	const Int duration = chromaEffectFrames( s_effect );
	const Real elapsed = (Real)(frame - s_effectStartFrame) / (Real)LOGICFRAMES_PER_SECOND;
	const Real fade = 1.0f - (Real)(frame - s_effectStartFrame) / (Real)duration;

	if( s_effect == EFFECT_NUKE )
	{
		// One ripple, from the middle, big enough to reach the corners.
		const Real distance = (Real)sqrt( across * across + down * down );
		Real heat = chromaRippleIntensity( distance, elapsed * NUKE_RIPPLE_SPEED,
																			 NUKE_RIPPLE_WAVELENGTH, NUKE_RIPPLE_REACH );

		// The detonation itself, before the first ring has gone anywhere.
		if( elapsed < NUKE_FLASH_SECONDS )
		{
			const Real flash = 1.0f - elapsed / NUKE_FLASH_SECONDS;
			if( flash > heat )
				heat = flash;
		}

		heat *= fade;
		// Deep red where it is weakest, orange through the middle, white only at
		// the front and the flash, which is what a fireball does.
		return chromaColor( heat, heat * heat, heat * heat * heat * heat );
	}

	if( s_effect == EFFECT_LASER )
	{
		// A flame, not a wave: every lamp burns, and the pattern crawls.
		const UnsignedInt crawl = frame / 2;
		const Real flicker = chromaHashUnit( (UnsignedInt)cellIndex * 2654435761u + crawl );
		const Real body = (0.40f + 0.60f * flicker) * fade;
		// Dark blue through the body, cyan where it burns hardest, no red anywhere.
		return chromaColor( body * body * body * 0.25f, body * body * 0.75f, body );
	}

	// A lot of small ripples instead of one big one: rain on water rather than a
	// single stone, in no particular place and at no particular moment.
	const Real lastLanding = (Real)duration / (Real)LOGICFRAMES_PER_SECOND - SCUD_RIPPLE_LIFE;
	Real best = 0.0f;
	for( Int ripple = 0; ripple < SCUD_RIPPLE_COUNT; ++ripple )
	{
		const UnsignedInt rippleSeed = s_effectSeed + (UnsignedInt)ripple * 0x9e3779b9u;
		// One after another down the salvo, each nudged off its slot so the rhythm
		// is a bombardment rather than a metronome.
		const Real spacing = lastLanding / (Real)SCUD_RIPPLE_COUNT;
		const Real jitter = (chromaHashUnit( rippleSeed + 2 ) - 0.5f) * spacing * SCUD_LANDING_JITTER;
		const Real age = elapsed - ((Real)ripple * spacing + jitter);
		if( age < 0.0f || age >= SCUD_RIPPLE_LIFE )
			continue;

		const Real originAcross = (chromaHashUnit( rippleSeed ) - 0.5f) * 2.0f;
		const Real originDown = (chromaHashUnit( rippleSeed + 1 ) - 0.5f) * 2.0f;
		const Real dx = across - originAcross;
		const Real dy = down - originDown;
		const Real distance = (Real)sqrt( dx * dx + dy * dy );

		const Real intensity = chromaRippleIntensity( distance, age * SCUD_RIPPLE_SPEED,
																									SCUD_RIPPLE_WAVELENGTH, SCUD_RIPPLE_REACH )
												 * (1.0f - age / SCUD_RIPPLE_LIFE);
		if( intensity > best )
			best = intensity;
	}
	const Real gas = best * fade;
	return chromaColor( gas * gas * gas * 0.4f, gas, gas * gas * gas * 0.4f );
}

//-----------------------------------------------------------------------------
void chromaSuperweaponLaunched( Int specialPowerType )
{
	ChromaEffect effect = EFFECT_NONE;
	switch( specialPowerType )
	{
		case SPECIAL_PARTICLE_UPLINK_CANNON:
		case SUPW_SPECIAL_PARTICLE_UPLINK_CANNON:
		case LAZR_SPECIAL_PARTICLE_UPLINK_CANNON:
			effect = EFFECT_LASER;
			break;

		case SPECIAL_NEUTRON_MISSILE:
		case NUKE_SPECIAL_NEUTRON_MISSILE:
		case SUPW_SPECIAL_NEUTRON_MISSILE:
			effect = EFFECT_NUKE;
			break;

		case SPECIAL_SCUD_STORM:
			effect = EFFECT_SCUD;
			break;

		default:
			return;
	}

	s_effect = effect;
	s_effectStartFrame = TheGameLogic ? TheGameLogic->getFrame() : 0;
	s_effectSeed = chromaHash( s_effectStartFrame + (UnsignedInt)specialPowerType );
}

//-----------------------------------------------------------------------------
/** Paint the running effect over every device, on top of whatever the layout put
	* there.  Nothing else on the board matters while a nuke is in the air.
	*
	* Both axes are divided by the same number, which is the half width.  Dividing
	* each axis by its own extent instead stretches a keyboard's six rows to the
	* same span as its twenty-two columns, and a ripple drawn in those coordinates
	* jumps four rings per row: it comes out as a grid of dots rather than as a
	* wave.  Keys are about square, so one step across and one step down have to
	* be worth the same. */
static void chromaPaintEffect( Int *cells, UnsignedInt frame )
{
	for( Int device = 0; device < CHROMA_DEVICE_COUNT; ++device )
	{
		const ChromaDevice &info = CHROMA_DEVICES[ device ];
		const Real halfWidth = (Real)(info.columns - 1) * 0.5f;
		const Real halfHeight = (Real)(info.rows - 1) * 0.5f;
		for( Int row = 0; row < info.rows; ++row )
		{
			for( Int column = 0; column < info.columns; ++column )
			{
				const Real across = halfWidth > 0.0f ? ((Real)column - halfWidth) / halfWidth : 0.0f;
				const Real down = halfWidth > 0.0f ? ((Real)row - halfHeight) / halfWidth : 0.0f;
				const Int cell = info.firstCell + row * info.columns + column;
				cells[ cell ] = chromaEffectColor( across, down, cell, frame );
			}
		}
	}
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
// The walk.  Everything below here runs on the main thread.
//
// One pass over the player's objects, twice a second, collects every special
// power's charge and every factory's build clock.  The stock control bar pays a
// full object walk per shortcut button per frame; this pays one for all of it.
//-----------------------------------------------------------------------------
struct ChromaProducer
{
	ObjectID id;
	Real progress;			///< 0 to 1, or -1 for a factory that is building nothing
};

struct ChromaSnapshot
{
	Real powerCharge[ SPECIALPOWER_COUNT ];		///< -1 where the player has no such power
	Bool powerReady[ SPECIALPOWER_COUNT ];
	Real bestSuperweaponCharge;
	Bool anySuperweaponReady;
	ChromaProducer producers[ PRODUCTION_LAMPS ];
	Int producerCount;
};

//-----------------------------------------------------------------------------
/** Keep the lowest object ids, so a lamp stays with the same factory for the
	* whole match instead of shuffling every time a building goes up. */
static void chromaRememberProducer( ChromaSnapshot *snapshot, ObjectID id, Real progress )
{
	Int slot = snapshot->producerCount;
	if( slot >= PRODUCTION_LAMPS )
	{
		if( id >= snapshot->producers[ PRODUCTION_LAMPS - 1 ].id )
			return;
		slot = PRODUCTION_LAMPS - 1;
	}
	else
	{
		++snapshot->producerCount;
	}

	while( slot > 0 && snapshot->producers[ slot - 1 ].id > id )
	{
		snapshot->producers[ slot ] = snapshot->producers[ slot - 1 ];
		--slot;
	}
	snapshot->producers[ slot ].id = id;
	snapshot->producers[ slot ].progress = progress;
}

//-----------------------------------------------------------------------------
static void chromaVisitObject( Object *obj, void *userData )
{
	ChromaSnapshot *snapshot = (ChromaSnapshot *)userData;

	if( obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION )
			|| obj->testStatus( OBJECT_STATUS_SOLD )
			|| obj->isEffectivelyDead() )
		return;

	const Bool isSuperweapon = obj->isKindOf( KINDOF_FS_SUPERWEAPON );
	for( BehaviorModule **module = obj->getBehaviorModules(); module && *module; ++module )
	{
		SpecialPowerModuleInterface *power = (*module)->getSpecialPower();
		if( power == NULL || power->isScriptOnly() )
			continue;

		const SpecialPowerTemplate *powerTemplate = power->getSpecialPowerTemplate();
		if( powerTemplate == NULL )
			continue;

		const Int type = (Int)powerTemplate->getSpecialPowerType();
		if( type < 0 || type >= SPECIALPOWER_COUNT )
			continue;

		const Real charge = power->getPercentReady();
		if( charge > snapshot->powerCharge[ type ] )
			snapshot->powerCharge[ type ] = charge;
		if( power->isReady() )
			snapshot->powerReady[ type ] = TRUE;

		if( isSuperweapon )
		{
			if( charge > snapshot->bestSuperweaponCharge )
				snapshot->bestSuperweaponCharge = charge;
			if( power->isReady() )
				snapshot->anySuperweaponReady = TRUE;
		}
	}

	ProductionUpdateInterface *production = obj->getProductionUpdateInterface();
	if( production != NULL )
	{
		const ProductionEntry *head = production->firstProduction();
		// getPercentComplete is already a percentage: the control bar hands it
		// straight to the button clock, which wants 0 to 100.
		const Real progress = head != NULL ? head->getPercentComplete() / 100.0f : -1.0f;
		chromaRememberProducer( snapshot, obj->getID(), progress );
	}
}

//-----------------------------------------------------------------------------
static const ChromaSnapshot &chromaWalkPlayer( Player *localPlayer, UnsignedInt frame )
{
	static ChromaSnapshot snapshot;
	static UnsignedInt lastWalkFrame = 0;
	static Bool everWalked = FALSE;

	if( everWalked && frame >= lastWalkFrame
			&& frame - lastWalkFrame < (UnsignedInt)WALK_INTERVAL_FRAMES )
		return snapshot;

	everWalked = TRUE;
	lastWalkFrame = frame;
	for( Int type = 0; type < SPECIALPOWER_COUNT; ++type )
	{
		snapshot.powerCharge[ type ] = -1.0f;
		snapshot.powerReady[ type ] = FALSE;
	}
	snapshot.bestSuperweaponCharge = 0.0f;
	snapshot.anySuperweaponReady = FALSE;
	snapshot.producerCount = 0;

	if( localPlayer != NULL )
		localPlayer->iterateObjects( chromaVisitObject, &snapshot );

	for( Int type = 0; type < SPECIALPOWER_COUNT; ++type )
	{
		if( !snapshot.powerReady[ type ] )
			s_powerReadySince[ type ] = 0;
		else if( s_powerReadySince[ type ] == 0 )
			s_powerReadySince[ type ] = frame;
	}
	if( !snapshot.anySuperweaponReady )
		s_superweaponReadySince = 0;
	else if( s_superweaponReadySince == 0 )
		s_superweaponReadySince = frame;

	return snapshot;
}

//-----------------------------------------------------------------------------
// The alert lamps.  EVA cannot be polled - its flag array is private and is
// consumed before anyone outside could see it - and the radar's event ring lets
// only a position escape, so the score counters are what an edge is taken from.
//-----------------------------------------------------------------------------
enum ChromaAlert
{
	ALERT_UNIT_LOST = 0,
	ALERT_BUILDING_LOST,
	ALERT_UNDER_ATTACK,
	ALERT_PROMOTION,
	ALERT_UPGRADE_DONE,
	ALERT_BUILT,

	ALERT_COUNT
};

struct ChromaAlertState
{
	UnsignedInt firedFrame[ ALERT_COUNT ];
	Int unitsLost;
	Int buildingsLost;
	Int unitsBuilt;
	Int buildingsBuilt;
	UpgradeMaskType upgrades;
	Bool haveBaseline;
};

static ChromaAlertState s_alerts;

//-----------------------------------------------------------------------------
static Int chromaAlertCell( Int alert )
{
	const Int row = alert < ALERTS_PER_ROW ? ALERT_TOP_ROW : ALERT_BOTTOM_ROW;
	return chromaKeyboardCell( row, ALERT_FIRST_COLUMN + alert % ALERTS_PER_ROW );
}

//-----------------------------------------------------------------------------
static void chromaUpdateAlerts( Player *localPlayer, UnsignedInt frame )
{
	ScoreKeeper *score = localPlayer->getScoreKeeper();
	const Int unitsLost = score->getTotalUnitsLost();
	const Int buildingsLost = score->getTotalBuildingsLost();
	const Int unitsBuilt = score->getTotalUnitsBuilt();
	const Int buildingsBuilt = score->getTotalBuildingsBuilt();
	const UpgradeMaskType upgrades = localPlayer->getCompletedUpgradeMask();

	if( s_alerts.haveBaseline )
	{
		if( unitsLost > s_alerts.unitsLost )
			s_alerts.firedFrame[ ALERT_UNIT_LOST ] = frame;
		if( buildingsLost > s_alerts.buildingsLost )
			s_alerts.firedFrame[ ALERT_BUILDING_LOST ] = frame;
		if( unitsBuilt > s_alerts.unitsBuilt || buildingsBuilt > s_alerts.buildingsBuilt )
			s_alerts.firedFrame[ ALERT_BUILT ] = frame;
		if( upgrades != s_alerts.upgrades )
			s_alerts.firedFrame[ ALERT_UPGRADE_DONE ] = frame;
	}

	s_alerts.unitsLost = unitsLost;
	s_alerts.buildingsLost = buildingsLost;
	s_alerts.unitsBuilt = unitsBuilt;
	s_alerts.buildingsBuilt = buildingsBuilt;
	s_alerts.upgrades = upgrades;
	s_alerts.haveBaseline = TRUE;
}

//-----------------------------------------------------------------------------
static void chromaResetAlerts( void )
{
	for( Int alert = 0; alert < ALERT_COUNT; ++alert )
		s_alerts.firedFrame[ alert ] = 0;
	s_alerts.haveBaseline = FALSE;
}

//-----------------------------------------------------------------------------
static Bool chromaAlertIsLit( Int alert, UnsignedInt frame )
{
	const UnsignedInt fired = s_alerts.firedFrame[ alert ];
	return fired > 0 && frame >= fired && frame - fired < (UnsignedInt)ALERT_HOLD_FRAMES;
}

//-----------------------------------------------------------------------------
// The zones.
//-----------------------------------------------------------------------------

/** The digit row reads as a tank of power that empties, and it keeps to the same
	* three colours the meter on the command bar uses so the two never disagree. */
static void chromaFillPowerRow( Int *cells, const Energy *energy, UnsignedInt frame, Real alarm )
{
	const Int production = energy->getProduction();
	const Int consumption = energy->getConsumption();
	const Int litSegments = chromaPowerSegments( production, consumption );
	const Int yellowRange = TheGlobalData ? TheGlobalData->m_powerBarYellowRange : 0;
	const Bool warning = consumption > production - yellowRange && consumption <= production;
	const Int litColor = chromaAlarmed( warning ? COLOR_YELLOW : COLOR_GREEN, alarm );
	const Int shortColor = chromaBlinkIsOn( frame, BLINK_PERIOD_FRAMES ) ? COLOR_RED : COLOR_OFF;

	const char *digits = CHROMA_KEY_ROWS[ CHROMA_POWER_ROW ];
	for( Int segment = 0; segment < CHROMA_POWER_SEGMENTS; ++segment )
	{
		Int color = COLOR_OFF;
		if( litSegments == CHROMA_POWER_BROWNOUT )
			color = shortColor;
		else if( segment < litSegments )
			color = litColor;
		cells[ chromaCellForKey( digits[ segment ] ) ] = color;
	}
}

//-----------------------------------------------------------------------------
/** The command bar, key for key, with the build clock the button is drawing on
	* screen carried as brightness.  The clock is read straight off the gadget the
	* control bar already wrote it to, so the keys cannot drift from the buttons. */
static void chromaFillCommandBar( Int *cells, Int pressable, Int unavailable )
{
	for( Int row = CHROMA_FIRST_HOTKEY_ROW; row < CHROMA_KEY_ROW_COUNT; ++row )
	{
		const char *keys = CHROMA_KEY_ROWS[ row ];
		for( Int column = 0; keys[ column ] != 0; ++column )
		{
			Bool isPressable = FALSE;
			const char keyText[ 2 ] = { keys[ column ], 0 };
			GameWindow *win = TheHotKeyManager->findHotKey( AsciiString( keyText ), &isPressable );
			if( win == NULL )
				continue;

			Int color = isPressable ? pressable : unavailable;
			if( isPressable && BitTest( win->winGetStyle(), GWS_PUSH_BUTTON ) )
			{
				const PushButtonData *buttonData = (const PushButtonData *)win->winGetUserData();
				if( buttonData != NULL && buttonData->drawClock != NO_CLOCK )
					color = chromaScale( color, buttonData->percentClock / 100.0f );
			}
			cells[ chromaCellForKey( keys[ column ] ) ] = color;
		}
	}
}

//-----------------------------------------------------------------------------
/** The function row is the generals powers tray, slot for slot, taken from the
	* same command set the on-screen tray is built from. */
static void chromaFillPowerTray( Int *cells, Player *localPlayer, const ChromaSnapshot &snapshot,
																 Int factionColor, UnsignedInt frame )
{
	const PlayerTemplate *playerTemplate = localPlayer->getPlayerTemplate();
	if( playerTemplate == NULL || TheControlBar == NULL )
		return;

	const AsciiString setName = playerTemplate->getSpecialPowerShortcutCommandSet();
	if( setName.isEmpty() )
		return;

	const CommandSet *commandSet = TheControlBar->findCommandSet( setName );
	if( commandSet == NULL )
		return;

	Int slots = playerTemplate->getSpecialPowerShortcutButtonCount();
	if( slots > FKEY_COUNT )
		slots = FKEY_COUNT;

	for( Int slot = 0; slot < slots; ++slot )
	{
		const CommandButton *button = commandSet->getCommandButton( slot );
		if( button == NULL )
			continue;

		const SpecialPowerTemplate *powerTemplate = button->getSpecialPowerTemplate();
		if( powerTemplate == NULL )
			continue;

		// A power whose promotion has not been bought is not on the tray on screen
		// and has no business on the key either, which is what made the first two
		// keys of the row light for powers nobody could use.
		const ScienceType required = powerTemplate->getRequiredScience();
		if( required != SCIENCE_INVALID && !localPlayer->hasScience( required ) )
			continue;

		const Int type = (Int)powerTemplate->getSpecialPowerType();
		if( type < 0 || type >= SPECIALPOWER_COUNT || snapshot.powerCharge[ type ] < 0.0f )
			continue;

		const Int cell = chromaKeyboardCell( FKEY_ROW, FKEY_FIRST_COLUMN + slot );
		if( snapshot.powerReady[ type ] )
			cells[ cell ] = chromaReadyColor( frame, s_powerReadySince[ type ], COLOR_WARM_WHITE );
		else
			cells[ cell ] = chromaScale( factionColor, snapshot.powerCharge[ type ] );
	}
}

//-----------------------------------------------------------------------------
/** The numpad fills from the bottom as the nearest superweapon charges, and the
	* whole block blinks once any of them can fire. */
static void chromaFillNumpad( Int *cells, const ChromaSnapshot &snapshot, Int factionColor,
															UnsignedInt frame )
{
	const Int readyColor = chromaReadyColor( frame, s_superweaponReadySince, COLOR_WARM_WHITE );
	const Int lit = chromaBarSegments( snapshot.bestSuperweaponCharge, NUMPAD_CELLS );

	Int index = 0;
	for( Int row = NUMPAD_LAST_ROW; row >= NUMPAD_FIRST_ROW; --row )
	{
		for( Int column = NUMPAD_FIRST_COLUMN; column <= NUMPAD_LAST_COLUMN; ++column )
		{
			Int color;
			if( snapshot.anySuperweaponReady )
				color = readyColor;
			else
				color = index < lit ? factionColor : COLOR_OFF;
			cells[ chromaKeyboardCell( row, column ) ] = color;
			++index;
		}
	}
}

//-----------------------------------------------------------------------------
/** One lamp per factory along the bottom row, ordered by object id so a lamp
	* keeps its building.  Dark is a factory building nothing, which is the thing
	* worth noticing. */
static void chromaFillProduction( Int *cells, const ChromaSnapshot &snapshot, Int factionColor )
{
	for( Int lamp = 0; lamp < snapshot.producerCount && lamp < PRODUCTION_LAMPS; ++lamp )
	{
		const Real progress = snapshot.producers[ lamp ].progress;
		const Int color = progress < 0.0f ? chromaScale( COLOR_RED, IDLE_PRODUCER_SCALE )
																			: chromaScale( factionColor, progress );
		cells[ chromaKeyboardCell( PRODUCTION_ROW, PRODUCTION_COLUMNS[ lamp ] ) ] = color;
	}
}

//-----------------------------------------------------------------------------
/** Two arrow keys carry the selection: how hurt it is, and what rank the one in
	* front of it holds. */
static void chromaFillSelection( Int *cells )
{
	if( TheInGameUI == NULL || TheInGameUI->getSelectCount() <= 0 )
		return;

	const DrawableList *selected = TheInGameUI->getAllSelectedLocalDrawables();
	if( selected == NULL )
		return;

	Real health = 0.0f;
	Real maxHealth = 0.0f;
	VeterancyLevel rank = LEVEL_INVALID;
	for( DrawableList::const_iterator it = selected->begin(); it != selected->end(); ++it )
	{
		const Object *obj = (*it)->getObject();
		if( obj == NULL || obj->getBodyModule() == NULL )
			continue;

		health += obj->getBodyModule()->getHealth();
		maxHealth += obj->getBodyModule()->getMaxHealth();
		if( rank == LEVEL_INVALID )
			rank = obj->getVeterancyLevel();
	}

	if( maxHealth > 0.0f )
	{
		const Real fraction = health / maxHealth;
		cells[ chromaKeyboardCell( SELECTION_HEALTH_ROW, SELECTION_HEALTH_COLUMN ) ] =
			chromaColor( 1.0f - fraction, fraction, 0.0f );
	}

	Int rankColor = COLOR_OFF;
	switch( rank )
	{
		case LEVEL_VETERAN:	rankColor = COLOR_GREEN; break;
		case LEVEL_ELITE:		rankColor = COLOR_BLUE; break;
		case LEVEL_HEROIC:	rankColor = COLOR_GOLD; break;
		default:						break;
	}
	cells[ chromaKeyboardCell( SELECTION_RANK_ROW, SELECTION_RANK_COLUMN ) ] = rankColor;
}

//-----------------------------------------------------------------------------
/** Escape says what the match itself is doing, and the three keys beside the
	* function row say whether the network, the radar and the clock are healthy. */
static void chromaFillMatchState( Int *cells, UnsignedInt frame )
{
	Int matchColor = COLOR_OFF;
	if( TheVictoryConditions != NULL && TheVictoryConditions->isLocalDefeat() )
		matchColor = COLOR_RED;
	else if( TheVictoryConditions != NULL && TheVictoryConditions->isLocalAlliedVictory() )
		matchColor = COLOR_WHITE;
	else if( TheGameLogic->isPeaceTime() )
		matchColor = chromaBlinkIsOn( frame, STAR_PERIOD_FRAMES ) ? COLOR_BLUE : COLOR_OFF;
	else if( TheGameLogic->isGamePaused()
					 || (TheInGameUI != NULL && (TheInGameUI->isQuitMenuVisible() || !TheInGameUI->getInputEnabled())) )
		matchColor = COLOR_AMBER;
	cells[ chromaKeyboardCell( MATCH_STATE_ROW, MATCH_STATE_COLUMN ) ] = matchColor;

	Int networkColor = COLOR_OFF;
	if( TheNetwork != NULL )
	{
		networkColor = TheNetwork->isFrameDataReady()
									 ? chromaScale( COLOR_GREEN, AMBIENT_SCALE )
									 : (chromaBlinkIsOn( frame, BLINK_PERIOD_FRAMES ) ? COLOR_RED : COLOR_OFF);
	}
	cells[ chromaKeyboardCell( STATUS_ROW, STATUS_NETWORK_COLUMN ) ] = networkColor;

	cells[ chromaKeyboardCell( STATUS_ROW, STATUS_PAUSED_COLUMN ) ] =
		TheGameLogic->isGamePaused() ? COLOR_AMBER : COLOR_OFF;
}

//-----------------------------------------------------------------------------
static void chromaFillAlerts( Int *cells, Player *localPlayer, UnsignedInt frame )
{
	static const Int ALERT_COLORS[ ALERT_COUNT ] =
	{
		COLOR_RED,				// a unit of yours died
		COLOR_RED,				// a building of yours died
		COLOR_RED,				// something is shooting at the base
		COLOR_GOLD,				// a promotion is waiting to be spent
		COLOR_GREEN,			// an upgrade finished
		COLOR_WHITE				// something of yours finished building
	};

	for( Int alert = 0; alert < ALERT_COUNT; ++alert )
	{
		if( alert == ALERT_PROMOTION || alert == ALERT_UNDER_ATTACK )
			continue;
		cells[ chromaAlertCell( alert ) ] =
			chromaAlertIsLit( alert, frame ) ? ALERT_COLORS[ alert ] : COLOR_OFF;
	}

	const UnsignedInt attackedFrame = localPlayer->getAttackedFrame();
	const Bool underAttack = attackedFrame > 0 && frame >= attackedFrame
												 && frame - attackedFrame < (UnsignedInt)UNDER_ATTACK_FRAMES;
	cells[ chromaAlertCell( ALERT_UNDER_ATTACK ) ] =
		underAttack && chromaBlinkIsOn( frame, BLINK_PERIOD_FRAMES )
		? ALERT_COLORS[ ALERT_UNDER_ATTACK ] : COLOR_OFF;

	// The general's star on screen flashes on the frame count, not a timer of its
	// own; the lamp keeps to the same cycle so the two blink together.
	const Bool promotionWaiting = localPlayer->getSciencePurchasePoints() > 0;
	cells[ chromaAlertCell( ALERT_PROMOTION ) ] =
		promotionWaiting && chromaBlinkIsOn( frame, STAR_PERIOD_FRAMES )
		? ALERT_COLORS[ ALERT_PROMOTION ] : COLOR_OFF;
}

//-----------------------------------------------------------------------------
static void chromaFillMoney( Int *cells, Player *localPlayer, Real alarm )
{
	const UnsignedInt money = localPlayer->getMoney()->countMoney();
	const Int lit = chromaMoneySegments( money, MOUSEPAD_CELLS );
	const Int color = chromaAlarmed( COLOR_GREEN, alarm );

	Int *mousepad = cells + MOUSEPAD_FIRST_CELL;
	for( Int led = 0; led < MOUSEPAD_CELLS; ++led )
		mousepad[ led ] = led < lit ? color : COLOR_OFF;
}

//-----------------------------------------------------------------------------
static void chromaFillCells( Int *cells )
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
	const Int factionColor = chromaColor( red, green, blue );

	// The shell map is a running game as far as GameLogic is concerned, so the
	// match test has to exclude it or the main menu lights up like a battle.
	const Bool inMatch = TheGameLogic && TheGameLogic->isInGame() && !TheGameLogic->isInShellGame();
	const UnsignedInt frame = TheGameLogic ? TheGameLogic->getFrame() : 0;

	Real alarm = 0.0f;
	if( inMatch && localPlayer != NULL )
	{
		const UnsignedInt attackedFrame = localPlayer->getAttackedFrame();
		if( attackedFrame > 0 && frame >= attackedFrame
				&& frame - attackedFrame < (UnsignedInt)UNDER_ATTACK_FRAMES )
		{
			const Real seconds = (Real)frame / (Real)LOGICFRAMES_PER_SECOND;
			const Real phase = sinf( seconds * UNDER_ATTACK_PULSE_HZ * TWO_PI );
			alarm = 0.5f + 0.5f * phase;
		}
	}
	const Real alarmEmpty = alarm * ALARM_DEPTH_EMPTY;
	const Real alarmData = alarm * ALARM_DEPTH_DATA;

	const Int ambient = chromaAlarmed( chromaScale( factionColor, AMBIENT_SCALE ), alarmEmpty );
	for( Int cell = 0; cell < CHROMA_CELLS; ++cell )
		cells[ cell ] = ambient;

	if( !inMatch || localPlayer == NULL || TheHotKeyManager == NULL )
	{
		chromaResetAlerts();
		s_effect = EFFECT_NONE;
		s_superweaponReadySince = 0;
		for( Int type = 0; type < SPECIALPOWER_COUNT; ++type )
			s_powerReadySince[ type ] = 0;
		return;
	}

	chromaUpdateAlerts( localPlayer, frame );
	const ChromaSnapshot &snapshot = chromaWalkPlayer( localPlayer, frame );

	const Int pressable = chromaAlarmed( factionColor, alarmData );
	const Int unavailable = chromaAlarmed( COLOR_OFF, alarmData );

	chromaFillCommandBar( cells, pressable, unavailable );
	chromaFillPowerRow( cells, localPlayer->getEnergy(), frame, alarmData );
	chromaFillPowerTray( cells, localPlayer, snapshot, factionColor, frame );
	chromaFillNumpad( cells, snapshot, factionColor, frame );
	chromaFillProduction( cells, snapshot, factionColor );
	chromaFillSelection( cells );
	chromaFillMatchState( cells, frame );
	chromaFillAlerts( cells, localPlayer, frame );
	chromaFillMoney( cells, localPlayer, alarmData );

	// The radar lamp is the one piece of match state that comes off the player
	// rather than the logic, so it is set here where the player is in hand.
	cells[ chromaKeyboardCell( STATUS_ROW, STATUS_RADAR_COLUMN ) ] =
		localPlayer->hasRadar() ? chromaScale( COLOR_GREEN, AMBIENT_SCALE ) : COLOR_OFF;

	// A superweapon going off takes the lot for a few seconds.  It goes on last
	// because it is meant to bury everything under it.
	if( chromaEffectIsLive( frame ) )
		chromaPaintEffect( cells, frame );
}

//-----------------------------------------------------------------------------
void updateChromaKeyboard( void )
{
	if( s_disabled )
		return;

	if( !s_workerRunning )
	{
		InitializeCriticalSection( &s_cellLock );
		DWORD threadId = 0;
		s_workerThread = ::CreateThread( NULL, 0, chromaWorkerMain, NULL, 0, &threadId );
		if( s_workerThread == NULL )
		{
			DeleteCriticalSection( &s_cellLock );
			s_disabled = TRUE;
			return;
		}
		s_workerRunning = TRUE;
	}

	Int cells[ CHROMA_CELLS ];
	// The option applies live, so turning it off mid-match has to hand the
	// hardware back dark rather than freeze it on the last frame.
	if( TheGlobalData != NULL && !TheGlobalData->m_chromaLighting )
		memset( cells, 0, sizeof( cells ) );
	else
		chromaFillCells( cells );

	EnterCriticalSection( &s_cellLock );
	memcpy( s_pendingCells, cells, sizeof( s_pendingCells ) );
	LeaveCriticalSection( &s_cellLock );
}

//-----------------------------------------------------------------------------
void disableChromaKeyboard( void )
{
	s_disabled = TRUE;
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
