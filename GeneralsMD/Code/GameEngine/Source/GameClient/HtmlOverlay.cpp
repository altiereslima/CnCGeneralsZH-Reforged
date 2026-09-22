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

// HtmlOverlay.cpp ////////////////////////////////////////////////////////////////////////////////
// litehtml's drawing callbacks answered with the game's 2D calls, fonts and mapped images.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/FileSystem.h"
#include "Common/file.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GameFont.h"
#include "GameClient/HtmlOverlay.h"
#include "GameClient/Image.h"
#include "GameNetwork/GameSpy/ThreadUtils.h"

// BaseType.h's min and max macros would eat std::min and std::max inside litehtml's headers
#undef min
#undef max

#include <litehtml.h>

#include <algorithm>
#include <map>
#include <math.h>

namespace
{

const litehtml::pixel_t DEFAULT_FONT_SIZE = 12;		///< CSS pixels at 800x600, where a page names none
const litehtml::pixel_t PIXELS_PER_POINT = 96.0f / 72.0f;
const litehtml::pixel_t BROWSER_DPI = 96;
const Int COLOR_BITS = 8;
const Int BOLD_WEIGHT = 600;										///< CSS font-weight from which the game's bold face is used
const Real ASCENT_SHARE = 0.8f;								///< of the font's height; GameFont only knows the height
const Real X_HEIGHT_SHARE = 0.5f;
const char *const PAGE_FOLDER = "Window\\Html\\";
const char *const GENERIC_FAMILIES[] = { "serif", "sans-serif", "monospace", "cursive", "fantasy", "system-ui" };

Color gameColor( const litehtml::web_color &color )
{
	return GameMakeColor( color.red, color.green, color.blue, color.alpha );
}

/** The first family in a CSS font-family list, without its quotes. */
std::string firstFamily( const std::string &families )
{
	std::string family = families.substr( 0, families.find( ',' ) );
	const size_t first = family.find_first_not_of( " \t\"'" );
	const size_t last = family.find_last_not_of( " \t\"'" );
	return first == std::string::npos ? std::string() : family.substr( first, last - first + 1 );
}

Bool isGenericFamily( const std::string &family )
{
	for( size_t index = 0; index < ARRAY_SIZE( GENERIC_FAMILIES ); index++ )
		if( _stricmp( family.c_str(), GENERIC_FAMILIES[ index ] ) == 0 )
			return TRUE;
	return FALSE;
}

}	// namespace

//-------------------------------------------------------------------------------------------------
class HtmlOverlayContainer : public litehtml::document_container
{
public:
	explicit HtmlOverlayContainer( const AsciiString &defaultFont );
	~HtmlOverlayContainer( void );

	void setPage( const std::string &html );
	void draw( void );
	Bool hover( const ICoord2D &mouse );
	std::string click( const ICoord2D &mouse );
	Int bottomOf( const char *selector );

