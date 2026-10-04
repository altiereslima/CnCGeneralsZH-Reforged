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

// compress.c
// Compress interface for packets and files
// Author: Jeff Brown, January 1999

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Lib/BaseType.h"
#include "NoxCompress.h"
#include "CompLibHeader/lzhl.h"

#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma message("************************************** WARNING, optimization disabled for debugging purposes")
#endif

#define BLOCKSIZE 500000
#define NoxRead fread
#define DbgMalloc malloc
#define DbgFree free
#define DEBUG_LOG(x) {}

/* LZH-Light's compressor reads up to four bytes past the block it is handed.  Its _updateTable (Lz.cpp)
	 hashes each position of a match with the LZMATCH (5) bytes that start there, so a match that ends
	 within LZMATCH bytes of the block's end hashes bytes after it.  Inside the input those are the next
	 block's own bytes, and the table the compressor keeps carries them into that block, so every block
	 but the last is handed over in place.  After the last block they are whatever follows the caller's
	 buffer: ASan's global-buffer-overflow in test_compression.  Nothing reads what they steer - a block
	 that ends that close to a match ends with its raw tail straight after, and the compressor is then
	 destroyed - so the last block is compressed from a copy with zeroed slack, and the output is the
	 same bytes it always was. */
#define LZHL_READ_PAST 8		// more than LZMATCH - 1

/// One block into `dst`; FALSE only when the last block's copy cannot be allocated.
static Bool compressBlock( LZHL_CHANDLE compressor, void *dst, const void *src, UnsignedInt len, Bool last,
	UnsignedInt &compressed )
{
	if (!last)
	{
		compressed = (UnsignedInt)LZHLCompress( compressor, dst, src, len );
		return TRUE;
	}
	UnsignedByte *copy = (UnsignedByte *)DbgMalloc( len + LZHL_READ_PAST );
	if (copy == NULL)
		return FALSE;
	memcpy( copy, src, len );
	memset( copy + len, 0, LZHL_READ_PAST );
	compressed = (UnsignedInt)LZHLCompress( compressor, dst, copy, len );
	DbgFree( copy );
	return TRUE;
}

Bool DecompressFile		(char *infile, char *outfile)
{
	UnsignedInt	rawSize = 0, compressedSize = 0;
	FILE *inFilePtr = NULL;
	FILE *outFilePtr= NULL;
	char *inBlock		= NULL;
	char *outBlock	= NULL;
	LZHL_DHANDLE decompress;
	Int ok = 0;
	size_t srcSz, dstSz;

	// Parameter checking

	if (( infile == NULL ) || ( outfile == NULL ))
		return FALSE;

	inFilePtr = fopen( infile, "rb" );
	if ( inFilePtr )
	{
		// Allocate the appropriate amount of memory
		// Get compressed size of file.
		fseek( inFilePtr, 0, SEEK_END );
		compressedSize = ftell( inFilePtr );
		fseek( inFilePtr, 0, SEEK_SET );

		compressedSize -= sizeof(UnsignedInt);

		// Get uncompressed size. Don't worry about endian, 
		// this is always INTEL baby!
		NoxRead(&rawSize, 1, sizeof(UnsignedInt), inFilePtr);

		// This is ick, but allocate a BIIIIG chunk o' memory x 2
		inBlock = (char *) DbgMalloc( compressedSize );
		outBlock= (char *) DbgMalloc( rawSize );

		if (( inBlock == NULL ) || ( outBlock == NULL ))
			return FALSE;

		// Read in a big chunk o file
		NoxRead(inBlock, 1, compressedSize, inFilePtr);

		fclose(inFilePtr);

		// Decompress
		srcSz = compressedSize;
		dstSz = rawSize;
		
		// Just Do it!
		decompress = LZHLCreateDecompressor();

		for (;;)
		{
			ok = LZHLDecompress( decompress, outBlock + rawSize - dstSz, &dstSz, 
																			 inBlock + compressedSize - srcSz, &srcSz);

			if ( !ok )
				break;

			if (srcSz <= 0)
				break;
		}

		DEBUG_LOG(("Decompressed %s to %s, output size = %d\n", infile, outfile, rawSize));

		LZHLDestroyDecompressor(decompress);
		outFilePtr = fopen(outfile, "wb");
		if (outFilePtr)
		{
			fwrite (outBlock, rawSize, 1, outFilePtr);
			fclose(outFilePtr);
		}
		else
			return FALSE;

		// Clean up this mess
		DbgFree(inBlock);
		DbgFree(outBlock);
		return TRUE;

	} // End of if fileptr

	return FALSE;
}


