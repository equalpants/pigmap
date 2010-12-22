// Copyright 2010 Michael J. Nelson
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
#include <fstream>
#include <math.h>

#include "chunk.h"
#include "utils.h"
#include "render.h"

using namespace std;



const char *chunkdirs[64] = {"/0", "/1", "/2", "/3", "/4", "/5", "/6", "/7", "/8", "/9", "/a", "/b", "/c", "/d", "/e", "/f",
                             "/g", "/h", "/i", "/j", "/k", "/l", "/m", "/n", "/o", "/p", "/q", "/r", "/s", "/t", "/u", "/v",
                             "/w", "/x", "/y", "/z", "/10", "/11", "/12", "/13", "/14", "/15", "/16", "/17", "/18", "/19", "/1a", "/1b",
                             "/1c", "/1d", "/1e", "/1f", "/1g", "/1h", "/1i", "/1j", "/1k", "/1l", "/1m", "/1n", "/1o", "/1p", "/1q", "/1r",};

bool makeAllChunksRequired(const string& topdir, ChunkTable& chunktable, TileTable& tiletable, MapParams& mp, int64_t& reqchunkcount, int64_t& reqtilecount)
{
	bool findBaseZoom = mp.baseZoom == -1;
	// if finding the baseZoom, we'll just start from 0 and increase it whenever we hit a tile that's out of bounds
	if (findBaseZoom)
		mp.baseZoom = 0;
	reqchunkcount = 0;
	// go through each world subdirectory
	for (int x = 0; x < 64; x++)
		for (int z = 0; z < 64; z++)
		{
			// get all files in the subdirectory
			vector<string> chunkpaths;
			string path = topdir + chunkdirs[x] + chunkdirs[z];
			listEntries(path, chunkpaths);
			for (vector<string>::const_iterator it = chunkpaths.begin(); it != chunkpaths.end(); it++)
			{
				ChunkIdx ci(0,0);
				// if this is a proper chunk filename, use it
				if (ChunkIdx::fromFilePath(*it, ci))
				{
					// mark the chunk required
					PosChunkIdx pci(ci);
					if (pci.valid())
					{
						chunktable.setRequired(pci);
						reqchunkcount++;
					}
					else
					{
						cerr << "chunk table too small!  can't fit chunk [" << ci.x << "," << ci.z << "]" << endl;
						return false;
					}
					// get the tiles it touches and mark them required
					vector<TileIdx> tiles = ci.getTiles(mp);
					for (vector<TileIdx>::const_iterator tile = tiles.begin(); tile != tiles.end(); tile++)
					{
						// first check if this tile fits in the TileTable, whose size is fixed
						PosTileIdx pti(*tile);
						if (pti.valid())
							tiletable.setRequired(pti);
						else
						{
							cerr << "tile table too small!  can't fit tile [" << tile->x << "," << tile->y << "]" << endl;
							return false;
						}
						// now see if the tile fits on the Google map
						if (!tile->valid(mp))
						{
							// if we're supposed to be finding baseZoom, then bump it up until this tile fits
							if (findBaseZoom)
							{
								while (!tile->valid(mp))
									mp.baseZoom++;
							}
							// otherwise, abort
							else
							{
								cerr << "baseZoom too small!  can't fit tile [" << tile->x << "," << tile->y << "]" << endl;
								return false;
							}
						}
					}
				}
			}
		}
	reqtilecount = tiletable.reqcount;
	if (findBaseZoom)
		cout << "baseZoom set to " << mp.baseZoom << endl;
	return true;
}

int readChunklist(const string& chunklist, ChunkTable& chunktable, TileTable& tiletable, const MapParams& mp, int64_t& reqchunkcount, int64_t& reqtilecount)
{
	ifstream infile(chunklist.c_str());
	if (infile.fail())
	{
		cerr << "couldn't open chunklist " << chunklist << endl;
		return -2;
	}
	reqchunkcount = 0;
	while (!infile.eof() && !infile.fail())
	{
		string chunkfile;
		getline(infile, chunkfile);
		if (chunkfile.empty())
			continue;
		ChunkIdx ci(0,0);
		if (ChunkIdx::fromFilePath(chunkfile, ci))
		{
			PosChunkIdx pci(ci);
			if (pci.valid())
			{
				chunktable.setRequired(pci);
				reqchunkcount++;
			}
			else
			{
				cerr << "chunk table too small!  can't fit chunk [" << ci.x << "," << ci.z << "]" << endl;
				return -2;
			}
			vector<TileIdx> tiles = ci.getTiles(mp);
			for (vector<TileIdx>::const_iterator tile = tiles.begin(); tile != tiles.end(); tile++)
			{
				PosTileIdx pti(*tile);
				if (pti.valid())
					tiletable.setRequired(pti);
				else
				{
					cerr << "tile table too small!  can't fit tile [" << tile->x << "," << tile->y << "]" << endl;
					return -2;
				}
				if (!tile->valid(mp))
				{
					cerr << "baseZoom too small!  can't fit tile [" << tile->x << "," << tile->y << "]" << endl;
					return -1;
				}
			}
		}
	}
	reqtilecount = tiletable.reqcount;
	return 0;
}

