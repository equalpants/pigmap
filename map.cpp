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

#include <fstream>
#include <map>

#include "map.h"
#include "utils.h"

using namespace std;



bool MapParams::valid() const
{
	return B >= 2 && B <= 16 && T >= 1 && T <= 16;
}

bool MapParams::validZoom() const
{
	return baseZoom >= 0 && baseZoom <= 30;
}

bool MapParams::validYRange() const
{
	return minY <= maxY && minY >= 0 && maxY <= 255;
}


bool buildParamMap(const vector<string>& lines, map<string, string>& params)
{
	for (vector<string>::const_iterator it = lines.begin(); it != lines.end(); it++)
	{
		vector<string> tokens = tokenize(*it, ' ');
		if (tokens.size() != 2)
			return false;
		params.insert(make_pair(tokens[0], tokens[1]));
	}
	return true;
}

bool readParam(const map<string, string>& params, const string& key, int& value)
{
	map<string, string>::const_iterator it = params.find(key);
	if (it == params.end())
		return false;
	return fromstring(it->second, value);
}

bool MapParams::readFile(const string& outputpath)
{
	string filename = outputpath + "/pigmap.params";
	vector<string> lines;
	map<string, string> params;
	if (!readLines(filename, lines) || !buildParamMap(lines, params))
		return false;
	if (!readParam(params, "B", B) || !readParam(params, "T", T) || !readParam(params, "baseZoom", baseZoom))
		return false;
	userMinY = readParam(params, "userMinY", minY);
	userMaxY = readParam(params, "userMaxY", maxY);
	return valid() && validZoom();
}

void MapParams::writeFile(const string& outputpath) const
{
	string filename = outputpath + "/pigmap.params";
	ofstream outfile(filename.c_str());
	outfile << "B " << B << endl << "T " << T << endl << "baseZoom " << baseZoom << endl;
	if (userMinY)
		outfile << "userMinY " << minY << endl;
	if (userMaxY)
		outfile << "userMaxY " << maxY << endl;
}






Pixel operator+(const Pixel& p1, const Pixel& p2) {Pixel p = p1; return p += p2;}
Pixel operator-(const Pixel& p1, const Pixel& p2) {Pixel p = p1; return p -= p2;}

TileIdx Pixel::getTile(const MapParams& mp) const
{
	int64_t xx = x + 2*mp.B, yy = y + mp.tileSize() - 17*mp.B;
	return TileIdx(floordiv(xx, mp.tileSize()), floordiv(yy, mp.tileSize()));
}


bool BBox::includes(const Pixel& p) const {return p.x >= topLeft.x && p.x < bottomRight.x && p.y >= topLeft.y && p.y < bottomRight.y;}
bool BBox::overlaps(const BBox& bb) const
{
	if (bb.topLeft.x >= bottomRight.x || bb.topLeft.y >= bottomRight.y || bb.bottomRight.x <= topLeft.x || bb.bottomRight.y <= topLeft.y)
		return false;
	return true;
}


BlockIdx operator+(const BlockIdx& bi1, const BlockIdx& bi2) {BlockIdx bi = bi1; return bi += bi2;}
BlockIdx operator-(const BlockIdx& bi1, const BlockIdx& bi2) {BlockIdx bi = bi1; return bi -= bi2;}

bool BlockIdx::occludes(const BlockIdx& bi) const
{
	int64_t dx = bi.x - x, dz = bi.z - z, dy = bi.y - y;
	// we cannot occlude anyone to the N, W, or U of us
	if (dx < 0 || dz > 0 || dy > 0)
		return false;
	// see if the other block's center is 0 or 1 steps away from ours on the triangular grid
	// (the actual grid size doesn't matter; just use a dummy size of 2x1)
	int64_t imgxdiff = dx*2 + dz*2;
	int64_t imgydiff = -dx + dz - dy*2;
	return imgxdiff <= 2 && imgydiff <= 2;
}

ChunkIdx BlockIdx::getChunkIdx() const
{
	return ChunkIdx(floordiv16(x), floordiv16(z));
}