Bool CompressFile			(char *infile, char *outfile)
{
	UnsignedInt	rawSize = 0;
	UnsignedInt compressedSize = 0, compressed = 0, i = 0;
	FILE *inFilePtr = NULL;
	FILE *outFilePtr= NULL;
	char *inBlock		= NULL;
	char *outBlock	= NULL;
	LZHL_CHANDLE compressor;
	UnsignedInt blocklen;

	// Parameter checking
	 
	if (( infile == NULL ) || ( outfile == NULL ))
		return FALSE;

	// Allocate the appropriate amount of memory
	inFilePtr = fopen( infile, "rb" );
	if ( inFilePtr )
	{
		// Get size of file.
		fseek( inFilePtr, 0, SEEK_END );
		rawSize = ftell( inFilePtr );
		fseek( inFilePtr, 0, SEEK_SET );

		// This is ick, but allocate a BIIIIG chunk o' memory x 2
		inBlock = (char *) DbgMalloc(rawSize);
		outBlock= (char *) DbgMalloc( LZHLCompressorCalcMaxBuf( rawSize ));

		if (( inBlock == NULL ) || ( outBlock == NULL ))
			return FALSE;

		// Read in a big chunk o file
		NoxRead(inBlock, 1, rawSize, inFilePtr);

		fclose(inFilePtr);

		// Compress
		compressor = LZHLCreateCompressor();
		for ( i = 0; i < rawSize; i += BLOCKSIZE )
		{
			blocklen = min((UnsignedInt)BLOCKSIZE, rawSize - i);
			if (!compressBlock(compressor, outBlock + compressedSize, inBlock + i, blocklen, i + blocklen >= rawSize, compressed))
			{
				LZHLDestroyCompressor(compressor);
				DbgFree(inBlock);
				DbgFree(outBlock);
				return FALSE;
			}
			compressedSize += compressed;
		}

		LZHLDestroyCompressor(compressor);

		outFilePtr = fopen(outfile, "wb");
		if (outFilePtr)
		{
			// write out the uncompressed size first.
			fwrite(&rawSize, sizeof(UnsignedInt), 1, outFilePtr);
			fwrite(outBlock, compressedSize, 1, outFilePtr);
			fclose(outFilePtr);
		}
		else
			return FALSE;

		// Clean up
		DbgFree(inBlock);
		DbgFree(outBlock);
		return TRUE;
	}

	return FALSE;
}

Bool CompressPacket		(char *inPacket, char *outPacket)
{
	// Parameter checking
	 
	if (( inPacket == NULL ) || ( outPacket == NULL ))
		return FALSE;

	return TRUE;
}


Bool DecompressPacket	(char *inPacket, char *outPacket)
{
	// Parameter checking
	 
	if (( inPacket == NULL ) || ( outPacket == NULL ))
		return FALSE;
	return TRUE;
}


UnsignedInt CalcNewSize		(UnsignedInt rawSize)
{
	return LZHLCompressorCalcMaxBuf(rawSize);
}

Bool DecompressMemory		(void *inBufferVoid, Int inSize, void *outBufferVoid, Int& outSize)
{
	UnsignedByte *inBuffer = (UnsignedByte *)inBufferVoid;
	UnsignedByte *outBuffer = (UnsignedByte *)outBufferVoid;
	UnsignedInt	rawSize = 0, compressedSize = 0;
	LZHL_DHANDLE decompress;
	Int ok = 0;
	size_t srcSz, dstSz;

	// Parameter checking
	 
	if (( inBuffer == NULL ) || ( outBuffer == NULL ) || ( inSize < 4 ) || ( outSize == 0 ))
		return FALSE;

	// Get compressed size of file.
	compressedSize = inSize;

	// Get uncompressed size.
	rawSize = outSize;

	// Decompress
	srcSz = compressedSize;
	dstSz = rawSize;
	
	// Just Do it!
	decompress = LZHLCreateDecompressor();

	for (;;)
	{
		ok = LZHLDecompress( decompress, outBuffer + rawSize - dstSz, &dstSz, 
																		 inBuffer + compressedSize - srcSz, &srcSz);

		if ( !ok )
			break;

		if (srcSz <= 0)
			break;
	}

	LZHLDestroyDecompressor(decompress);

	outSize = rawSize;

	return TRUE;

}

Bool CompressMemory			(void *inBufferVoid, Int inSize, void *outBufferVoid, Int& outSize)
{
	UnsignedByte *inBuffer = (UnsignedByte *)inBufferVoid;
	UnsignedByte *outBuffer = (UnsignedByte *)outBufferVoid;
	UnsignedInt	rawSize = 0;
	UnsignedInt compressedSize = 0, compressed = 0, i = 0;
	LZHL_CHANDLE compressor;
	UnsignedInt blocklen;

	// Parameter checking
	 
	if (( inBuffer == NULL ) || ( outBuffer == NULL ) || ( inSize < 4 ) || ( outSize == 0 ))
		return FALSE;

	rawSize = inSize;

	// Compress
	compressor = LZHLCreateCompressor();
	for ( i = 0; i < rawSize; i += BLOCKSIZE )
	{
		blocklen = min((UnsignedInt)BLOCKSIZE, rawSize - i);
		if (!compressBlock(compressor, outBuffer + compressedSize, inBuffer + i, blocklen, i + blocklen >= rawSize, compressed))
		{
			LZHLDestroyCompressor(compressor);
			return FALSE;
		}
		compressedSize += compressed;
	}

	LZHLDestroyCompressor(compressor);

	outSize = compressedSize;

	return TRUE;
}
