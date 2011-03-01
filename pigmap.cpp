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

//
//
// TODO:
// -cosmetic things:
//   -have signs actually face correct directions
//   -edge shadows on vertical edges, too?
//   -proper redstone wire directions
// -premultiply block image alphas?
// -dump list of corrupted chunks at end, so they can be retried later
// -keep some space around for PNG row pointers instead of allocating every time
// -see if it's better to have threads work together and share a cache?
// -for the love of god, clean up blockimages.cpp!
//

#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <set>
#include <memory>
#include <limits>
#include <fstream>
#include <sstream>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

#include "blockimages.h"
#include "rgba.h"
#include "map.h"
#include "utils.h"
#include "tables.h"
#include "chunk.h"
#include "render.h"
#include "world.h"

using namespace std;



//-------------------------------------------------------------------------------------------------------------------

void printStats(int seconds, const RenderStats& stats)
{
	cout << stats.reqchunkcount << " chunks    " << stats.reqregioncount << " regions   "
	     << stats.reqtilecount << " base tiles    " << seconds << " seconds" << endl;
	cout << "chunk cache: " << stats.chunkcache.hits << " hits   " << stats.chunkcache.misses << " misses" << endl;
	cout << "             " << stats.chunkcache.read << " read   " << stats.chunkcache.skipped << " skipped   " << stats.chunkcache.missing << " missing   "
	     << stats.chunkcache.reqmissing << " reqmissing   " << stats.chunkcache.corrupt << " corrupt" << endl;
	cout << "region requests: " << stats.region.read << " read (containing " << stats.region.chunksread << " chunks)   " << stats.region.skipped << " skipped" << endl;
	cout << "                 " << stats.region.missing << " missing   " << stats.region.reqmissing << " reqmissing   " << stats.region.corrupt << " corrupt" << endl;
}

void runSingleThread(RenderJob& rj)
{
	cout << "single thread will render " << rj.stats.reqtilecount << " base tiles" << endl;
	// allocate storage/caches
	rj.chunkcache.reset(new ChunkCache(*rj.chunktable, *rj.regiontable, rj.inputpath, rj.fullrender, rj.regionformat, rj.stats.chunkcache, rj.stats.region));
	rj.tilecache.reset(new TileCache(rj.mp));
	rj.scenegraph.reset(new SceneGraph);
	RGBAImage topimg;
	// render the tiles recursively (starting at the very top)
	renderZoomTile(ZoomTileIdx(0,0,0), rj, topimg);
}

struct WorkerThreadParams
{
	RenderJob *rj;
	ThreadOutputCache *tocache;
	vector<ZoomTileIdx> zoomtiles;  // tiles that this thread is responsible for
};

void *runWorkerThread(void *arg)
{
	WorkerThreadParams *wtp = (WorkerThreadParams*)arg;
	for (vector<ZoomTileIdx>::const_iterator it = wtp->zoomtiles.begin(); it != wtp->zoomtiles.end(); it++)
	{
		int idx = wtp->tocache->getIndex(*it);
		wtp->tocache->used[idx] = renderZoomTile(*it, *wtp->rj, wtp->tocache->images[idx]);
	}
	return 0;
}

// see if there's enough available memory for some number of tile images
// (...by just attempting to allocate it!)
//!!!!!!! better way to do this?   maybe allow user to specify max memory for
//         ThreadOutputCache instead?
bool memoryAvailable(int tiles, const MapParams& mp)
{
	int64_t imgsize = mp.tileSize() * mp.tileSize();  // in pixels
	try
	{
		RGBAPixel *tempbuf = new RGBAPixel[imgsize * tiles];
		delete[] tempbuf;
	}
	catch (bad_alloc& ba)
	{
		return false;
	}
	return true;
}

