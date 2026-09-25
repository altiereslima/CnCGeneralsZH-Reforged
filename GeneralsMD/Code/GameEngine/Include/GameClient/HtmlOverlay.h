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

// HtmlOverlay.h //////////////////////////////////////////////////////////////////////////////////
// An HTML page laid out by litehtml and drawn over the battlefield with the game's own 2D calls.
//
// The page covers the whole screen and CSS places what is on it.  A CSS pixel is a pixel of the
// 800x600 screen everything else was authored at, so a page keeps its proportions at any
// resolution, and font-size is the game font's point size at 800x600.  A background on <html> or
// <body> is not drawn: it is there for a browser, the battlefield is the game's background.
//
// An element with data-click="..." is a button; click() hands back that text and the caller
// decides what it means.  Clicks walk up from what is under the pointer to the first element
// that has one.  data-tip="..." is the tooltip of what it is on, found the same way by tip().
//
// Not drawn: rounded corners, clipping for overflow, list bullets, and gradients other than
// linear ones, which draw as their first colour.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef _HTML_OVERLAY_H_
#define _HTML_OVERLAY_H_

#include "Lib/BaseType.h"
#include "Common/AsciiString.h"

#include <string>
#include <vector>

class HtmlOverlayContainer;

class HtmlOverlay
{
public:
	/** `defaultFont` is the game font a page gets where its CSS names none. */
	explicit HtmlOverlay( const AsciiString &defaultFont );
	~HtmlOverlay( void );

	/** Lay this page out, unless it is the page already laid out on a screen of the same size. */
	void setPage( const std::string &html );

	void draw( void );

	/** Fade everything the page draws, text, fills and images, 0 to 255; 255 until it is set. */
	void setAlpha( Int alpha );

	/** Move the pointer over the page, for :hover.  TRUE when it is on something the page drew
		* rather than on the empty body round it. */
	Bool hover( const ICoord2D &mouse );

	/** Click at the pointer and return the data-click of what was clicked, empty for nothing. */
	std::string click( const ICoord2D &mouse );

	/** The data-tip of what the last hover() found under the pointer, empty for nothing. */
	std::string tip( void );

	/** The bottom edge in screen pixels of the first element the CSS selector finds, 0 for none. */
	Int bottomOf( const char *selector );

	/** The screen rectangle of every element the CSS selector finds, empty ones left out. */
	void rectsOf( const char *selector, std::vector< IRegion2D > &rects );

private:
	HtmlOverlayContainer *m_container;
};

#endif // _HTML_OVERLAY_H_
