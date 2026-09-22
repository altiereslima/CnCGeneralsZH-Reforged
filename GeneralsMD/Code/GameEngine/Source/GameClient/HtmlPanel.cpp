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

// HtmlPanel.cpp //////////////////////////////////////////////////////////////////////////////////
// The rows an HTML page describes.  See HtmlPanel.h for the part of HTML that is read.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameClient/HtmlPanel.h"

#include <ctype.h>

namespace
{

struct DetailsFrame
{
	Bool	open;				///< the open attribute
	Int		summaryRow;	///< its <summary>'s row, -1 until that has been read
};

struct HtmlTag
{
	std::string	name;		///< lower case
	Bool				closing;
	std::vector< std::pair< std::string, std::string > > attributes;	///< names lower case, values decoded

	const std::string *attribute( const char *wanted ) const
	{
		for( size_t index = 0; index < attributes.size(); index++ )
			if( attributes[ index ].first == wanted )
				return &attributes[ index ].second;
		return NULL;
	}
};

struct HtmlParse
{
	std::vector< HtmlRow > *rows;
	std::vector< DetailsFrame > details;
	Bool				rowOpen;		///< a <label> or <summary> is collecting its words into `row`
	HtmlRow			row;
	std::string	looseText;	///< words outside any label, waiting for the tag that ends them
};

struct HtmlEntity
{
	const char *name;
	const char *text;
};

// &nbsp; becomes a plain space: the panel collapses whitespace and has no line to keep together
const HtmlEntity TheHtmlEntities[] =
{
	{ "amp", "&" }, { "lt", "<" }, { "gt", ">" }, { "quot", "\"" }, { "apos", "'" }, { "#39", "'" }, { "nbsp", " " }
};

// elements whose content is never a row: a browser's business, or not text at all
const char *const TheSkippedElements[] = { "head", "style", "script", "title" };

// elements that sit inside a line of words, so they do not end the loose text before them
const char *const TheInlineElements[] = { "a", "b", "i", "em", "strong", "span", "small", "code" };

template< size_t COUNT >
Bool isListed( const std::string &name, const char *const (&list)[ COUNT ] )
{
	for( size_t index = 0; index < COUNT; index++ )
		if( name == list[ index ] )
			return TRUE;
	return FALSE;
}

std::string decodedEntities( const std::string &raw )
{
	std::string decoded;
	size_t at = 0;
	while( at < raw.size() )
	{
		Bool replaced = FALSE;
		const size_t end = raw[ at ] == '&' ? raw.find( ';', at ) : std::string::npos;
		if( end != std::string::npos )
		{
			const std::string name = raw.substr( at + 1, end - at - 1 );
			for( size_t entity = 0; entity < ARRAY_SIZE( TheHtmlEntities ) && !replaced; entity++ )
			{
				if( name != TheHtmlEntities[ entity ].name )
					continue;
				decoded += TheHtmlEntities[ entity ].text;
				at = end + 1;
				replaced = TRUE;
			}
		}
		if( !replaced )
			decoded += raw[ at++ ];
	}
	return decoded;
}

std::string collapsedWhitespace( const std::string &text )
{
	std::string collapsed;
	Bool gap = FALSE;
	for( size_t index = 0; index < text.size(); index++ )
	{
		const char letter = text[ index ];
		if( isspace( (unsigned char)letter ) )
		{
			gap = !collapsed.empty();
			continue;
		}
		if( gap )
			collapsed += ' ';
		gap = FALSE;
		collapsed += letter;
	}
	return collapsed;
}

std::string lowered( const std::string &text )
{
	std::string lower = text;
	for( size_t index = 0; index < lower.size(); index++ )
		lower[ index ] = (char)tolower( (unsigned char)lower[ index ] );
	return lower;
}

/** The summary row of the nearest <details> round this point, -1 at the top level.  A <summary>
	* skips its own <details>: it is the row that opens that one, so it belongs to the one outside. */
Int enclosingSummary( const std::vector< DetailsFrame > &details, Bool skipInnermost )
{
	for( Int frame = (Int)details.size() - ( skipInnermost ? 2 : 1 ); frame >= 0; frame-- )
		if( details[ frame ].summaryRow >= 0 )
			return details[ frame ].summaryRow;
	return -1;
}

/** Read the tag whose '<' is at `start`, and return the index just past its '>'. */
size_t readTag( const std::string &html, size_t start, HtmlTag &tag )
{
	const size_t length = html.size();
	size_t at = start + 1;

	tag.closing = at < length && html[ at ] == '/';
	if( tag.closing )
		at++;
	while( at < length && ( isalnum( (unsigned char)html[ at ] ) || html[ at ] == '-' ) )
		tag.name += (char)tolower( (unsigned char)html[ at++ ] );

	while( at < length && html[ at ] != '>' )
	{
		if( isspace( (unsigned char)html[ at ] ) || html[ at ] == '/' )
		{
			at++;
			continue;
		}

		std::string name;
		while( at < length && !isspace( (unsigned char)html[ at ] ) && strchr( "=>/", html[ at ] ) == NULL )
			name += (char)tolower( (unsigned char)html[ at++ ] );
		while( at < length && isspace( (unsigned char)html[ at ] ) )
			at++;

		std::string value;
		if( at < length && html[ at ] == '=' )
		{
			at++;
			while( at < length && isspace( (unsigned char)html[ at ] ) )
				at++;
			if( at < length && ( html[ at ] == '"' || html[ at ] == '\'' ) )
			{
				const size_t close = html.find( html[ at ], at + 1 );
				const size_t end = close == std::string::npos ? length : close;
				value = html.substr( at + 1, end - at - 1 );
				at = end == length ? length : end + 1;
			}
			else
			{
				while( at < length && !isspace( (unsigned char)html[ at ] ) && html[ at ] != '>' )
					value += html[ at++ ];
			}
		}
		tag.attributes.push_back( std::make_pair( name, decodedEntities( value ) ) );
	}

	return at < length ? at + 1 : length;
}

void pushRow( HtmlParse &parse, const HtmlRow &row )
{
	parse.rows->push_back( row );
}

void flushLooseText( HtmlParse &parse )
{
	HtmlRow row;
	row.kind = HTML_ROW_TEXT;
	row.text = collapsedWhitespace( decodedEntities( parse.looseText ) );
	row.owner = enclosingSummary( parse.details, FALSE );
	row.open = FALSE;
	parse.looseText.clear();

	if( !row.text.empty() )
		pushRow( parse, row );
}

void closeRow( HtmlParse &parse )
{
	if( !parse.rowOpen )
		return;
	parse.rowOpen = FALSE;

	HtmlRow &row = parse.row;
	row.text = collapsedWhitespace( decodedEntities( row.text ) );
	if( row.kind == HTML_ROW_TEXT && row.text.empty() && row.textKey.empty() )
		return;

	if( row.kind == HTML_ROW_SUMMARY && !parse.details.empty() && parse.details.back().summaryRow < 0 )
	{
		parse.details.back().summaryRow = (Int)parse.rows->size();
		row.open = parse.details.back().open;
	}
	pushRow( parse, row );
}

void openRow( HtmlParse &parse, const HtmlTag &tag )
{
	closeRow( parse );

	const Bool summary = tag.name == "summary";
	const std::string *textKey = tag.attribute( "data-text" );

	parse.rowOpen = TRUE;
	parse.row.kind = summary ? HTML_ROW_SUMMARY : HTML_ROW_TEXT;
	parse.row.text.clear();
	parse.row.textKey = textKey ? *textKey : std::string();
	parse.row.name.clear();
	parse.row.owner = enclosingSummary( parse.details, summary );
	parse.row.open = FALSE;
}

void readCheckbox( HtmlParse &parse, const HtmlTag &tag )
{
	const std::string *type = tag.attribute( "type" );
	if( type == NULL || lowered( *type ) != "checkbox" )
		return;

	const std::string *name = tag.attribute( "name" );
	if( parse.rowOpen && parse.row.kind == HTML_ROW_TEXT )
	{
		parse.row.kind = HTML_ROW_CHECKBOX;
		parse.row.name = name ? *name : std::string();
		return;
	}
	if( parse.rowOpen )
		return;		// a check box inside a <summary> would have two things to do on one click

	HtmlRow row;
	row.kind = HTML_ROW_CHECKBOX;
	row.name = name ? *name : std::string();
	row.owner = enclosingSummary( parse.details, FALSE );
	row.open = FALSE;
	pushRow( parse, row );
}

void readElement( HtmlParse &parse, const HtmlTag &tag )
{
	const Bool rowElement = tag.name == "summary" || tag.name == "label";

	if( tag.closing )
	{
		if( rowElement )
			closeRow( parse );
		else if( tag.name == "details" && !parse.details.empty() )
			parse.details.pop_back();
		return;
	}

	if( tag.name == "details" )
	{
		DetailsFrame frame;
		frame.open = tag.attribute( "open" ) != NULL;
		frame.summaryRow = -1;
		parse.details.push_back( frame );
	}
	else if( rowElement )
		openRow( parse, tag );
	else if( tag.name == "input" )
		readCheckbox( parse, tag );
}

}	// namespace