// returns zoom level chosen for partitioning
int assignThreadTasks(vector<WorkerThreadParams>& wtps, const TileTable& ttable, const MapParams& mp, int threads)
{
	vector<ZoomTileIdx> best_reqzoomtiles;
	vector<int64_t> best_costs;
	vector<int> best_assignments;
	double best_error = 1.1;
	// start with zoom level 1 and go up from there
	for (int zoom = 1; zoom <= mp.baseZoom; zoom++)
	{
		// find all zoom tiles at this level that need to be drawn (i.e. contain > 0 required base tiles),
		//  and their costs (number of required base tiles)
		vector<ZoomTileIdx> reqzoomtiles;
		vector<int64_t> costs;
		vector<int> assignments;
		int64_t size = (1 << zoom);
		ZoomTileIdx zti(-1, -1, zoom);
		for (zti.x = 0; zti.x < size; zti.x++)
			for (zti.y = 0; zti.y < size; zti.y++)
			{
				int numreq = ttable.getNumRequired(zti, mp);
				if (numreq > 0)
				{
					reqzoomtiles.push_back(zti);
					costs.push_back(numreq);
				}
			}
		// if there are too many tiles at this zoom level (that is, if the ThreadOutputCache wouldn't
		//  fit in memory), then forget it (and those above it, too)
		if (!memoryAvailable(reqzoomtiles.size(), mp))
			break;
		// compute a good schedule for this level and get its "error" (difference between max thread
		//  cost and min thread cost, as a fraction of max thread cost)
		pair<int64_t, double> error = schedule(costs, assignments, threads);
		// if the error is less than 5%, or under 50 tiles (for small worlds), that's good enough
		bool stop = error.second < 0.05 || error.first < 50;
		// if this error is the best so far, remember these tiles/assignments
		if (error.second < best_error || stop)
		{
			best_reqzoomtiles = reqzoomtiles;
			best_costs = costs;
			best_assignments = assignments;
			best_error = error.second;
		}
		if (stop)
			break;
	}

	// perform actual assignments
	for (int i = 0; i < best_assignments.size(); i++)
	{
		wtps[best_assignments[i]].zoomtiles.push_back(best_reqzoomtiles[i]);
		wtps[best_assignments[i]].rj->stats.reqtilecount += best_costs[i];
	}

	return best_reqzoomtiles.front().zoom;
}

void runMultithreaded(RenderJob& rj, int threads)
{
	// create a separate RenderJob for each thread; each one gets its own copy of the parameters,
	//  plus its own storage (caches, scenegraph, etc.)
	RenderJob *rjs = new RenderJob[threads];
	arrayDeleter<RenderJob> adrj(rjs);
	for (int i = 0; i < threads; i++)
	{
		rjs[i].testmode = rj.testmode;
		rjs[i].fullrender = rj.fullrender;
		rjs[i].regionformat = rj.regionformat;
		rjs[i].mp = rj.mp;
		rjs[i].inputpath = rj.inputpath;
		rjs[i].outputpath = rj.outputpath;
		rjs[i].blockimages = rj.blockimages;
		rjs[i].chunktable.reset(new ChunkTable);
		rjs[i].chunktable->copyFrom(*rj.chunktable);
		rjs[i].tiletable.reset(new TileTable);
		rjs[i].tiletable->copyFrom(*rj.tiletable);
		rjs[i].regiontable.reset(new RegionTable);
		rjs[i].regiontable->copyFrom(*rj.regiontable);
		if (!rjs[i].testmode)
			rjs[i].chunkcache.reset(new ChunkCache(*rjs[i].chunktable, *rjs[i].regiontable, rjs[i].inputpath, rjs[i].fullrender, rjs[i].regionformat, rjs[i].stats.chunkcache, rjs[i].stats.region));
		rjs[i].tilecache.reset(new TileCache(rjs[i].mp));
		if (!rjs[i].testmode)
			rjs[i].scenegraph.reset(new SceneGraph);
	}

	// divide the required tiles evenly among the threads: find a zoom level that has enough tiles for us
	//  to make a balanced assignment, then give each thread some tiles from that level
	vector<WorkerThreadParams> wtps(threads);
	for (int i = 0; i < threads; i++)
		wtps[i].rj = &rjs[i];
	int threadzoom = assignThreadTasks(wtps, *rj.tiletable, rj.mp, threads);
	for (int i = 0; i < threads; i++)
		cout << "thread " << i << " will render " << rjs[i].stats.reqtilecount << " base tiles" << endl;

	// allocate storage for the threads to store their rendered zoom tiles into
	// (doesn't need to be synchronized, because threads only touch the images for their own
	//  zoom tiles)
	auto_ptr<ThreadOutputCache> tocache(new ThreadOutputCache(threadzoom));
	for (int i = 0; i < threads; i++)
	{
		wtps[i].tocache = tocache.get();
		for (vector<ZoomTileIdx>::const_iterator it = wtps[i].zoomtiles.begin(); it != wtps[i].zoomtiles.end(); it++)
		{
			int idx = tocache->getIndex(*it);
			tocache->images[idx].create(rj.mp.tileSize(), rj.mp.tileSize());  // reserve the memory
		}
	}

	// run the threads; each one renders all the zoom tiles assigned to it
	cout << "running threads..." << endl;
	vector<pthread_t> pthrs(threads);
	for (int i = 0; i < threads; i++)
	{
		if (0 != pthread_create(&pthrs[i], NULL, runWorkerThread, (void*)&wtps[i]))
			cerr << "failed to create thread!" << endl;
	}
	for (int i = 0; i < threads; i++)
	{
		pthread_join(pthrs[i], NULL);
	}

	// now that the threads are done, render the final zoom levels (the ones above the ThreadOutputCache level)
	cout << "finishing top zoom levels..." << endl;
	rj.tilecache.reset(new TileCache(rj.mp));
	RGBAImage topimg;
	renderZoomTile(ZoomTileIdx(0,0,0), rj, topimg, *tocache);

	// combine the thread stats
	for (int i = 0; i < threads; i++)
	{
		rj.stats.chunkcache += rjs[i].stats.chunkcache;
		rj.stats.region += rjs[i].stats.region;
	}

	// copy the drawn flags over from the thread TileTables (for the double-check)
	for (RequiredTileIterator it(*rj.tiletable); !it.end; it.advance())
	{
		for (int i = 0; i < threads; i++)
			if (rjs[i].tiletable->isDrawn(it.current))
			{
				rj.tiletable->setDrawn(it.current);
				break;
			}
	}
}

