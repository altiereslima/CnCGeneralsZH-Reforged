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

// FILE: GameConsole.cpp ////////////////////////////////////////////////////////////////////////
// Desc: The drop-down console.  Drawn with drawFillRect and DisplayStrings the way GraphDraw is,
//       rather than through a .wnd layout, so it needs no data file and works in the shell too.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/GameConsole.h"

#include "Common/GameEngine.h"
#include "Common/MessageStream.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Recorder.h"
#include "GameLogic/GameLogic.h"

#include "GameClient/Color.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GameFont.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/Keyboard.h"

GameConsole *TheGameConsole = NULL;

static const char *CONSOLE_FONT_NAME = "Courier";
static const Int CONSOLE_FONT_POINT_SIZE = 12;
static const Bool CONSOLE_FONT_BOLD = FALSE;

static const Real CONSOLE_HEIGHT_FRACTION = 0.45f;	///< share of the screen the panel covers
static const Int CONSOLE_PADDING = 6;
static const Int CONSOLE_LINE_GAP = 2;								///< pixels between rows on top of the font height
static const Int CONSOLE_EDGE_THICKNESS = 2;

static const Int CONSOLE_MAX_ROWS = 64;								///< display strings kept alive, one per drawable row
static const Int CONSOLE_SCROLLBACK_LIMIT = 256;
static const Int CONSOLE_HISTORY_LIMIT = 32;

static const WideChar *CONSOLE_PROMPT = L"> ";
static const WideChar *CONSOLE_CURSOR = L"_";
static const WideChar CONSOLE_FIRST_PRINTABLE_CHAR = L' ';

static const Color CONSOLE_PANEL_COLOR = GameMakeColor( 0, 0, 0, 225 );
static const Color CONSOLE_EDGE_COLOR = GameMakeColor( 90, 90, 90, 255 );
static const Color CONSOLE_INPUT_COLOR = GameMakeColor( 255, 255, 255, 255 );
static const Color CONSOLE_TEXT_COLOR = GameMakeColor( 190, 190, 190, 255 );
static const Color CONSOLE_SHADOW_COLOR = GameMakeColor( 0, 0, 0, 0 );

struct ConsoleCheat
{
	const char *name;
	CheatKind kind;
	Int defaultAmount;	///< what a bare command gives; zero marks a toggle
	const char *help;
};

static const ConsoleCheat CONSOLE_CHEATS[] =
{
	{ "money",				CHEAT_MONEY,						10000,	"money [n]       add cash, 10000 by default" },
	{ "points",				CHEAT_GENERAL_POINTS,		1,			"points [n]      add general's points" },
	{ "rankup",				CHEAT_RANK_UP,					1,			"rankup [n]      raise the general's rank" },
	{ "heroic",				CHEAT_HEROIC,						1,			"heroic          every unit you have goes heroic" },
	{ "reveal",				CHEAT_REVEAL_MAP,				1,			"reveal          lift the shroud for good" },
	{ "power",				CHEAT_INFINITE_POWER,		0,			"power           never run short of power" },
	{ "nocooldown",		CHEAT_NO_COOLDOWN,			0,			"nocooldown      generals powers and superweapons always ready" },
	{ "god",					CHEAT_GOD_MODE,					0,			"god             your side takes no damage" },
	{ "instantbuild",	CHEAT_INSTANT_BUILD,		0,			"instantbuild    build, train and upgrade in one frame" },
	{ "onehitkill",		CHEAT_ONE_HIT_KILL,			0,			"onehitkill      every hit you land kills" },
};

static const Int CONSOLE_CHEAT_COUNT = sizeof( CONSOLE_CHEATS ) / sizeof( CONSOLE_CHEATS[ 0 ] );

//-------------------------------------------------------------------------------------------------
/** Cheats exist only in a campaign or skirmish match being played; anywhere else the console does
	* not mention them at all. */