	litehtml::uint_ptr create_font( const litehtml::font_description &description, const litehtml::document *document,
																	litehtml::font_metrics *metrics ) override;
	void delete_font( litehtml::uint_ptr font ) override {}
	litehtml::pixel_t text_width( const char *text, litehtml::uint_ptr font ) override;
	void draw_text( litehtml::uint_ptr hdc, const char *text, litehtml::uint_ptr font, litehtml::web_color color,
									const litehtml::position &place ) override;
	litehtml::pixel_t pt_to_px( float points ) const override { return points * PIXELS_PER_POINT; }
	litehtml::pixel_t get_default_font_size( void ) const override { return DEFAULT_FONT_SIZE; }
	const char *get_default_font_name( void ) const override { return m_defaultFont.c_str(); }
	void draw_list_marker( litehtml::uint_ptr hdc, const litehtml::list_marker &marker ) override {}
	void load_image( const char *source, const char *baseUrl, bool redrawOnReady ) override {}
	void get_image_size( const char *source, const char *baseUrl, litehtml::size &size ) override;
	void draw_image( litehtml::uint_ptr hdc, const litehtml::background_layer &layer, const std::string &url,
									 const std::string &baseUrl ) override;
	void draw_solid_fill( litehtml::uint_ptr hdc, const litehtml::background_layer &layer,
												const litehtml::web_color &color ) override;
	void draw_linear_gradient( litehtml::uint_ptr hdc, const litehtml::background_layer &layer,
														 const litehtml::background_layer::linear_gradient &gradient ) override;
	void draw_radial_gradient( litehtml::uint_ptr hdc, const litehtml::background_layer &layer,
														 const litehtml::background_layer::radial_gradient &gradient ) override;
	void draw_conic_gradient( litehtml::uint_ptr hdc, const litehtml::background_layer &layer,
														const litehtml::background_layer::conic_gradient &gradient ) override;
	void draw_borders( litehtml::uint_ptr hdc, const litehtml::borders &borders, const litehtml::position &place,
										 bool root ) override;
	void set_caption( const char *caption ) override {}
	void set_base_url( const char *baseUrl ) override {}
	void link( const std::shared_ptr< litehtml::document > &document, const litehtml::element::ptr &element ) override {}
	void on_anchor_click( const char *url, const litehtml::element::ptr &element ) override {}
	bool on_element_click( const litehtml::element::ptr &element ) override;
	void on_mouse_event( const litehtml::element::ptr &element, litehtml::mouse_event event ) override {}
	void set_cursor( const char *cursor ) override {}
	void transform_text( litehtml::string &text, litehtml::text_transform transform ) override;
	void import_css( litehtml::string &text, const litehtml::string &url, litehtml::string &baseUrl ) override;
	void set_clip( const litehtml::position &place, const litehtml::border_radiuses &radiuses ) override {}
	void del_clip( void ) override {}
	void get_viewport( litehtml::position &viewport ) const override;
	litehtml::element::ptr create_element( const char *tagName, const litehtml::string_map &attributes,
																				 const std::shared_ptr< litehtml::document > &document ) override { return nullptr; }
	void get_media_features( litehtml::media_features &media ) const override;
	void get_language( litehtml::string &language, litehtml::string &culture ) const override;

private:
	struct CachedString
	{
		DisplayString *string;
		UnsignedInt stamp;	///< the draw that last used it
	};
	typedef std::map< std::pair< GameFont *, std::string >, CachedString > StringCache;

	Int screen( litehtml::pixel_t pagePixels ) const { return (Int)floorf( pagePixels * m_scale + 0.5f ); }
	litehtml::pixel_t page( Int screenPixels ) const { return screenPixels / m_scale; }
	litehtml::pixel_t viewportWidth( void ) const { return page( m_screenWidth ); }
	litehtml::pixel_t viewportHeight( void ) const { return page( m_screenHeight ); }
	DisplayString *displayString( GameFont *font, const char *text );
	void freeStrings( Bool all );
	void fillBox( const litehtml::position &box, const litehtml::web_color &color );

	std::string							m_defaultFont;
	std::string							m_page;
	litehtml::document::ptr	m_document;
	Real										m_scale;
	Int											m_screenWidth;
	Int											m_screenHeight;
	StringCache							m_strings;
	UnsignedInt							m_stamp;
	std::string							m_clicked;
};

//-------------------------------------------------------------------------------------------------
HtmlOverlayContainer::HtmlOverlayContainer( const AsciiString &defaultFont ) :
	m_defaultFont( defaultFont.str() ),
	m_scale( 1.0f ),
	m_screenWidth( 0 ),
	m_screenHeight( 0 ),
	m_stamp( 0 )
{
}

//-------------------------------------------------------------------------------------------------
HtmlOverlayContainer::~HtmlOverlayContainer( void )
{
	m_document = nullptr;
	freeStrings( TRUE );
}

//-------------------------------------------------------------------------------------------------
void HtmlOverlayContainer::setPage( const std::string &html )
{
	const Int width = TheDisplay->getWidth();
	const Int height = TheDisplay->getHeight();
	const Real scale = ControlBarUniformScale();
	if( m_document && html == m_page && width == m_screenWidth && height == m_screenHeight && scale == m_scale )
		return;

	m_page = html;
	m_screenWidth = width;
	m_screenHeight = height;
	m_scale = scale;
	m_document = litehtml::document::createFromString( litehtml::estring( html, litehtml::encoding::utf_8 ), this );
	m_document->render( viewportWidth() );
}

//-------------------------------------------------------------------------------------------------
void HtmlOverlayContainer::draw( void )
{
	if( !m_document )
		return;

	m_stamp++;
	const litehtml::position clip( 0, 0, viewportWidth(), viewportHeight() );
	TheDisplay->beginBatch2D();
	m_document->draw( 0, 0, 0, &clip );
	TheDisplay->endBatch2D();
	freeStrings( FALSE );
}

//-------------------------------------------------------------------------------------------------
Bool HtmlOverlayContainer::hover( const ICoord2D &mouse )
{
	if( !m_document )
		return FALSE;

	litehtml::position::vector redraw;
	const litehtml::pixel_t x = page( mouse.x );
	const litehtml::pixel_t y = page( mouse.y );
	if( m_document->on_mouse_over( x, y, x, y, redraw ) )
		m_document->render( viewportWidth() );

	// the root and the body span the whole screen; only what is drawn on them is the page's
	std::shared_ptr< const litehtml::element > over = m_document->get_over_element();
	return over && over->parent() && !over->is_body();
}