bool expandMap(const string& outputpath)
{
	// read old params
	MapParams mp;
	if (!mp.readFile(outputpath))
	{
		cerr << "pigmap.params missing or corrupt" << endl;
		return false;
	}
	int32_t tileSize = mp.tileSize();

	// to expand a map, the following must be done:
	//  1. the top-left quadrant of the current zoom level 1 needs to be moved to zoom level 2, where
	//     it will become the bottom-right quadrant of the top-left quadrant of the new zoom level 1,
	//     so the top-level file "0.png" and subdirectory "0" must become "0/3.png" and "0/3",
	//     respectively; and similarly for the other three quadrants
	//  2. new zoom level 1 tiles must be created: "0.png" is 3/4 empty, but has a shrunk version of
	//     the old "0.png" (which is the new "0/3.png") in its bottom-right, etc.
	//  3. a new "base.png" must be created from the new zoom level 1 tiles

	// move everything at zoom 1 or higher one level deeper
	// ...first the subdirectories
	renameFile(outputpath + "/0", outputpath + "/old0");
	renameFile(outputpath + "/1", outputpath + "/old1");
	renameFile(outputpath + "/2", outputpath + "/old2");
	renameFile(outputpath + "/3", outputpath + "/old3");
	makePath(outputpath + "/0");
	makePath(outputpath + "/1");
	makePath(outputpath + "/2");
	makePath(outputpath + "/3");
	renameFile(outputpath + "/old0", outputpath + "/0/3");
	renameFile(outputpath + "/old1", outputpath + "/1/2");
	renameFile(outputpath + "/old2", outputpath + "/2/1");
	renameFile(outputpath + "/old3", outputpath + "/3/0");
	// ...now the zoom 1 files
	renameFile(outputpath + "/0.png", outputpath + "/0/3.png");
	renameFile(outputpath + "/1.png", outputpath + "/1/2.png");
	renameFile(outputpath + "/2.png", outputpath + "/2/1.png");
	renameFile(outputpath + "/3.png", outputpath + "/3/0.png");

	// build the new zoom 1 tiles
	RGBAImage old0img;
	bool used0 = old0img.readPNG(outputpath + "/0/3.png");
	RGBAImage new0img;
	new0img.create(tileSize, tileSize);
	if (used0)
	{
		reduceHalf(new0img, ImageRect(tileSize/2, tileSize/2, tileSize/2, tileSize/2), old0img);
		new0img.writePNG(outputpath + "/0.png");
	}
	RGBAImage old1img;
	bool used1 = old1img.readPNG(outputpath + "/1/2.png");
	RGBAImage new1img;
	new1img.create(tileSize, tileSize);
	if (used1)
	{
		reduceHalf(new1img, ImageRect(0, tileSize/2, tileSize/2, tileSize/2), old1img);
		new1img.writePNG(outputpath + "/1.png");
	}
	RGBAImage old2img;
	bool used2 = old2img.readPNG(outputpath + "/2/1.png");
	RGBAImage new2img;
	new2img.create(tileSize, tileSize);
	if (used2)
	{
		reduceHalf(new2img, ImageRect(tileSize/2, 0, tileSize/2, tileSize/2), old2img);
		new2img.writePNG(outputpath + "/2.png");
	}
	RGBAImage old3img;
	bool used3 = old3img.readPNG(outputpath + "/3/0.png");
	RGBAImage new3img;
	new3img.create(tileSize, tileSize);
	if (used3)
	{
		reduceHalf(new3img, ImageRect(0, 0, tileSize/2, tileSize/2), old3img);
		new3img.writePNG(outputpath + "/3.png");
	}

	// build the new base tile
	RGBAImage newbase;
	newbase.create(tileSize, tileSize);
	if (used0)
		reduceHalf(newbase, ImageRect(0, 0, tileSize/2, tileSize/2), new0img);
	if (used1)
		reduceHalf(newbase, ImageRect(tileSize/2, 0, tileSize/2, tileSize/2), new1img);
	if (used2)
		reduceHalf(newbase, ImageRect(0, tileSize/2, tileSize/2, tileSize/2), new2img);
	if (used3)
		reduceHalf(newbase, ImageRect(tileSize/2, tileSize/2, tileSize/2, tileSize/2), new3img);
	newbase.writePNG(outputpath + "/base.png");

	// write new params (with incremented baseZoom)
	mp.baseZoom++;
	mp.writeFile(outputpath);

	// touch all tiles, to prevent browser cache mishaps (since many new tiles will have the same
	//  filename as some old tile, but possibly with an earlier timestamp)
	system((string("find ") + outputpath + " -exec touch {} +").c_str());

	return true;
}

