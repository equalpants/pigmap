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

#ifndef TABLES_H
#define TABLES_H

#include <bitset>
#include <stdint.h>

#include "map.h"
#include "utils.h"



#define CTDATASIZE 3

#define CTLEVEL1BITS 5
#define CTLEVEL2BITS 5
#define CTLEVEL3BITS 8

#define CTLEVEL1SIZE (1 << CTLEVEL1BITS)
#define CTLEVEL2SIZE (1 << CTLEVEL2BITS)
#define CTLEVEL3SIZE (1 << CTLEVEL3BITS)
#define CTTOTALSIZE (CTLEVEL1SIZE * CTLEVEL2SIZE * CTLEVEL3SIZE)

#define CTLEVEL1MASK (CTLEVEL1SIZE - 1)
#define CTLEVEL2MASK ((CTLEVEL2SIZE - 1) << CTLEVEL1BITS)
#define CTLEVEL3MASK (((CTLEVEL3SIZE - 1) << CTLEVEL1BITS) << CTLEVEL2BITS)

#define CTGETLEVEL1(a) (a & CTLEVEL1MASK)
#define CTGETLEVEL2(a) ((a & CTLEVEL2MASK) >> CTLEVEL1BITS)
#define CTGETLEVEL3(a) (((a & CTLEVEL3MASK) >> CTLEVEL2BITS) >> CTLEVEL1BITS)

// variation of ChunkIdx for use with the ChunkTable: translates so that all coords are positive
// ...can also be used to check for the map being too big
struct PosChunkIdx
{
	int64_t x, z;

	PosChunkIdx(int64_t xx, int64_t zz) : x(xx), z(zz) {}
	PosChunkIdx(const ChunkIdx& ci) : x(ci.x + CTTOTALSIZE/2), z(ci.z + CTTOTALSIZE/2) {}
	ChunkIdx toChunkIdx() const {return ChunkIdx(x - CTTOTALSIZE/2, z - CTTOTALSIZE/2);}
	bool valid() const {return x >= 0 && x < CTTOTALSIZE && z >= 0 && z < CTTOTALSIZE;}

	bool operator==(const PosChunkIdx& ci) const {return x == ci.x && z == ci.z;}
	bool operator!=(const PosChunkIdx& ci) const {return !operator==(ci);}
};

// structure to hold information about a 32x32 set of chunks: for each chunk, whether it needs to be drawn,
//  whether it's even present on disk, etc.
struct ChunkSet
{
	// each chunk gets 3 bits:
	//  -first bit is 1 for required (must be drawn), 0 for not required
	//  -last two bits describe state of chunk on disk:
	//    00: have not tried to find chunk on disk yet
	//    01: have successfully read chunk from disk (i.e. it should be in the cache, if we still need it)
	//    10: chunk does not exist on disk
	//    11: chunk file is corrupted
	static const int CHUNK_UNKNOWN = 0;
	static const int CHUNK_CACHED = 1;
	static const int CHUNK_MISSING = 2;
	static const int CHUNK_CORRUPTED = 3;
	std::bitset<CTLEVEL1SIZE*CTLEVEL1SIZE*CTDATASIZE> bits;

	size_t bitIdx(const PosChunkIdx& ci) const {return (CTGETLEVEL1(ci.z) * CTLEVEL1SIZE + CTGETLEVEL1(ci.x)) * CTDATASIZE;}

	void setRequired(const PosChunkIdx& ci) {bits.set(bitIdx(ci));}
	void setDiskState(const PosChunkIdx& ci, int state) {size_t bi = bitIdx(ci); bits[bi+1] = state & 0x2; bits[bi+2] = state & 0x1;}
};

// first level of indirection: information about a 32x32 group of ChunkSets, and hence a 1024x1024 set of chunks
struct ChunkGroup
{
	// pointers to ChunkSets with the data, or NULL for sets that aren't used
	ChunkSet *chunksets[CTLEVEL2SIZE*CTLEVEL2SIZE];