//-------------------------------------------------------------------------------------------------
static Bool areCheatsAvailable( void )
{
	return TheGameLogic->isInGame() && !TheGameLogic->isInMultiplayerGame()
		&& TheRecorder->getMode() != RECORDERMODETYPE_PLAYBACK;
}

//-------------------------------------------------------------------------------------------------
/** Hands the cheat to the logic as a message and says what it will do.  A toggle reads the local
	* player's bit before the message lands, which is safe because only this machine sends one. */
//-------------------------------------------------------------------------------------------------
static AsciiString runCheat( const ConsoleCheat &cheat, AsciiString arguments )
{
	const Bool isToggle = cheat.defaultAmount == 0;
	const Int amount = arguments.isEmpty() ? cheat.defaultAmount : atoi( arguments.str() );

	GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_CHEAT );
	msg->appendIntegerArgument( cheat.kind );
	msg->appendIntegerArgument( amount );

	AsciiString result;
	if( isToggle )
		result.format( "%s %s", cheat.name, ThePlayerList->getLocalPlayer()->hasCheat( cheat.kind ) ? "off" : "on" );
	else
		result.format( "%s done", cheat.name );
	return result;
}

//-------------------------------------------------------------------------------------------------
/** The game speed as a share of normal, the same thing numpad plus and minus move.  It changes how
	* often a logic frame runs and nothing inside one, so a network game, which runs at the pace the
	* whole room agrees on, is the one place it is refused. */
//-------------------------------------------------------------------------------------------------
static const Int SPEED_MIN_PERCENT = 17;		///< 5 logic frames a second
static const Int SPEED_MAX_PERCENT = 666;		///< 200 logic frames a second
static const Int SPEED_NORMAL_PERCENT = 100;

static AsciiString runSpeed( AsciiString arguments )
{
	AsciiString result;
	if( !TheGameLogic->isInGame() || TheGameLogic->isInMultiplayerGame() )
	{
		result = "speed: single-player matches only";
		return result;
	}

	if( !arguments.isEmpty() )
	{
		Int percent = arguments.compareNoCase( "reset" ) == 0 ? SPEED_NORMAL_PERCENT : atoi( arguments.str() );
		if( percent < SPEED_MIN_PERCENT ) percent = SPEED_MIN_PERCENT;
		if( percent > SPEED_MAX_PERCENT ) percent = SPEED_MAX_PERCENT;
		TheGameEngine->setFramesPerSecondLimit( percent * LOGICFRAMES_PER_SECOND / SPEED_NORMAL_PERCENT );
	}

	result.format( "speed %d%%", TheGameEngine->getFramesPerSecondLimit() * SPEED_NORMAL_PERCENT / LOGICFRAMES_PER_SECOND );
	return result;
}

//-------------------------------------------------------------------------------------------------
GameConsole::GameConsole()
	: m_isOpen( FALSE ),
		m_historyCursor( 0 ),
		m_font( NULL ),
		m_lineStrings( NULL ),
		m_lineStringCount( 0 )
{
	m_font = TheFontLibrary->getFont( AsciiString( CONSOLE_FONT_NAME ),
																		CONSOLE_FONT_POINT_SIZE,
																		CONSOLE_FONT_BOLD );

	m_lineStrings = new DisplayString *[ CONSOLE_MAX_ROWS ];
	for( Int row = 0; row < CONSOLE_MAX_ROWS; ++row )
	{
		m_lineStrings[ row ] = TheDisplayStringManager->newDisplayString();
		m_lineStrings[ row ]->setFont( m_font );
	}
	m_lineStringCount = CONSOLE_MAX_ROWS;

	printLine( AsciiString( "Zero Hour Reforged console.  'help' lists what there is." ) );
}

//-------------------------------------------------------------------------------------------------
GameConsole::~GameConsole()
{
	for( Int row = 0; row < m_lineStringCount; ++row )
		TheDisplayStringManager->freeDisplayString( m_lineStrings[ row ] );

	delete [] m_lineStrings;
	m_lineStrings = NULL;
	m_lineStringCount = 0;
}