void writeHTML(const RenderJob& rj, const string& htmlpath)
{
	string templatePath = htmlpath + "/template.html";
	stringbuf strbuf;
	ifstream infile(templatePath.c_str());
	infile.get(strbuf, 0);  // get entire file (unless it happens to have a '\0' in it)
	if (infile.fail())
		return;
	string templateText = strbuf.str();
	if (!replace(templateText, "{tileSize}", tostring(rj.mp.tileSize())) ||
	    !replace(templateText, "{B}", tostring(rj.mp.B)) ||
	    !replace(templateText, "{T}", tostring(rj.mp.T)) ||
	    !replace(templateText, "{baseZoom}", tostring(rj.mp.baseZoom)))
		return;
	string htmlOutPath = rj.outputpath + "/pigmap-default.html";
	ofstream outfile(htmlOutPath.c_str());
	outfile << templateText;

	copyFile(htmlpath + "/style.css", rj.outputpath + "/style.css");
}

bool performRender(const string& inputpath, const string& outputpath, const string& imgpath, const MapParams& mp, const string& chunklist, const string& regionlist, int threads, int testworldsize, bool expand, const string& htmlpath)
{
	time_t tstart = time(NULL);

	// prepare the rendering params and the chunk/tile tables
	// ...note that mp.baseZoom might not be set yet if this is a full render; makeAllChunksRequired
	//  will handle it
	RenderJob rj;
	rj.testmode = testworldsize != -1;
	rj.mp = mp;
	rj.inputpath = inputpath;
	rj.outputpath = outputpath;
	if (!rj.blockimages.create(rj.mp.B, imgpath))
	{
		cerr << "no block images available; aborting render" << endl;
		return false;
	}
	rj.chunktable.reset(new ChunkTable);
	rj.tiletable.reset(new TileTable);
	rj.regiontable.reset(new RegionTable);
	rj.regionformat = !rj.testmode && detectRegionFormat(rj.inputpath);
	if (rj.regionformat)
		cout << "region-format world detected" << endl;
	else
		cout << "no regions detected; assuming chunk-format world" << endl;
	// test world
	if (testworldsize != -1)
	{
		rj.fullrender = true;
		cout << "building test world..." << endl;
		makeTestWorld(testworldsize, *rj.chunktable, *rj.tiletable, rj.mp, rj.stats.reqchunkcount, rj.stats.reqtilecount);
	}
	// full render
	else if (chunklist.empty() && regionlist.empty())
	{
		rj.fullrender = true;
		cout << "scanning world data..." << endl;
		if (rj.regionformat)
		{
			if (!makeAllRegionsRequired(rj.inputpath, *rj.chunktable, *rj.tiletable, *rj.regiontable, rj.mp, rj.stats.reqchunkcount, rj.stats.reqtilecount, rj.stats.reqregioncount))
				return false;
		}
		else
		{
			if (!makeAllChunksRequired(rj.inputpath, *rj.chunktable, *rj.tiletable, rj.mp, rj.stats.reqchunkcount, rj.stats.reqtilecount))
				return false;
		}
	}
	// incremental update
	else
	{
		rj.fullrender = false;
		int rv;
		if (rj.regionformat)
		{
			cout << "processing regionlist..." << endl;
			rv = readRegionlist(regionlist, rj.inputpath, *rj.chunktable, *rj.tiletable, *rj.regiontable, rj.mp, rj.stats.reqchunkcount, rj.stats.reqtilecount, rj.stats.reqregioncount);
		}
		else
		{
			cout << "processing chunklist..." << endl;
			rv = readChunklist(chunklist, *rj.chunktable, *rj.tiletable, rj.mp, rj.stats.reqchunkcount, rj.stats.reqtilecount);
		}
		if (rv == -2)
			return false;
		// if we failed because baseZoom is too small, and -x was specified, expand the world and try once more
		if (rv == -1 && expand)
		{
			if (!expandMap(rj.outputpath))
				return false;
			rj.mp.baseZoom++;
			cout << "baseZoom of output map has been increased to " << rj.mp.baseZoom << endl;
			rj.chunktable.reset(new ChunkTable);
			rj.tiletable.reset(new TileTable);
			rj.regiontable.reset(new RegionTable);
			if (rj.regionformat)
			{
				if (0 != readRegionlist(regionlist, rj.inputpath, *rj.chunktable, *rj.tiletable, *rj.regiontable, rj.mp, rj.stats.reqchunkcount, rj.stats.reqtilecount, rj.stats.reqregioncount))
					return false;
			}
			else
			{
				if (0 != readChunklist(chunklist, *rj.chunktable, *rj.tiletable, rj.mp, rj.stats.reqchunkcount, rj.stats.reqtilecount))
					return false;
			}
		}
	}

	if (rj.stats.reqtilecount == 0)
	{
		cout << "nothing to do!  (no required tiles)" << endl;
		return true;
	}

	// render stuff
	cout << "rendering tiles..." << endl;
	if (threads >= 2)
		runMultithreaded(rj, threads);
	else
		runSingleThread(rj);

	// double-check that all the required tiles were drawn
	cout << "performing double-check..." << endl;
	for (RequiredTileIterator it(*rj.tiletable); !it.end; it.advance())
	{
		if (!rj.tiletable->isDrawn(it.current))
			cerr << "required tile " << it.current.toTileIdx().toFilePath(rj.mp) << " was somehow not drawn!" << endl;
	}

	// write map params, HTML
	if (!rj.testmode)
	{
		rj.mp.writeFile(rj.outputpath);
		writeHTML(rj, htmlpath);
	}

	// done; print stats
	time_t tfinish = time(NULL);
	printStats(tfinish - tstart, rj.stats);
	return true;
}

