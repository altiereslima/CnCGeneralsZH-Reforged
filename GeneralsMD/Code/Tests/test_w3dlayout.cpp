/*
**	Copyright 2026 İlyas Akın
**	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
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
/*
 * On-disk layout pins for the .w3d file format.
 *
 * w3d_file.h and chunkio.h declare the structs that make up a .w3d file, and
 * the asset loaders read them as raw bytes - `cload.Read(&header,
 * sizeof(W3dMeshHeader3Struct))` and thirty-odd calls like it.  Nothing in the
 * tree asserted that those structs lay out the way the file on disk does.  On
 * one compiler that is invisible; the moment there are two it is the whole
 * ballgame, because a misread .w3d is wrong geometry rather than a failed
 * build.
 *
 * Every number below is the layout MSVC produces, which is the layout the
 * retail .w3d files in a Zero Hour install actually have.  They were measured
 * by dumping clang's record layouts for the real headers with `long` forced to
 * 32 bits (B7), not by counting members.
 *
 * WHAT THIS FILE WILL DO ON A MAC BUILD TODAY: fail to compile, loudly, on the
 * first static_assert.  That is the intended outcome and not a bug in this
 * file.  bittype.h defines `uint32` as `unsigned long`, which is 4 bytes under
 * MSVC's LLP64 and 8 bytes under Apple's LP64, so 50 of the 77 structs in
 * w3d_file.h currently double in size under clang.  These asserts are what
 * turns that from a silent misread into a build error.  The fix belongs to
 * bittype.h, not here - do NOT "fix" this file by relaxing the numbers, and do
 * NOT add #pragma pack to w3d_file.h, which would change the very layout these
 * asserts exist to pin.
 *
 * Structs deliberately NOT asserted here are listed at the bottom of the file
 * with the reason, so the list is honest rather than merely long.
 */

#include "test_harness.h"

#include <stddef.h>

#include "chunkio.h"
#include "w3d_file.h"

/*
 * offsetof on a type with a user-declared constructor is conditionally
 * supported; clang supports it and warns.  Several of these structs (the
 * shader, texture-info and vertex-material ones) have a do-nothing ctor EA
 * added for convenience, and they are still standard-layout.  Silence the
 * pedantic warning rather than dropping the asserts that matter most.
 */
#if defined(__clang__)
#pragma clang diagnostic ignored "-Winvalid-offsetof"
#endif

