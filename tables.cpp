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

#include <iostream>

#include "tables.h"

using namespace std;



void ChunkGroup::setRequired(const PosChunkIdx& ci)
{
	int csi = chunkSetIdx(ci);
	if (chunksets[csi] == NULL)
		chunksets[csi] = new ChunkSet;
	chunksets[csi]->setRequired(ci);
}

void ChunkGroup::setDiskState(const PosChunkIdx& ci, int state)
{
	int csi = chunkSetIdx(ci);
	if (chunksets[csi] == NULL)
		chunksets[csi] = new ChunkSet;
	chunksets[csi]->setDiskState(ci, state);
}




PosChunkIdx ChunkTable::toPosChunkIdx(int cgi, int csi, int bi)
{
	PosChunkIdx ci(0,0);
	ci.x += (cgi % CTLEVEL3SIZE) * CTLEVEL1SIZE * CTLEVEL2SIZE;
	ci.z += (cgi / CTLEVEL3SIZE) * CTLEVEL1SIZE * CTLEVEL2SIZE;
	ci.x += (csi % CTLEVEL2SIZE) * CTLEVEL1SIZE;
	ci.z += (csi / CTLEVEL2SIZE) * CTLEVEL1SIZE;
	ci.x += ((bi / CTDATASIZE) % CTLEVEL1SIZE);
	ci.z += ((bi / CTDATASIZE) / CTLEVEL1SIZE);
	return ci;
}

void ChunkTable::setRequired(const PosChunkIdx& ci)
{
	int cgi = chunkGroupIdx(ci);
	if (chunkgroups[cgi] == NULL)
		chunkgroups[cgi] = new ChunkGroup;
	chunkgroups[cgi]->setRequired(ci);
}

void ChunkTable::setDiskState(const PosChunkIdx& ci, int state)
{
	int cgi = chunkGroupIdx(ci);
	if (chunkgroups[cgi] == NULL)
		chunkgroups[cgi] = new ChunkGroup;
	chunkgroups[cgi]->setDiskState(ci, state);
}

void ChunkTable::copyFrom(const ChunkTable& ctable)
{
	for (int cgi = 0; cgi < CTLEVEL3SIZE*CTLEVEL3SIZE; cgi++)
	{
		if (ctable.chunkgroups[cgi] != NULL)
		{
			chunkgroups[cgi] = new ChunkGroup;
			for (int csi = 0; csi < CTLEVEL2SIZE*CTLEVEL2SIZE; csi++)
			{
				if (ctable.chunkgroups[cgi]->chunksets[csi] != NULL)
				{
					chunkgroups[cgi]->chunksets[csi] = new ChunkSet(*(ctable.chunkgroups[cgi]->chunksets[csi]));
				}
			}
		}
	}
}



RequiredChunkIterator::RequiredChunkIterator(ChunkTable& ctable) : chunktable(ctable), current(-1,-1)
{
	// if the very first chunk is required, use it
	cgi = csi = bi = 0;
	current = ChunkTable::toPosChunkIdx(cgi, csi, bi);
	if (chunktable.isRequired(current))
	{
		end = false;
		return;
	}
	// ...otherwise, advance to the next one after it
	advance();
}

void RequiredChunkIterator::advance()
{
	bi += CTDATASIZE;
	for (; cgi < CTLEVEL3SIZE*CTLEVEL3SIZE; cgi++)
	{
		ChunkGroup *cg = chunktable.chunkgroups[cgi];
		if (cg == NULL)
			continue;
		for (; csi < CTLEVEL2SIZE*CTLEVEL2SIZE; csi++)
		{
			ChunkSet *cs = cg->chunksets[csi];
			if (cs == NULL)
				continue;
			for (; bi < CTLEVEL1SIZE*CTLEVEL1SIZE*CTDATASIZE; bi += CTDATASIZE)
			{
				if (cs->bits[bi])
				{
					end = false;
					current = chunktable.toPosChunkIdx(cgi, csi, bi);
					return;
				}
			}
			bi = 0;
		}
		csi = 0;
		bi = 0;
	}
	end = true;
}