//-------------------------------------------------------------------------------------------------------------------

// warning: slow
void testTileBBoxes(const MapParams& mp)
{
	// check tile bounding boxes for a few tiles
	for (int64_t tx = -5; tx <= 5; tx++)
		for (int64_t ty = -5; ty <= 5; ty++)
		{
			// get computed BBox
			TileIdx ti(tx,ty);
			BBox bbox = ti.getBBox(mp);

			// this is what the box is supposed to be
			int64_t xmin = 64*mp.B*mp.T*tx - 2*mp.B;
			int64_t ymax = 64*mp.B*mp.T*ty + 17*mp.B;
			int64_t xmax = xmin + mp.tileSize();
			int64_t ymin = ymax - mp.tileSize();

			// test pixels
			for (int64_t x = xmin - 15; x <= xmax + 15; x++)
				for (int64_t y = ymin - 15; y <= ymax + 15; y++)
				{
					bool result = bbox.includes(Pixel(x,y));
					bool expected = x >= xmin && x < xmax && y >= ymin && y < ymax;
					if (result != expected)
					{
						cout << "failed tile bounding box test!  " << tx << " " << ty << endl;
						cout << "[" << bbox.topLeft.x << "," << bbox.topLeft.y << "] to [" << bbox.bottomRight.x << "," << bbox.bottomRight.y << "]" << endl;
						cout << "[" << xmin << "," << ymin << "] to [" << xmax << "," << ymax << "]" << endl;
						cout << x << "," << y << endl;
						return;
					}
				}
		}
}

// warning: slow
void testMath()
{
	// vary map params: block size B from 2 to 6, tile multiplier T from 1 to 4
	MapParams mp(0,0,0);
	for (mp.B = 2; mp.B <= 6; mp.B++)
		for (mp.T = 1; mp.T <= 4; mp.T++)
		{
			cout << "B = " << mp.B << "   T = " << mp.T << endl;

			testTileBBoxes(mp);
		}
}

void testBase36()
{
	for (int64_t i = -2473; i <= 1472; i += 93)
		cout << i << "   " << toBase36(i) << "   " << fromBase36(toBase36(i)) << endl;
	for (int64_t x = -123; x <= 201; x += 45)
		for (int64_t z = -239; z <= 196; z += 57)
		{
			ChunkIdx ci(x,z);
			string filepath = ci.toFilePath();
			ChunkIdx ci2(-999999,-999999);
			if (ChunkIdx::fromFilePath(filepath, ci2))
				cout << "[" << x << "," << z << "]   " << filepath << "   [" << ci2.x << "," << ci2.z << "]" << endl;
			else
			{
				cout << "failed to get ChunkIdx from filename: " << filepath << endl;
				return;
			}
		}
}

void testMod64()
{
	for (int64_t i = -135; i < 135; i++)
		cout << i << "   mod64: " << mod64pos(i) << "    base36: " << toBase36(mod64pos(i)) << endl;
}

struct compareChunks
{
	bool operator()(const ChunkIdx& ci1, const ChunkIdx& ci2) const {if (ci1.x == ci2.x) return ci1.z < ci2.z; return ci1.x < ci2.x;}
};

