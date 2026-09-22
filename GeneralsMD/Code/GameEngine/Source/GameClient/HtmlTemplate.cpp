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

// HtmlTemplate.cpp ///////////////////////////////////////////////////////////////////////////////
// {{name}} and data-each, see HtmlTemplate.h.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/HtmlTemplate.h"

#include <ctype.h>

namespace
{

const std::string EACH_ATTRIBUTE = "data-each=";
const std::string OPEN_BRACES = "{{";
const std::string CLOSE_BRACES = "}}";

std::string lowered( const std::string &text )
{
	std::string lower = text;
	for( size_t index = 0; index < lower.size(); index++ )
		lower[ index ] = (char)tolower( (unsigned char)lower[ index ] );
	return lower;
}

/** The page without its <!-- comments -->, which may well describe {{names}} and data-each. */
std::string withoutComments( const std::string &page )
{
	std::string kept;
	size_t at = 0;
	for( size_t open = page.find( "<!--" ); open != std::string::npos; open = page.find( "<!--", at ) )
	{
		kept.append( page, at, open - at );
		const size_t close = page.find( "-->", open );
		if( close == std::string::npos )
			return kept;
		at = close + 3;
	}
	kept.append( page, at, std::string::npos );
	return kept;
}

std::string trimmed( const std::string &text )
{
	size_t first = 0;
	size_t last = text.size();
	while( first < last && isspace( (unsigned char)text[ first ] ) )
		first++;
	while( last > first && isspace( (unsigned char)text[ last - 1 ] ) )
		last--;
	return text.substr( first, last - first );
}

std::string valueOf( const std::string &name, const HtmlValues *entry, const HtmlValues &values, const HtmlLookup &lookup )
{
	if( entry )
	{
		HtmlValues::const_iterator found = entry->find( name );
		if( found != entry->end() )
			return found->second;
	}

	HtmlValues::const_iterator found = values.find( name );
	if( found != values.end() )
		return found->second;

	std::string looked;
	if( lookup && lookup( name, looked ) )
		return looked;
	return std::string();
}

std::string filled( const std::string &text, const HtmlValues *entry, const HtmlValues &values, const HtmlLookup &lookup )
{
	std::string out;
	size_t at = 0;
	for( ;; )
	{
		const size_t open = text.find( OPEN_BRACES, at );
		const size_t close = open == std::string::npos ? std::string::npos : text.find( CLOSE_BRACES, open + OPEN_BRACES.size() );
		if( close == std::string::npos )
		{
			out.append( text, at, std::string::npos );
			return out;
		}

		out.append( text, at, open - at );
		const std::string name = trimmed( text.substr( open + OPEN_BRACES.size(), close - open - OPEN_BRACES.size() ) );
		out += HtmlTemplate_escape( valueOf( name, entry, values, lookup ) );
		at = close + CLOSE_BRACES.size();
	}
}

/** Just past the end tag of the element whose start tag is at `open`, counting elements of the same
	* name nested inside it.
	* ponytail: an element with no end tag (<img data-each>) runs to the end of the page; wrap it in a
	* <div data-each> if that ever matters. */
size_t elementEnd( const std::string &lower, size_t open, const std::string &tag )
{
	Int depth = 0;
	for( size_t at = lower.find( '<', open ); at != std::string::npos; at = lower.find( '<', at + 1 ) )
	{
		const Bool closing = at + 1 < lower.size() && lower[ at + 1 ] == '/';
		const size_t nameAt = at + ( closing ? 2 : 1 );
		const size_t after = nameAt + tag.size();
		if( lower.compare( nameAt, tag.size(), tag ) != 0 || after >= lower.size() || strchr( " \t\r\n/>", lower[ after ] ) == NULL )
			continue;

		depth += closing ? -1 : 1;
		if( depth == 0 )
		{
			const size_t end = lower.find( '>', at );
			return end == std::string::npos ? lower.size() : end + 1;
		}
	}
	return lower.size();
}

}	// namespace

//-------------------------------------------------------------------------------------------------
std::string HtmlTemplate_escape( const std::string &text )
{
	std::string escaped;
	for( size_t index = 0; index < text.size(); index++ )
	{
		switch( text[ index ] )
		{
			case '&':		escaped += "&amp;"; break;
			case '<':		escaped += "&lt;"; break;
			case '>':		escaped += "&gt;"; break;
			case '"':		escaped += "&quot;"; break;
			case '\'':	escaped += "&#39;"; break;
			default:		escaped += text[ index ]; break;
		}
	}
	return escaped;
}

//-------------------------------------------------------------------------------------------------
std::string HtmlTemplate_expand( const std::string &written, const HtmlValues &values,
																 const HtmlLists &lists, const HtmlLookup &lookup )
{
	const std::string page = withoutComments( written );
	const std::string lower = lowered( page );
	std::string out;
	size_t at = 0;
	for( ;; )
	{
		const size_t attribute = lower.find( EACH_ATTRIBUTE, at );
		const size_t open = attribute == std::string::npos ? std::string::npos : lower.rfind( '<', attribute );
		if( open == std::string::npos || open < at )
		{
			out += filled( page.substr( at ), NULL, values, lookup );
			return out;
		}

		size_t nameAt = attribute + EACH_ATTRIBUTE.size();
		const char quote = nameAt < page.size() ? page[ nameAt ] : 0;
		const Bool quoted = quote == '"' || quote == '\'';
		if( quoted )
			nameAt++;
		size_t nameEnd = nameAt;
		while( nameEnd < page.size() && ( quoted ? page[ nameEnd ] != quote : strchr( " \t\r\n>", page[ nameEnd ] ) == NULL ) )
			nameEnd++;
		const std::string listName = page.substr( nameAt, nameEnd - nameAt );

		size_t tagEnd = open + 1;
		while( tagEnd < lower.size() && isalnum( (unsigned char)lower[ tagEnd ] ) )
			tagEnd++;
		const std::string tag = lower.substr( open + 1, tagEnd - open - 1 );
		const size_t end = elementEnd( lower, open, tag );

		out += filled( page.substr( at, open - at ), NULL, values, lookup );

		const std::string element = page.substr( open, end - open );
		HtmlLists::const_iterator list = lists.find( listName );
		if( list != lists.end() )
			for( size_t entry = 0; entry < list->second.size(); entry++ )
				out += filled( element, &list->second[ entry ], values, lookup );

		at = end;
	}
}