bool TileGroup::setRequired(const PosTileIdx& ti)
{
	int tsi = tileSetIdx(ti);
	if (tilesets[tsi] == NULL)
		tilesets[tsi] = new TileSet;
	bool prevset = tilesets[tsi]->setRequired(ti);
	if (!prevset)
		reqcount++;
	return prevset;
}

void TileGroup::setDrawn(const PosTileIdx& ti)
{
	int tsi = tileSetIdx(ti);
	if (tilesets[tsi] == NULL)
		tilesets[tsi] = new TileSet;
	tilesets[tsi]->setDrawn(ti);
}

PosTileIdx TileTable::toPosTileIdx(int tgi, int tsi, int bi)
{
	PosTileIdx ti(0,0);
	ti.x += (tgi % TTLEVEL3SIZE) * TTLEVEL1SIZE * TTLEVEL2SIZE;
	ti.y += (tgi / TTLEVEL3SIZE) * TTLEVEL1SIZE * TTLEVEL2SIZE;
	ti.x += (tsi % TTLEVEL2SIZE) * TTLEVEL1SIZE;
	ti.y += (tsi / TTLEVEL2SIZE) * TTLEVEL1SIZE;
	ti.x += ((bi / TTDATASIZE) % TTLEVEL1SIZE);
	ti.y += ((bi / TTDATASIZE) / TTLEVEL1SIZE);
	return ti;
}

bool TileTable::setRequired(const PosTileIdx& ti)
{
	int tgi = tileGroupIdx(ti);
	if (tilegroups[tgi] == NULL)
		tilegroups[tgi] = new TileGroup;
	bool prevset = tilegroups[tgi]->setRequired(ti);
	if (!prevset)
		reqcount++;
	return prevset;
}

void TileTable::setDrawn(const PosTileIdx& ti)
{
	int tgi = tileGroupIdx(ti);
	if (tilegroups[tgi] == NULL)
		tilegroups[tgi] = new TileGroup;
	tilegroups[tgi]->setDrawn(ti);
}

bool TileTable::reject(const ZoomTileIdx& zti, const MapParams& mp) const
{
	// if this zoom tile includes more than one TileGroup, we can't reject early
	if (zti.zoom < mp.baseZoom - TTLEVEL1BITS - TTLEVEL2BITS)
		return false;
	// zoom tiles anywhere except level 0 have the property of not crossing TileSet/TileGroup
	//  boundaries--either they're entirely inside a set/group, or they contain entire
	//  sets/groups--but for 0, that's not the case, so we'd have to check multiple sets/groups;
	//  instead, we just don't bother trying, since the tile at level 0 is going to have to be
	//  drawn anyway
	if (zti.zoom == 0)
		return false;
	TileIdx ti = zti.toTileIdx(mp);
	// if this zoom tile is contained within a TileSet, see if the set is NULL
	if (zti.zoom >= mp.baseZoom - TTLEVEL1BITS)
		return getTileSet(ti) == NULL;
	// otherwise, the tile is within a TileGroup, but covers more than one TileSet; see if the TileGroup is NULL
	return getTileGroup(ti) == NULL;
}

