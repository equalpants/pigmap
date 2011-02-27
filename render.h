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

#ifndef RENDER_H
#define RENDER_H

#include <string>
#include <stdint.h>

#include "map.h"
#include "tables.h"
#include "chunk.h"
#include "blockimages.h"
#include "rgba.h"



struct RenderStats
{
	int64_t reqchunkcount, reqregioncount, reqtilecount;  // number of required chunks/regions and base tiles
	ChunkCacheStats chunkcache;
	RegionStats region;

	RenderStats() : reqchunkcount(0), reqregioncount(0), reqtilecount(0) {}
};


struct SceneGraph;
struct TileCache;
struct ThreadOutputCache;

struct RenderJob : private nocopy
{
	bool fullrender;  // whether we're doing the entire world, as opposed to an incremental update
	bool regionformat;  // whether the world is in region format (chunk format assumed if not)
	MapParams mp;
	std::string inputpath, outputpath;
	BlockImages blockimages;
	std::auto_ptr<ChunkTable> chunktable;
	std::auto_ptr<ChunkCache> chunkcache;
	std::auto_ptr<RegionTable> regiontable;
	std::auto_ptr<TileTable> tiletable;
	std::auto_ptr<TileCache> tilecache;
	std::auto_ptr<SceneGraph> scenegraph;  // reuse this for each tile to avoid reallocation
	RenderStats stats;

	// don't actually draw anything or read chunks; just iterate through the data structures
	// ...scenegraph and chunkcache are not required if in test mode
	bool testmode;
};

// render a base tile into an RGBAImage, and also write it to disk
// ...do nothing and return false if the tile is not required or is out of range
bool renderTile(const TileIdx& ti, RenderJob& rj, RGBAImage& tile);

// recursively render all the required tiles that a zoom tile depends on, and then the tile itself;
//  stores the result into the supplied RGBAImage, and also writes it to disk
// do nothing and return false if the tile is not required
bool renderZoomTile(const ZoomTileIdx& zti, RenderJob& rj, RGBAImage& tile);

// for second phase of multithreaded operation: recursively render all the required tiles that a zoom tile
//  depends on, but stop recursing at the ThreadOutputCache level rather than the base tile level
bool renderZoomTile(const ZoomTileIdx& zti, RenderJob& rj, RGBAImage& tile, const ThreadOutputCache& tocache);



// as we render tiles recursively, we need to be able to hold 4 intermediate results at each zoom level;
//  this holds the space for those images, so we don't reallocate all the time
struct TileCache
{
	struct ZoomLevel
	{
		bool used[4];  // which of the images actually have data
		RGBAImage tiles[4];  // actual image data for the four tiles
	};

	std::vector<ZoomLevel> levels;  // indexed by baseZoom - zoom

	TileCache(const MapParams& mp) : levels(mp.baseZoom)
	{
		// reserve memory
		for (int i = 0; i < mp.baseZoom; i++)
			for (int j = 0; j < 4; j++)
				levels[i].tiles[j].create(mp.tileSize(), mp.tileSize());
	}
};


// when rendering with multiple threads, the individual threads only go up to a certain zoom level, then
//  the main thread does the last few levels on its own; the worker threads store their results in this
struct ThreadOutputCache
{
    int zoom;  // which zoom level the threads are working at

	std::vector<RGBAImage> images;  // use getIndex() to get index into this from zoom tile
	std::vector<bool> used;  // which images actually have data

	int getIndex(const ZoomTileIdx& zti) const;  // get index into images, or -1 if zoom is wrong

	ThreadOutputCache(int z) : zoom(z), images((1 << zoom) * (1 << zoom)), used((1 << zoom) * (1 << zoom), false) {}
};




// the blocks in a tile can be partitioned by their center pixels into pseudocolumns--sets of blocks that cover
//  exactly the same pixels (each block covers the block immediately SED of it, and so on)
// also, each block can partially occlude blocks in 6 neighboring pseudocolumns: E, SE, S, W, NW, N (that is,
//  the pseudocolumns that contain the block's immediate neighbors to the E, SE, etc.)
// ...so we can build a DAG representing the blocks in the tile: each block has up to 7 pointers, each one going to
//  the topmost occluded block in a pseudocolumn
// a block can be drawn when all its descendents have been drawn

struct SceneGraphNode
{
	int32_t xstart, ystart;  // top-left corner of block bounding box in tile image coords
	int bimgoffset;  // offset into blockimages
	// whether to darken various edges to indicate drop-off
	bool darkenEU, darkenSU, darkenND, darkenWD;
	bool drawn;
	BlockIdx bi;
	// first child is same pseudocolumn, then N, E, SE, S, W, NW; values are indices into
	//  the SceneGraph's nodes vector, or -1 for "null"
	int children[7];

	SceneGraphNode(int32_t x, int32_t y, const BlockIdx& bidx, int offset)
		: xstart(x), ystart(y), bimgoffset(offset), darkenEU(false), darkenSU(false), darkenND(false), darkenWD(false),
		drawn(false), bi(bidx) {std::fill(children, children + 7, -1);}
};

struct SceneGraph
{
	// all nodes from all pseudocolumns go in here, in sequence (ordered by pseudocolumn, and within
	//  pseudocolumns by height)
	std::vector<SceneGraphNode> nodes;
	// offset into nodes vector of each pseudocolumn (-1 for pseudocolumns with no nodes)
	std::vector<int> pcols;

	void clear() {nodes.clear(); pcols.clear();}

	int getTopNode(int pcol) {return pcols[pcol];}

	// scratch space for use while traversing the DAG
	std::vector<int> nodestack;

	SceneGraph() {nodes.reserve(2048);}
};




// iterate over the hexagonal block-center grid pixels whose blocks touch a tile
struct TileBlockIterator
{
	bool end;  // true when there are no more points

	// these guys are valid when end == false:
	Pixel current;  // the current grid point
	int pos;  // the position of this point within this tile's sequence of points
	int nextN, nextE, nextSE;  // the sequence positions of the neighboring points; -1 if the neighbor isn't in the tile

	const MapParams& mparams;
	TileIdx tile;
	// the tile's bounding box, expanded by half a block's bounding box, so that any block centered on a point
	//  within this box will hit the tile
	BBox expandedBBox;
	int lastTop, lastBottom;  // positions of the most recent column top, bottom we've encountered (or -1)

	// constructor initializes to the upper-left grid point
	TileBlockIterator(const TileIdx& ti, const MapParams& mp);

	// movement goes down the columns, then rightward to the next column
	void advance();
};

// iterate through the blocks that project to the same place, from top to bottom
struct PseudocolumnIterator
{
	bool end;  // true when we've run out of blocks
	BlockIdx current;  // when end == false, holds current block

	// constructor initializes to topmost block
	PseudocolumnIterator(const Pixel& center, const MapParams& mp);

	// move to the next block (which is one step SED), or the end
	void advance();
};




void testTileIterator();
void testPColIterator();


#endif // RENDER_H