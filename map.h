// Copyright 2010, 2011 Michael J. Nelson
//
// This file is part of pigmap.
//
// pigmap is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// pigmap is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with pigmap.  If not, see <http://www.gnu.org/licenses/>.

#ifndef MAP_H
#define MAP_H

#include <stdint.h>
#include <string>
#include <vector>

// IMPORTANT NOTE:
//  This program was written before the location of the sun moved in Minecraft Beta 1.9 or so,
//   therefore all of the N/S/E/W directions here are now wrong--rotated 90 degrees from what they
//   should be.  For example, the positive X direction used to be South, and is called South here,
//   but is now East in the game (as of Minecraft 1.0, anyway).
//  I decided to leave the old direction names here, because it would be pretty easy to mess
//   something up trying to go through and change everything.  Apologies for the confusion!

// Minecraft coord system:
//
// +x = S    +z = W    +y = U
// -x = N    -z = E    -y = D

// the block size is a parameter B >= 2
//
// BlockIdx delta         image coord delta
// [bx,bz,by]             [x,y]
//---------------------------------------------
// [-1,0,0] (N)           [-2B,B]
// [1,0,0] (S)            [2B,-B]
// [0,-1,0] (E)           [-2B,-B]
// [0,1,0] (W)            [2B,B]
// [0,0,-1] (D)           [0,2B]
// [0,0,1] (U)            [0,-2B]
//
// block size        endpoint-exclusive bounding box (from block center)
// B                 [-2B,-2B] to [2B,2B]   (size 4Bx4B)
//
// [2B  2B  0  ]   [bx]       [x]
// [-B  B   -2B] * [bz]   =   [y]
//                 [by]
//
// so the absolute pixel coords of the center of block [bx,bz,by] are [2*B*bx + 2*B*bz, -B*bx + B*bz - 2*B*by]
//
// the pixels that correspond to block centers form a hexagonal grid:
//  x % 2B = 0
//  y % 2B = 0 (if x % 4B = 0)
//  y % 2B = B (if x % 4B = 2B)
//
// example, with B = 1:
//
// X...X...X...
// ..X...X...X.
// X...X...X...
// ..X...X...X.
// X...X...X...
// ..X...X...X.

// a chunk is 16x16x128 blocks (chunk/region format) or 16x16x256 blocks (Anvil format), but we can
//  also specify the min and max Y values we want, and consider a chunk to be 16x16x(MAXY-MINY+1),
//  where MINY and MAXY are inclusive--e.g. for an entire Anvil chunk, MINY=0 and MAXY=255
//
// each chunk covers a hexagonal area of the image; the corners of the hexagon (in clockwise order)
//  are the farthest blocks NED, NEU, SEU, SWU, SWD, NWD
//
//               SEU
//              / . \
//             /  .  \
//           NEU  .  SWU
//            |\  .  /|
//            | \ . / |
//            |  NWU  |
//            |   |   |
//            | (SED) |
//            | . | . |
//            |.  |  .|
//           NED  |  SWD
//             \  |  /
//              \ | /
//               NWD
//
// in block coordinates, let the origin of the chunk be the block at [0,0,0] relative to the chunk--i.e.
//  the NED corner of the full chunk (as opposed to the slice of it defined by MINY and MAXY)
//
// relative to the origin block of the chunk, the corner blocks are:
// NED              NEU              SEU                 SWU              SWD              NWD
// [0,0,MINY]       [0,0,MAXY]       [15,0,MAXY]         [15,15,MAXY]     [15,15,MINY]     [0,15,MINY]
//
// ...and the image-coord centers of these blocks, relative to the image-coord center of the origin block, are:
// NED              NEU              SEU                 SWU              SWD              NWD
// [0,-2B*MINY]     [0,-2B*MAXY]     [30B,-15B-2B*MAXY]  [60B,-2B*MAXY]   [60B,-2B*MINY]   [30B,15B-2B*MINY]
//
// ...and the distances to the origin blocks of the neighboring chunks are:
// N            E            S            W
// [-32B,16B]   [-32B,-16B]  [32B,-16B]   [32B,16B]
// NE           SE           SW           NW
// [-64B,0]     [0,-32B]     [64B,0]      [0,32B]
//
// ...and the endpoint-exclusive bounding box of a chunk, from the center of the origin block, is:
// [-2B,-17B-2B*MAXY] to [62B,17B-2B*MINY]   (size 64B x 34B-2B*(MAXY-MINY))
//
// the center of the origin block of chunk [0,0] is the origin for the absolute pixel
//  coord system, so the center of the origin block of chunk [cx,cz] is [32*B*cx + 32*B*cz, -16B*cx + 16*B*cz]