int64_t TileTable::getNumRequired(const ZoomTileIdx& zti, const MapParams& mp) const
{
	// if this is the very top level, we already know the answer
	if (zti.zoom == 0)
		return reqcount;
	// if this zoom tile is smaller than a TileSet, get its TileSet and check the tiles individually
	if (zti.zoom > mp.baseZoom - TTLEVEL1BITS)
	{
		TileIdx topleft = zti.toTileIdx(mp);
		TileSet *ts = getTileSet(topleft);
		if (ts == NULL)
			return 0;
		int64_t count = 0;
		int64_t size = 1 << (mp.baseZoom - zti.zoom);
		for (int64_t x = 0; x < size; x++)
			for (int64_t y = 0; y < size; y++)
				if (ts->isRequired(topleft + TileIdx(x, y)))
					count++;
		return count;
	}
	// if >= TileSet size, but < TileGroup size, get the TileGroup and check the sets individually
	if (zti.zoom > mp.baseZoom - TTLEVEL1BITS - TTLEVEL2BITS)
	{
		TileIdx topleft = zti.toTileIdx(mp);
		TileGroup *tg = getTileGroup(topleft);
		if (tg == NULL)
			return 0;
		int64_t count = 0;
		int64_t size = 1 << (mp.baseZoom - TTLEVEL1BITS - zti.zoom);
		for (int64_t x = 0; x < size; x++)
			for (int64_t y = 0; y < size; y++)
			{
				TileSet *ts = tg->getTileSet(topleft + TileIdx(x << TTLEVEL1BITS, y << TTLEVEL1BITS));
				if (ts != NULL)
					count += ts->bits.count();
			}
		return count;
	}
	// if >= TileGroup size, check the TileGroups individually
	TileIdx topleft = zti.toTileIdx(mp);
	int64_t count = 0;
	int64_t size = 1 << (mp.baseZoom - TTLEVEL1BITS - TTLEVEL2BITS - zti.zoom);
	for (int64_t x = 0; x < size; x++)
		for (int64_t y = 0; y < size; y++)
		{
			TileGroup *tg = getTileGroup(topleft + TileIdx(x << (TTLEVEL1BITS + TTLEVEL2BITS), y << (TTLEVEL1BITS + TTLEVEL2BITS)));
			if (tg != NULL)
				count += tg->reqcount;
		}
	return count;
}

void TileTable::copyFrom(const TileTable& ttable)
{
	for (int tgi = 0; tgi < TTLEVEL3SIZE*TTLEVEL3SIZE; tgi++)
	{
		if (ttable.tilegroups[tgi] != NULL)
		{
			tilegroups[tgi] = new TileGroup;
			for (int tsi = 0; tsi < TTLEVEL2SIZE*TTLEVEL2SIZE; tsi++)
			{
				if (ttable.tilegroups[tgi]->tilesets[tsi] != NULL)
				{
					tilegroups[tgi]->tilesets[tsi] = new TileSet(*(ttable.tilegroups[tgi]->tilesets[tsi]));
				}
			}
		}
	}
}



RequiredTileIterator::RequiredTileIterator(TileTable& ttable) : tiletable(ttable), current(-1,-1)
{
	// if the very first tile is required, use it
	ztgi = ztsi = zbi = 0;
	current = TileTable::toPosTileIdx(fromZOrder(ztgi, TTLEVEL3SIZE), fromZOrder(ztsi, TTLEVEL2SIZE), fromZOrder(zbi, TTLEVEL1SIZE)*TTDATASIZE);
	if (tiletable.isRequired(current))
	{
		end = false;
		return;
	}
	// ...otherwise, advance to the next one after it
	advance();
}

void RequiredTileIterator::advance()
{
	zbi++;
	for (; ztgi < TTLEVEL3SIZE*TTLEVEL3SIZE; ztgi++)
	{
		int tgi = fromZOrder(ztgi, TTLEVEL3SIZE);
		TileGroup *tg = tiletable.tilegroups[tgi];
		if (tg == NULL)
			continue;
		for (; ztsi < TTLEVEL2SIZE*TTLEVEL2SIZE; ztsi++)
		{
			int tsi = fromZOrder(ztsi, TTLEVEL2SIZE);
			TileSet *ts = tg->tilesets[tsi];
			if (ts == NULL)
				continue;
			for (; zbi < TTLEVEL1SIZE*TTLEVEL1SIZE; zbi++)
			{
				int bi = fromZOrder(zbi, TTLEVEL1SIZE);
				if (ts->bits[bi*TTDATASIZE])
				{
					end = false;
					current = tiletable.toPosTileIdx(tgi, tsi, bi*TTDATASIZE);
					return;
				}
			}
			zbi = 0;
		}
		ztsi = 0;
		zbi = 0;
	}
	end = true;
}



