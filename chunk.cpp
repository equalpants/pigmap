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

#include <stdint.h>
#include <iostream>
#include <stdlib.h>
#include <time.h>
#include <memory>

#include "chunk.h"
#include "utils.h"

using namespace std;



//---------------------------------------------------------------------------------------------------


bool ChunkData::loadFromOldFile(const vector<uint8_t>& filebuf)
{
	anvil = false;
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


//---------------------------------------------------------------------------------------------------


// quasi-NBT-parsing stuff for Anvil format: doesn't actually bother trying to read the whole thing,
//  just skips through the data looking for what we're interested in
#define TAG_END            0
#define TAG_BYTE           1
#define TAG_SHORT          2
#define TAG_INT            3
#define TAG_LONG           4
#define TAG_FLOAT          5
#define TAG_DOUBLE         6
#define TAG_BYTE_ARRAY     7
#define TAG_STRING         8
#define TAG_LIST           9
#define TAG_COMPOUND       10
#define TAG_INT_ARRAY      11

// although tag names are UTF8, we'll just pretend they're ASCII--we don't really care about how the
//  actual string data breaks down into characters, as long as we know where the end of the string is
void parseTypeAndName(const uint8_t*& ptr, uint8_t& type, string& name)
{
	type = *ptr;
	ptr++;
	if (type != TAG_END)
	{
		uint16_t len = fromBigEndian(*((uint16_t*)ptr));
		name.resize(len);
		copy(ptr + 2, ptr + 2 + len, name.begin());
		ptr += 2 + len;
	}
}

// structure for locating the block data for a 16x16x16 section--the compound tags on the "Sections" list will pass
//  this down to their immediate children, so they can fill in pointers to their payloads if appropriate
// ...after the whole structure is parsed, the block data will be copied into the ChunkData
// (note that we can't read the block data immediately upon finding it, because we have to know the Y value
//  for the section first, and the tags may appear in any order)
struct chunkSection
{
	int y;  // or -1 for "not found yet"
	const uint8_t *blockIDs;  // pointer into the file buffer, or NULL for "not found yet"
	const uint8_t *blockData;  // pointer into the file buffer, or NULL for "not found yet"
	const uint8_t *blockAdd;  // pointer into the file buffer, or NULL for "not found" (this one may not be present at all)

	chunkSection() : y(-1), blockIDs(NULL), blockData(NULL), blockAdd(NULL) {}
	bool complete() const {return y >= 0 && y < 16 && blockIDs != NULL && blockData != NULL;}
	
	void extract(ChunkData& chunkdata) const
	{
		copy(blockIDs, blockIDs + 4096, chunkdata.blockIDs + (y * 4096));
		copy(blockData, blockData + 2048, chunkdata.blockData + (y * 2048));
		if (blockAdd != NULL)
			copy(blockAdd, blockAdd + 2048, chunkdata.blockAdd + (y * 2048));
	}
};

bool isSection(const vector<string>& names)
{
	return names.size() == 4 &&
	       names[3] == "" &&
	       names[2] == "Sections" &&
	       names[1] == "Level" &&
	       names[0] == "";
}

// if section != NULL, then the immediate parent of this tag is one of the compound tags in the "Sections"
//  list, so the block data tags will fill in their locations
bool parsePayload(const uint8_t*& ptr, uint8_t type, vector<string>& names, chunkSection *section, vector<chunkSection>& completedSections)
{
	switch (type)
	{
		case TAG_END:
		{
			return true;
		}
		case TAG_BYTE:
		{
			if (section != NULL && names.back() == "Y")
				section->y = *ptr;
			ptr++;
			return true;
		}
		case TAG_SHORT:
		{
			ptr += 2;
			return true;
		}
		case TAG_INT:
		case TAG_FLOAT:
		{
			ptr += 4;
			return true;
		}
		case TAG_LONG:
		case TAG_DOUBLE:
		{
			ptr += 8;
			return true;
		}
		case TAG_BYTE_ARRAY:
		{
			uint32_t len = fromBigEndian(*((uint32_t*)ptr));
			ptr += 4;
			if (section != NULL)
			{
				if (names.back() == "Blocks" && len == 4096)
					section->blockIDs = ptr;
				else if (names.back() == "Data" && len == 2048)
					section->blockData = ptr;
				else if (names.back() == "Add" && len == 2048)
					section->blockAdd = ptr;
			}
			ptr += len;
			return true;
		}
		case TAG_INT_ARRAY:
		{
			uint32_t len = fromBigEndian(*((uint32_t*)ptr));
			ptr += 4 + len*4;
			return true;
		}
		case TAG_STRING:
		{
			uint16_t len = fromBigEndian(*((uint16_t*)ptr));
			ptr += 2 + len;
			return true;
		}
		case TAG_LIST:
		{
			uint8_t listtype = *ptr;
			ptr++;
			uint32_t len = fromBigEndian(*((uint32_t*)ptr));
			ptr += 4;
			stackPusher<string> sp(names, "");
			for (uint32_t i = 0; i < len; i++)
				if (!parsePayload(ptr, listtype, names, NULL, completedSections))
					return false;
			return true;
		}
		case TAG_COMPOUND:
		{
			chunkSection section;
			chunkSection *sectionPtr = isSection(names) ? &section : NULL;
			
			uint8_t nexttype;
			string nextname;
			parseTypeAndName(ptr, nexttype, nextname);
			while (nexttype != TAG_END)
			{
				stackPusher<string> sp(names, nextname);
				if (!parsePayload(ptr, nexttype, names, sectionPtr, completedSections))
					return false;
				parseTypeAndName(ptr, nexttype, nextname);
			}
			
			if (sectionPtr != NULL)
			{
				if (section.complete())
					completedSections.push_back(section);
				else
				{
					cerr << "incomplete chunk section!" << endl;
					return false;
				}
			}

			return true;
		}
		default:
		{
			// unknown tag--since we have no idea how large it is, we must abort
			cerr << "unknown NBT tag: type " << type << endl;
			return false;
		}
	}
	return false;  // shouldn't be able to reach here
}

bool ChunkData::loadFromAnvilFile(const vector<uint8_t>& filebuf)
{
	anvil = true;
	fill(blockIDs, blockIDs + 65536, 0);
	fill(blockAdd, blockAdd + 32768, 0);
	fill(blockData, blockData + 32768, 0);

	const uint8_t *ptr = &(filebuf[0]);
	uint8_t type;
	string name;
	parseTypeAndName(ptr, type, name);
	if (type != TAG_COMPOUND || !name.empty())
	{
		cerr << "unrecognized NBT chunk file: top tag has type " << (int)type << " and name " << name << endl;
		return false;
	}

	vector<string> names(1, name);
	vector<chunkSection> completedSections;
	if (!parsePayload(ptr, type, names, NULL, completedSections))
		return false;

	for (vector<chunkSection>::const_iterator it = completedSections.begin(); it != completedSections.end(); it++)
		it->extract(*this);

	return true;
}


//---------------------------------------------------------------------------------------------------


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
	parseReadBuf(ci, false);
}

void ChunkCache::readFromRegionCache(const PosChunkIdx& ci)
{
	// try to decompress the chunk data
	bool anvil;
	int result = regioncache.getDecompressedChunk(ci, readbuf, anvil);
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
	parseReadBuf(ci, anvil);
}

void ChunkCache::parseReadBuf(const PosChunkIdx& ci, bool anvil)
{
	// evict current tenant of chunk's cache slot
	int e = getEntryNum(ci);
	if (entries[e].ci.valid())
		chunktable.setDiskState(entries[e].ci, ChunkSet::CHUNK_UNKNOWN);
	entries[e].ci = PosChunkIdx(-1,-1);
	// ...and put this chunk's data into the slot, assuming the data can actually be parsed
	bool result = anvil ? entries[e].data.loadFromAnvilFile(readbuf) : entries[e].data.loadFromOldFile(readbuf);
	if (result)
	{
		entries[e].ci = ci;
		chunktable.setDiskState(ci, ChunkSet::CHUNK_CACHED);
	}
	else
		chunktable.setDiskState(ci, ChunkSet::CHUNK_CORRUPTED);
}
