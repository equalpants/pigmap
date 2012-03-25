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

#include <stdio.h>
#include <iostream>
#include <stdlib.h>

#include "region.h"
#include "utils.h"

using namespace std;



struct fcloser
{
	FILE *f;
	fcloser(FILE *ff) : f(ff) {}
	~fcloser() {fclose(f);}
};

FILE* openRegionFile(const RegionIdx& ri, const string& inputpath, bool& anvil)
{
	string filename = inputpath + "/region/" + ri.toAnvilFileName();
	FILE *anvilfile = fopen(filename.c_str(), "rb");
	if (anvilfile != NULL)
	{
		anvil = true;
		return anvilfile;
	}
	filename = inputpath + "/region/" + ri.toOldFileName();
	anvil = false;
	return fopen(filename.c_str(), "rb");
}

int RegionFileReader::loadFromFile(const RegionIdx& ri, const string& inputpath)
{
	// open file
	FILE *f = openRegionFile(ri, inputpath, anvil);
	if (f == NULL)
		return -1;
	fcloser fc(f);

	// get file length
	fseek(f, 0, SEEK_END);
	size_t length = (size_t)ftell(f);
	fseek(f, 0, SEEK_SET);
	if (length < 4096)
		return -2;

	// read the header
	size_t count = fread(&(offsets[0]), 4096, 1, f);
	if (count < 1)
		return -2;

	// read the rest of the file
	chunkdata.resize(length - 4096);
	if (length > 4096)
	{
		count = fread(&(chunkdata[0]), length - 4096, 1, f);
		if (count < 1)
			return -2;
	}
	return 0;
}

int RegionFileReader::loadHeaderOnly(const RegionIdx& ri, const string& inputpath)
{
	// open file
	FILE *f = openRegionFile(ri, inputpath, anvil);
	if (f == NULL)
		return -1;
	fcloser fc(f);

	// read the header
	size_t count = fread(&(offsets[0]), 4096, 1, f);
	if (count < 1)
		return -2;

	return 0;
}

int RegionFileReader::decompressChunk(const ChunkOffset& co, vector<uint8_t>& buf)
{
	// see if chunk is present
	if (!containsChunk(co))
		return -1;

	// attempt to decompress chunk data into buffer
	int idx = getIdx(co);
	uint32_t sector = getSectorOffset(idx);
	if ((sector - 1) * 4096 >= chunkdata.size())
		return -2;
	uint8_t *chunkstart = &(chunkdata[(sector - 1) * 4096]);
	uint32_t datasize = fromBigEndian(*((uint32_t*)chunkstart));
	bool okay = readGzOrZlib(chunkstart + 5, datasize - 1, buf);
	if (!okay)
		return -2;
	return 0;
}

int RegionFileReader::getContainedChunks(const RegionIdx& ri, const string84& inputpath, vector<ChunkIdx>& chunks)
{
	chunks.clear();
	int result = loadHeaderOnly(ri, inputpath.s);
	if (0 != result)
		return result;
	for (RegionChunkIterator it(ri); !it.end; it.advance())
		if (containsChunk(it.current))
			chunks.push_back(it.current);
	return 0;
}





RegionChunkIterator::RegionChunkIterator(const RegionIdx& ri)
	: end(false), current(ri.baseChunk()), basechunk(ri.baseChunk())
{
}

void RegionChunkIterator::advance()
{
	current.x++;
	if (current.x >= basechunk.x + 32)
	{
		current.x = basechunk.x;
		current.z++;
	}
	if (current.z >= basechunk.z + 32)
		end = true;
}



RegionCacheStats& RegionCacheStats::operator+=(const RegionCacheStats& rcs)
{
	hits += rcs.hits;
	misses += rcs.misses;
	read += rcs.read;
	skipped += rcs.skipped;
	missing += rcs.missing;
	reqmissing += rcs.reqmissing;
	corrupt += rcs.corrupt;
	return *this;
}


