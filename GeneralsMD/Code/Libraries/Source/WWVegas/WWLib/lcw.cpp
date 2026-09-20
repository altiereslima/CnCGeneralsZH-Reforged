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
 *                     $Archive:: /G/wwlib/lcw.cpp                                            $* 
 *                                                                                             * 
 *                      $Author:: Neal_k                                                      $*
 *                                                                                             * 
 *                     $Modtime:: 10/04/99 10:25a                                             $*
 *                                                                                             * 
 *                    $Revision:: 4                                                           $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------* 
 * Functions:                                                                                  * 
 *   LCW_Comp -- Performes LCW compression on a block of data.                                 * 
 *   LCW_Uncomp -- Decompress an LCW encoded data block.                                       *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include	"always.h"
#include	"lcw.h"
#include	<string.h>

/***************************************************************************
 * LCW_Uncomp -- Decompress an LCW encoded data block.                     *
 *                                                                         *
 * Uncompress data to the following codes in the format b = byte, w = word *
 * n = byte code pulled from compressed data.                              *
 *                                                                         *
 *   Command code, n        |Description                                   *
 * ------------------------------------------------------------------------*
 * n=0xxxyyyy,yyyyyyyy      |short copy back y bytes and run x+3 from dest *
 * n=10xxxxxx,n1,n2,...,nx+1|med length copy the next x+1 bytes from source*
 * n=11xxxxxx,w1            |med copy from dest x+3 bytes from offset w1   *
 * n=11111111,w1,w2         |long copy from dest w1 bytes from offset w2   *
 * n=11111110,w1,b1         |long run of byte b1 for w1 bytes              *
 * n=10000000               |end of data reached                           *
 *                                                                         *
 *                                                                         *
 * INPUT:                                                                  *
 *      void * source ptr                                                  *
 *      void * destination ptr                                             *
 *      unsigned long length of uncompressed data                          *
 *                                                                         *
 *                                                                         *
 * OUTPUT:                                                                 *
 *     unsigned long # of destination bytes written                        *
 *                                                                         *
 * WARNINGS:                                                               *
 *     3rd argument is dummy. It exists to provide cross-platform          *
 *      compatibility. Note therefore that this implementation does not    *
 *      check for corrupt source data by testing the uncompressed length.  *
 *                                                                         *
 * HISTORY:                                                                *
 *    03/20/1995 IML : Created.                                            *
 *=========================================================================*/
int LCW_Uncomp(void const * source, void * dest, unsigned long )
{
	unsigned char * source_ptr, * dest_ptr, * copy_ptr;
	unsigned char op_code, data;
	unsigned count;
	unsigned * word_dest_ptr;
	unsigned word_data;

	/* Copy the source and destination ptrs. */
	source_ptr = (unsigned char*) source;
	dest_ptr   = (unsigned char*) dest;

	for (;;) {

		/* Read in the operation code. */
		op_code = *source_ptr++;

		if (!(op_code & 0x80)) {

			/* Do a short copy from destination. */
			count = (op_code >> 4) + 3;
			copy_ptr = dest_ptr - ((unsigned) *source_ptr++ + (((unsigned) op_code & 0x0f) << 8));

			while (count--) *dest_ptr++ = *copy_ptr++;

		} else {

			if (!(op_code & 0x40)) {

				if (op_code == 0x80) {

					/* Return # of destination bytes written. */
					return ((unsigned long) (dest_ptr - (unsigned char*) dest));

				} else {

					/* Do a medium copy from source. */
					count = op_code & 0x3f;

					while (count--) *dest_ptr++ = *source_ptr++;
				}

			} else {

				if (op_code == 0xfe) {

					/* Do a long run. */
					count = *source_ptr + ((unsigned) *(source_ptr + 1) << 8);
					word_data = data = *(source_ptr + 2);
					word_data  = (word_data << 24) + (word_data << 16) + (word_data << 8) + word_data;
					source_ptr += 3;

					copy_ptr = dest_ptr + 4 - ((unsigned) dest_ptr & 0x3);
					count -= (copy_ptr - dest_ptr);
					while (dest_ptr < copy_ptr) *dest_ptr++ = data;

					word_dest_ptr = (unsigned*) dest_ptr;

					dest_ptr += (count & 0xfffffffc);

					while (word_dest_ptr < (unsigned*) dest_ptr) {
						*word_dest_ptr		= word_data;
						*(word_dest_ptr + 1) = word_data;
						word_dest_ptr += 2;
					}

					copy_ptr = dest_ptr + (count & 0x3);
					while (dest_ptr < copy_ptr) *dest_ptr++ = data;

				} else {

					if (op_code == 0xff) {

						/* Do a long copy from destination. */
						count = *source_ptr + ((unsigned) *(source_ptr + 1) << 8);
						copy_ptr = (unsigned char*) dest + *(source_ptr + 2) + ((unsigned) *(source_ptr + 3) << 8);
						source_ptr += 4;

						while (count--) *dest_ptr++ = *copy_ptr++;

					} else {

						/* Do a medium copy from destination. */
						count = (op_code & 0x3f) + 3;
						copy_ptr = (unsigned char*) dest + *source_ptr + ((unsigned) *(source_ptr + 1) << 8);
						source_ptr += 2;

						while (count--) *dest_ptr++ = *copy_ptr++;
					}
				}
			}
		}
	}
}