	ChunkGroup() {for (int i = 0; i < CTLEVEL2SIZE*CTLEVEL2SIZE; i++) chunksets[i] = NULL;}
	~ChunkGroup() {for (int i = 0; i < CTLEVEL2SIZE*CTLEVEL2SIZE; i++) if (chunksets[i] != NULL) delete chunksets[i];}

	int chunkSetIdx(const PosChunkIdx& ci) const {return CTGETLEVEL2(ci.z) * CTLEVEL2SIZE + CTGETLEVEL2(ci.x);}
	ChunkSet* getChunkSet(const PosChunkIdx& ci) const {return chunksets[chunkSetIdx(ci)];}

	void setRequired(const PosChunkIdx& ci);
	void setDiskState(const PosChunkIdx& ci, int state);
};

// second (and final) level of indirection: 256x256 groups, so 262144x262144 possible chunks
struct ChunkTable : private nocopy
{
	ChunkGroup *chunkgroups[CTLEVEL3SIZE*CTLEVEL3SIZE];

	ChunkTable() {for (int i = 0; i < CTLEVEL3SIZE*CTLEVEL3SIZE; i++) chunkgroups[i] = NULL;}
	~ChunkTable() {for (int i = 0; i < CTLEVEL3SIZE*CTLEVEL3SIZE; i++) if (chunkgroups[i] != NULL) delete chunkgroups[i];}

	int chunkGroupIdx(const PosChunkIdx& ci) const {return CTGETLEVEL3(ci.z) * CTLEVEL3SIZE + CTGETLEVEL3(ci.x);}
	ChunkGroup* getChunkGroup(const PosChunkIdx& ci) const {return chunkgroups[chunkGroupIdx(ci)];}
	ChunkSet* getChunkSet(const PosChunkIdx& ci) const {ChunkGroup *cg = getChunkGroup(ci); return (cg == NULL) ? NULL : cg->getChunkSet(ci);}

	// given indices into the ChunkGroups/ChunkSets/bitset, construct a PosChunkIdx
	static PosChunkIdx toPosChunkIdx(int cgi, int csi, int bi);
	
	bool isRequired(const PosChunkIdx& ci) const {ChunkSet *cs = getChunkSet(ci); return (cs == NULL) ? false : cs->bits[cs->bitIdx(ci)];}
	int getDiskState(const PosChunkIdx& ci) const {ChunkSet *cs = getChunkSet(ci); return (cs == NULL) ? 0 : ((cs->bits[cs->bitIdx(ci)+1] ? 0x2 : 0) | (cs->bits[cs->bitIdx(ci)+2] ? 0x1 : 0));}

	void setRequired(const PosChunkIdx& ci);
	void setDiskState(const PosChunkIdx& ci, int state);

	void copyFrom(const ChunkTable& ctable);
};


// given a ChunkTable, iterates over the required chunks
// ...this is obsolete and not used, except in some test functions
struct RequiredChunkIterator
{
	bool end;  // true once we've reached the end
	PosChunkIdx current;  // if end == false, holds the current chunk

	ChunkTable& chunktable;
	int cgi, csi, bi;

	// constructor initializes us to the first required chunk
	RequiredChunkIterator(ChunkTable& ctable);

	// move to the next required chunk, or to the end
	void advance();
};










#define TTDATASIZE 2

#define TTLEVEL1BITS 4
#define TTLEVEL2BITS 4
#define TTLEVEL3BITS 8

#define TTLEVEL1SIZE (1 << TTLEVEL1BITS)
#define TTLEVEL2SIZE (1 << TTLEVEL2BITS)
#define TTLEVEL3SIZE (1 << TTLEVEL3BITS)
#define TTTOTALSIZE (TTLEVEL1SIZE * TTLEVEL2SIZE * TTLEVEL3SIZE)

#define TTLEVEL1MASK (TTLEVEL1SIZE - 1)
#define TTLEVEL2MASK ((TTLEVEL2SIZE - 1) << TTLEVEL1BITS)
#define TTLEVEL3MASK (((TTLEVEL3SIZE - 1) << TTLEVEL1BITS) << TTLEVEL2BITS)