int RegionCache::getDecompressedChunk(const PosChunkIdx& ci, vector<uint8_t>& buf, bool& anvil)
{
	PosRegionIdx ri = ci.toChunkIdx().getRegionIdx();
	int e = getEntryNum(ri);
	int state = regiontable.getDiskState(ri);
	
	if (state == RegionSet::REGION_UNKNOWN)
		stats.misses++;
	else
		stats.hits++;
	
	// if we already tried and failed to read this region, don't try again
	if (state == RegionSet::REGION_CORRUPTED || state == RegionSet::REGION_MISSING)
	{
		// actually, it shouldn't even be possible to get here, since the disk state
		//  flags for all chunks in the region should have been set the first time we failed
		cerr << "cache invariant failure!  tried to read already-failed region" << endl;
		return -1;
	}
	
	// if the region is in the cache, try to extract the chunk from it
	if (state == RegionSet::REGION_CACHED)
	{
		// try the "real" cache entry, then the extra readbuf
		if (entries[e].ri == ri)
		{
			anvil = entries[e].regionfile.anvil;
			return entries[e].regionfile.decompressChunk(ci.toChunkIdx(), buf);
		}
		else if (readbuf.ri == ri)
		{
			anvil = readbuf.regionfile.anvil;
			return readbuf.regionfile.decompressChunk(ci.toChunkIdx(), buf);
		}
		// if it wasn't in one of those two places, it shouldn't have been marked as cached
		cerr << "grievous region cache failure!" << endl;
		cerr << "[" << ri.x << "," << ri.z << "]   [" << entries[e].ri.x << "," << entries[e].ri.z << "]   [" << readbuf.ri.x << "," << readbuf.ri.z << "]" << endl;
		exit(-1);
	}

	// if this is a full render and the region is not required, we already know it doesn't exist
	bool req = regiontable.isRequired(ri);
	if (fullrender && !req)
	{
		stats.skipped++;
		regiontable.setDiskState(ri, RegionSet::REGION_MISSING);
		for (RegionChunkIterator it(ri.toRegionIdx()); !it.end; it.advance())
			chunktable.setDiskState(it.current, ChunkSet::CHUNK_MISSING);
		return -1;
	}
	
	// okay, we actually have to read the region from disk, if it's there
	readRegionFile(ri);
	
	// check whether the read succeeded; try to extract the chunk if so
	state = regiontable.getDiskState(ri);
	if (state == RegionSet::REGION_CORRUPTED)
	{
		stats.corrupt++;
		return -1;
	}
	if (state == RegionSet::REGION_MISSING)
	{
		if (req)
			stats.reqmissing++;
		else
			stats.missing++;
		return -1;
	}
	// since we've actually just done a read, the region should now be in a real cache entry, not the readbuf
	if (state != RegionSet::REGION_CACHED || entries[e].ri != ri)
	{
		cerr << "grievous region cache failure!" << endl;
		cerr << "[" << ri.x << "," << ri.z << "]   [" << entries[e].ri.x << "," << entries[e].ri.z << "]" << endl;
		exit(-1);
	}
	stats.read++;
	anvil = entries[e].regionfile.anvil;
	return entries[e].regionfile.decompressChunk(ci.toChunkIdx(), buf);
}

void RegionCache::readRegionFile(const PosRegionIdx& ri)
{
	// forget the data in the readbuf
	if (readbuf.ri.valid())
		regiontable.setDiskState(readbuf.ri, RegionSet::REGION_UNKNOWN);
	readbuf.ri = PosRegionIdx(-1,-1);
	
	// read the region file from disk, if it's there
	int result = readbuf.regionfile.loadFromFile(ri.toRegionIdx(), inputpath);
	if (result == -1)
	{
		regiontable.setDiskState(ri, RegionSet::REGION_MISSING);
		for (RegionChunkIterator it(ri.toRegionIdx()); !it.end; it.advance())
			chunktable.setDiskState(it.current, ChunkSet::CHUNK_MISSING);
		return;
	}
	if (result == -2)
	{
		regiontable.setDiskState(ri, RegionSet::REGION_CORRUPTED);
		for (RegionChunkIterator it(ri.toRegionIdx()); !it.end; it.advance())
			chunktable.setDiskState(it.current, ChunkSet::CHUNK_MISSING);
		return;
	}
	
	// read was successful; evict current tenant of chunk's cache slot (swap it into the readbuf)
	int e = getEntryNum(ri);
	entries[e].regionfile.swap(readbuf.regionfile);
	swap(entries[e].ri, readbuf.ri);
	// mark the entry as vaild and the region as cached
	entries[e].ri = ri;
	regiontable.setDiskState(ri, RegionSet::REGION_CACHED);
}