//-------------------------------------------------------------------------------------------------
std::string HtmlOverlayContainer::click( const ICoord2D &mouse )
{
	m_clicked.clear();
	if( !m_document )
		return m_clicked;

	litehtml::position::vector redraw;
	const litehtml::pixel_t x = page( mouse.x );
	const litehtml::pixel_t y = page( mouse.y );
	m_document->on_lbutton_down( x, y, x, y, redraw );
	m_document->on_lbutton_up( x, y, x, y, redraw );
	return m_clicked;
}

//-------------------------------------------------------------------------------------------------
Int HtmlOverlayContainer::bottomOf( const char *selector )
{
	if( !m_document )
		return 0;

	litehtml::element::ptr element = m_document->root()->select_one( selector );
	if( !element )
		return 0;
	const litehtml::position placement = element->get_placement();
	return screen( placement.y + placement.height );
}

//-------------------------------------------------------------------------------------------------
bool HtmlOverlayContainer::on_element_click( const litehtml::element::ptr &element )
{
	const char *action = element->get_attr( "data-click" );
	if( action == NULL )
		return false;
	m_clicked = action;
	return true;
}

//-------------------------------------------------------------------------------------------------
DisplayString *HtmlOverlayContainer::displayString( GameFont *font, const char *text )
{
	const StringCache::key_type key( font, text );
	StringCache::iterator found = m_strings.find( key );
	if( found == m_strings.end() )
	{
		CachedString cached;
		cached.string = TheDisplayStringManager->newDisplayString();
		cached.string->setFont( font );
		cached.string->setText( UnicodeString( MultiByteToWideCharSingleLine( text ).c_str() ) );
		found = m_strings.insert( std::make_pair( key, cached ) ).first;
	}
	found->second.stamp = m_stamp;
	return found->second.string;
}

//-------------------------------------------------------------------------------------------------
/** Strings the last draw did not use go, so a number that changes every half second does not leave
	* a string behind for every value it has had. */
void HtmlOverlayContainer::freeStrings( Bool all )
{
	for( StringCache::iterator cached = m_strings.begin(); cached != m_strings.end(); )
	{
		if( !all && cached->second.stamp == m_stamp )
		{
			++cached;
			continue;
		}
		TheDisplayStringManager->freeDisplayString( cached->second.string );
		cached = m_strings.erase( cached );
	}
}

//-------------------------------------------------------------------------------------------------
litehtml::uint_ptr HtmlOverlayContainer::create_font( const litehtml::font_description &description,
																											const litehtml::document *document,
																											litehtml::font_metrics *metrics )
{
	std::string family = firstFamily( description.family );
	if( family.empty() || isGenericFamily( family ) )
		family = m_defaultFont;

	const Int points = std::max( 1, screen( description.size ) );
	const Bool bold = description.weight >= BOLD_WEIGHT;
	GameFont *font = TheFontLibrary->getFont( AsciiString( family.c_str() ), points, bold );
	if( font == NULL )
		font = TheFontLibrary->getFont( AsciiString( m_defaultFont.c_str() ), points, bold );
	if( font == NULL )
		return 0;

	if( metrics )
	{
		metrics->font_size = description.size;
		metrics->height = page( font->height );
		metrics->ascent = metrics->height * ASCENT_SHARE;
		metrics->descent = metrics->height - metrics->ascent;
		metrics->x_height = metrics->height * X_HEIGHT_SHARE;
		metrics->ch_width = text_width( "0", (litehtml::uint_ptr)font );
		metrics->draw_spaces = false;
	}
	return (litehtml::uint_ptr)font;
}

//-------------------------------------------------------------------------------------------------
litehtml::pixel_t HtmlOverlayContainer::text_width( const char *text, litehtml::uint_ptr font )
{
	if( font == 0 )
		return 0;

	Int width = 0, height = 0;
	displayString( (GameFont *)font, text )->getSize( &width, &height );
	return page( width );
}

//-------------------------------------------------------------------------------------------------
void HtmlOverlayContainer::draw_text( litehtml::uint_ptr hdc, const char *text, litehtml::uint_ptr font,
																		 litehtml::web_color color, const litehtml::position &place )
{
	if( font == 0 || color.alpha == 0 )
		return;

	displayString( (GameFont *)font, text )->draw( screen( place.x ), screen( place.y ), gameColor( color ),
																								 GameMakeColor( 0, 0, 0, color.alpha ) );
}

