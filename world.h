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

#ifndef WORLD_H
#define WORLD_H

#include <stdint.h>

#include "map.h"
#include "tables.h"


// see whether the input world is in region format
bool detectRegionFormat(const std::string& inputdir);


// find all regions on disk; set them to required in the RegionTable; set all chunks they contain to
//  required in the ChunkTable; set all tiles touched by those chunks to required in the TileTable
// returns false if the world is too big to fit in one of the tables
// if mp.baseZoom is set to -1 coming in, then this function will set it to the smallest zoom
//  that can fit everything
bool makeAllRegionsRequired(const std::string& inputdir, ChunkTable& chunktable, TileTable& tiletable, RegionTable& regiontable, MapParams& mp, int64_t& reqchunkcount, int64_t& reqtilecount, int64_t& reqregioncount);

// read a list of region filenames from a file; set the regions to required in the RegionTable; set the chunks they
//  contain to required in the ChunkTable; set all tiles touched by those chunks to required in the TileTable
// returns 0 on success, -1 if baseZoom is too small, -2 for other errors (can't read regionlist, world too big
//  for our internal data structures, etc.)
int readRegionlist(const std::string& regionlist, const std::string& inputdir, ChunkTable& chunktable, TileTable& tiletable, RegionTable& regiontable, const MapParams& mp, int64_t& reqrchunkcount, int64_t& reqtilecount, int64_t& reqregioncount);


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

// get the filepaths of all chunks on disk (used only for testing)
void findAllChunks(const std::string& inputdir, std::vector<std::string>& chunkpaths);


#endif // WORLD_H