//-------------------------------------------------------------------------------------------------
void HtmlPanel_buildRows( const std::string &html, std::vector< HtmlRow > &rows )
{
	rows.clear();

	HtmlParse parse;
	parse.rows = &rows;
	parse.rowOpen = FALSE;

	const std::string lowerHtml = lowered( html );
	const size_t length = html.size();
	size_t at = 0;
	while( at < length )
	{
		// a '<' that does not start a tag is a letter, the way a browser reads "a < b"
		const Bool tagStart = html[ at ] == '<' && at + 1 < length
			&& ( isalpha( (unsigned char)html[ at + 1 ] ) || strchr( "/!?", html[ at + 1 ] ) != NULL );
		if( !tagStart )
		{
			const size_t next = html.find( '<', at + 1 );
			const size_t end = next == std::string::npos ? length : next;
			( parse.rowOpen ? parse.row.text : parse.looseText ) += html.substr( at, end - at );
			at = end;
			continue;
		}

		if( html.compare( at, 4, "<!--" ) == 0 )
		{
			const size_t end = html.find( "-->", at );
			at = end == std::string::npos ? length : end + 3;
			continue;
		}
		if( html[ at + 1 ] == '!' || html[ at + 1 ] == '?' )
		{
			const size_t end = html.find( '>', at );
			at = end == std::string::npos ? length : end + 1;
			continue;
		}

		HtmlTag tag;
		at = readTag( html, at, tag );

		if( !parse.rowOpen && !isListed( tag.name, TheInlineElements ) )
			flushLooseText( parse );

		if( !tag.closing && isListed( tag.name, TheSkippedElements ) )
		{
			const size_t close = lowerHtml.find( "</" + tag.name, at );
			const size_t end = close == std::string::npos ? std::string::npos : html.find( '>', close );
			at = end == std::string::npos ? length : end + 1;
			continue;
		}

		readElement( parse, tag );
	}

	closeRow( parse );
	flushLooseText( parse );
}

//-------------------------------------------------------------------------------------------------
Bool HtmlPanel_isRowShown( const std::vector< HtmlRow > &rows, Int row )
{
	for( Int owner = rows[ row ].owner; owner >= 0; owner = rows[ owner ].owner )
		if( !rows[ owner ].open )
			return FALSE;
	return TRUE;
}