//-------------------------------------------------------------------------------------------------
void HtmlOverlayContainer::get_image_size( const char *source, const char *baseUrl, litehtml::size &size )
{
	const Image *image = TheMappedImageCollection->findImageByName( AsciiString( source ) );
	size.width = image ? (litehtml::pixel_t)image->getImageWidth() : 0;
	size.height = image ? (litehtml::pixel_t)image->getImageHeight() : 0;
}

//-------------------------------------------------------------------------------------------------
/** An image source is the name of one of the game's mapped images, and it is stretched over the box. */
void HtmlOverlayContainer::draw_image( litehtml::uint_ptr hdc, const litehtml::background_layer &layer,
																			const std::string &url, const std::string &baseUrl )
{
	const Image *image = TheMappedImageCollection->findImageByName( AsciiString( url.c_str() ) );
	if( image == NULL || layer.is_root )
		return;

	const litehtml::position &box = layer.origin_box;
	TheDisplay->drawImage( image, screen( box.x ), screen( box.y ), screen( box.x + box.width ), screen( box.y + box.height ) );
}

//-------------------------------------------------------------------------------------------------
void HtmlOverlayContainer::fillBox( const litehtml::position &box, const litehtml::web_color &color )
{
	const Int left = screen( box.x );
	const Int top = screen( box.y );
	const Int right = screen( box.x + box.width );
	const Int bottom = screen( box.y + box.height );
	if( color.alpha > 0 && right > left && bottom > top )
		TheDisplay->drawFillRect( left, top, right - left, bottom - top, gameColor( color ) );
}

//-------------------------------------------------------------------------------------------------
void HtmlOverlayContainer::draw_solid_fill( litehtml::uint_ptr hdc, const litehtml::background_layer &layer,
																					 const litehtml::web_color &color )
{
	if( layer.is_root )
		return;
	fillBox( layer.border_box, color );
}

//-------------------------------------------------------------------------------------------------
/** A line as thick as the box, run along whichever axis the gradient runs further on, one line per
	* pair of stops: the game's two-colour line blends between its ends. */
void HtmlOverlayContainer::draw_linear_gradient( litehtml::uint_ptr hdc, const litehtml::background_layer &layer,
																								const litehtml::background_layer::linear_gradient &gradient )
{
	if( layer.is_root || gradient.color_points.empty() )
		return;

	std::vector< litehtml::background_layer::color_point > stops = gradient.color_points;
	if( stops.front().offset > 0 )
		stops.insert( stops.begin(), litehtml::background_layer::color_point( 0, stops.front().color ) );
	if( stops.back().offset < 1 )
		stops.push_back( litehtml::background_layer::color_point( 1, stops.back().color ) );

	const litehtml::position &box = layer.border_box;
	const Real runX = gradient.end.x - gradient.start.x;
	const Real runY = gradient.end.y - gradient.start.y;
	const Bool across = fabsf( runX ) >= fabsf( runY );
	const Int middle = across ? ( screen( box.y ) + screen( box.y + box.height ) ) / 2
														: ( screen( box.x ) + screen( box.x + box.width ) ) / 2;
	const Real thickness = across ? (Real)( screen( box.y + box.height ) - screen( box.y ) )
																: (Real)( screen( box.x + box.width ) - screen( box.x ) );

	for( size_t stop = 0; stop + 1 < stops.size(); stop++ )
	{
		const Color from = gameColor( stops[ stop ].color );
		const Color to = gameColor( stops[ stop + 1 ].color );
		if( across )
		{
			const Int startX = screen( gradient.start.x + runX * stops[ stop ].offset );
			const Int endX = screen( gradient.start.x + runX * stops[ stop + 1 ].offset );
			TheDisplay->drawLine( startX, middle, endX, middle, thickness, from, to );
		}
		else
		{
			const Int startY = screen( gradient.start.y + runY * stops[ stop ].offset );
			const Int endY = screen( gradient.start.y + runY * stops[ stop + 1 ].offset );
			TheDisplay->drawLine( middle, startY, middle, endY, thickness, from, to );
		}
	}
}

//-------------------------------------------------------------------------------------------------
void HtmlOverlayContainer::draw_radial_gradient( litehtml::uint_ptr hdc, const litehtml::background_layer &layer,
																								const litehtml::background_layer::radial_gradient &gradient )
{
	if( !layer.is_root && !gradient.color_points.empty() )
		fillBox( layer.border_box, gradient.color_points.front().color );
}

