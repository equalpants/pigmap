// Copyright 2010-2012 Michael J. Nelson
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
	uint8_t blockIDs[65536];  // one byte per block (only half of this space used for old-style chunks)
	uint8_t blockAdd[32768];  // only in Anvil--extra bits for block ID (4 bits per block)
	uint8_t blockData[32768];  // 4 bits per block (only half of this space used for old-style chunks)
	bool anvil;  // whether this data came from an Anvil chunk or an old-style one

	// these guys assume that the BlockIdx actually points to this chunk
	//  (so they only look at the lower bits)
	uint16_t id(const BlockOffset& bo) const
	{
		if (!anvil)
			return (bo.y > 127) ? 0 : blockIDs[(bo.x * 16 + bo.z) * 128 + bo.y];
		int i = (bo.y * 16 + bo.z) * 16 + bo.x;
		if ((i % 2) == 0)
			return ((blockAdd[i/2] & 0xf) << 8) | blockIDs[i];
		return ((blockAdd[i/2] & 0xf0) << 4) | blockIDs[i];
	}
	uint8_t data(const BlockOffset& bo) const
	{
		int i;
		if (!anvil)
		{
			if (bo.y > 127)
				return 0;
			i = (bo.x * 16 + bo.z) * 128 + bo.y;
		}
		else
			i = (bo.y * 16 + bo.z) * 16 + bo.x;
		if ((i % 2) == 0)
			return blockData[i/2] & 0xf;
		return (blockData[i/2] & 0xf0) >> 4;
	}

	bool loadFromOldFile(const std::vector<uint8_t>& filebuf);
	bool loadFromAnvilFile(const std::vector<uint8_t>& filebuf);
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
	//  read: chunk was successfully read from region cache (which may or may not have triggered an
	//         actual read of the region file from disk)
	//  missing: not present in the region file, or region file missing/corrupt
	//  corrupt: region file itself is okay, but chunk data within it is corrupt
	//  skipped/reqmissing: unused

	ChunkCacheStats() : hits(0), misses(0), read(0), skipped(0), missing(0), reqmissing(0), corrupt(0) {}

	ChunkCacheStats& operator+=(const ChunkCacheStats& ccs);
};

struct ChunkCacheEntry
{
	PosChunkIdx ci;  // or [-1,-1] if this entry is empty
	ChunkData data;

	ChunkCacheEntry() : ci(-1,-1) {}
};

#define CACHEBITSX 5
#define CACHEBITSZ 5
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
	RegionCache& regioncache;
	std::string inputpath;
	bool fullrender;
	bool regionformat;
	std::vector<uint8_t> readbuf;  // buffer for decompressing into when reading
	ChunkCache(ChunkTable& ctable, RegionTable& rtable, RegionCache& rcache, const std::string& inpath, bool fullr, bool regform, ChunkCacheStats& st)
		: chunktable(ctable), regiontable(rtable), regioncache(rcache), inputpath(inpath), fullrender(fullr), regionformat(regform), stats(st)
	{
		memset(blankdata.blockIDs, 0, 65536);
		memset(blankdata.blockData, 0, 32768);
		memset(blankdata.blockAdd, 0, 32768);
		blankdata.anvil = true;
		readbuf.reserve(262144);
	}

	// look up a chunk and return a pointer to its data
	// ...for missing/corrupt chunks, return a pointer to some blank data
	ChunkData* getData(const PosChunkIdx& ci);

	static int getEntryNum(const PosChunkIdx& ci) {return (ci.x & CACHEXMASK) * CACHEZSIZE + (ci.z & CACHEZMASK);}

	void readChunkFile(const PosChunkIdx& ci);
	void readFromRegionCache(const PosChunkIdx& ci);
	void parseReadBuf(const PosChunkIdx& ci, bool anvil);
};



#endif // CHUNK_H