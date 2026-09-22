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

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Command & Conquer                                            *
 *                                                                                             *
 *                     $Archive:: /Commando/Library/RLERLE.h                                  $*
 *                                                                                             *
 *                      $Author:: Greg_h                                                      $*
 *                                                                                             *
 *                     $Modtime:: 7/22/97 11:37a                                              $*
 *                                                                                             *
 *                    $Revision:: 1                                                           $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#ifndef RLERLE_H
#define RLERLE_H

/*
**	This class holds the RLE enabled blitter object definitions. There is a blitter object
**	type for every kind of pixel operation required of RLE shapes. These are defined as
**	templates to support the different destination pixel formats.
*/

#include	"blitter.h"
#include	<assert.h>
#include	<string.h>


/*
**	This is a helper function that will skip N pixels in the RLE compressed source. This is
**	necessary for clipping purposes. The return value represents the number of transparent
**	pixels before actual pixel data starts when the RLE uncompression is resumed.
*/
inline int Skip_Leading_Pixels(unsigned char const * & sptr, int skipper)
{
	/*
	**	Skip leading pixels as requested.
	*/
	while (skipper > 0) {
		if (*sptr++ == '\0') {
			skipper -= *sptr++;
		} else {
			skipper--;
		}
	}

	/*
	**	Return with then number of leading transparent pixels in the pixel stream
	**	after the end of the skip process. This value must be tracked since the pixel
	**	skip process may have ended in the middle of a transparent pixel run.
	*/
	return(-skipper);
}


/*
**	Blits with transparency checking and translation to destination pixel format.
*/
template<class T>
class RLEBlitTransXlat : public RLEBlitter {
	public:
		RLEBlitTransXlat(T const * translator) : TranslateTable(translator) {assert(TranslateTable != NULL);}

		virtual void Blit(void * dest, void const * source, int length, int leadskip=0) const
		{
			unsigned char const * sptr = (unsigned char const *)source;
			T * dptr = (T *)dest;

			/*
			**	Skip any leading pixels as requested.
			*/
			if (leadskip > 0) {
				int transcount = Skip_Leading_Pixels(sptr, leadskip);
				dptr += transcount;
				length -= transcount;
			}

			/*
			**	Uncompress and store the pixel stream until the length has been
			**	exhausted.
			*/
			while (length > 0) {
				unsigned char value = *sptr++;
				if (value == '\0') {
					value = *sptr++;
					length -= value;
					dptr += value;
				} else {
					*dptr++ = TranslateTable[value];
					length -= 1;
				}
			}
		}

	private:
		T const * TranslateTable;
};


/*
**	This blits RLE compressed pixels by first remapping through a 256 byte table and then
**	translating the pixel to screen format.
*/
template<class T>
class RLEBlitTransRemapXlat : public RLEBlitter {
	public:
		RLEBlitTransRemapXlat(unsigned char const * remapper, T const * translator) : RemapTable(remapper), TranslateTable(translator) {assert(TranslateTable != NULL);assert(RemapTable != NULL);}

		virtual void Blit(void * dest, void const * source, int length, int leadskip=0) const
		{
			unsigned char const * sptr = (unsigned char const *)source;
			T * dptr = (T *)dest;

			/*
			**	Skip any leading pixels as requested.
			*/
			if (leadskip > 0) {
				int transcount = Skip_Leading_Pixels(sptr, leadskip);
				dptr += transcount;
				length -= transcount;
			}

			/*
			**	Uncompress and store the pixel stream until the length has been
			**	exhausted.
			*/
			while (length > 0) {
				unsigned char value = *sptr++;
				if (value == '\0') {
					value = *sptr++;
					length -= value;
					dptr += value;
				} else {
					*dptr++ = TranslateTable[RemapTable[value]];
					length -= 1;
				}
			}
		}

	private:
		unsigned char const * RemapTable;
		T const * TranslateTable;
};


