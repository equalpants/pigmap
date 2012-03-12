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

#include <stdint.h>
#include <iostream>
#include <stdlib.h>
#include <time.h>
#include <memory>

#include "chunk.h"
#include "utils.h"

using namespace std;








bool ChunkData::loadFromFile(const vector<uint8_t>& filebuf)
{
	// the hell with parsing this whole godforsaken NBT format; just look for the arrays we need
	uint8_t idsTag[13] = {7, 0, 6, 'B', 'l', 'o', 'c', 'k', 's', 0, 0, 128, 0};
	uint8_t dataTag[11] = {7, 0, 4, 'D', 'a', 't', 'a', 0, 0, 64, 0};
	bool foundIDs = false, foundData = false;
	for (vector<uint8_t>::const_iterator it = filebuf.begin(); it != filebuf.end(); it++)
	{
		if (*it != 7)
			continue;
		if (!foundIDs && it + 13 + 32768 <= filebuf.end() && equal(it, it + 13, idsTag))
		{
			copy(it + 13, it + 13 + 32768, blockIDs);
			it += 13 + 32768 - 1;  // one less because of the loop we're in
			foundIDs = true;
		}
		else if (!foundData && it + 11 + 16384 <= filebuf.end() && equal(it, it + 11, dataTag))
		{
			copy(it + 11, it + 11 + 16384, blockData);
			it += 11 + 16384 - 1;  // one less because of the loop we're in
			foundData = true;
		}
		if (foundIDs && foundData)
			return true;
	}
	return false;
}



ChunkCacheStats& ChunkCacheStats::operator+=(const ChunkCacheStats& ccs)
{
	hits += ccs.hits;
	misses += ccs.misses;
	read += ccs.read;
	skipped += ccs.skipped;
	missing += ccs.missing;
	reqmissing += ccs.reqmissing;
	corrupt += ccs.corrupt;
	return *this;
}




ChunkData* ChunkCache::getData(const PosChunkIdx& ci)
{
	int e = getEntryNum(ci);
	int state = chunktable.getDiskState(ci);

	if (state == ChunkSet::CHUNK_UNKNOWN)
		stats.misses++;
	else
		stats.hits++;

	// if we've already tried and failed to read the chunk, don't try again
	if (state == ChunkSet::CHUNK_CORRUPTED || state == ChunkSet::CHUNK_MISSING)
		return &blankdata;

	// if the chunk is in the cache, return it
	if (state == ChunkSet::CHUNK_CACHED)
	{
		if (entries[e].ci != ci)
		{
			cerr << "grievous chunk cache failure!" << endl;
			cerr << "[" << ci.x << "," << ci.z << "]   [" << entries[e].ci.x << "," << entries[e].ci.z << "]" << endl;
			exit(-1);
		}
		return &entries[e].data;
	}

	// if this is a full render and the chunk is not required, we already know it doesn't exist
	bool req = chunktable.isRequired(ci);
	if (fullrender && !req)
	{
		stats.skipped++;
		chunktable.setDiskState(ci, ChunkSet::CHUNK_MISSING);
		return &blankdata;
	}

	// okay, we actually have to read the chunk from disk
	if (regionformat)
		readFromRegionCache(ci);
	else
		readChunkFile(ci);

	// check whether the read succeeded; return the data if so
	state = chunktable.getDiskState(ci);
	if (state == ChunkSet::CHUNK_CORRUPTED)
	{
		stats.corrupt++;
		return &blankdata;
	}
	if (state == ChunkSet::CHUNK_MISSING)
	{
		if (req)
			stats.reqmissing++;
		else
			stats.missing++;
		return &blankdata;
	}
	if (state != ChunkSet::CHUNK_CACHED || entries[e].ci != ci)
	{
		cerr << "grievous chunk cache failure!" << endl;
		cerr << "[" << ci.x << "," << ci.z << "]   [" << entries[e].ci.x << "," << entries[e].ci.z << "]" << endl;
		exit(-1);
	}
	stats.read++;
	return &entries[e].data;
}

void ChunkCache::readChunkFile(const PosChunkIdx& ci)
{
	// read the gzip file from disk, if it's there
	string filename = inputpath + "/" + ci.toChunkIdx().toFilePath();
	int result = readGzFile(filename, readbuf);
	if (result == -1)
	{
		chunktable.setDiskState(ci, ChunkSet::CHUNK_MISSING);
		return;
	}
	if (result == -2)
	{
		chunktable.setDiskState(ci, ChunkSet::CHUNK_CORRUPTED);
		return;
	}

	// gzip read was successful; extract the data we need from the chunk
	//  and put it in the cache
	parseReadBuf(ci);
}

void ChunkCache::readFromRegionCache(const PosChunkIdx& ci)
{
	// try to decompress the chunk data
	int result = regioncache.getDecompressedChunk(ci, readbuf);
	if (result == -1)
	{
		chunktable.setDiskState(ci, ChunkSet::CHUNK_MISSING);
		return;
	}
	if (result == -2)
	{
		chunktable.setDiskState(ci, ChunkSet::CHUNK_CORRUPTED);
		return;
	}
	
	// decompression was successful; extract the data we need from the chunk
	//  and put it in the cache
	parseReadBuf(ci);
}

void ChunkCache::parseReadBuf(const PosChunkIdx& ci)
{
	// evict current tenant of chunk's cache slot
	int e = getEntryNum(ci);
	if (entries[e].ci.valid())
		chunktable.setDiskState(entries[e].ci, ChunkSet::CHUNK_UNKNOWN);
	entries[e].ci = ChunkIdx(-1,-1);
	// ...and put this chunk's data into the slot, assuming the data can actually be parsed
	if (entries[e].data.loadFromFile(readbuf))
	{
		entries[e].ci = ci;
		chunktable.setDiskState(ci, ChunkSet::CHUNK_CACHED);
	}
	else
		chunktable.setDiskState(ci, ChunkSet::CHUNK_CORRUPTED);
}