// tile size must be = T * 64B for some T (so it covers the width of at least one chunk)
// ...each tile has a base chunk; the tile's bounding box shares its bottom-left corner with
//  its base chunk's full bounding box (so the base chunk is contained within the tile on the
//  left, right, and bottom, but may extend past the top of the tile)
//
// TileIdx delta      ChunkIdx delta
// [tx,ty]            [cx,cz]
// -----------------------------------------------
// [1,0]              [T,T]
// [0,1]              [-2T,2T]
//
// TileIdx:
//     [tx,ty] in tile coords
// ChunkIdx of base chunk:
//     [T*tx - 2*T*ty, T*tx + 2*T*ty] in chunk coords
// center of base chunk's origin block:
//     [64*B*T*tx, 64*B*T*ty] in absolute pixels
// base chunk's endpoint-exclusive bounding box:
//     [64*B*T*tx - 2*B, 64*B*T*ty - 17*B-2*B*MAXY] to [64*B*T*tx + 62*B, 64*B*T*ty + 17*B-2*B*MINY] in absolute pixels
// tile's endpoint-exclusive bounding box:
//     [64*B*T*tx - 2*B, 64*B*T*ty + 17*B - 64*B*T] to [64*B*T*tx - 2*B + 64*B*T, 64*B*T*ty + 17*B] in absolute pixels
//
// to compute the TileIdx [tx,ty] that includes a pixel [x,y]:
//  1. let x' = x + 2B, y' = y + 64BT - 17B
//  2. tx = floor(x' / 64BT)
//  3. ty = floor(y' / 64BT)
// ...where floor(a / b) represents floored division, i.e. the result you'd get by performing the real-number division a / b
//  and then taking the floor

// the tiles required to draw a chunk can be determined by finding the tile that the origin block's center
//  is in, then checking the tiles below, right, and above that one, looking for bounding box intersections

// the set of chunks required to draw a tile can be constructed thusly:
// set #1: start with the base chunk and the chunk directly SE of it
// set #2: if T > 1, add all the chunks that can be reached by moving up to T-1 steps SW
//          and/or up to T-1 double steps SE from the chunks in set #1 (this is the set of
//          chunks that would be in set #1 for *some* tile, if T was = 1)
// set #3: add all chunks that are immediate N, E, S, or W neighbors of chunks in set #2
//          (these are the chunks whose bottom layer of blocks is partially within the tile)
// set #4: add all chunks that are up to 8 steps NW of a chunk in set #3 (these are the chunks
//          *some* layer of which is partially within the tile)


struct ChunkIdx;
struct RegionIdx;
struct TileIdx;
struct ZoomTileIdx;

struct MapParams
{
	int B;  // block size; must be >= 2
	int T;  // tile multiplier; must be >= 1
	// Google Maps zoom level of the base tiles; maximum map size is 2^baseZoom by 2^baseZoom tiles
	int baseZoom;

	// MINY and MAXY values used during rendering; these are either specified by the user, or filled
	//  in with Minecraft's limits (0-255)
	int minY, maxY;
	
	// whether or not the MINY/MAXY values are user-provided or defaults; default values are not stored
	//  in pigmap.params
	bool userMinY, userMaxY;

	MapParams(int b, int t, int bz) : B(b), T(t), baseZoom(bz), minY(0), maxY(255), userMinY(false), userMaxY(false) {}
	MapParams() : B(0), T(0), baseZoom(0), minY(0), maxY(255), userMinY(false), userMaxY(false) {}

	int tileSize() const {return 64*B*T;}