// warning: slow; only use with small worlds
void testChunkTable(const string& inputpath)
{
	vector<string> chunkpaths;
	findAllChunks(inputpath, chunkpaths);

	auto_ptr<ChunkTable> chunktable(new ChunkTable);
	auto_ptr<TileTable> tiletable(new TileTable);
	int64_t reqchunkcount, reqtilecount;
	MapParams mp(6,1,-1);
	makeAllChunksRequired(inputpath, *chunktable, *tiletable, mp, reqchunkcount, reqtilecount);

	// make sure all chunks in the file list are present and marked required in the ChunkTable
	// ...also build list of ChunkIdxs for comparison with the RequiredChunkIterator later
	set<ChunkIdx, compareChunks> chunklist;
	for (vector<string>::const_iterator it = chunkpaths.begin(); it != chunkpaths.end(); it++)
	{
		ChunkIdx ci(0,0);
		if (ChunkIdx::fromFilePath(*it, ci))
		{
			chunklist.insert(ci);
			if (!chunktable->isRequired(ci))
			{
				cout << "chunk file " << *it << " is not marked as required!" << endl;
				return;
			}
		}
	}

	// iterate over the required chunks in the ChunkTable and make sure each one is present in the file list
	// ...also compute the total size of the ChunkTable
	int lastcgi = -1, lastcsi = -1;
	int64_t level3size = sizeof(ChunkTable);
	int64_t level2size = 0;
	int64_t level1size = 0;
	int64_t chunkcount = 0;
	for (RequiredChunkIterator it(*chunktable); !it.end; it.advance())
	{
		ChunkIdx ci = it.current.toChunkIdx();
		if (chunklist.find(ci) == chunklist.end())
		{
			cout << "chunk [" << ci.x << "," << ci.z << "] was iterated over, but is not in file list!" << endl;
			return;
		}

		chunkcount++;
		if (it.cgi != lastcgi || it.csi != lastcsi)
		{
			level1size += sizeof(ChunkSet);
			if (it.cgi != lastcgi)
				level2size += sizeof(ChunkGroup);
			lastcgi = it.cgi;
			lastcsi = it.csi;
		}
	}
	cout << "world size: " << chunkcount << " chunks" << endl;
	cout << "ChunkTable size: " << level3size << " + " << level2size << " + " << level1size << " bytes" << endl;
}

void testPNG()
{
	RGBAImage img;
	img.create(100, 100);
	for (vector<RGBAPixel>::iterator it = img.data.begin(); it != img.data.end(); it++)
	{
		*it = ((rand() % 256) << 24) | ((rand() % 256) << 16) | ((rand() % 256) << 8) | (rand() % 256);
	}
	img.writePNG("test.png");

	RGBAImage img2;
	img2.readPNG("test.png");
	if (img2.data != img.data)
		cout << "images don't match after trip through PNG!" << endl;
	else
		cout << "PNG test successful" << endl;
}

struct compareTiles
{
	bool operator()(const TileIdx& ti1, const TileIdx& ti2) const {if (ti1.x == ti2.x) return ti1.y < ti2.y; return ti1.x < ti2.x;}
};

void testIterators(const string& inputpath)
{
	MapParams mp(3,2,10);
	auto_ptr<ChunkTable> chunktable(new ChunkTable);
	auto_ptr<TileTable> tiletable(new TileTable);
	int64_t reqchunkcount, reqtilecount;
	makeAllChunksRequired(inputpath, *chunktable, *tiletable, mp, reqchunkcount, reqtilecount);

	// make sure the RequiredChunkIterator and RequiredTileIterator compute the same results
	set<TileIdx, compareTiles> tiles1, tiles2;

	for (RequiredChunkIterator it(*chunktable); !it.end; it.advance())
	{
		ChunkIdx ci = it.current.toChunkIdx();
		vector<TileIdx> tiles = ci.getTiles(mp);
		for (vector<TileIdx>::const_iterator tile = tiles.begin(); tile != tiles.end(); tile++)
			tiles1.insert(*tile);
	}

	for (RequiredTileIterator it(*tiletable); !it.end; it.advance())
	{
		tiles2.insert(it.current.toTileIdx());
	}

	if (tiles1 != tiles2)
		cout << "iterators don't match!" << endl;
	else
		cout << "iterators match" << endl;
}

void testZOrder()
{
	int SIZE = 64;
	vector<int> hits1(SIZE*SIZE, 0), hits2(SIZE*SIZE, 0);
	for (int i = 0; i < SIZE*SIZE; i++)
	{
		hits1[toZOrder(i, SIZE)]++;
		hits2[fromZOrder(i, SIZE)]++;
	}
	for (int i = 0; i < SIZE*SIZE; i++)
		if (hits1[i] != 1 || hits2[i] != 1)
			cout << "position " << i << " was hit " << hits1[i] << ", " << hits2[i] << " times!" << endl;
}

