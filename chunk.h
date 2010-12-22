#ifndef CHUNK_H
#define CHUNK_H

#include <string.h>
#include <string>
#include <vector>
#include <stdint.h>

#include "map.h"
#include "tables.h"



// find all chunks on disk, set them to required in the ChunkTable, and set all tiles they
//  touch to required in the TileTable
// returns false if the world is too big to fit in one of the tables
// if mp.baseZoom is set to -1 coming in, then this function will set it to the smallest zoom
//  that can fit everything
bool makeAllChunksRequired(const std::string& inputdir, ChunkTable& chunktable, TileTable& tiletable, MapParams& mp, int64_t& reqchunkcount, int64_t& reqtilecount);

// read a list of chunk filenames from a file and set the chunks to required in the ChunkTable, and set
//  any tiles they touch to required in the TileTable
// returns 0 on success, -1 if baseZoom is too small, -2 for other errors (can't read chunklist, world too big
//  for our internal data structures, etc.)
int readChunklist(const std::string& chunklist, ChunkTable& chunktable, TileTable& tiletable, const MapParams& mp, int64_t& reqchunkcount, int64_t& reqtilecount);

// build a test world by making approximately size chunks required
// if mp.baseZoom is set to -1 coming in, it will be set to the smallest zoom that can fit everything
void makeTestWorld(int size, ChunkTable& chunktable, TileTable& tiletable, MapParams& mp, int64_t& reqchunkcount, int64_t& reqtilecount);



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

	ChunkCacheStats() : hits(0), misses(0), read(0), skipped(0), missing(0), reqmissing(0), corrupt(0) {}

	ChunkCacheStats& operator+=(const ChunkCacheStats& ccs);
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
	ChunkCacheStats& stats;
	std::string inputpath;
	bool fullrender;
	std::vector<uint8_t> readbuf;  // buffer for decompressing into when reading
	ChunkCache(ChunkTable& ctable, const std::string& inpath, bool fullr, ChunkCacheStats& st)
		: chunktable(ctable), inputpath(inpath), fullrender(fullr), stats(st)
	{
		memset(&blankdata, 0, sizeof(ChunkData));
		readbuf.reserve(131072);
	}

	static int getEntryNum(const PosChunkIdx& ci) {return (ci.x & CACHEXMASK) * CACHEZSIZE + (ci.z & CACHEZMASK);}

	// look up a chunk and return a pointer to its data
	// ...for missing/corrupt chunks, return a pointer to some blank data
	ChunkData* getData(const PosChunkIdx& ci);


	// dummy function for testing: doesn't actually read anything from disk, but keeps all the statistics (treats
	//  required chunks as successful reads, non-required chunks as missing)
	void testLookup(const PosChunkIdx& ci);
};




void testChunkCache();

// get the filepaths of all chunks on disk
void findAllChunks(const std::string& inputdir, std::vector<std::string>& chunkpaths);



#endif // CHUNK_H