BlockIdx BlockIdx::topBlock(const Pixel& p, const MapParams& mp)
{
	// x = 2Bbx + 2Bbz
	//  2Bbx = x - 2Bbz
	//   bx = x/2B - bz
	// y = -Bbx +Bbz -2Bmaxy
	//  Bbz = y + Bbx + 2Bmaxy
	//   bz = y/B + bx + 2maxy
	// bx = x/2B - y/B - bx - 2maxy
	//  2bx = x/2B - y/B - 2maxy
	//   bx = x/4B - y/2B - maxy     <----
	// bz = y/B + x/4B - y/2B - maxy + 2maxy
	//  bz = x/4B + y/2B + maxy     <----
	return BlockIdx((p.x - 2*p.y)/(4*mp.B) - mp.maxY, (p.x + 2*p.y)/(4*mp.B) + mp.maxY, mp.maxY);
}




string ChunkIdx::toFileName() const
{
	return "c." + toBase36(x) + "." + toBase36(z) + ".dat";
}
string ChunkIdx::toFilePath() const
{
	return toBase36(mod64pos(x)) + "/" + toBase36(mod64pos(z)) + "/" + toFileName();
}

bool ChunkIdx::fromFilePath(const std::string& filename, ChunkIdx& result)
{
	string::size_type pos3 = filename.rfind('.');
	string::size_type pos2 = filename.rfind('.', pos3 - 1);
	string::size_type pos = filename.rfind('.', pos2 - 1);
	// must have three dots, must have only "dat" after last dot, must have only "c"
	//  and possibly some directories before the first dot
	if (pos == string::npos || pos2 == string::npos || pos3 == string::npos ||
		filename.compare(pos3, filename.size() - pos3, ".dat") != 0 ||
		pos < 1 || filename.compare(pos - 1, 1, "c") != 0 ||
		(pos > 1 && filename[pos - 2] != '/'))
		return false;
	return fromBase36(filename, pos + 1, pos2 - pos - 1, result.x)
		&& fromBase36(filename, pos2 + 1, pos3 - pos2 - 1, result.z);
}

RegionIdx ChunkIdx::getRegionIdx() const
{
	return RegionIdx(floordiv(x, 32), floordiv(z, 32));
}

vector<TileIdx> ChunkIdx::getTiles(const MapParams& mp) const
{
	BBox bbchunk = getBBox(mp);
	vector<TileIdx> tiles;

	// get tile of NED corner
	TileIdx tibase = nedCorner(mp).getCenter(mp).getTile(mp);
	tiles.push_back(tibase);

	// grab as many tiles down as we need
	TileIdx tidown = tibase + TileIdx(0,1);
	while (tidown.getBBox(mp).overlaps(bbchunk))
	{
		tiles.push_back(tidown);
		tidown += TileIdx(0,1);
	}

	// grab as many tiles up as we need
	TileIdx tiup = tibase - TileIdx(0,1);
	while (tiup.getBBox(mp).overlaps(bbchunk))
	{
		tiles.push_back(tiup);
		tiup -= TileIdx(0,1);
	}

	// we may also need the tiles to the right of all the ones we have so far
	TileIdx tiright = tibase + TileIdx(1,0);
	if (tiright.getBBox(mp).overlaps(bbchunk))
	{
		vector<TileIdx>::size_type oldsize = tiles.size();
		for (vector<TileIdx>::size_type i = 0; i < oldsize; i++)
			tiles.push_back(tiles[i] + TileIdx(1,0));
	}

	return tiles;
}

ChunkIdx operator+(const ChunkIdx& ci1, const ChunkIdx& ci2) {ChunkIdx ci = ci1; return ci += ci2;}
ChunkIdx operator-(const ChunkIdx& ci1, const ChunkIdx& ci2) {ChunkIdx ci = ci1; return ci -= ci2;}



string RegionIdx::toOldFileName() const
{
	return "r." + tostring(x) + "." + tostring(z) + ".mcr";
}

string RegionIdx::toAnvilFileName() const
{
	return "r." + tostring(x) + "." + tostring(z) + ".mca";
}