void testTileIdxs()
{
	for (int baseZoom = 3; baseZoom < 11; baseZoom++)
	{
		MapParams mp(6,1,baseZoom);
		for (int z = 0; z < 4; z++)
			for (int x = 0; x < (1 << z); x++)
				for (int y = 0; y < (1 << z); y++)
				{
					ZoomTileIdx zti(x, y, z);
					TileIdx ti = zti.toTileIdx(mp);
					ZoomTileIdx zti2 = zti.toZoom(baseZoom);
					TileIdx ti2 = zti2.toTileIdx(mp);
					if (ti != ti2)
						cout << "mismatch!   baseZoom "  << baseZoom << "   zoom tile [" << zti.x << ","
						     << zti.y << "] @ " << zti.zoom << endl;
				}
	}
}

void testReqTileCount(const string& inputpath)
{
	MapParams mp(6,1,10);
	auto_ptr<ChunkTable> chunktable(new ChunkTable);
	auto_ptr<TileTable> tiletable(new TileTable);
	int64_t reqchunkcount, reqtilecount;
	makeAllChunksRequired(inputpath, *chunktable, *tiletable, mp, reqchunkcount, reqtilecount);

	cout << "required base tiles: " << reqtilecount << endl;
	for (int z = 0; z <= mp.baseZoom; z++)
	{
		int64_t count = 0;
		for (int x = 0; x < (1 << z); x++)
			for (int y = 0; y < (1 << z); y++)
				count += tiletable->getNumRequired(ZoomTileIdx(x, y, z), mp);
		if (count != reqtilecount)
			cout << "tile counts don't match for zoom " << z << "!" << endl;
		else
			cout << "tile counts okay for zoom " << z << endl;
	}
}

//-------------------------------------------------------------------------------------------------------------------

bool validateParamsFull(const string& inputpath, const string& outputpath, const string& imgpath, const MapParams& mp, int threads, const string& chunklist, const string& regionlist, bool expand, const string& htmlpath)
{
	// -c and -x are not allowed for full renders
	if (!chunklist.empty() || !regionlist.empty() || expand)
	{
		cerr << "-c, -r, -x not allowed for full renders" << endl;
		return false;
	}

	// B and T must be within range (upper limits aren't really necessary and can be adjusted if
	//  someone really wants gigantic tile images for some reason)
	if (!mp.valid())
	{
		cerr << "-B must be in range 2-16; -T must be in range 1-16" << endl;
		return false;
	}

	// baseZoom must be within range, or -1 (omitted)
	if (!mp.validZoom() && mp.baseZoom != -1)
	{
		cerr << "-Z must be in range 0-30, or may be omitted to set automatically" << endl;
		return false;
	}

	// must have a sensible number of threads (upper limit is arbitrary, but you'd need a truly
	//  insanely large map to see any benefit to having that many...)
	if (threads < 1 || threads > 64)
	{
		cerr << "-h must be in range 1-64" << endl;
		return false;
	}

	// the various paths must be non-empty
	if (inputpath.empty() || outputpath.empty())
	{
		cerr << "must provide both input (-i) and output (-o) paths" << endl;
		return false;
	}
	if (imgpath.empty())
	{
		cerr << "must provide non-empty image path, or omit -g to use \".\"" << endl;
		return false;
	}
	if (htmlpath.empty())
	{
		cerr << "must provide non-empty HTML path, or omit -m to use \".\"" << endl;
		return false;
	}

	return true;
}

// also sets MapParams to values from existing map
bool validateParamsIncremental(const string& inputpath, const string& outputpath, const string& imgpath, MapParams& mp, int threads, const string& chunklist, const string& regionlist, bool expand, const string& htmlpath)
{
	// -B, -T, -Z are not allowed
	if (mp.B != -1 || mp.T != -1 || mp.baseZoom != -1)
	{
		cerr << "-B, -T, -Z not allowed for incremental updates" << endl;
		return false;
	}

	// the various paths must be non-empty
	if (inputpath.empty() || outputpath.empty())
	{
		cerr << "must provide both input (-i) and output (-o) paths" << endl;
		return false;
	}
	if (imgpath.empty())
	{
		cerr << "must provide non-empty image path, or omit -g to use \".\"" << endl;
		return false;
	}
	if (htmlpath.empty())
	{
		cerr << "must provide non-empty HTML path, or omit -m to use \".\"" << endl;
		return false;
	}

	// can't have both chunklist and regionlist
	if (!chunklist.empty() && !regionlist.empty())
	{
		cerr << "only one of -c, -r may be used" << endl;
		return false;
	}

	// if world is in region format, must use regionlist
	if (detectRegionFormat(inputpath) && regionlist.empty())
	{
		cerr << "world is in region format; must use -r, not -c" << endl;
		return false;
	}

	// pigmap.params must be present in output path; read it now
	if (!mp.readFile(outputpath))
	{
		cerr << "can't find pigmap.params in output path" << endl;
		return false;
	}

	// must have a sensible number of threads (upper limit is arbitrary, but you'd need a truly
	//  insanely large map to see any benefit to having that many...)
	if (threads < 1 || threads > 64)
	{
		cerr << "-h must be in range 1-64" << endl;
		return false;
	}

	return true;
}