//-------------------------------------------------------------------------------------------------
void GameConsole::toggle( void )
{
	m_isOpen = !m_isOpen;
	if( !m_isOpen )
		m_inputLine.clear();
}

//-------------------------------------------------------------------------------------------------
void GameConsole::close( void )
{
	m_isOpen = FALSE;
	m_inputLine.clear();
}

//-------------------------------------------------------------------------------------------------
void GameConsole::printLine( UnicodeString line )
{
	m_scrollback.push_back( line );
	while( (Int)m_scrollback.size() > CONSOLE_SCROLLBACK_LIMIT )
		m_scrollback.pop_front();
}

//-------------------------------------------------------------------------------------------------
void GameConsole::printLine( AsciiString line )
{
	UnicodeString wide;
	wide.translate( line );
	printLine( wide );
}

//-------------------------------------------------------------------------------------------------
void GameConsole::handleKey( UnsignedByte key, UnsignedShort keyState )
{
	if( !BitTest( keyState, KEY_STATE_DOWN ) )
		return;

	switch( key )
	{
		case KEY_ESC:
			close();
			return;

		case KEY_ENTER:
		case KEY_KPENTER:
			submitInputLine();
			return;

		case KEY_BACKSPACE:
			m_inputLine.removeLastChar();
			return;

		case KEY_UP:
			recallHistory( -1 );
			return;

		case KEY_DOWN:
			recallHistory( 1 );
			return;
	}

	// a modified key is a shortcut, not something to type
	if( BitTest( keyState, KEY_STATE_CONTROL | KEY_STATE_ALT ) )
		return;

	const Int shiftedTable = BitTest( keyState, KEY_STATE_SHIFT ) ? 1 : 0;
	const WideChar printable = TheKeyboard->getPrintableKey( key, shiftedTable );
	if( printable >= CONSOLE_FIRST_PRINTABLE_CHAR )
		m_inputLine.concat( printable );
}

//-------------------------------------------------------------------------------------------------
void GameConsole::recallHistory( Int direction )
{
	if( m_history.empty() )
		return;

	m_historyCursor += direction;
	if( m_historyCursor < 0 )
		m_historyCursor = 0;
	if( m_historyCursor > (Int)m_history.size() )
		m_historyCursor = (Int)m_history.size();

	if( m_historyCursor == (Int)m_history.size() )
		m_inputLine.clear();
	else
		m_inputLine.translate( m_history[ m_historyCursor ] );
}

//-------------------------------------------------------------------------------------------------
void GameConsole::submitInputLine( void )
{
	AsciiString commandLine;
	commandLine.translate( m_inputLine );
	commandLine.trim();
	m_inputLine.clear();

	if( commandLine.isEmpty() )
		return;

	UnicodeString typed;
	typed.translate( commandLine );
	UnicodeString echoed( CONSOLE_PROMPT );
	echoed.concat( typed );
	printLine( echoed );

	m_history.push_back( commandLine );
	while( (Int)m_history.size() > CONSOLE_HISTORY_LIMIT )
		m_history.erase( m_history.begin() );
	m_historyCursor = (Int)m_history.size();

	runCommand( commandLine );
}