void makeTestWorld(int size, ChunkTable& chunktable, TileTable& tiletable, MapParams& mp, int64_t& reqchunkcount, int64_t& reqtilecount)
{
	bool findBaseZoom = mp.baseZoom == -1;
	// if finding the baseZoom, we'll just start from 0 and increase it whenever we hit a tile that's out of bounds
	if (findBaseZoom)
		mp.baseZoom = 0;
	reqchunkcount = 0;
	// we'll start by putting 95% of the chunks in a solid block at the center
	int size2 = (int)(sqrt((double)size * 0.95) / 2.0);
	ChunkIdx ci(0,0);
	for (ci.x = -size2; ci.x < size2; ci.x++)
		for (ci.z = -size2; ci.z < size2; ci.z++)
		{
			chunktable.setRequired(ci);
			reqchunkcount++;
			vector<TileIdx> tiles = ci.getTiles(mp);
			for (vector<TileIdx>::const_iterator tile = tiles.begin(); tile != tiles.end(); tile++)
			{
				tiletable.setRequired(*tile);
				while (findBaseZoom && !tile->valid(mp))
					mp.baseZoom++;
			}
		}
	// now add some circles of required chunks with radii up to four times the (minimum) radius of the
	//  center block
	for (int m = 2; m <= 4; m++)
	{
		double rad = (double)size2 * (double)m;
		for (double t = -3.14159; t < 3.14159; t += 0.002)
		{
			ChunkIdx ci((int)(cos(t) * rad), (int)(sin(t) * rad));
			chunktable.setRequired(ci);
			reqchunkcount++;
			vector<TileIdx> tiles = ci.getTiles(mp);
			for (vector<TileIdx>::const_iterator tile = tiles.begin(); tile != tiles.end(); tile++)
			{
				tiletable.setRequired(*tile);
				while (findBaseZoom && !tile->valid(mp))
					mp.baseZoom++;
			}
		}
	}
	// now add some spokes going from the center out to the circle
	int irad = size2 * 4;
	for (ci.x = 0, ci.z = -irad; ci.z < irad; ci.z++)
	{
		chunktable.setRequired(ci);
		reqchunkcount++;
		vector<TileIdx> tiles = ci.getTiles(mp);
		for (vector<TileIdx>::const_iterator tile = tiles.begin(); tile != tiles.end(); tile++)
		{
			tiletable.setRequired(*tile);
			while (findBaseZoom && !tile->valid(mp))
				mp.baseZoom++;
		}
	}
	for (ci.x = -irad, ci.z = 0; ci.x < irad; ci.x++)
	{
		chunktable.setRequired(ci);
		reqchunkcount++;
		vector<TileIdx> tiles = ci.getTiles(mp);
		for (vector<TileIdx>::const_iterator tile = tiles.begin(); tile != tiles.end(); tile++)
		{
			tiletable.setRequired(*tile);
			while (findBaseZoom && !tile->valid(mp))
				mp.baseZoom++;
		}
	}
	for (ci.x = -irad, ci.z = -irad; ci.z < irad; ci.x++, ci.z++)
	{
		chunktable.setRequired(ci);
		reqchunkcount++;
		vector<TileIdx> tiles = ci.getTiles(mp);
		for (vector<TileIdx>::const_iterator tile = tiles.begin(); tile != tiles.end(); tile++)
		{
			tiletable.setRequired(*tile);
			while (findBaseZoom && !tile->valid(mp))
				mp.baseZoom++;
		}
	}
	for (ci.x = irad, ci.z = -irad; ci.z < irad; ci.x--, ci.z++)
	{
		chunktable.setRequired(ci);
		reqchunkcount++;
		vector<TileIdx> tiles = ci.getTiles(mp);
		for (vector<TileIdx>::const_iterator tile = tiles.begin(); tile != tiles.end(); tile++)
		{
			tiletable.setRequired(*tile);
			while (findBaseZoom && !tile->valid(mp))
				mp.baseZoom++;
		}
	}
	reqtilecount = tiletable.reqcount;
	if (findBaseZoom)
		cout << "baseZoom set to " << mp.baseZoom << endl;
}





