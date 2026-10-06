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
// Modified 2025-2026 by Olcay Seygan for Zero Hour Reforged; see the git history.
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: HotKey.cpp /////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Electronic Arts Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2002 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
//	created:	Sep 2002
//
//	Filename: 	HotKey.cpp
//
//	author:		Chris Huybregts
//	
//	purpose:	
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine
//-----------------------------------------------------------------------------
// USER INCLUDES //////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "GameClient/HotKey.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/MetaEvent.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Keyboard.h"
#include "GameClient/GameText.h"
#include "Common/AudioEventRTS.h"
//-----------------------------------------------------------------------------
// DEFINES ////////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
GameMessageDisposition HotKeyTranslator::translateGameMessage(const GameMessage *msg)
{
	GameMessageDisposition disp = KEEP_MESSAGE;
	GameMessage::Type t = msg->getType();

	if ( t == GameMessage::MSG_RAW_KEY_UP)
	{
		
		//char key = msg->getArgument(0)->integer;
		Int keyState = msg->getArgument(1)->integer;

		// for our purposes here, we don't care to distinguish between right and left keys,
		// so just fudge a little to simplify things.
		Int newModState = 0;

		if( keyState & KEY_STATE_CONTROL )
		{
			newModState |= CTRL;
		}

		if( keyState & KEY_STATE_SHIFT )
		{
			newModState |= SHIFT;
		}

		if( keyState & KEY_STATE_ALT )
		{
			newModState |= ALT;
		}
		// shift is allowed through: a shift-click on a build button queues a batch of units, and
		// the label hotkey is meant to be that click's equal. Ctrl and alt still block the hotkey.
		// Classic blocks every modifier, as the game shipped
		if( TheGlobalData->isClassicUI() ? newModState != 0 : ( newModState & ~SHIFT ) != 0 )
			return disp;
		// Ctrl+F let go of Ctrl first ends on a bare F release; it is still Ctrl+F
		if( keyState & KEY_STATE_PRESSED_WITH_CTRL_ALT )
			return disp;
		const UnsignedByte scanCode = (UnsignedByte)msg->getArgument(0)->integer;
		WideChar typed = TheKeyboard->getPrintableKey(scanCode, 0);
#if defined(_WIN32)
		// The game's own key tables know four layouts and name every key by its US position, so on a
		// Turkish keyboard the key marked i read as an apostrophe.  The letter the player's layout
		// puts on that key is the one a label's '&' means.
		{
			HKL layout = GetKeyboardLayout(0);
			const UINT virtualKey = MapVirtualKeyExW(scanCode, MAPVK_VSC_TO_VK, layout);
			BYTE keys[256] = { 0 };
			WCHAR out[4] = { 0 };
			if( virtualKey != 0 && ToUnicodeEx(virtualKey, scanCode, keys, out, 4, 0x4, layout) == 1 )
				typed = (WideChar)out[0];
		}
#endif
		if(TheHotKeyManager && TheHotKeyManager->executeHotKey(HotKeyManager::nameOf(typed)))
			disp = DESTROY_MESSAGE;
	}
	return disp;
}

//-----------------------------------------------------------------------------
HotKey::HotKey()
{
	m_win = NULL;
	//Added By Sadullah Nader
	//Initializations missing and needed
	m_key.clear();
	//
}

//-----------------------------------------------------------------------------
HotKeyManager::HotKeyManager( void )
{

}

//-----------------------------------------------------------------------------
HotKeyManager::~HotKeyManager( void )
{
	m_hotKeyMap.clear();
}
	
//-----------------------------------------------------------------------------
void HotKeyManager::init( void )
{
	m_hotKeyMap.clear();
}

//-----------------------------------------------------------------------------
void HotKeyManager::reset( void )
{
	m_hotKeyMap.clear();
}

//-----------------------------------------------------------------------------
void HotKeyManager::addHotKey( GameWindow *win, const AsciiString& keyIn)
{
	AsciiString key = keyIn;
	key.toLower();
	HotKeyMap::iterator it = m_hotKeyMap.find(key);
	if( it != m_hotKeyMap.end() )
	{
		DEBUG_ASSERTCRASH(FALSE,("Hotkey %s is already mapped to window %s, current window is %s", key.str(), it->second.m_win->winGetInstanceData()->m_decoratedNameString.str(), win->winGetInstanceData()->m_decoratedNameString.str()));
		return;
	}
	HotKey newHK;
	newHK.m_key.set(key);
	newHK.m_win = win;
	m_hotKeyMap[key] = newHK;
}