	bool valid() const;  // see if B and T are okay
	bool validZoom() const;  // see if baseZoom is okay
	bool validYRange() const;  // see if MINY/MAXY are okay

	// read/write the file "pigmap.params" in the output path (i.e. the top-level map directory)
	bool readFile(const std::string& outputpath);  // also validates stored values
	void writeFile(const std::string& outputpath) const;
};


struct Pixel
{
	int64_t x, y;

	Pixel(int64_t xx, int64_t yy) : x(xx), y(yy) {}

	Pixel& operator+=(const Pixel& p) {x += p.x; y += p.y; return *this;}
	Pixel& operator-=(const Pixel& p) {x -= p.x; y -= p.y; return *this;}
	bool operator==(const Pixel& p) const {return x == p.x && y == p.y;}
	bool operator!=(const Pixel& p) const {return !operator==(p);}

	TileIdx getTile(const MapParams& mp) const;
};

Pixel operator+(const Pixel& p1, const Pixel& p2);
Pixel operator-(const Pixel& p1, const Pixel& p2);

// endpoint-exclusive bounding box (right and bottom edges not included)
struct BBox
{
	Pixel topLeft, bottomRight;

	BBox(const Pixel& tl, const Pixel& br) : topLeft(tl), bottomRight(br) {}

	Pixel bottomLeft() const {return Pixel(topLeft.x, bottomRight.y);}
	Pixel topRight() const {return Pixel(bottomRight.x, topLeft.y);}

	bool includes(const Pixel& p) const;
	bool overlaps(const BBox& bb) const;
};

struct BlockIdx
{
	int64_t x, z, y;

	BlockIdx(int64_t xx, int64_t zz, int64_t yy) : x(xx), z(zz), y(yy) {}

	bool occludes(const BlockIdx& bi) const;
	bool isOccludedBy(const BlockIdx& bi) const {return bi.occludes(*this);}

	Pixel getCenter(const MapParams& mp) const {return Pixel(2*mp.B*(x+z), mp.B*(z-x-2*y));}
	BBox getBBox(const MapParams& mp) const {Pixel c = getCenter(mp); return BBox(c - Pixel(2*mp.B,2*mp.B), c + Pixel(2*mp.B,2*mp.B));}
	ChunkIdx getChunkIdx() const;

	// there are many blocks that project to each pixel on the map (one for each Y-value);
	//  this returns the topmost, assuming that the pixel is properly aligned on the block-center grid
	static BlockIdx topBlock(const Pixel& p, const MapParams& mp);

	BlockIdx& operator+=(const BlockIdx& bi) {x += bi.x; z += bi.z; y += bi.y; return *this;}
	BlockIdx& operator-=(const BlockIdx& bi) {x -= bi.x; z -= bi.z; y -= bi.y; return *this;}
	bool operator==(const BlockIdx& bi) const {return x == bi.x && z == bi.z && y == bi.y;}
	bool operator!=(const BlockIdx& bi) const {return !operator==(bi);}
};

BlockIdx operator+(const BlockIdx& bi1, const BlockIdx& bi2);
BlockIdx operator-(const BlockIdx& bi1, const BlockIdx& bi2);

struct ChunkIdx
{
	int64_t x, z;

	ChunkIdx(int64_t xx, int64_t zz) : x(xx), z(zz) {}

	// just the filename (e.g. "c.0.0.dat")
	std::string toFileName() const;
	// the relative path from the top level of world data (e.g. "0/0/c.0.0.dat")
	std::string toFilePath() const;

	// see if a path is a valid chunk file and return its ChunkIdx if so
	// ...can be plain filename, relative path, or absolute path; the chunk coords
	//  depend only on the filename
	static bool fromFilePath(const std::string& filename, ChunkIdx& result);

	BlockIdx originBlock() const {return BlockIdx(x*16, z*16, 0);}
	BlockIdx nedCorner(const MapParams& mp) const {return BlockIdx(x*16, z*16, mp.minY);}
	BBox getBBox(const MapParams& mp) const {Pixel c = originBlock().getCenter(mp); return BBox(c - Pixel(2*mp.B,(17+2*mp.maxY)*mp.B), c + Pixel(62*mp.B,(17-2*mp.minY)*mp.B));}
	RegionIdx getRegionIdx() const;

