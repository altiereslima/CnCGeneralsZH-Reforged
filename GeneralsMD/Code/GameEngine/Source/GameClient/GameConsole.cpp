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
		return;
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