//-----------------------------------------------------------------------------
GameWindow *HotKeyManager::findHotKey( const AsciiString& keyIn, Bool *pressable ) const
{
	AsciiString key = keyIn;
	key.toLower();
	HotKeyMap::const_iterator it = m_hotKeyMap.find(key);
	if( it == m_hotKeyMap.end() || it->second.m_win == NULL )
		return NULL;

	GameWindow *win = it->second.m_win;
	const UnsignedInt status = win->winGetStatus();
	*pressable = !BitTest( status, WIN_STATUS_HIDDEN ) && BitTest( status, WIN_STATUS_ENABLED );
	return win;
}

//-----------------------------------------------------------------------------
Bool HotKeyManager::executeHotKey( const AsciiString& keyIn )
{
	AsciiString key = keyIn;
	key.toLower();
	HotKeyMap::iterator it = m_hotKeyMap.find(key);
	if( it == m_hotKeyMap.end() )
		return FALSE;
	GameWindow *win = it->second.m_win;
	if( !win )
		return FALSE;
	if( !BitTest( win->winGetStatus(), WIN_STATUS_HIDDEN ) )
	{
		if( BitTest( win->winGetStatus(), WIN_STATUS_ENABLED ) )
 		{
 			TheWindowManager->winSendSystemMsg( win->winGetParent(), GBM_SELECTED, (WindowMsgData)win, win->winGetWindowId() );
 
 			// here we make the same click sound that the GUI uses when you click a button
 			AudioEventRTS buttonClick("GUIClick");
 
 			if( TheAudio )
 			{
 				TheAudio->addAudioEvent( &buttonClick );
 			}  // end if
			return TRUE;
 		}

		AudioEventRTS disabledClick( "GUIClickDisabled" );
		if( TheAudio )
		{
			TheAudio->addAudioEvent( &disabledClick );
		}
	}
	return FALSE;
}

//-----------------------------------------------------------------------------
/** The map key for a letter: lower case, as UTF-8, so a Turkish letter is itself rather than the
	* low byte of itself.  I, dotted İ and dotless ı are all one key: a Turkish label's İptal and an
	* English player's I key have to meet, and so do the I key of a Turkish Q layout and a label's I. */
AsciiString HotKeyManager::nameOf( WideChar c )
{
	if( c == 0x0130 || c == 0x0131 || c == L'I' )
		c = L'i';
	else if( c >= L'A' && c <= L'Z' )
		c = c + ( L'a' - L'A' );
	else if( c >= 0x00C0 && c <= 0x00DE && c != 0x00D7 )
		c = c + 0x20;
	else if( c >= 0x0100 && c <= 0x017F && ( c & 1 ) == 0 )
		c = c + 1;	// Latin Extended-A pairs: Ğ ğ, Ş ş

	char utf8[ 4 ] = { 0 };
	if( c < 0x80 )
		utf8[ 0 ] = (char)c;
	else if( c < 0x800 )
	{
		utf8[ 0 ] = (char)( 0xC0 | ( c >> 6 ) );
		utf8[ 1 ] = (char)( 0x80 | ( c & 0x3F ) );
	}
	else
	{
		utf8[ 0 ] = (char)( 0xE0 | ( c >> 12 ) );
		utf8[ 1 ] = (char)( 0x80 | ( ( c >> 6 ) & 0x3F ) );
		utf8[ 2 ] = (char)( 0x80 | ( c & 0x3F ) );
	}
	return AsciiString( utf8 );
}

//-----------------------------------------------------------------------------
AsciiString HotKeyManager::searchHotKey( const AsciiString& label)
{
	return searchHotKey(TheGameText->fetch(label));
}

//-----------------------------------------------------------------------------
AsciiString HotKeyManager::searchHotKey( const UnicodeString& uStr )
{
	if(uStr.isEmpty())
		return AsciiString::TheEmptyString;

	const WideChar *marker = (const WideChar *)uStr.str();
	while (marker && *marker)
	{
		if (*marker == u'&')
		{
			// found a '&' - now look for the next char
			return nameOf( *(marker+1) );
		}
		marker++;
	}
	return AsciiString::TheEmptyString;	
}

//-----------------------------------------------------------------------------
HotKeyManager *TheHotKeyManager = NULL;

//-----------------------------------------------------------------------------
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