	std::vector<TileIdx> getTiles(const MapParams& mp) const;

	ChunkIdx& operator+=(const ChunkIdx& ci) {x += ci.x; z += ci.z; return *this;}
	ChunkIdx& operator-=(const ChunkIdx& ci) {x -= ci.x; z -= ci.z; return *this;}
	bool operator==(const ChunkIdx& ci) const {return x == ci.x && z == ci.z;}
	bool operator!=(const ChunkIdx& ci) const {return !operator==(ci);}
};

ChunkIdx operator+(const ChunkIdx& ci1, const ChunkIdx& ci2);
ChunkIdx operator-(const ChunkIdx& ci1, const ChunkIdx& ci2);

struct RegionIdx
{
	int64_t x, z;

	RegionIdx(int64_t xx, int64_t zz) : x(xx), z(zz) {}

	// just the filename (e.g. "r.-1.2.mcr" for old-style, "r.-1.2.mca" for Anvil)
	std::string toOldFileName() const;
	std::string toAnvilFileName() const;

	// see if a path is a valid region file and return its RegionIdx if so
	// ...can be plain filename, relative path, or absolute path; region coords
	//  depend only on the filename
	// ...can be either an old (".mcr") or Anvil (".mca") filename
	static bool fromFilePath(const std::string& filename, RegionIdx& result);

	ChunkIdx baseChunk() const {return ChunkIdx(x*32, z*32);}  // NE corner

	bool operator==(const RegionIdx& ri) const {return x == ri.x && z == ri.z;}
	bool operator!=(const RegionIdx& ri) const {return !operator==(ri);}
};

// these guys represent tiles at the base zoom level
struct TileIdx
{
	// these are not the same coords used by Google Maps, since their coords are all positive;
	//  our tile [0,0] maps to their tile [2^(baseZoom-1),2^(baseZoom-1)], etc.
	int64_t x, y;

	TileIdx(int64_t xx, int64_t yy) : x(xx), y(yy) {}

	// Google Maps limit is 2^Z by 2^Z tiles per zoom level Z; check whether this TileIdx is
	//  within the allowed range for baseZoom
	bool valid(const MapParams& mp) const;
	// get Google Maps filepath (e.g. "0/3/2/0/0/1/2.png"), or empty string for invalid tile
	std::string toFilePath(const MapParams& mp) const;

	ChunkIdx baseChunk(const MapParams& mp) const {return ChunkIdx(mp.T*(x-2*y), mp.T*(x+2*y));}
	BBox getBBox(const MapParams& mp) const;
	ZoomTileIdx toZoomTileIdx(const MapParams& mp) const;

	TileIdx& operator+=(const TileIdx& t) {x += t.x; y += t.y; return *this;}
	TileIdx& operator-=(const TileIdx& t) {x -= t.x; y -= t.y; return *this;}
	bool operator==(const TileIdx& t) const {return t.x == x && t.y == y;}
	bool operator!=(const TileIdx& t) const {return !operator==(t);}
};

TileIdx operator+(const TileIdx& t1, const TileIdx& t2);
TileIdx operator-(const TileIdx& t1, const TileIdx& t2);

// these guys represent tiles at the other zoom levels
struct ZoomTileIdx
{
	int64_t x, y;  // Google Maps coords--each coord goes from 0 to 2^zoom
	int zoom;  // Google Maps zoom level--0 for top level (base.png), etc.

	ZoomTileIdx(int64_t xx, int64_t yy, int z) : x(xx), y(yy), zoom(z) {}

	bool valid() const;
	std::string toFilePath() const;

	// get the top-left base tile contained in this tile
	TileIdx toTileIdx(const MapParams& mp) const;

	// if z > zoom, gets the top-left tile of those at level z that this tile includes;
	//  if z < zoom, gets the tile at level z that includes this tile
	ZoomTileIdx toZoom(int z) const;

	// no operator+; addition shouldn't be defined for tiles with different zoom levels
	ZoomTileIdx add(int dx, int dy) const {return ZoomTileIdx(x + dx, y + dy, zoom);}
};

#endif // MAP_H