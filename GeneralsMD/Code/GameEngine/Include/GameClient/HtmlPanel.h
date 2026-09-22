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

// HtmlPanel.h ////////////////////////////////////////////////////////////////////////////////////
// A panel over the battlefield described by an .html file under Window/Html.
//
// The game reads a small part of HTML and turns it into a column of rows: a <summary> is a row
// that opens and closes its <details>, a <label> or a bare <input type="checkbox"> is a check box
// row, and loose text is a row of words.  <head>, <style>, <script> and <title> are skipped, so a
// page can carry the style a browser needs to show the same panel.  There is no layout beyond the
// column and no CSS: the look is the game's.
//
// An attribute carries what the words between the tags cannot: name= on a check box is the
// Options.ini key it is bound to, data-text= is a string table label the game shows instead of
// the literal words, so the page reads in English in a browser and in the player's language in
// the game.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _HTML_PANEL_H_
#define _HTML_PANEL_H_

#include "Lib/BaseType.h"

#include <string>
#include <vector>

enum HtmlRowKind
{
	HTML_ROW_SUMMARY,		///< a <summary>: clicking it opens or closes the rows of its <details>
	HTML_ROW_CHECKBOX,	///< a check box, bound to the Options.ini key in `name`
	HTML_ROW_TEXT,			///< words and nothing to click
};

struct HtmlRow
{
	HtmlRowKind	kind;
	std::string	text;			///< the words between the tags, whitespace collapsed, UTF-8
	std::string	textKey;	///< data-text: a string table label, empty when `text` is the label
	std::string	name;			///< a check box's name=
	Int					owner;		///< the summary row of the <details> this row sits in, -1 at the top level
	Bool				open;			///< a summary: whether its <details> is open
};

/** The rows an HTML page describes, in document order.  Anything outside the part of HTML the
	* panel reads is passed over rather than refused, the way a browser passes over a tag it does
	* not know. */
extern void HtmlPanel_buildRows( const std::string &html, std::vector< HtmlRow > &rows );

/** Is this row on screen: every <details> round it open? */
extern Bool HtmlPanel_isRowShown( const std::vector< HtmlRow > &rows, Int row );

#endif // _HTML_PANEL_H_
