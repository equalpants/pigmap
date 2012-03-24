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

#include <iostream>
#include <math.h>
#include <fstream>

#include "world.h"
#include "region.h"

using namespace std;




bool detectRegionFormat(const string& inputdir)
{
	return dirExists(inputdir + "/region");
}





bool makeAllRegionsRequired(const string& topdir, ChunkTable& chunktable, TileTable& tiletable, RegionTable& regiontable, MapParams& mp, int64_t& reqchunkcount, int64_t& reqtilecount, int64_t& reqregioncount)
{
	bool findBaseZoom = mp.baseZoom == -1;
	// if finding the baseZoom, we'll just start from 0 and increase it whenever we hit a tile that's out of bounds
	if (findBaseZoom)
		mp.baseZoom = 0;
	reqregioncount = 0;
	// get all files in the region directory
	RegionFileReader rfreader;
	vector<string> regionpaths;
	listEntries(topdir + "/region", regionpaths);
	for (vector<string>::const_iterator it = regionpaths.begin(); it != regionpaths.end(); it++)
	{
		RegionIdx ri(0,0);
		// if this is a proper region filename, use it
		if (RegionIdx::fromFilePath(*it, ri))
		{
			PosRegionIdx pri(ri);
			if (!pri.valid())
			{
				cerr << "ignoring extremely-distant region " << *it << " (world may be corrupt)" << endl;
				continue;
			}
			// we might have found this region already, if the world data contains both .mca and .mcr files
			if (regiontable.isRequired(pri))
				continue;
			// get the chunks that currently exist in this region; if there aren't any, ignore it
			vector<ChunkIdx> chunks;
			if (0 != rfreader.getContainedChunks(ri, string84(topdir), chunks))
			{
				cerr << "can't open region " << *it << " to list chunks" << endl;
				continue;
			}
			if (chunks.empty())
				continue;
			// mark the region required
			regiontable.setRequired(pri);
			reqregioncount++;
			// go through the contained chunks
			for (vector<ChunkIdx>::const_iterator chunk = chunks.begin(); chunk != chunks.end(); chunk++)
			{
				// mark the chunk required
				PosChunkIdx pci(*chunk);
				if (pci.valid())
				{
					chunktable.setRequired(pci);
					reqchunkcount++;
				}
				else
				{
					cerr << "ignoring extremely-distant chunk " << chunk->toFileName() << " (world may be corrupt)" << endl;
					continue;
				}
				// get the tiles it touches and mark them required
				vector<TileIdx> tiles = chunk->getTiles(mp);
				for (vector<TileIdx>::const_iterator tile = tiles.begin(); tile != tiles.end(); tile++)
				{
					// first check if this tile fits in the TileTable, whose size is fixed
					PosTileIdx pti(*tile);
					if (pti.valid())
						tiletable.setRequired(pti);
					else
					{
						cerr << "ignoring extremely-distant tile [" << tile->x << "," << tile->y << "]" << endl;
						cerr << "(world may be corrupt; is region " << *it << " supposed to exist?)" << endl;
						continue;
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

int readRegionlist(const string& regionlist, const string& inputdir, ChunkTable& chunktable, TileTable& tiletable, RegionTable& regiontable, const MapParams& mp, int64_t& reqchunkcount, int64_t& reqtilecount, int64_t& reqregioncount)
{
	ifstream infile(regionlist.c_str());
	if (infile.fail())
	{
		cerr << "couldn't open regionlist " << regionlist << endl;
		return -2;
	}
	reqregioncount = 0;
	RegionFileReader rfreader;
	while (!infile.eof() && !infile.fail())
	{
		string regionfile;
		getline(infile, regionfile);
		if (regionfile.empty())
			continue;
		RegionIdx ri(0,0);
		if (RegionIdx::fromFilePath(regionfile, ri))
		{
			PosRegionIdx pri(ri);
			if (!pri.valid())
			{
				cerr << "ignoring extremely-distant region " << regionfile << " (world may be corrupt)" << endl;
				continue;
			}
			if (regiontable.isRequired(pri))
				continue;
			vector<ChunkIdx> chunks;
			if (0 != rfreader.getContainedChunks(ri, string84(inputdir), chunks))
			{
				cerr << "can't open region " << regionfile << " to list chunks" << endl;
				continue;
			}
			if (chunks.empty())
				continue;
			regiontable.setRequired(pri);
			reqregioncount++;
			for (vector<ChunkIdx>::const_iterator chunk = chunks.begin(); chunk != chunks.end(); chunk++)
			{
				PosChunkIdx pci(*chunk);
				if (pci.valid())
				{
					chunktable.setRequired(pci);
					reqchunkcount++;
				}
				else
				{
					cerr << "ignoring extremely-distant chunk " << chunk->toFileName() << " (world may be corrupt)" << endl;
					continue;
				}
				vector<TileIdx> tiles = chunk->getTiles(mp);
				for (vector<TileIdx>::const_iterator tile = tiles.begin(); tile != tiles.end(); tile++)
				{
					PosTileIdx pti(*tile);
					if (pti.valid())
						tiletable.setRequired(pti);
					else
					{
						cerr << "ignoring extremely-distant tile [" << tile->x << "," << tile->y << "]" << endl;
						cerr << "(world may be corrupt; is region " << regionfile << " supposed to exist?)" << endl;
						continue;
					}
					if (!tile->valid(mp))
					{
						cerr << "baseZoom too small!  can't fit tile [" << tile->x << "," << tile->y << "]" << endl;
						return -1;
					}
				}
			}
		}
	}
	reqtilecount = tiletable.reqcount;
	return 0;
}





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
						cerr << "ignoring extremely-distant chunk " << ci.toFileName() << " (world may be corrupt)" << endl;
						continue;
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
							cerr << "ignoring extremely-distant tile [" << tile->x << "," << tile->y << "]" << endl;
							cerr << "(world may be corrupt; is chunk " << ci.toFileName() << " supposed to exist?)" << endl;
							continue;
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
				cerr << "ignoring extremely-distant chunk " << ci.toFileName() << " (world may be corrupt)" << endl;
				continue;
			}
			vector<TileIdx> tiles = ci.getTiles(mp);
			for (vector<TileIdx>::const_iterator tile = tiles.begin(); tile != tiles.end(); tile++)
			{
				PosTileIdx pti(*tile);
				if (pti.valid())
					tiletable.setRequired(pti);
				else
				{
					cerr << "ignoring extremely-distant tile [" << tile->x << "," << tile->y << "]" << endl;
					cerr << "(world may be corrupt; is chunk " << ci.toFileName() << " supposed to exist?)" << endl;
					continue;
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