ZoomTileIdx getZoomTile(int tgi, const MapParams& mp)
{
	TileIdx ti = TileTable::toPosTileIdx(tgi, 0, 0).toTileIdx();
	ZoomTileIdx zti = ti.toZoomTileIdx(mp);
	return zti.toZoom(mp.baseZoom - TTLEVEL1BITS - TTLEVEL2BITS);
}

TileGroupIterator::TileGroupIterator(TileTable& ttable, const MapParams& mparams)
	: tiletable(ttable), mp(mparams), zti(-1,-1,-1)
{
	// if the very first TileGroup is non-NULL, use it
	tgi = 0;
	zti = getZoomTile(tgi, mp);
	end = false;
	if (tiletable.tilegroups[tgi] != NULL)
		return;
	// ...otherwise, advance to the next one
	advance();
}

void TileGroupIterator::advance()
{
	tgi++;
	for (; tgi < TTLEVEL3SIZE*TTLEVEL3SIZE; tgi++)
	{
		if (tiletable.tilegroups[tgi] != NULL)
		{
			zti = getZoomTile(tgi, mp);
			return;
		}
	}
	end = true;
}





void RegionGroup::setRequired(const PosRegionIdx& ri)
{
	int rsi = regionSetIdx(ri);
	if (regionsets[rsi] == NULL)
		regionsets[rsi] = new RegionSet;
	regionsets[rsi]->setRequired(ri);
}

void RegionGroup::setFailed(const PosRegionIdx& ri)
{
	int rsi = regionSetIdx(ri);
	if (regionsets[rsi] == NULL)
		regionsets[rsi] = new RegionSet;
	regionsets[rsi]->setFailed(ri);
}

PosRegionIdx RegionTable::toPosRegionIdx(int rgi, int rsi, int bi)
{
	PosRegionIdx ri(0,0);
	ri.x += (rgi % RTLEVEL3SIZE) * RTLEVEL1SIZE * RTLEVEL2SIZE;
	ri.z += (rgi / RTLEVEL3SIZE) * RTLEVEL1SIZE * RTLEVEL2SIZE;
	ri.x += (rsi % RTLEVEL2SIZE) * RTLEVEL1SIZE;
	ri.z += (rsi / RTLEVEL2SIZE) * RTLEVEL1SIZE;
	ri.x += ((bi / RTDATASIZE) % RTLEVEL1SIZE);
	ri.z += ((bi / RTDATASIZE) / RTLEVEL1SIZE);
	return ri;
}

void RegionTable::setRequired(const PosRegionIdx& ri)
{
	int rgi = regionGroupIdx(ri);
	if (regiongroups[rgi] == NULL)
		regiongroups[rgi] = new RegionGroup;
	regiongroups[rgi]->setRequired(ri);
}

void RegionTable::setFailed(const PosRegionIdx& ri)
{
	int rgi = regionGroupIdx(ri);
	if (regiongroups[rgi] == NULL)
		regiongroups[rgi] = new RegionGroup;
	regiongroups[rgi]->setFailed(ri);
}

void RegionTable::copyFrom(const RegionTable& rtable)
{
	for (int rgi = 0; rgi < RTLEVEL3SIZE*RTLEVEL3SIZE; rgi++)
	{
		if (rtable.regiongroups[rgi] != NULL)
		{
			regiongroups[rgi] = new RegionGroup;
			for (int rsi = 0; rsi < RTLEVEL2SIZE*RTLEVEL2SIZE; rsi++)
			{
				if (rtable.regiongroups[rgi]->regionsets[rsi] != NULL)
				{
					regiongroups[rgi]->regionsets[rsi] = new RegionSet(*(rtable.regiongroups[rgi]->regionsets[rsi]));
				}
			}
		}
	}
}