#if defined(_MSC_VER)


/*********************************************************************************************** 
 * LCW_Comp -- Performes LCW compression on a block of data.                                   * 
 *                                                                                             * 
 *    This routine will compress a block of data using the LCW compression method. LCW has     * 
 *    the primary characteristic of very fast uncompression at the expense of very slow        * 
 *    compression times.                                                                       * 
 *                                                                                             * 
 * INPUT:   source   -- Pointer to the source data to compress.                                * 
 *                                                                                             * 
 *          dest     -- Pointer to the destination location to store the compressed data       * 
 *                      to.                                                                    * 
 *                                                                                             * 
 *          datasize -- The size (in bytes) of the source data to compress.                    * 
 *                                                                                             * 
 * OUTPUT:  Returns with the number of bytes of output data stored into the destination        * 
 *          buffer.                                                                            * 
 *                                                                                             * 
 * WARNINGS:   Be sure that the destination buffer is big enough. The maximum size required    * 
 *             for the destination buffer is (datasize + datasize/128).                        * 
 *                                                                                             * 
 * HISTORY:                                                                                    * 
 *   05/20/1997 JLB : Created.                                                                 * 
 *=============================================================================================*/
/*ARGSUSED*/
int LCW_Comp(void const * source, void * dest, int datasize)
{
	int retval = 0;

	/*
	**	The assembler below opens by unconditionally emitting a "length of 1"
	**	code plus the first source byte, and only then starts testing for the
	**	end of the input.  With one byte of input the source pointer is already
	**	spent at that point, so it walks off the end and emits a second, bogus
	**	byte.  Handle the two degenerate sizes here rather than restructuring
	**	the asm.
	*/
	if (datasize <= 1) {
		unsigned char * dest_ptr = (unsigned char *)dest;
		if (datasize == 1) {
			*dest_ptr++ = 0x81;								// medium copy, one byte follows
			*dest_ptr++ = *(unsigned char const *)source;
		}
		*dest_ptr++ = 0x80;									// end of data
		return ((int)(dest_ptr - (unsigned char *)dest));
	}

	/*
	**	EA's encoder was 32-bit assembly and searched back for matches; this one
	**	emits two of the codes LCW_Uncomp reads, 0xfe long runs and 0x80|n literal
	**	copies, so it packs runs but not back references.  Nothing in the game
	**	compresses with it - the archives are read, not written - and the tests in
	**	test_wwlib round-trip it.  A run shorter than eight bytes stays literal
	**	because the long-run decoder aligns to four bytes before it counts and
	**	would underflow on a tiny count.
	*/
	{
		enum { MIN_RUN = 8, MAX_RUN = 0xffff, MAX_LITERAL = 0x3f };
		unsigned char const * source_ptr = (unsigned char const *)source;
		unsigned char const * source_end = source_ptr + datasize;
		unsigned char * dest_ptr = (unsigned char *)dest;
		unsigned char const * literal_start = source_ptr;

		while (source_ptr <= source_end) {
			int run = 1;
			if (source_ptr < source_end) {
				while (source_ptr + run < source_end && run < MAX_RUN && source_ptr[run] == source_ptr[0]) {
					++run;
				}
			}
			bool const at_end = (source_ptr == source_end);
			if (at_end || run >= MIN_RUN) {
				while (literal_start < source_ptr) {
					int literal = (int)(source_ptr - literal_start);
					if (literal > MAX_LITERAL) literal = MAX_LITERAL;
					*dest_ptr++ = (unsigned char)(0x80 | literal);
					memcpy(dest_ptr, literal_start, literal);
					dest_ptr += literal;
					literal_start += literal;
				}
				if (at_end) break;
				*dest_ptr++ = 0xfe;
				*dest_ptr++ = (unsigned char)(run & 0xff);
				*dest_ptr++ = (unsigned char)(run >> 8);
				*dest_ptr++ = source_ptr[0];
				source_ptr += run;
				literal_start = source_ptr;
			} else {
				source_ptr += 1;
			}
		}
		*dest_ptr++ = 0x80;
		retval = (int)(dest_ptr - (unsigned char *)dest);
	}
	return(retval);
}
#endif