/*
**	This blits RLE compressed pixels by first remapping through a 256 byte table and then
**	translating the pixel to screen format. The remapping table is doubly indirected so that
**	it is possible to change the remapping table pointer without creating a separate blitter
**	object.
*/
template<class T>
class RLEBlitTransZRemapXlat : public RLEBlitter {
	public:
		RLEBlitTransZRemapXlat(unsigned char const * const * remapper, T const * translator) : RemapTable(remapper), TranslateTable(translator) {assert(TranslateTable != NULL);assert(RemapTable != NULL);}

		virtual void Blit(void * dest, void const * source, int length, int leadskip=0) const
		{
			unsigned char const * sptr = (unsigned char const *)source;
			unsigned char const * remapper = *RemapTable;
			T * dptr = (T *)dest;

			/*
			**	Skip any leading pixels as requested.
			*/
			if (leadskip > 0) {
				int transcount = Skip_Leading_Pixels(sptr, leadskip);
				dptr += transcount;
				length -= transcount;
			}

			/*
			**	Uncompress and store the pixel stream until the length has been
			**	exhausted.
			*/
			while (length > 0) {
				unsigned char value = *sptr++;
				if (value == '\0') {
					value = *sptr++;
					length -= value;
					dptr += value;
				} else {
					*dptr++ = TranslateTable[remapper[value]];
					length -= 1;
				}
			}
		}

	private:
		unsigned char const * const * RemapTable;
		T const * TranslateTable;
};


/*
**	This will remap the destination pixels but under the control of the source pixels.
**	Where the source pixel is not transparent, the dest pixel is remapped. This algorithm
**	really only applies to lowcolor display.
*/
template<class T>
class RLEBlitTransRemapDest : public RLEBlitter {
	public:
		RLEBlitTransRemapDest(T const * remap) : RemapTable(remap) {}

		virtual void Blit(void * dest, void const * source, int length, int leadskip=0) const
		{
			unsigned char const * sptr = (unsigned char const *)source;
			T * dptr = (T *)dest;

			/*
			**	Skip any leading pixels as requested.
			*/
			if (leadskip > 0) {
				int transcount = Skip_Leading_Pixels(sptr, leadskip);
				dptr += transcount;
				length -= transcount;
			}

			/*
			**	Uncompress and store the pixel stream until the length has been
			**	exhausted.
			*/
			while (length > 0) {
				unsigned char value = *sptr++;
				if (value == '\0') {
					value = *sptr++;
					length -= value;
					dptr += value;
				} else {
					*dptr = RemapTable[*dptr];
					length -= 1;
					dptr++;
				}
			}
		}

	private:
		T const * RemapTable;
};


/*
**	Algorithmic darkening of hicolor pixels controlled by the source pixels. The source
**	pixels are examined only to determine if the destination pixel should be darkened.
**	If the source pixel is transparent, then the dest pixel is skipped. The darkening
**	algorithm works only for hicolor pixels.
*/
template<class T>
class RLEBlitTransDarken : public RLEBlitter {
	public:
		RLEBlitTransDarken(T mask) : Mask(mask) {}

		virtual void Blit(void * dest, void const * source, int length, int leadskip=0) const
		{
			unsigned char const * sptr = (unsigned char const *)source;
			T * dptr = (T *)dest;

			/*
			**	Skip any leading pixels as requested.
			*/
			if (leadskip > 0) {
				int transcount = Skip_Leading_Pixels(sptr, leadskip);
				dptr += transcount;
				length -= transcount;
			}

			/*
			**	Uncompress and store the pixel stream until the length has been
			**	exhausted.
			*/
			while (length > 0) {
				unsigned char value = *sptr++;
				if (value == '\0') {
					value = *sptr++;
					length -= value;
					dptr += value;
				} else {
					*dptr = (T)((*dptr >> 1) & Mask);
					length -= 1;
					dptr++;
				}
			}
		}

	private:
		T Mask;
};


/*
**	This blitter performs 50% translucency as it draws. It is commonly used for animation
**	effects and other stealth like images. It only works with hicolor pixels but is a good
**	candidate for optimization.
*/
template<class T>
class RLEBlitTransLucent50 : public RLEBlitter {
	public:
		RLEBlitTransLucent50(T const * translator, T mask) : TranslateTable(translator), Mask(mask) {}