#define TTGETLEVEL1(a) (a & TTLEVEL1MASK)
#define TTGETLEVEL2(a) ((a & TTLEVEL2MASK) >> TTLEVEL1BITS)
#define TTGETLEVEL3(a) (((a & TTLEVEL3MASK) >> TTLEVEL2BITS) >> TTLEVEL1BITS)

// variation of TileIdx for use with the TileTable: translates so that all coords are positive
// ...can also be used to check for the map being too big
struct PosTileIdx
{
	int64_t x, y;

	PosTileIdx(int64_t xx, int64_t yy) : x(xx), y(yy) {}
	PosTileIdx(const TileIdx& ti) : x(ti.x + TTTOTALSIZE/2), y(ti.y + TTTOTALSIZE/2) {}
	TileIdx toTileIdx() const {return TileIdx(x - TTTOTALSIZE/2, y - TTTOTALSIZE/2);}
	bool valid() const {return x >= 0 && x < TTTOTALSIZE && y >= 0 && y < TTTOTALSIZE;}

	bool operator==(const PosTileIdx& ti) const {return x == ti.x && y == ti.y;}
	bool operator!=(const PosTileIdx& ti) const {return !operator==(ti);}
};

// structure to hold information about a 16x16 set of tiles: for each tile, whether it's been drawn yet
struct TileSet
{
	// each tile gets two bits: first is whether it's required, second is whether it's been drawn
	std::bitset<TTLEVEL1SIZE*TTLEVEL1SIZE*TTDATASIZE> bits;

	size_t bitIdx(const PosTileIdx& ti) const {return (TTGETLEVEL1(ti.y) * TTLEVEL1SIZE + TTGETLEVEL1(ti.x)) * TTDATASIZE;}

	// assumes that ti actually belongs to this set
	bool isRequired(const PosTileIdx& ti) const {return bits[bitIdx(ti)];}

	// set tile's required bit and return previous state of bit
	bool setRequired(const PosTileIdx& ti) {size_t bi = bitIdx(ti); bool rv = bits[bi]; bits.set(bi); return rv;}
	void setDrawn(const PosTileIdx& ti) {bits.set(bitIdx(ti)+1);}
};

// first level of indirection: information about a 256x256 set of tiles
struct TileGroup
{
	// pointers to TileSets with the data, or NULL for 16x16 sets that aren't used
	TileSet *tilesets[TTLEVEL2SIZE*TTLEVEL2SIZE];

	// number of tiles in this group that have been set to required
	int64_t reqcount;

	TileGroup() : reqcount(0) {for (int i = 0; i < TTLEVEL2SIZE*TTLEVEL2SIZE; i++) tilesets[i] = NULL;}
	~TileGroup() {for (int i = 0; i < TTLEVEL2SIZE*TTLEVEL2SIZE; i++) if (tilesets[i] != NULL) delete tilesets[i];}

	int tileSetIdx(const PosTileIdx& ti) const {return TTGETLEVEL2(ti.y) * TTLEVEL2SIZE + TTGETLEVEL2(ti.x);}
	TileSet* getTileSet(const PosTileIdx& ti) const {return tilesets[tileSetIdx(ti)];}

	bool setRequired(const PosTileIdx& ti);  // set tile's required bit and return previous state of bit
	void setDrawn(const PosTileIdx& ti);
};

// second (and final) level of indirection: a 65536x65536 set of tiles
struct TileTable : private nocopy
{
	TileGroup *tilegroups[TTLEVEL3SIZE*TTLEVEL3SIZE];

	int64_t reqcount;

	TileTable() : reqcount(0) {for (int i = 0; i < TTLEVEL3SIZE*TTLEVEL3SIZE; i++) tilegroups[i] = NULL;}
	~TileTable() {for (int i = 0; i < TTLEVEL3SIZE*TTLEVEL3SIZE; i++) if (tilegroups[i] != NULL) delete tilegroups[i];}

