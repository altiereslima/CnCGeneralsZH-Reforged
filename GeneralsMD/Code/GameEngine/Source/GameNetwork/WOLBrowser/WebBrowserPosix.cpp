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
// Modified 2026 by İlyas Akın for the macOS/Linux port: parts come from GeneralsMD/Code/GameEngine/Source/GameNetwork/WOLBrowser/WebBrowser.cpp, the rest is new code, copyright 2026 İlyas Akın; see NOTICE.md and the git history.

// FILE: WebBrowserPosix.cpp //////////////////////////////////////////////////////////////////////
// Desc:   GameNetwork/WOLBrowser/WebBrowser.h off Windows: no browser.
///////////////////////////////////////////////////////////////////////////////////////////////////

/* WebBrowser.cpp is the Windows body, an ATL/COM wrapper around Internet Explorer, and is not built
	 here.  Off Windows there is no browser, as there has not been one on Windows either in practice:
	 nothing ever creates TheWebBrowser, and every caller tests it for NULL first.

	 WebBrowserURL is the one piece callers use without a browser: INI::parseWebpageURLDefinition names
	 its parse table.  Its definitions below are WebBrowser.cpp's, unchanged.  (That parser only runs
	 when WebBrowser::init loads Webpages.ini, which with no browser it never does.) */

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/INI.h"
#include "GameNetwork/WOLBrowser/WebBrowser.h"

WebBrowser *TheWebBrowser = NULL;

WebBrowserURL *WebBrowser::findURL( AsciiString )
{
	return NULL;
}

WebBrowserURL *WebBrowser::makeNewURL( AsciiString )
{
	return NULL;
}

const FieldParse WebBrowserURL::m_URLFieldParseTable[] =
{

	{ "URL",										INI::parseAsciiString,							NULL, offsetof( WebBrowserURL, m_url ) },
	{ NULL,											NULL,																NULL, 0 },

};

WebBrowserURL::WebBrowserURL()
{
	m_next = NULL;
	m_tag.clear();
	m_url.clear();
}

WebBrowserURL::~WebBrowserURL()
{
}