bool validateParamsTest(const string& inputpath, const string& outputpath, const string& imgpath, const MapParams& mp, int threads, const string& chunklist, const string& regionlist, bool expand, const string& htmlpath, int testworldsize)
{
	// -i, -o, -c, -r, -x, -m are not allowed
	if (!inputpath.empty() || !outputpath.empty() || !chunklist.empty() || !regionlist.empty() || expand || htmlpath != ".")
	{
		cerr << "-i, -o, -c, -r, -x, -m not allowed for test worlds" << endl;
		return false;
	}

	// B and T must be within range (upper limits aren't really necessary and can be adjusted if
	//  someone really wants gigantic tile images for some reason)
	if (!mp.valid())
	{
		cerr << "-B must be in range 2-16; -T must be in range 1-16" << endl;
		return false;
	}

	// baseZoom must be within range, or -1 (omitted)
	if (!mp.validZoom() && mp.baseZoom != -1)
	{
		cerr << "-Z must be in range 0-30, or may be omitted to set automatically" << endl;
		return false;
	}

	// must have a sensible number of threads (upper limit is arbitrary, but you'd need a truly
	//  insanely large map to see any benefit to having that many...)
	if (threads < 1 || threads > 64)
	{
		cerr << "-h must be in range 1-64" << endl;
		return false;
	}

	// image path must be non-empty
	if (imgpath.empty())
	{
		cerr << "must provide non-empty image path, or omit -g to use \".\"" << endl;
		return false;
	}

	// test world size must be positive
	if (testworldsize < 0)
	{
		cerr << "testworld size must be positive" << endl;
		return false;
	}

	return true;
}

int main(int argc, char **argv)
{
	//testMath();
	//testBase36();
	//testMod64();
	//testChunkTable(inputpath);
	//testTileIterator();
	//testPColIterator();
	//testChunkCache();
	//testPNG();
	//testIterators(inputpath);
	//testZOrder();
	//testTileIdxs();
	//testReqTileCount(inputpath);

	string inputpath, outputpath, imgpath = ".", chunklist, regionlist, htmlpath = ".";
	MapParams mp(-1,-1,-1);
	int threads = 1;
	int testworldsize = -1;
	bool expand = false;

	int c;
	while ((c = getopt(argc, argv, "i:o:g:c:B:T:Z:h:w:xm:r:")) != -1)
	{
		switch (c)
		{
			case 'i':
				inputpath = optarg;
				break;
			case 'o':
				outputpath = optarg;
				break;
			case 'g':
				imgpath = optarg;
				break;
			case 'c':
				chunklist = optarg;
				break;
			case 'r':
				regionlist = optarg;
				break;
			case 'B':
				mp.B = atoi(optarg);
				break;
			case 'T':
				mp.T = atoi(optarg);
				break;
			case 'Z':
				mp.baseZoom = atoi(optarg);
				break;
			case 'h':
				threads = atoi(optarg);
				break;
			case 'x':
				expand = true;
				break;
			case 'm':
				htmlpath = optarg;
				break;
			case 'w':
				testworldsize = atoi(optarg);
				break;
			case '?':
				cerr << "-" << (char)optopt << ": unrecognized option or missing argument" << endl;
				return 1;
			default:  // should never happen (?)
				cerr << "getopt not working?" << endl;
				return 1;
		}
	}

	if (testworldsize != -1)
	{
		if (!validateParamsTest(inputpath, outputpath, imgpath, mp, threads, chunklist, regionlist, expand, htmlpath, testworldsize))
			return 1;
	}
	else if (chunklist.empty() && regionlist.empty())
	{
		if (!validateParamsFull(inputpath, outputpath, imgpath, mp, threads, chunklist, regionlist, expand, htmlpath))
			return 1;
	}
	else
	{
		if (!validateParamsIncremental(inputpath, outputpath, imgpath, mp, threads, chunklist, regionlist, expand, htmlpath))
			return 1;
	}

	if (!performRender(inputpath, outputpath, imgpath, mp, chunklist, regionlist, threads, testworldsize, expand, htmlpath))
		return 1;

	return 0;
}