//-------------------------------------------------------------------------------------------------
void HtmlOverlayContainer::draw_conic_gradient( litehtml::uint_ptr hdc, const litehtml::background_layer &layer,
																							 const litehtml::background_layer::conic_gradient &gradient )
{
	if( !layer.is_root && !gradient.color_points.empty() )
		fillBox( layer.border_box, gradient.color_points.front().color );
}

//-------------------------------------------------------------------------------------------------
void HtmlOverlayContainer::draw_borders( litehtml::uint_ptr hdc, const litehtml::borders &borders,
																				const litehtml::position &place, bool root )
{
	if( root )
		return;

	const litehtml::border *sides[] = { &borders.top, &borders.bottom, &borders.left, &borders.right };
	for( size_t side = 0; side < ARRAY_SIZE( sides ); side++ )
	{
		const litehtml::border &border = *sides[ side ];
		if( border.width <= 0 || border.style == litehtml::border_style_none || border.style == litehtml::border_style_hidden )
			continue;

		litehtml::position edge = place;
		if( sides[ side ] == &borders.top )
			edge.height = border.width;
		else if( sides[ side ] == &borders.bottom )
		{
			edge.y = place.y + place.height - border.width;
			edge.height = border.width;
		}
		else if( sides[ side ] == &borders.left )
			edge.width = border.width;
		else
		{
			edge.x = place.x + place.width - border.width;
			edge.width = border.width;
		}
		fillBox( edge, border.color );
	}
}

//-------------------------------------------------------------------------------------------------
void HtmlOverlayContainer::transform_text( litehtml::string &text, litehtml::text_transform transform )
{
	Bool wordStart = TRUE;
	for( size_t index = 0; index < text.size(); index++ )
	{
		const unsigned char letter = (unsigned char)text[ index ];
		if( transform == litehtml::text_transform_uppercase || ( transform == litehtml::text_transform_capitalize && wordStart ) )
			text[ index ] = (char)toupper( letter );
		else if( transform == litehtml::text_transform_lowercase )
			text[ index ] = (char)tolower( letter );
		wordStart = isspace( letter ) != 0;
	}
}

//-------------------------------------------------------------------------------------------------
/** <link rel="stylesheet" href="x.css"> reads x.css from beside the pages. */
void HtmlOverlayContainer::import_css( litehtml::string &text, const litehtml::string &url, litehtml::string &baseUrl )
{
	const std::string path = std::string( PAGE_FOLDER ) + url;
	File *file = TheFileSystem->openFile( path.c_str(), File::READ | File::BINARY );
	if( file == NULL )
	{
		DEBUG_LOG(( "HtmlOverlay: the stylesheet %s is missing\n", path.c_str() ));
		return;
	}
	const Int size = file->size();
	char *contents = file->readEntireAndClose();
	text.assign( contents, size );
	delete [] contents;
}

//-------------------------------------------------------------------------------------------------
void HtmlOverlayContainer::get_viewport( litehtml::position &viewport ) const
{
	viewport = litehtml::position( 0, 0, viewportWidth(), viewportHeight() );
}

//-------------------------------------------------------------------------------------------------
void HtmlOverlayContainer::get_media_features( litehtml::media_features &media ) const
{
	media.type = litehtml::media_type_screen;
	media.width = media.device_width = viewportWidth();
	media.height = media.device_height = viewportHeight();
	media.color = COLOR_BITS;
	media.color_index = 0;
	media.monochrome = 0;
	media.resolution = BROWSER_DPI;
}

//-------------------------------------------------------------------------------------------------
void HtmlOverlayContainer::get_language( litehtml::string &language, litehtml::string &culture ) const
{
	language = "en";
	culture.clear();
}

//-------------------------------------------------------------------------------------------------
HtmlOverlay::HtmlOverlay( const AsciiString &defaultFont ) :
	m_container( new HtmlOverlayContainer( defaultFont ) )
{
}

//-------------------------------------------------------------------------------------------------
HtmlOverlay::~HtmlOverlay( void )
{
	delete m_container;
}

void HtmlOverlay::setPage( const std::string &html )	{ m_container->setPage( html ); }
void HtmlOverlay::draw( void )													{ m_container->draw(); }
Bool HtmlOverlay::hover( const ICoord2D &mouse )				{ return m_container->hover( mouse ); }
std::string HtmlOverlay::click( const ICoord2D &mouse )	{ return m_container->click( mouse ); }
Int HtmlOverlay::bottomOf( const char *selector )				{ return m_container->bottomOf( selector ); }