		virtual void Blit(void * dest, void const * source, int length, int leadskip=0) const
		{
			unsigned char const * sptr = (unsigned char const *)source;
			T * dptr = (T *)dest;

			/*
			**	Skip any leading pixels as requested.
			*/
			if (leadskip > 0) {
				int transcount = Skip_Leading_Pixels(sptr, leadskip);
				dptr += transcount;
				length -= transcount;
			}

			/*
			**	Uncompress and store the pixel stream until the length has been
			**	exhausted.
			*/
			while (length > 0) {
				unsigned char value = *sptr++;
				if (value == '\0') {
					value = *sptr++;
					length -= value;
					dptr += value;
				} else {
					*dptr = (T)((((*dptr) >> 1) & Mask) + ((TranslateTable[value] >> 1) & Mask));
					length -= 1;
					dptr++;
				}
			}
		}

	private:
		T const * TranslateTable;
		T Mask;
};


/*
**	This blitter performs 25% translucency as it draws. This effect is less than spectacular,
**	but there are some uses for it. It only works with hicolor pixels.
*/
template<class T>
class RLEBlitTransLucent25 : public RLEBlitter {
	public:
		RLEBlitTransLucent25(T const * translator, T mask) : TranslateTable(translator), Mask(mask) {}

		virtual void Blit(void * dest, void const * source, int length, int leadskip=0) const
		{
			unsigned char const * sptr = (unsigned char const *)source;
			T * dptr = (T *)dest;

			/*
			**	Skip any leading pixels as requested.
			*/
			if (leadskip > 0) {
				int transcount = Skip_Leading_Pixels(sptr, leadskip);
				dptr += transcount;
				length -= transcount;
			}

			/*
			**	Uncompress and store the pixel stream until the length has been
			**	exhausted.
			*/
			while (length > 0) {
				unsigned char value = *sptr++;
				if (value == '\0') {
					value = *sptr++;
					length -= value;
					dptr += value;
				} else {
					T qsource = (T)(((TranslateTable[value] >> 2) & Mask));
					T qdest = (T)(((*dptr) >> 2) & Mask);
					*dptr++ = (T)(qdest + qsource + qsource + qsource);
					length -= 1;
				}
			}
		}

	private:
		T const * TranslateTable;
		T Mask;
};


/*
**	This blitter performs 75% translucency as it draws. This is quite useful for explosions and
**	other gas animation effects. It only works with hicolor pixels and is a good candidate
**	for optimization.
*/
template<class T>
class RLEBlitTransLucent75 : public RLEBlitter {
	public:
		RLEBlitTransLucent75(T const * translator, T mask) : TranslateTable(translator), Mask(mask) {}

		virtual void Blit(void * dest, void const * source, int length, int leadskip=0) const
		{
			unsigned char const * sptr = (unsigned char const *)source;
			T * dptr = (T *)dest;

			/*
			**	Skip any leading pixels as requested.
			*/
			if (leadskip > 0) {
				int transcount = Skip_Leading_Pixels(sptr, leadskip);
				dptr += transcount;
				length -= transcount;
			}

			/*
			**	Uncompress and store the pixel stream until the length has been
			**	exhausted.
			*/
			while (length > 0) {
				unsigned char value = *sptr++;
				if (value == '\0') {
					value = *sptr++;
					length -= value;
					dptr += value;
				} else {
					T qsource = (T)(((TranslateTable[value] >> 2) & Mask));
					T qdest = (T)(((*dptr) >> 2) & Mask);
					*dptr++ = (T)(qdest + qdest + qdest + qsource);
					length -= 1;
				}
			}
		}

	private:
		T const * TranslateTable;
		T Mask;
};


/*
**	EA had assembly specializations of three RLE blitters here, 32-bit inline __asm.
**	The templates they specialized are what the build uses.
*/


#endif