bool ChunkData::loadFromFile(const vector<uint8_t>& filebuf)
{
	// the hell with parsing this whole godforsaken NBT format; just look for the arrays we need
	uint8_t idsTag[13] = {7, 0, 6, 'B', 'l', 'o', 'c', 'k', 's', 0, 0, 128, 0};
	uint8_t dataTag[11] = {7, 0, 4, 'D', 'a', 't', 'a', 0, 0, 64, 0};
	bool foundIDs = false, foundData = false;
	for (vector<uint8_t>::const_iterator it = filebuf.begin(); it != filebuf.end(); it++)
	{
		if (*it != 7)
			it++;
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
			cerr << "grievous cache failure!" << endl;
			cerr << "[" << ci.x << "," << ci.z << "]   [" << entries[e].ci.x << "," << entries[e].ci.z << "]" << endl;
			exit(-1);
		}
		return &entries[e].data;
	}

	// we've never tried to read the chunk; we'll try to fetch it from the disk
	bool req = chunktable.isRequired(ci);
	// ...unless this is a full render and the chunk is not required, in which case we already
	//  know it doesn't exist
	if (fullrender && !req)
	{
		stats.skipped++;
		chunktable.setDiskState(ci, ChunkSet::CHUNK_MISSING);
		return &blankdata;
	}
	// okay, we actually have to attempt to read
	string filename = inputpath + "/" + ci.toChunkIdx().toFilePath();
	int result = readGzFile(filename, readbuf);
	if (result == -1)
	{
		if (req)
			stats.reqmissing++;
		else
			stats.missing++;
		chunktable.setDiskState(ci, ChunkSet::CHUNK_MISSING);
		return &blankdata;
	}
	if (result == -2)
	{
		stats.corrupt++;
		chunktable.setDiskState(ci, ChunkSet::CHUNK_CORRUPTED);
		return &blankdata;
	}

	// read was successful; evict current tenant of this chunk's slot, if there is one
	stats.read++;
	if (entries[e].ci.valid())
		chunktable.setDiskState(entries[e].ci, ChunkSet::CHUNK_UNKNOWN);
	// ...and put this chunk's data into the slot
	chunktable.setDiskState(ci, ChunkSet::CHUNK_CACHED);
	entries[e].ci = ci;
	entries[e].data.loadFromFile(readbuf);
	return &entries[e].data;
}







void ChunkCache::testLookup(const PosChunkIdx& ci)
{
	int e = getEntryNum(ci);
	int state = chunktable.getDiskState(ci);
	if (state == ChunkSet::CHUNK_CACHED && entries[e].ci != ci)
	{
		cerr << "grievous cache failure!" << endl;
		cerr << "[" << ci.x << "," << ci.z << "]   [" << entries[e].ci.x << "," << entries[e].ci.z << "]" << endl;
		exit(-1);
	}
	if (state != ChunkSet::CHUNK_UNKNOWN)
	{
		stats.hits++;
		return;
	}
	stats.misses++;
	if (!chunktable.isRequired(ci))
	{
		stats.skipped++;
		chunktable.setDiskState(ci, ChunkSet::CHUNK_MISSING);
		return;
	}
	if (entries[e].ci.valid())
		chunktable.setDiskState(entries[e].ci, ChunkSet::CHUNK_UNKNOWN);
	stats.read++;
	chunktable.setDiskState(ci, ChunkSet::CHUNK_CACHED);
	entries[e].ci = ci;
}

// this is obsolete; it tests the RequiredChunkIterator, which doesn't even move in Z-order
void testChunkCache()
{
	time_t tstart = time(NULL);
	MapParams mp(6,2,10);
	auto_ptr<ChunkTable> chunktable(new ChunkTable);
	auto_ptr<TileTable> tiletable(new TileTable);
	int64_t dummy, dummy2;
	makeTestWorld(10000, *chunktable, *tiletable, mp, dummy, dummy2);
	ChunkCacheStats ccstats;
	auto_ptr<ChunkCache> cache(new ChunkCache(*chunktable, "", true, ccstats));
	int64_t chunkcount = 0, tilecount = 0;
	for (RequiredChunkIterator it(*chunktable); !it.end; it.advance())
	{
		chunkcount++;
		ChunkIdx ci = it.current.toChunkIdx();
		cout << "chunk [" << ci.x << "," << ci.z << "]" << endl;
		vector<TileIdx> tiles = ci.getTiles(mp);
		for (vector<TileIdx>::const_iterator tile = tiles.begin(); tile != tiles.end(); tile++)
		{
			if (tiletable->isDrawn(*tile))
				continue;
			tilecount++;
			tiletable->setDrawn(*tile);
			for (TileBlockIterator tbit(*tile, mp); !tbit.end; tbit.advance())
			{
				for (PseudocolumnIterator pcit(tbit.current, mp); !pcit.end; pcit.advance())
				{
					ChunkIdx ci = pcit.current.getChunkIdx();
					cache->testLookup(ci);
				}
			}
		}
	}
	time_t tfinish = time(NULL);
	cout << "chunks: " << chunkcount << "   tiles: " << tilecount << "   cache size: " << sizeof(ChunkCache) << endl;
	cout << "total cache hits: " << ccstats.hits << "   misses: " << ccstats.misses << "   reads: " << ccstats.read << "   skipped: " << ccstats.skipped << endl;
	cout << "running time: " << (tfinish - tstart) << endl;
}

// used only for testing
void findAllChunks(const string& topdir, vector<string>& chunkpaths)
{
	for (int x = 0; x < 64; x++)
		for (int z = 0; z < 64; z++)
		{
			string path = topdir + chunkdirs[x] + chunkdirs[z];
			listEntries(path, chunkpaths);
		}
}