	int tileGroupIdx(const PosTileIdx& ti) const {return TTGETLEVEL3(ti.y) * TTLEVEL3SIZE + TTGETLEVEL3(ti.x);}
	TileGroup* getTileGroup(const PosTileIdx& ti) const {return tilegroups[tileGroupIdx(ti)];}
	TileSet* getTileSet(const PosTileIdx& ti) const {TileGroup *tg = getTileGroup(ti); return (tg == NULL) ? NULL : tg->getTileSet(ti);}

	// given indices into the TileGroups/TileSets/bitset, construct a PosTileIdx
	static PosTileIdx toPosTileIdx(int tgi, int tsi, int bi);
	
	bool isRequired(const PosTileIdx& ti) const {TileSet *ts = getTileSet(ti); return (ts == NULL) ? false : ts->bits[ts->bitIdx(ti)];}
	bool isDrawn(const PosTileIdx& ti) const {TileSet *ts = getTileSet(ti); return (ts == NULL) ? false : ts->bits[ts->bitIdx(ti)+1];}

	bool setRequired(const PosTileIdx& ti);  // set tile's required bit and return previous state of bit
	void setDrawn(const PosTileIdx& ti);

	// see if an entire zoom tile can be rejected because its TileGroup or TileSet is NULL
	bool reject(const ZoomTileIdx& zti, const MapParams& mp) const;

	// get the total number of base tiles required to draw a zoom tile
	int64_t getNumRequired(const ZoomTileIdx& zti, const MapParams& mp) const;

	void copyFrom(const TileTable& ttable);
};



// given a TileTable, iterates over the required tiles
struct RequiredTileIterator
{
	bool end;  // true once we've reached the end
	PosTileIdx current;  // if end == false, holds the current tile

	TileTable& tiletable;
	// these guys are Z-order indices and must be converted to row-major when accessing the TileTable
	int ztgi, ztsi, zbi;

	// constructor initializes us to the first required tile
	RequiredTileIterator(TileTable& ttable);

	// move in Z-order to the next required tile, or to the end
	void advance();
};

// given a TileTable, iterates over the non-NULL TileGroups
struct TileGroupIterator
{
	bool end;  // true once we've reached the end
	int tgi;  // if end == false, holds the current index into TileTable::tilegroups
	ZoomTileIdx zti;  // if end == false, holds the zoom tile corresponding to the current TileGroup

	TileTable& tiletable;
	MapParams mp;

	// constructor initializes to first non-NULL TileGroup
	TileGroupIterator(TileTable& ttable, const MapParams& mparams);

	// move to the next non-NULL TileGroup, or to the end
	void advance();
};




#define RTDATASIZE 2

#define RTLEVEL1BITS 4
#define RTLEVEL2BITS 4
#define RTLEVEL3BITS 6

#define RTLEVEL1SIZE (1 << RTLEVEL1BITS)
#define RTLEVEL2SIZE (1 << RTLEVEL2BITS)
#define RTLEVEL3SIZE (1 << RTLEVEL3BITS)
#define RTTOTALSIZE (RTLEVEL1SIZE * RTLEVEL2SIZE * RTLEVEL3SIZE)

#define RTLEVEL1MASK (RTLEVEL1SIZE - 1)
#define RTLEVEL2MASK ((RTLEVEL2SIZE - 1) << RTLEVEL1BITS)
#define RTLEVEL3MASK (((RTLEVEL3SIZE - 1) << RTLEVEL1BITS) << RTLEVEL2BITS)

#define RTGETLEVEL1(a) (a & RTLEVEL1MASK)
#define RTGETLEVEL2(a) ((a & RTLEVEL2MASK) >> RTLEVEL1BITS)
#define RTGETLEVEL3(a) (((a & RTLEVEL3MASK) >> RTLEVEL2BITS) >> RTLEVEL1BITS)

struct PosRegionIdx
{
	int64_t x, z;