#define W3D_SIZE(T, n)                                                \
	static_assert(sizeof(T) == (n), #T " is " #n " bytes on disk")

#define W3D_AT(T, m, n)                                               \
	static_assert(offsetof(T, m) == (n), #T "::" #m " sits at " #n)

/* ------------------------------------------------------------------ */
/* Chunk framing.                                                     */
/*                                                                    */
/* ChunkHeader is the single most load-bearing struct in the format:  */
/* ChunkLoadClass::Open_Chunk reads one per chunk (chunkio.cpp:426)   */
/* and the seek arithmetic at chunkio.cpp:465 advances by             */
/* sizeof(ChunkHeader).  Get it wrong and every offset in the file is */
/* wrong from the first chunk onward.                                 */
/* ------------------------------------------------------------------ */
W3D_SIZE(ChunkHeader, 8);
W3D_AT(ChunkHeader, ChunkType, 0);
W3D_AT(ChunkHeader, ChunkSize, 4);

W3D_SIZE(MicroChunkHeader, 2);
W3D_AT(MicroChunkHeader, ChunkType, 0);
W3D_AT(MicroChunkHeader, ChunkSize, 1);

/* ------------------------------------------------------------------ */
/* Mesh.  meshgeometry.cpp:1596 reads the header, :1863 the tris,     */
/* :1946 the vertex influences, :1795/:1827 the vectors.              */
/* ------------------------------------------------------------------ */
W3D_SIZE(W3dMeshHeader3Struct, 116);
W3D_AT(W3dMeshHeader3Struct, Version, 0);
W3D_AT(W3dMeshHeader3Struct, MeshName, 8);
/* NumTris follows a char[16]; this is where a padding disagreement shows. */
W3D_AT(W3dMeshHeader3Struct, NumTris, 40);
W3D_AT(W3dMeshHeader3Struct, VertexChannels, 68);
W3D_AT(W3dMeshHeader3Struct, Min, 76);
W3D_AT(W3dMeshHeader3Struct, SphRadius, 112);

W3D_SIZE(W3dTriStruct, 32);
W3D_AT(W3dTriStruct, Vindex, 0);
W3D_AT(W3dTriStruct, Normal, 16);
W3D_AT(W3dTriStruct, Dist, 28);

W3D_SIZE(W3dVertInfStruct, 8);
W3D_AT(W3dVertInfStruct, BoneIdx, 0);
W3D_AT(W3dVertInfStruct, Pad, 2);

/* ------------------------------------------------------------------ */
/* Materials and shaders.  vertmaterial.cpp:481 and                   */
/* meshmdlio.cpp:951 both read these whole.                           */
/* ------------------------------------------------------------------ */
W3D_SIZE(W3dVertexMaterialStruct, 32);
W3D_AT(W3dVertexMaterialStruct, Attributes, 0);
W3D_AT(W3dVertexMaterialStruct, Ambient, 4);
/* Shininess follows four uint8-wide colour structs. */
W3D_AT(W3dVertexMaterialStruct, Shininess, 20);
W3D_AT(W3dVertexMaterialStruct, Translucency, 28);

W3D_SIZE(W3dShaderStruct, 16);
W3D_AT(W3dShaderStruct, DepthCompare, 0);
W3D_AT(W3dShaderStruct, PostDetailAlphaFunc, 14);

W3D_SIZE(W3dTextureInfoStruct, 12);
W3D_AT(W3dTextureInfoStruct, Attributes, 0);
/* FrameCount follows two uint16s - the classic padding tell. */
W3D_AT(W3dTextureInfoStruct, FrameCount, 4);
W3D_AT(W3dTextureInfoStruct, FrameRate, 8);

W3D_SIZE(W3dRGBStruct, 4);
W3D_SIZE(W3dRGBAStruct, 4);
W3D_SIZE(W3dTexCoordStruct, 8);
W3D_AT(W3dTexCoordStruct, V, 4);

/* ------------------------------------------------------------------ */
/* Hierarchy.  htree.cpp:191 reads the header, :284 each pivot.       */
/* ------------------------------------------------------------------ */
W3D_SIZE(W3dHierarchyStruct, 36);
W3D_AT(W3dHierarchyStruct, Version, 0);
W3D_AT(W3dHierarchyStruct, Name, 4);
W3D_AT(W3dHierarchyStruct, NumPivots, 20);
W3D_AT(W3dHierarchyStruct, Center, 24);

W3D_SIZE(W3dPivotStruct, 60);
W3D_AT(W3dPivotStruct, Name, 0);
W3D_AT(W3dPivotStruct, ParentIdx, 16);
W3D_AT(W3dPivotStruct, Translation, 20);
W3D_AT(W3dPivotStruct, Rotation, 44);

/* ------------------------------------------------------------------ */
/* Animation.  hrawanim.cpp:219 and hcanim.cpp:255 read the headers;  */
/* motchan.cpp:168/:280/:393/:761/:947 read the five channel forms.   */
/* ------------------------------------------------------------------ */
W3D_SIZE(W3dAnimHeaderStruct, 44);
W3D_AT(W3dAnimHeaderStruct, Name, 4);
W3D_AT(W3dAnimHeaderStruct, NumFrames, 36);
W3D_AT(W3dAnimHeaderStruct, FrameRate, 40);

W3D_SIZE(W3dCompressedAnimHeaderStruct, 44);
W3D_AT(W3dCompressedAnimHeaderStruct, NumFrames, 36);
W3D_AT(W3dCompressedAnimHeaderStruct, FrameRate, 40);
W3D_AT(W3dCompressedAnimHeaderStruct, Flavor, 42);

W3D_SIZE(W3dAnimChannelStruct, 16);
W3D_AT(W3dAnimChannelStruct, Pivot, 8);
W3D_AT(W3dAnimChannelStruct, Data, 12);

W3D_SIZE(W3dBitChannelStruct, 10);
W3D_AT(W3dBitChannelStruct, DefaultVal, 8);
W3D_AT(W3dBitChannelStruct, Data, 9);

W3D_SIZE(W3dTimeCodedAnimChannelStruct, 12);
W3D_AT(W3dTimeCodedAnimChannelStruct, Pivot, 4);
/* Data follows two uint8s. */
W3D_AT(W3dTimeCodedAnimChannelStruct, Data, 8);

W3D_SIZE(W3dTimeCodedBitChannelStruct, 12);
W3D_AT(W3dTimeCodedBitChannelStruct, DefaultVal, 7);
W3D_AT(W3dTimeCodedBitChannelStruct, Data, 8);

W3D_SIZE(W3dAdaptiveDeltaAnimChannelStruct, 16);
/* Scale follows two uint8s. */
W3D_AT(W3dAdaptiveDeltaAnimChannelStruct, Scale, 8);
W3D_AT(W3dAdaptiveDeltaAnimChannelStruct, Data, 12);

/* ------------------------------------------------------------------ */
/* Hierarchical models and LODs.  hmdldef.cpp:146/:237,               */
/* distlod.cpp:304/:346.                                              */
/* ------------------------------------------------------------------ */
W3D_SIZE(W3dHModelHeaderStruct, 40);
W3D_AT(W3dHModelHeaderStruct, HierarchyName, 20);
W3D_AT(W3dHModelHeaderStruct, NumConnections, 36);

W3D_SIZE(W3dHModelNodeStruct, 18);
W3D_AT(W3dHModelNodeStruct, PivotIdx, 16);

W3D_SIZE(W3dLODModelHeaderStruct, 24);
W3D_AT(W3dLODModelHeaderStruct, NumLODs, 20);

W3D_SIZE(W3dLODStruct, 40);
W3D_AT(W3dLODStruct, LODMin, 32);
W3D_AT(W3dLODStruct, LODMax, 36);

W3D_SIZE(W3dAggregateSubobjectStruct, 64);
W3D_AT(W3dAggregateSubobjectStruct, BoneName, 32);

/* ------------------------------------------------------------------ */
/* Emitters.  part_ldr.cpp:1545/:1593/:1640 write these headers back  */
/* out, so they are a write path as well as a read path.              */
/* ------------------------------------------------------------------ */
W3D_SIZE(W3dEmitterRotationHeaderStruct, 16);
W3D_AT(W3dEmitterRotationHeaderStruct, OrientationRandom, 8);

W3D_SIZE(W3dEmitterFrameHeaderStruct, 16);
W3D_AT(W3dEmitterFrameHeaderStruct, Reserved, 8);

W3D_SIZE(W3dEmitterBlurTimeHeaderStruct, 12);
W3D_AT(W3dEmitterBlurTimeHeaderStruct, Reserved, 8);

/*
 * The scalar typedefs the whole format is built out of.  If one of these
 * fires, every assert above it is a consequence rather than a separate
 * problem, so check this block first.
 */
static_assert(sizeof(uint8)   == 1, "bittype.h uint8 must be 1 byte");
static_assert(sizeof(uint16)  == 2, "bittype.h uint16 must be 2 bytes");
static_assert(sizeof(uint32)  == 4,
	"bittype.h defines uint32 as `unsigned long`, which is 8 bytes under LP64 "
	"(macOS, Linux) and 4 under LLP64 (Windows).  Fix bittype.h; do not relax "
	"this assert.");
static_assert(sizeof(sint32)  == 4, "bittype.h sint32 must be 4 bytes");
static_assert(sizeof(float32) == 4, "bittype.h float32 must be 4 bytes");

/*
 * A runtime case so the suite reports something rather than nothing when it
 * passes.  The static_asserts above are the real test - this exists so that
 * `ctest` shows a green line for w3d layout, and so that a future reader sees
 * the file is wired up rather than inert.
 */
TEST(w3d_on_disk_layout_is_pinned)
{
	CHECK_EQ(sizeof(ChunkHeader), 8u);
	CHECK_EQ(sizeof(W3dMeshHeader3Struct), 116u);
	CHECK_EQ(sizeof(W3dHierarchyStruct), 36u);
	CHECK_EQ(sizeof(W3dVertexMaterialStruct), 32u);
}

/*
 * ---------------------------------------------------------------------------
 * Deliberately NOT asserted, with reasons.  This list is part of the
 * deliverable: "we only pinned some of them" is only useful if you can tell
 * which ones and why.
 *
 * - W3dChunkHeader (w3d_file.h:521).  B4 recon named this one of the
 *   load-bearing four; it is dead.  Zero references outside its own
 *   declaration - chunkio.h's ChunkHeader, pinned above, is what the loaders
 *   actually read.  W3dChunkHeader is a documentation duplicate.
 *
 * - The ~45 structs in w3d_file.h that no loader reads in bulk.  They are
 *   either written only by the Max exporter (Tools/, out of scope and staying
 *   Windows), or read field-by-field through micro-chunks, which carries its
 *   own length and does not depend on C struct layout.  Asserting them would
 *   pin layouts nothing depends on and would have to be re-derived every time
 *   somebody edited the header.
 *
 * - W3dMeshDamageStruct, W3dMeshDamageVertexStruct, W3dMeshDamageColorStruct,
 *   W3dMaterial3Struct, W3dMap3Struct (w3d_obsolete.h).  These ARE read in
 *   bulk - meshdam.cpp:121/:209/:245 and meshmdlio.cpp:669/:706.  They are
 *   omitted only because w3d_obsolete.h is the pre-3.0 format and no shipping
 *   asset uses it; W3dMeshDamageStruct nonetheless differs 64 vs 32 under
 *   clang today.  If anyone ever finds a retail asset that still hits those
 *   paths, they belong in this file.  Worth a follow-up, not a blocker.
 *
 * - Everything under WWSaveLoad.  B4 recon explicitly did not audit whether
 *   the save format blits structs, and that question is C5's and remains
 *   OPEN.  Nothing here answers it and nothing here should be read as
 *   answering it.
 *
 * - The .big archive reader.  Win32BIGFileSystem.cpp:251 builds
 *   ArchivedFileInfo field-by-field and has no layout exposure at all.  C1
 *   should keep it that way.
 * ---------------------------------------------------------------------------
 */