//-------------------------------------------------------------------------------------------------
void GameConsole::runCommand( AsciiString commandLine )
{
	AsciiString arguments = commandLine;
	AsciiString command;
	arguments.nextToken( &command );
	command.toLower();
	arguments.trim();

	if( command == "help" )
	{
		printLine( AsciiString( "help          this list" ) );
		printLine( AsciiString( "clear         empty the scrollback" ) );
		printLine( AsciiString( "echo <text>   print the text back" ) );
		printLine( AsciiString( "speed [n]     game speed in percent, 100 is normal, 'reset' goes back" ) );
		if( areCheatsAvailable() )
			printLine( AsciiString( "cheats        single-player cheats" ) );
		return;
	}

	const Bool cheatsAvailable = areCheatsAvailable();
	if( cheatsAvailable && command == "cheats" )
	{
		for( Int i = 0; i < CONSOLE_CHEAT_COUNT; ++i )
			printLine( AsciiString( CONSOLE_CHEATS[ i ].help ) );
		return;
	}

	for( Int i = 0; cheatsAvailable && i < CONSOLE_CHEAT_COUNT; ++i )
	{
		if( command == CONSOLE_CHEATS[ i ].name )
		{
			printLine( runCheat( CONSOLE_CHEATS[ i ], arguments ) );
			return;
		}
	}

	if( command == "clear" )
	{
		m_scrollback.clear();
		return;
	}

	if( command == "echo" )
	{
		printLine( arguments );
		return;
	}

	if( command == "speed" )
	{
		printLine( runSpeed( arguments ) );
		return;
	}

	UnicodeString unknown( L"unknown command: " );
	UnicodeString name;
	name.translate( command );
	unknown.concat( name );
	printLine( unknown );
}

//-------------------------------------------------------------------------------------------------
void GameConsole::render( void )
{
	if( !m_isOpen )
		return;

	const Int screenWidth = TheDisplay->getWidth();
	const Int panelHeight = REAL_TO_INT( TheDisplay->getHeight() * CONSOLE_HEIGHT_FRACTION );
	const Int lineHeight = m_font->height + CONSOLE_LINE_GAP;

	TheDisplay->drawFillRect( 0, 0, screenWidth, panelHeight, CONSOLE_PANEL_COLOR );
	TheDisplay->drawFillRect( 0, panelHeight - CONSOLE_EDGE_THICKNESS,
														screenWidth, CONSOLE_EDGE_THICKNESS, CONSOLE_EDGE_COLOR );

	Int y = panelHeight - CONSOLE_EDGE_THICKNESS - CONSOLE_PADDING - lineHeight;

	UnicodeString prompt( CONSOLE_PROMPT );
	prompt.concat( m_inputLine );
	prompt.concat( CONSOLE_CURSOR );
	m_lineStrings[ 0 ]->setText( prompt );
	m_lineStrings[ 0 ]->draw( CONSOLE_PADDING, y, CONSOLE_INPUT_COLOR, CONSOLE_SHADOW_COLOR );

	Int rowsThatFit = (y - CONSOLE_PADDING) / lineHeight;
	if( rowsThatFit > m_lineStringCount - 1 )
		rowsThatFit = m_lineStringCount - 1;

	Int row = 0;
	ScrollbackLines::reverse_iterator it = m_scrollback.rbegin();
	while( row < rowsThatFit && it != m_scrollback.rend() )
	{
		y -= lineHeight;
		m_lineStrings[ row + 1 ]->setText( *it );
		m_lineStrings[ row + 1 ]->draw( CONSOLE_PADDING, y, CONSOLE_TEXT_COLOR, CONSOLE_SHADOW_COLOR );
		++row;
		++it;
	}
}

//-------------------------------------------------------------------------------------------------
GameMessageDisposition GameConsoleTranslator::translateGameMessage( const GameMessage *msg )
{
	if( TheGameConsole == NULL )
		return KEEP_MESSAGE;

	switch( msg->getType() )
	{
		case GameMessage::MSG_RAW_KEY_DOWN:
		case GameMessage::MSG_RAW_KEY_UP:
		{
			const UnsignedByte key = msg->getArgument( 0 )->integer;
			const UnsignedShort keyState = msg->getArgument( 1 )->integer;

			// the key above Tab belongs to the console whether it is open or shut
			if( key == KEY_TICK )
			{
				if( BitTest( keyState, KEY_STATE_DOWN ) && !BitTest( keyState, KEY_STATE_AUTOREPEAT ) )
					TheGameConsole->toggle();
				return DESTROY_MESSAGE;
			}

			if( !TheGameConsole->isOpen() )
				return KEEP_MESSAGE;

			TheGameConsole->handleKey( key, keyState );
			return DESTROY_MESSAGE;
		}
	}

	return KEEP_MESSAGE;
}
