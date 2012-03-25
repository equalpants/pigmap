// Copyright 2011 Michael J. Nelson
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

#ifndef REGION_H
#define REGION_H

#include <stdint.h>

#include "map.h"
#include "tables.h"


// offset into a region of a chunk
struct ChunkOffset
{
	int64_t x, z;
	ChunkOffset(const ChunkIdx& ci)
	{
		RegionIdx ri = ci.getRegionIdx();
		x = ci.x - ri.x*32;
		z = ci.z - ri.z*32;
	}
};

struct string84
{
	std::string s;
	explicit string84(const std::string& sss) : s(sss) {}
};

struct RegionFileReader
{
	// region file is broken into 4096-byte sectors; first sector is the header that holds the
	//  chunk offsets, remaining sectors are chunk data

	// chunk offsets are big-endian; lower (that is, 4th) byte is size in sectors, upper 3 bytes are
	//  sector offset *in region file* (one more than the offset into chunkdata)
	// offsets are indexed by Z*32 + X
	std::vector<uint32_t> offsets;
	// each set of chunk data contains:
	//  -a 4-byte big-endian data length (not including the length field itself)
	//  -a single-byte version: 1 for gzip, 2 for zlib (this byte *is* included in the length)
	//  -length - 1 bytes of actual compressed data
	std::vector<uint8_t> chunkdata;
	// whether this data was read from an Anvil region file or an old-style one
	bool anvil;

	RegionFileReader()
	{
		offsets.resize(32 * 32);
		chunkdata.reserve(8388608);
	}
	
	void swap(RegionFileReader& rfr)
	{
		offsets.swap(rfr.offsets);
		chunkdata.swap(rfr.chunkdata);
		std::swap(anvil, rfr.anvil);
	}

	// extract values from the offsets
	static int getIdx(const ChunkOffset& co) {return co.z*32 + co.x;}
	uint32_t getSizeSectors(int idx) const {return fromBigEndian(offsets[idx]) & 0xff;}
	uint32_t getSectorOffset(int idx) const {return fromBigEndian(offsets[idx]) >> 8;}
	bool containsChunk(const ChunkOffset& co) {return offsets[getIdx(co)] != 0;}


	// attempt to read a region file; return 0 for success, -1 for file not found, -2 for
	//  other errors
	// looks for an Anvil region file (.mca) first, then an old-style one (.mcr)
	int loadFromFile(const RegionIdx& ri, const std::string& inputpath);

	// attempt to decompress a chunk into a buffer; return 0 for success, -1 for missing chunk,
	//  -2 for other errors
	// (this is not const only because zlib won't take const pointers for input)
	int decompressChunk(const ChunkOffset& co, std::vector<uint8_t>& buf);

	// attempt to read only the header (i.e. the chunk offsets) from a region file; return 0
	//  for success, -1 for file not found, -2 for other errors
	// looks for an Anvil region file (.mca) first, then an old-style one (.mcr)
	int loadHeaderOnly(const RegionIdx& ri, const std::string& inputpath);

	// open a region file, load only its header, and return a list of chunks it contains (i.e. the ones that
	//  actually currently exist)
	// ...returns 0 for success, -1 for file not found, -2 for other errors
	int getContainedChunks(const RegionIdx& ri, const string84& inputpath, std::vector<ChunkIdx>& chunks);
};

// iterates over the chunks in a region
struct RegionChunkIterator
{
	bool end;  // true once we've reached the end
	ChunkIdx current;  // if end == false, holds the current chunk

	ChunkIdx basechunk;

	// constructor initializes to to first chunk in provided region
	RegionChunkIterator(const RegionIdx& ri);

	// move to the next chunk, or to the end
	void advance();
};


struct RegionCacheStats
{
	int64_t hits, misses;
	// types of misses:
	int64_t read;  // successfully read from disk
	int64_t skipped;  // assumed not to exist because not required in a full render
	int64_t missing;  // non-required region not present on disk
	int64_t reqmissing;  // required region not present on disk
	int64_t corrupt;  // found on disk, but failed to read

	RegionCacheStats() : hits(0), misses(0), read(0), skipped(0), missing(0), reqmissing(0), corrupt(0) {}

	RegionCacheStats& operator+=(const RegionCacheStats& rs);
};

struct RegionCacheEntry
{
	PosRegionIdx ri;  // or [-1, -1] if this entry is empty
	RegionFileReader regionfile;
	
	RegionCacheEntry() : ri(-1,-1) {}
};

#define RCACHEBITSX 1
#define RCACHEBITSZ 1
#define RCACHEXSIZE (1 << RCACHEBITSX)
#define RCACHEZSIZE (1 << RCACHEBITSZ)
#define RCACHESIZE (RCACHEXSIZE * RCACHEZSIZE)
#define RCACHEXMASK (RCACHEXSIZE - 1)
#define RCACHEZMASK (RCACHEZSIZE - 1)

struct RegionCache : private nocopy
{
	RegionCacheEntry entries[RCACHESIZE];

	ChunkTable& chunktable;
	RegionTable& regiontable;
	RegionCacheStats& stats;
	std::string inputpath;
	bool fullrender;
	// readbuf is an extra less-important cache entry--when a new region is read, it's this entry which will be trashed
	//  and its storage used for the read (which might fail), but if the read succeeds, the new region is swapped
	//  into its proper place in the cache, and the previous tenant there moves here
	RegionCacheEntry readbuf;
	RegionCache(ChunkTable& ctable, RegionTable& rtable, const std::string& inpath, bool fullr, RegionCacheStats& st)
		: chunktable(ctable), regiontable(rtable), inputpath(inpath), fullrender(fullr), stats(st)
	{
	}

	// attempt to decompress a chunk into a buffer; return 0 for success, -1 for missing chunk,
	//  -2 for other errors
	// (this is not const only because zlib won't take const pointers for input)
	int getDecompressedChunk(const PosChunkIdx& ci, std::vector<uint8_t>& buf, bool& anvil);

	static int getEntryNum(const PosRegionIdx& ri) {return (ri.x & RCACHEXMASK) * RCACHEZSIZE + (ri.z & RCACHEZMASK);}

	void readRegionFile(const PosRegionIdx& ri);
};


#endif // REGION_H