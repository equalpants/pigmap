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

#ifndef CHUNK_H
#define CHUNK_H

#include <string.h>
#include <string>
#include <vector>
#include <stdint.h>

#include "map.h"
#include "tables.h"
#include "region.h"



// offset into a chunk of a block
struct BlockOffset
{
	int64_t x, z, y;
	BlockOffset(const BlockIdx& bi)
	{
		ChunkIdx ci = bi.getChunkIdx();
		x = bi.x - ci.x*16;
		z = bi.z - ci.z*16;
		y = bi.y;
	}
};

struct ChunkData
{
	uint8_t blockIDs[32768];  // one byte per block
	uint8_t blockData[16384];  // 4 bits per block

	// these guys assume that the BlockIdx actually points to this chunk
	//  (so they only look at the lower bits)
	uint8_t id(const BlockOffset& bo) const {return blockIDs[(bo.x * 16 + bo.z) * 128 + bo.y];}
	uint8_t data(const BlockOffset& bo) const
	{
		int i = (bo.x * 16 + bo.z) * 128 + bo.y;
		if ((i % 2) == 0)
			return blockData[i/2] & 0xf;
		return (blockData[i/2] & 0xf0) >> 4;
	}

	bool loadFromFile(const std::vector<uint8_t>& filebuf);
};



struct ChunkCacheStats
{
	int64_t hits, misses;
	// types of misses:
	int64_t read;  // successfully read from disk
	int64_t skipped;  // assumed not to exist because not required in a full render
	int64_t missing;  // non-required chunk not present on disk
	int64_t reqmissing;  // required chunk not present on disk
	int64_t corrupt;  // found on disk, but failed to read

	// when in region mode, the miss stats have slightly different meanings:
	//  read: caused region file to be read, and chunk was then successfully read from region
	//  missing: not present in the region file, or region file missing/corrupt
	//  corrupt: region file itself is okay, but chunk data within it is corrupt
	//  skipped/reqmissing: unused
	// ...also note that each read of a region file dumps *all* of the region's chunks into the
	//  cache, so the read statistic here should usually be much smaller than the total number of
	//  chunks in the world

	ChunkCacheStats() : hits(0), misses(0), read(0), skipped(0), missing(0), reqmissing(0), corrupt(0) {}

	ChunkCacheStats& operator+=(const ChunkCacheStats& ccs);
};

struct RegionStats
{
	int64_t read;  // successfully read from disk
	int64_t chunksread;  // total number of chunks brought in from region file reads
	int64_t skipped;  // assumed not to exist because not required in a full render
	int64_t missing;  // non-required region not found on disk
	int64_t reqmissing;  // required region not found on disk
	int64_t corrupt;  // found on disk, but failed to read

	RegionStats() : read(0), chunksread(0), skipped(0), missing(0), reqmissing(0), corrupt(0) {}

	RegionStats& operator+=(const RegionStats& rs);
};

struct ChunkCacheEntry
{
	PosChunkIdx ci;  // or [-1,-1] if this entry is empty
	ChunkData data;

	ChunkCacheEntry() : ci(-1,-1) {}
};

#define CACHEBITSX 6
#define CACHEBITSZ 6
#define CACHEXSIZE (1 << CACHEBITSX)
#define CACHEZSIZE (1 << CACHEBITSZ)
#define CACHESIZE (CACHEXSIZE * CACHEZSIZE)
#define CACHEXMASK (CACHEXSIZE - 1)
#define CACHEZMASK (CACHEZSIZE - 1)

struct ChunkCache : private nocopy
{
	ChunkCacheEntry entries[CACHESIZE];
	ChunkData blankdata;  // for use with missing chunks

	ChunkTable& chunktable;
	RegionTable& regiontable;
	ChunkCacheStats& stats;
	RegionStats& regstats;
	std::string inputpath;
	bool fullrender;
	bool regionformat;
	std::vector<uint8_t> readbuf;  // buffer for decompressing into when reading
	RegionFileReader regionfile;  // buffer for reading region files into
	ChunkCache(ChunkTable& ctable, RegionTable& rtable, const std::string& inpath, bool fullr, bool regform, ChunkCacheStats& st, RegionStats& rst)
		: chunktable(ctable), regiontable(rtable), inputpath(inpath), fullrender(fullr), regionformat(regform), stats(st), regstats(rst)
	{
		memset(&blankdata, 0, sizeof(ChunkData));
		readbuf.reserve(131072);
	}

	// look up a chunk and return a pointer to its data
	// ...for missing/corrupt chunks, return a pointer to some blank data
	ChunkData* getData(const PosChunkIdx& ci);

	static int getEntryNum(const PosChunkIdx& ci) {return (ci.x & CACHEXMASK) * CACHEZSIZE + (ci.z & CACHEZMASK);}

	void readChunkFile(const PosChunkIdx& ci);
	void readRegionFile(const PosRegionIdx& ri);
};




#endif // CHUNK_H