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

#include "region.h"
#include "utils.h"

using namespace std;



struct fcloser
{
	FILE *f;
	fcloser(FILE *ff) : f(ff) {}
	~fcloser() {fclose(f);}
};

int RegionFileReader::loadFromFile(const string& filename)
{
	// open file
	FILE *f = fopen(filename.c_str(), "rb");
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
	size_t count = fread(offsets, 4096, 1, f);
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

int RegionFileReader::loadHeaderOnly(const string& filename)
{
	// open file
	FILE *f = fopen(filename.c_str(), "rb");
	if (f == NULL)
		return -1;
	fcloser fc(f);

	// read the header
	size_t count = fread(offsets, 4096, 1, f);
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

bool RegionFileReader::getContainedChunks(const RegionIdx& ri, const string& filename, vector<ChunkIdx>& chunks)
{
	chunks.clear();
	if (0 != loadHeaderOnly(filename))
		return false;
	for (RegionChunkIterator it(ri); !it.end; it.advance())
		if (containsChunk(it.current))
			chunks.push_back(it.current);
	return true;
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