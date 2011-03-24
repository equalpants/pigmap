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

struct RegionFileReader
{
	// region file is broken into 4096-byte sectors; first sector is the header that holds the
	//  chunk offsets, remaining sectors are chunk data

	// chunk offsets are big-endian; lower (that is, 4th) byte is size in sectors, upper 3 bytes are
	//  sector offset *in region file* (one more than the offset into chunkdata)
	// offsets are indexed by Z*32 + X
	uint32_t offsets[32 * 32];
	// each set of chunk data contains:
	//  -a 4-byte big-endian data length (not including the length field itself)
	//  -a single-byte version: 1 for gzip, 2 for zlib (this byte *is* included in the length)
	//  -length - 1 bytes of actual compressed data
	std::vector<uint8_t> chunkdata;

	RegionFileReader()
	{
		chunkdata.reserve(4194304);
	}

	// extract values from the offsets
	static int getIdx(const ChunkOffset& co) {return co.z*32 + co.x;}
	uint32_t getSizeSectors(int idx) const {return fromBigEndian(offsets[idx]) & 0xff;}
	uint32_t getSectorOffset(int idx) const {return fromBigEndian(offsets[idx]) >> 8;}
	bool containsChunk(const ChunkOffset& co) {return offsets[getIdx(co)] != 0;}


	// attempt to read a region file; return 0 for success, -1 for file not found, -2 for
	//  other errors
	int loadFromFile(const std::string& filename);

	// attempt to decompress a chunk into a buffer; return 0 for success, -1 for missing chunk,
	//  -2 for other errors
	// (this is not const only because zlib won't take const pointers for input)
	int decompressChunk(const ChunkOffset& co, std::vector<uint8_t>& buf);

	// attempt to read only the header (i.e. the chunk offsets) from a region file; return 0
	//  for success, -1 for file not found, -2 for other errors
	int loadHeaderOnly(const std::string& filename);

	// open a region file, load only its header, and return a list of chunks it contains (i.e. the ones that
	//  actually currently exist)
	// (RegionIdx is needed to compute the ChunkIdxs)
	// ...returns false if region file can't be read
	bool getContainedChunks(const RegionIdx& ri, const std::string& filename, std::vector<ChunkIdx>& chunks);
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


#endif // REGION_H