bool RegionIdx::fromFilePath(const std::string& filename, RegionIdx& result)
{
	string::size_type pos3 = filename.rfind('.');
	string::size_type pos2 = filename.rfind('.', pos3 - 1);
	string::size_type pos = filename.rfind('.', pos2 - 1);
	// must have three dots, must have only "mcr" or "mca" after last dot, must have only "r"
	//  and possibly some directories before the first dot
	if (pos == string::npos || pos2 == string::npos || pos3 == string::npos ||
		(filename.compare(pos3, filename.size() - pos3, ".mcr") != 0 && filename.compare(pos3, filename.size() - pos3, ".mca") != 0) ||
		pos < 1 || filename.compare(pos - 1, 1, "r") != 0 ||
		(pos > 1 && filename[pos - 2] != '/'))
		return false;
	return fromstring(filename.substr(pos + 1, pos2 - pos - 1), result.x)
		&& fromstring(filename.substr(pos2 + 1, pos3 - pos2 - 1), result.z);
}


TileIdx operator+(const TileIdx& t1, const TileIdx& t2) {TileIdx t = t1; return t += t2;}
TileIdx operator-(const TileIdx& t1, const TileIdx& t2) {TileIdx t = t1; return t -= t2;}

bool TileIdx::valid(const MapParams& mp) const
{
	if (mp.baseZoom == 0)
		return x == 0 && y == 0;
	int64_t max = (1 << mp.baseZoom);
	int64_t offset = max/2;
	int64_t gx = x + offset, gy = y + offset;
	return gx >= 0 && gx < max && gy >= 0 && gy < max;
}

string TileIdx::toFilePath(const MapParams& mp) const
{
	if (!valid(mp))
		return string();
	if (mp.baseZoom == 0)
		return "base.png";
	int64_t offset = (1 << (mp.baseZoom-1));
	int64_t gx = x + offset, gy = y + offset;
	string s;
	for (int zoom = mp.baseZoom-1; zoom >= 0; zoom--)
	{
		int64_t xbit = (gx >> zoom) & 0x1;
		int64_t ybit = (gy >> zoom) & 0x1;
		s += tostring(xbit + 2*ybit) + "/";
	}
	s.resize(s.size() - 1);  // drop final slash
	s += ".png";
	return s;
}

BBox TileIdx::getBBox(const MapParams& mp) const
{
	Pixel bco = baseChunk(mp).originBlock().getCenter(mp);
	Pixel tl = bco + Pixel(-2*mp.B, 17*mp.B - mp.tileSize());
	return BBox(tl, tl + Pixel(mp.tileSize(), mp.tileSize()));
}

ZoomTileIdx TileIdx::toZoomTileIdx(const MapParams& mp) const
{
	// adjust by offset
	int64_t max = (1 << mp.baseZoom);
	int64_t offset = max/2;
	return ZoomTileIdx(x + offset, y + offset, mp.baseZoom);
}



bool ZoomTileIdx::valid() const
{
	int64_t max = (1 << zoom);
	return x >= 0 && x < max && y >= 0 && y < max && zoom >= 0;
}

string ZoomTileIdx::toFilePath() const
{
	if (!valid())
		return string();
	if (zoom == 0)
		return "base.png";
	string s;
	for (int z = zoom-1; z >= 0; z--)
	{
		int64_t xbit = (x >> z) & 0x1;
		int64_t ybit = (y >> z) & 0x1;
		s += tostring(xbit + 2*ybit) + "/";
	}
	s.resize(s.size() - 1);  // drop final slash
	s += ".png";
	return s;
}

TileIdx ZoomTileIdx::toTileIdx(const MapParams& mp) const
{
	// scale coords up to base zoom
	int64_t newx = x, newy = y;
	int shift = mp.baseZoom - zoom;
	newx <<= shift;
	newy <<= shift;
	// adjust by offset to get TileIdx
	int64_t max = (1 << mp.baseZoom);
	int64_t offset = max/2;
	return TileIdx(newx - offset, newy - offset);
}

ZoomTileIdx ZoomTileIdx::toZoom(int z) const
{
	if (z > zoom)
	{
		int shift = z - zoom;
		return ZoomTileIdx(x << shift, y << shift, z);
	}
	int shift = zoom - z;
	return ZoomTileIdx(x >> shift, y >> shift, z);
}