	PosRegionIdx(int64_t xx, int64_t zz) : x(xx), z(zz) {}
	PosRegionIdx(const RegionIdx& ri) : x(ri.x + RTTOTALSIZE/2), z(ri.z + RTTOTALSIZE/2) {}
	RegionIdx toRegionIdx() const {return RegionIdx(x - RTTOTALSIZE/2, z - RTTOTALSIZE/2);}
	bool valid() const {return x >= 0 && x < RTTOTALSIZE && z >= 0 && z < RTTOTALSIZE;}

	bool operator==(const PosRegionIdx& ri) const {return x == ri.x && z == ri.z;}
	bool operator!=(const PosRegionIdx& ri) const {return !operator==(ri);}
};

struct RegionSet
{
	// each region gets two bits: first is whether it's required, second is whether it has already failed to
	//  read from disk (either by being missing or corrupted)
	std::bitset<RTLEVEL1SIZE*RTLEVEL1SIZE*RTDATASIZE> bits;

	size_t bitIdx(const PosRegionIdx& ri) const {return (RTGETLEVEL1(ri.z) * RTLEVEL1SIZE + RTGETLEVEL1(ri.x)) * RTDATASIZE;}

	void setRequired(const PosRegionIdx& ri) {bits.set(bitIdx(ri));}
	void setFailed(const PosRegionIdx& ri) {bits.set(bitIdx(ri)+1);}
};

struct RegionGroup
{
	RegionSet *regionsets[RTLEVEL2SIZE*RTLEVEL2SIZE];

	RegionGroup() {for (int i = 0; i < RTLEVEL2SIZE*RTLEVEL2SIZE; i++) regionsets[i] = NULL;}
	~RegionGroup() {for (int i = 0; i < RTLEVEL2SIZE*RTLEVEL2SIZE; i++) if (regionsets[i] != NULL) delete regionsets[i];}

	int regionSetIdx(const PosRegionIdx& ri) const {return RTGETLEVEL2(ri.z) * RTLEVEL2SIZE + RTGETLEVEL2(ri.x);}
	RegionSet* getRegionSet(const PosRegionIdx& ri) const {return regionsets[regionSetIdx(ri)];}

	void setRequired(const PosRegionIdx& ri);
	void setFailed(const PosRegionIdx& ri);
};

struct RegionTable : private nocopy
{
	RegionGroup *regiongroups[RTLEVEL3SIZE*RTLEVEL3SIZE];

	RegionTable() {for (int i = 0; i < RTLEVEL3SIZE*RTLEVEL3SIZE; i++) regiongroups[i] = NULL;}
	~RegionTable() {for (int i = 0; i < RTLEVEL3SIZE*RTLEVEL3SIZE; i++) if (regiongroups[i] != NULL) delete regiongroups[i];}

	int regionGroupIdx(const PosRegionIdx& ri) const {return RTGETLEVEL3(ri.z) * RTLEVEL3SIZE + RTGETLEVEL3(ri.x);}
	RegionGroup* getRegionGroup(const PosRegionIdx& ri) const {return regiongroups[regionGroupIdx(ri)];}
	RegionSet* getRegionSet(const PosRegionIdx& ri) const {RegionGroup *rg = getRegionGroup(ri); return (rg == NULL) ? NULL : rg->getRegionSet(ri);}

	// given indices into the RegionGroups/RegionSets/bitset, construct a PosRegionIdx
	static PosRegionIdx toPosRegionIdx(int rgi, int rsi, int bi);
	
	bool isRequired(const PosRegionIdx& ri) const {RegionSet *rs = getRegionSet(ri); return (rs == NULL) ? false : rs->bits[rs->bitIdx(ri)];}
	bool hasFailed(const PosRegionIdx& ri) const {RegionSet *rs = getRegionSet(ri); return (rs == NULL) ? false : rs->bits[rs->bitIdx(ri)+1];}

	void setRequired(const PosRegionIdx& ri);
	void setFailed(const PosRegionIdx& ri);

	void copyFrom(const RegionTable& rtable);
};



#endif // TABLES_H