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
#include <fstream>

#include "blockimages.h"
#include "utils.h"

using namespace std;


// in this file, confusingly, "tile" refers to the tiles of terrain.png, not to the map tiles
//
// also, this is a nasty mess in here; apologies to anyone reading this


void writeBlockImagesVersion(int B, const string& imgpath, int32_t version)
{
	string versionfile = imgpath + "/blocks-" + tostring(B) + ".version";
	ofstream outfile(versionfile.c_str());
	outfile << version;
}

// get the version number associated with blocks-B.png; this is stored
//  in blocks-B.version, which is just a single string with the version number
int getBlockImagesVersion(int B, const string& imgpath)
{
	string versionfile = imgpath + "/blocks-" + tostring(B) + ".version";
	ifstream infile(versionfile.c_str());
	// if there's no version file, assume the version is 157, which is how many
	//  blocks there were at the first "release" (before the version file was in use)
	if (infile.fail())
	{
		infile.close();
		writeBlockImagesVersion(B, imgpath, 157);
		return 157;
	}
	// otherwise, read the version
	int32_t v;
	infile >> v;
	// if the version is clearly insane, ignore it
	if (v < 0 || v > 1000)
		v = 0;
	return v;
}



bool BlockImages::create(int B, const string& imgpath)
{
	rectsize = 4*B;
	setOffsets();

	// first, see if blocks-B.png exists, and what its version is
	int biversion = getBlockImagesVersion(B, imgpath);
	string blocksfile = imgpath + "/blocks-" + tostring(B) + ".png";
	RGBAImage oldimg;
	bool preserveold = false;
	if (img.readPNG(blocksfile))
	{
		// if it's the correct size and version, we're okay
		int w = rectsize*16, h = (NUMBLOCKIMAGES/16 + 1) * rectsize;
		if (img.w == w && img.h == h && biversion == NUMBLOCKIMAGES)
		{
			retouchAlphas(B);
			checkOpacityAndTransparency(B);
			return true;
		}
		// if it's a previous version (and the correct size for that version), we'll
		//  use terrain.png to build the new blocks, but preserve the existing ones
		if (biversion < NUMBLOCKIMAGES && img.w == w && img.h == (biversion/16 + 1) * rectsize)
		{
			oldimg = img;
			preserveold = true;
			cerr << blocksfile << " is missing some blocks; will try to fill them in from terrain.png" << endl;
		}
		// otherwise, the file's been trashed somehow; rebuild it
		else
		{
			cerr << blocksfile << " has incorrect size (expected " << w << "x" << h << endl;
			cerr << "...will try to create from terrain.png, but without overwriting " << blocksfile << endl;
		}
	}
	else
		cerr << blocksfile << " not found (or failed to read as PNG); will try to build from terrain.png" << endl;

	// build blocks-B.png from terrain.png and fire.png
	string terrainfile = imgpath + "/terrain.png";
	string firefile = imgpath + "/fire.png";
	if (!construct(B, terrainfile, firefile))
	{
		cerr << "couldn't find terrain.png and/or fire.png" << endl;
		return false;
	}

	// if we need to preserve the old version's blocks, copy them over
	if (preserveold)
	{
		for (int i = 0; i < biversion; i++)
		{
			ImageRect rect = getRect(i);
			blit(oldimg, rect, img, rect.x, rect.y);
		}
	}

	// write blocks-B.png and blocks-B.version
	img.writePNG(blocksfile);
	writeBlockImagesVersion(B, imgpath, NUMBLOCKIMAGES);

	retouchAlphas(B);
	checkOpacityAndTransparency(B);
	return true;
}




// given terrain.png, resize it so every texture becomes 2Bx2B instead of 16x16 (or whatever the actual
//  texture size is)
// ...so the resulting image will be a 16x16 array of 2Bx2B images
RGBAImage getResizedTerrain(const RGBAImage& terrain, int terrainSize, int B)
{
	int newsize = 2*B;
	RGBAImage img;
	img.create(16*newsize, 16*newsize);
	for (int y = 0; y < 16; y++)
		for (int x = 0; x < 16; x++)
			resize(terrain, ImageRect(x*terrainSize, y*terrainSize, terrainSize, terrainSize),
			       img, ImageRect(x*newsize, y*newsize, newsize, newsize));
	return img;
}


// iterate over the pixels of a 2B-sized terrain tile; used for both source rectangles and
//  destination parallelograms
struct FaceIterator
{
	bool end;  // true if we're done
	int x, y;  // current pixel
	int pos;

	int size;  // number of columns to draw, as well as number of pixels in each
	int deltaY;  // amount to skew y-coord every 2 columns: -1 or 1 for E/W or N/S facing destinations, 0 for source

	FaceIterator(int xstart, int ystart, int dY, int sz)
	{
		size = sz;
		deltaY = dY;
		end = false;
		x = xstart;
		y = ystart;
		pos = 0;
	}

	void advance()
	{
		pos++;
		if (pos >= size*size)
		{
			end = true;
			return;
		}
		y++;
		if (pos % size == 0)
		{
			x++;
			y -= size;
			if (pos % (2*size) == size)
				y += deltaY;
		}
	}
};

// like FaceIterator with no deltaY (for source rectangles), but with the source rotated and/or flipped
struct RotatedFaceIterator
{
	bool end;
	int x, y;
	int pos;

	int size;
	int rot; // 0 = down, then right; 1 = left, then down; 2 = up, then left; 3 = right, then up
	bool flipX;
	int dx1, dy1, dx2, dy2;

	RotatedFaceIterator(int xstart, int ystart, int r, int sz, bool fX)
	{
		size = sz;
		rot = r;
		flipX = fX;
		end = false;
		pos = 0;
		if (rot == 0)
		{
			x = flipX ? (xstart + size - 1) : xstart;
			y = ystart;
			dx1 = 0;
			dy1 = 1;
			dx2 = flipX ? -1 : 1;
			dy2 = 0;
		}
		else if (rot == 1)
		{
			x = flipX ? xstart : (xstart + size - 1);
			y = ystart;
			dx1 = flipX ? 1 : -1;
			dy1 = 0;
			dx2 = 0;
			dy2 = 1;
		}
		else if (rot == 2)
		{
			x = flipX ? xstart : (xstart + size - 1);
			y = ystart + size - 1;
			dx1 = 0;
			dy1 = -1;
			dx2 = flipX ? 1 : -1;
			dy2 = 0;
		}
		else
		{
			x = flipX ? (xstart + size - 1) : xstart;
			y = ystart + size - 1;
			dx1 = flipX ? -1 : 1;
			dy1 = 0;
			dx2 = 0;
			dy2 = -1;
		}
	}

	void advance()
	{
		pos++;
		if (pos >= size*size)
		{
			end = true;
			return;
		}
		x += dx1;
		y += dy1;
		if (pos % size == 0)
		{
			x += dx2;
			y += dy2;
			x -= dx1 * size;
			y -= dy1 * size;
		}
	}
};

// iterate over the pixels of the top face of a block
struct TopFaceIterator
{
	bool end;  // true if we're done
	int x, y;  // current pixel
	int pos;

	int size;  // number of "columns", and number of pixels in each

	TopFaceIterator(int xstart, int ystart, int sz)
	{
		size = sz;
		end = false;
		x = xstart;
		y = ystart;
		pos = 0;
	}

	void advance()
	{
		if ((pos/size) % 2 == 0)
		{
			int m = pos % size;
			if (m == size - 1)
			{
				x += size - 1;
				y -= size/2;
			}
			else if (m == size - 2)
				y++;
			else if (m % 2 == 0)
			{
				x--;
				y++;
			}
			else
				x--;
		}
		else
		{
			int m = pos % size;
			if (m == 0)
				y++;
			else if (m == size - 1)
			{
				x += size - 1;
				y -= size/2 - 1;
			}
			else if (m % 2 == 0)
			{
				x--;
				y++;
			}
			else
				x--;
		}
		pos++;
		if (pos >= size*size)
			end = true;
	}
};



// draw a "normal" block image, using three terrain tiles, and adding a bit of shadow to the N and W faces
// ...can skip faces by passing -1
void drawBlockImage(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int Nface, int Wface, int Uface, int B)
{
	int tilesize = 2*B;
	// N face starts at [0,B]
	if (Nface != -1)
	{
		for (FaceIterator srcit((Nface%16)*tilesize, (Nface/16)*tilesize, 0, tilesize),
			dstit(drect.x, drect.y + B, 1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
		{
			dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y), 0.9, 0.9, 0.9);
		}
	}
	// W face starts at [2B,2B]
	if (Wface != -1)
	{
		for (FaceIterator srcit((Wface%16)*tilesize, (Wface/16)*tilesize, 0, tilesize),
			dstit(drect.x + 2*B, drect.y + 2*B, -1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
		{
			dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y), 0.8, 0.8, 0.8);
		}
	}
	// U face starts at [2B-1,0]
	if (Uface != -1)
	{
		TopFaceIterator dstit(drect.x + 2*B-1, drect.y, tilesize);
		for (FaceIterator srcit((Uface%16)*tilesize, (Uface/16)*tilesize, 0, tilesize); !srcit.end; srcit.advance(), dstit.advance())
		{
			dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y);
		}
	}
}

// draw a block image where the block isn't full height (half-steps, snow, etc.)
// ...supplied fraction should be from 0 to 1, and describes how much of the block should be visible
//  (we take the top portion of the block, but draw it towards the bottom; for example, if fraction is 0.25,
//  it will appear that the block has been driven 3/4 of the way into the ground)
// U face can also be rotated, and N/W faces can be X-flipped (set 0x1 for N, 0x2 for W)
void drawPartialBlockImage(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int Nface, int Wface, int Uface, int B, double fraction, int rot, int flip)
{
	int tilesize = 2*B;
	// determine how many pixels to chop off the top of the N and W faces
	int cutoff = max(0, min(tilesize - 1, (int)(fraction * tilesize)));
	// N face starts at [0,B]
	if (Nface != -1)
	{
		FaceIterator dstit(drect.x, drect.y + B, 1, tilesize);
		for (RotatedFaceIterator srcit((Nface%16)*tilesize, (Nface/16)*tilesize, 0, tilesize, flip & 0x1); !srcit.end; srcit.advance(), dstit.advance())
		{
			if (dstit.pos % tilesize >= cutoff)
			{
				dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y - cutoff);
				darken(dest(dstit.x, dstit.y), 0.9, 0.9, 0.9);
			}
		}
	}
	// W face starts at [2B,2B]
	if (Wface != -1)
	{
		FaceIterator dstit(drect.x + 2*B, drect.y + 2*B, -1, tilesize);
		for (RotatedFaceIterator srcit((Wface%16)*tilesize, (Wface/16)*tilesize, 0, tilesize, flip & 0x2); !srcit.end; srcit.advance(), dstit.advance())
		{
			if (dstit.pos % tilesize >= cutoff)
			{
				dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y - cutoff);
				darken(dest(dstit.x, dstit.y), 0.8, 0.8, 0.8);
			}
		}
	}
	// U face starts at [2B-1,cutoff]
	if (Uface != -1)
	{
		TopFaceIterator dstit(drect.x + 2*B-1, drect.y + cutoff, tilesize);
		for (RotatedFaceIterator srcit((Uface%16)*tilesize, (Uface/16)*tilesize, rot, tilesize, false); !srcit.end; srcit.advance(), dstit.advance())
		{
			dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y);
		}
	}
}

// draw two flat copies of a tile intersecting at the block center (saplings, etc.)
void drawItemBlockImage(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int tile, int B)
{
	int tilesize = 2*B;
	// E/W face starting at [B,1.5B]
	for (FaceIterator srcit((tile%16)*tilesize, (tile/16)*tilesize, 0, tilesize),
	     dstit(drect.x + B, drect.y + B*3/2, -1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		blend(dest(dstit.x, dstit.y), tiles(srcit.x, srcit.y));
	}
	// N/S face starting at [B,0.5B]
	for (FaceIterator srcit((tile%16)*tilesize, (tile/16)*tilesize, 0, tilesize),
	     dstit(drect.x + B, drect.y + B/2, 1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		blend(dest(dstit.x, dstit.y), tiles(srcit.x, srcit.y));
	}
}

// draw a tile on a single upright face
// 0 = S, 1 = N, 2 = W, 3 = E
// ...handles transparency
void drawSingleFaceBlockImage(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int tile, int face, int B)
{
	int tilesize = 2*B;
	int xoff, yoff, deltaY;
	if (face == 0)
	{
		xoff = 2*B;
		yoff = 0;
		deltaY = 1;
	}
	else if (face == 1)
	{
		xoff = 0;
		yoff = B;
		deltaY = 1;
	}
	else if (face == 2)
	{
		xoff = 2*B;
		yoff = 2*B;
		deltaY = -1;
	}
	else
	{
		xoff = 0;
		yoff = B;
		deltaY = -1;
	}
	for (FaceIterator srcit((tile%16)*tilesize, (tile/16)*tilesize, 0, tilesize),
	     dstit(drect.x + xoff, drect.y + yoff, deltaY, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		blend(dest(dstit.x, dstit.y), tiles(srcit.x, srcit.y));
	}
}

// draw part of a tile on a single upright face
// 0 = S, 1 = N, 2 = W, 3 = E
// ...handles transparency
void drawPartialSingleFaceBlockImage(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int tile, int face, int B, double fstartv, double fendv, double fstarth, double fendh)
{
	int tilesize = 2*B;
	int vstartcutoff = max(0, min(tilesize, (int)(fstartv * tilesize)));
	int vendcutoff = max(0, min(tilesize, (int)(fendv * tilesize)));
	int hstartcutoff = max(0, min(tilesize, (int)(fstarth * tilesize)));
	int hendcutoff = max(0, min(tilesize, (int)(fendh * tilesize)));
	int xoff, yoff, deltaY;
	if (face == 0)
	{
		xoff = 2*B;
		yoff = 0;
		deltaY = 1;
	}
	else if (face == 1)
	{
		xoff = 0;
		yoff = B;
		deltaY = 1;
	}
	else if (face == 2)
	{
		xoff = 2*B;
		yoff = 2*B;
		deltaY = -1;
	}
	else
	{
		xoff = 0;
		yoff = B;
		deltaY = -1;
	}
	for (FaceIterator srcit((tile%16)*tilesize, (tile/16)*tilesize, 0, tilesize),
	     dstit(drect.x + xoff, drect.y + yoff, deltaY, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		if (dstit.pos % tilesize >= vstartcutoff && dstit.pos % tilesize < vendcutoff &&
		    dstit.pos / tilesize >= hstartcutoff && dstit.pos / tilesize < hendcutoff)
			blend(dest(dstit.x, dstit.y), tiles(srcit.x, srcit.y));
	}
}

// draw a single tile on the floor, possibly with rotation
// 0 = top of tile is on S side; 1 = W, 2 = N, 3 = E
void drawFloorBlockImage(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int tile, int rot, int B)
{
	int tilesize = 2*B;
	TopFaceIterator dstit(drect.x + 2*B-1, drect.y + 2*B, tilesize);
	for (RotatedFaceIterator srcit((tile%16)*tilesize, (tile/16)*tilesize, rot, tilesize, false); !srcit.end; srcit.advance(), dstit.advance())
	{
		dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y);
	}
}

// draw part of a single tile on the floor
void drawPartialFloorBlockImage(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int tile, int B, double fstartv, double fendv, double fstarth, double fendh)
{
	int tilesize = 2*B;
	int vstartcutoff = max(0, min(tilesize, (int)(fstartv * tilesize)));
	int vendcutoff = max(0, min(tilesize, (int)(fendv * tilesize)));
	int hstartcutoff = max(0, min(tilesize, (int)(fstarth * tilesize)));
	int hendcutoff = max(0, min(tilesize, (int)(fendh * tilesize)));
	TopFaceIterator dstit(drect.x + 2*B-1, drect.y + 2*B, tilesize);
	for (FaceIterator srcit((tile%16)*tilesize, (tile/16)*tilesize, 0, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		if (srcit.pos % tilesize >= vstartcutoff && srcit.pos % tilesize < vendcutoff &&
		    srcit.pos / tilesize >= hstartcutoff && srcit.pos / tilesize < hendcutoff)
			dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y);
	}
}

// draw a single tile on the floor, possibly with rotation, angled upwards
// rot: 0 = top of tile is on S side; 1 = W, 2 = N, 3 = E
// up: 0 = S side of tile is highest; 1 = W, 2 = N, 3 = E
void drawAngledFloorBlockImage(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int tile, int rot, int up, int B)
{
	int tilesize = 2*B;
	TopFaceIterator dstit(drect.x + 2*B-1, drect.y + 2*B, tilesize);
	for (RotatedFaceIterator srcit((tile%16)*tilesize, (tile/16)*tilesize, rot, tilesize, false); !srcit.end; srcit.advance(), dstit.advance())
	{
		int yoff = 0;
		int row = srcit.pos % tilesize, col = srcit.pos / tilesize;
		if (up == 0)
			yoff = tilesize - 1 - row;
		else if (up == 1)
			yoff = col;
		else if (up == 2)
			yoff = row;
		else if (up == 3)
			yoff = tilesize - 1 - col;
		blend(dest(dstit.x, dstit.y - yoff), tiles(srcit.x, srcit.y));
		blend(dest(dstit.x, dstit.y - yoff + 1), tiles(srcit.x, srcit.y));
	}
}

// draw a single tile on the ceiling, possibly with rotation
// 0 = top of tile is on S side; 1 = W, 2 = N, 3 = E
void drawCeilBlockImage(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int tile, int rot, int B)
{
	int tilesize = 2*B;
	TopFaceIterator dstit(drect.x + 2*B-1, drect.y, tilesize);
	for (RotatedFaceIterator srcit((tile%16)*tilesize, (tile/16)*tilesize, rot, tilesize, false); !srcit.end; srcit.advance(), dstit.advance())
	{
		dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y);
	}
}

// draw a block image that's just a single color (plus shadows)
void drawSolidColorBlockImage(RGBAImage& dest, const ImageRect& drect, RGBAPixel p, int B)
{
	int tilesize = 2*B;
	// N face starts at [0,B]
	for (FaceIterator dstit(drect.x, drect.y + B, 1, tilesize); !dstit.end; dstit.advance())
	{
		dest(dstit.x, dstit.y) = p;
		darken(dest(dstit.x, dstit.y), 0.9, 0.9, 0.9);
	}
	// W face starts at [2B,2B]
	for (FaceIterator dstit(drect.x + 2*B, drect.y + 2*B, -1, tilesize); !dstit.end; dstit.advance())
	{
		dest(dstit.x, dstit.y) = p;
		darken(dest(dstit.x, dstit.y), 0.8, 0.8, 0.8);
	}
	// U face starts at [2B-1,0]
	for (TopFaceIterator dstit(drect.x + 2*B-1, drect.y, tilesize); !dstit.end; dstit.advance())
	{
		dest(dstit.x, dstit.y) = p;
	}
}

// draw S-ascending stairs
void drawStairsS(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int tile, int B)
{
	int tilesize = 2*B;
	// normal N face starts at [0,B]; draw the bottom half of it
	for (FaceIterator srcit((tile%16)*tilesize, (tile/16)*tilesize, 0, tilesize),
	     dstit(drect.x, drect.y + B, 1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		if (dstit.pos % tilesize >= B)
		{
			dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y), 0.9, 0.9, 0.9);
		}
	}
	// normal W face starts at [2B,2B]; draw all but the upper-left quarter of it
	for (FaceIterator srcit((tile%16)*tilesize, (tile/16)*tilesize, 0, tilesize),
	     dstit(drect.x + 2*B, drect.y + 2*B, -1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		if (dstit.pos % tilesize >= B || dstit.pos / tilesize >= B)
		{
			dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y), 0.8, 0.8, 0.8);
		}
	}
	// normal U face starts at [2B-1,0]; draw the top half of it
	TopFaceIterator tdstit(drect.x + 2*B-1, drect.y, tilesize);
	for (FaceIterator srcit((tile%16)*tilesize, (tile/16)*tilesize, 0, tilesize); !srcit.end; srcit.advance(), tdstit.advance())
	{
		// if B is odd, we need B pixels from each column, but if it's even, we need to alternate between
		//  B-1 and B+1
		int cutoff = B;
		if (B % 2 == 0)
			cutoff += ((tdstit.pos / tilesize) % 2 == 0) ? -1 : 1;
		if (tdstit.pos % tilesize < cutoff)
		{
			dest(tdstit.x, tdstit.y) = tiles(srcit.x, srcit.y);
		}
	}
	// draw the top half of another N face at [B,B/2]
	for (FaceIterator srcit((tile%16)*tilesize, (tile/16)*tilesize, 0, tilesize),
	     dstit(drect.x + B, drect.y + B/2, 1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		// ...but if B is odd, we need to add an extra [0,1] to the even-numbered columns
		int adjust = 0;
		if (B % 2 == 1 && (dstit.pos / tilesize) % 2 == 0)
			adjust = 1;
		if (dstit.pos % tilesize < B)
		{
			dest(dstit.x, dstit.y + adjust) = tiles(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y + adjust), 0.9, 0.9, 0.9);
		}
	}
	// draw the bottom half of another U face at [2B-1,B]
	tdstit = TopFaceIterator(drect.x + 2*B-1, drect.y + B, tilesize);
	for (FaceIterator srcit((tile%16)*tilesize, (tile/16)*tilesize, 0, tilesize); !srcit.end; srcit.advance(), tdstit.advance())
	{
		// again, if B is odd, take B pixels from each column; if even, take B-1 or B+1
		int cutoff = B;
		if (B % 2 == 0)
			cutoff += ((tdstit.pos / tilesize) % 2 == 0) ? -1 : 1;
		if (tdstit.pos % tilesize >= cutoff)
		{
			dest(tdstit.x, tdstit.y) = tiles(srcit.x, srcit.y);
		}
	}
}

// draw N-ascending stairs
void drawStairsN(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int tile, int B)
{
	int tilesize = 2*B;
	// draw the top half of an an U face at [2B-1,B]
	TopFaceIterator tdstit(drect.x + 2*B-1, drect.y + B, tilesize);
	for (FaceIterator srcit((tile%16)*tilesize, (tile/16)*tilesize, 0, tilesize); !srcit.end; srcit.advance(), tdstit.advance())
	{
		// if B is odd, we need B pixels from each column, but if it's even, we need to alternate between
		//  B-1 and B+1
		int cutoff = B;
		if (B % 2 == 0)
			cutoff += ((tdstit.pos / tilesize) % 2 == 0) ? -1 : 1;
		if (tdstit.pos % tilesize < cutoff)
		{
			dest(tdstit.x, tdstit.y) = tiles(srcit.x, srcit.y);
		}
	}
	// draw the bottom half of the normal U face at [2B-1,0]
	tdstit = TopFaceIterator(drect.x + 2*B-1, drect.y, tilesize);
	for (FaceIterator srcit((tile%16)*tilesize, (tile/16)*tilesize, 0, tilesize); !srcit.end; srcit.advance(), tdstit.advance())
	{
		// again, if B is odd, take B pixels from each column; if even, take B-1 or B+1
		int cutoff = B;
		if (B % 2 == 0)
			cutoff += ((tdstit.pos / tilesize) % 2 == 0) ? -1 : 1;
		if (tdstit.pos % tilesize >= cutoff)
		{
			dest(tdstit.x, tdstit.y) = tiles(srcit.x, srcit.y);
		}
	}
	// normal N face starts at [0,B]; draw it all
	for (FaceIterator srcit((tile%16)*tilesize, (tile/16)*tilesize, 0, tilesize),
	     dstit(drect.x, drect.y + B, 1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y);
		darken(dest(dstit.x, dstit.y), 0.9, 0.9, 0.9);
	}
	// normal W face starts at [2B,2B]; draw all but the upper-right quarter of it
	for (FaceIterator srcit((tile%16)*tilesize, (tile/16)*tilesize, 0, tilesize),
	     dstit(drect.x + 2*B, drect.y + 2*B, -1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		if (dstit.pos % tilesize >= B || dstit.pos / tilesize < B)
		{
			dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y), 0.8, 0.8, 0.8);
		}
	}
}

// draw E-ascending stairs
void drawStairsE(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int tile, int B)
{
	int tilesize = 2*B;
	// normal N face starts at [0,B]; draw all but the upper-right quarter of it
	for (FaceIterator srcit((tile%16)*tilesize, (tile/16)*tilesize, 0, tilesize),
	     dstit(drect.x, drect.y + B, 1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		if (dstit.pos % tilesize >= B || dstit.pos / tilesize < B)
		{
			dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y), 0.9, 0.9, 0.9);
		}
	}
	// normal W face starts at [2B,2B]; draw the bottom half of it
	for (FaceIterator srcit((tile%16)*tilesize, (tile/16)*tilesize, 0, tilesize),
	     dstit(drect.x + 2*B, drect.y + 2*B, -1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		if (dstit.pos % tilesize >= B)
		{
			dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y), 0.8, 0.8, 0.8);
		}
	}
	// normal U face starts at [2B-1,0]; draw the left half of it
	TopFaceIterator tdstit(drect.x + 2*B-1, drect.y, tilesize);
	int tcutoff = tilesize * B;
	bool textra = false;
	// if B is odd, we need to skip the last pixel of the last left-half column, and add the very first
	//  pixel of the first right-half column
	if (B % 2 == 1)
	{
		tcutoff--;
		textra = true;
	}
	for (FaceIterator srcit((tile%16)*tilesize, (tile/16)*tilesize, 0, tilesize); !srcit.end; srcit.advance(), tdstit.advance())
	{
		if (tdstit.pos < tcutoff || (textra && tdstit.pos == tcutoff + 1))
		{
			dest(tdstit.x, tdstit.y) = tiles(srcit.x, srcit.y);
		}
	}
	// draw the top half of another W face at [B,1.5B]
	for (FaceIterator srcit((tile%16)*tilesize, (tile/16)*tilesize, 0, tilesize),
	     dstit(drect.x + B, drect.y + 3*B/2, -1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		// ...but if B is odd, we need to add an extra [0,1] to the odd-numbered columns
		int adjust = 0;
		if (B % 2 == 1 && (dstit.pos / tilesize) % 2 == 1)
			adjust = 1;
		if (dstit.pos % tilesize < B)
		{
			dest(dstit.x, dstit.y + adjust) = tiles(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y + adjust), 0.8, 0.8, 0.8);
		}
	}
	// draw the right half of another U face at [2B-1,B]
	tdstit = TopFaceIterator(drect.x + 2*B-1, drect.y + B, tilesize);
	tcutoff = tilesize * B;
	textra = false;
	// if B is odd, do the reverse of what we did with the top half
	if (B % 2 == 1)
	{
		tcutoff++;
		textra = true;
	}
	for (FaceIterator srcit((tile%16)*tilesize, (tile/16)*tilesize, 0, tilesize); !srcit.end; srcit.advance(), tdstit.advance())
	{
		if (tdstit.pos >= tcutoff || (textra && tdstit.pos == tcutoff - 2))
		{
			dest(tdstit.x, tdstit.y) = tiles(srcit.x, srcit.y);
		}
	}
}

// draw W-ascending stairs
void drawStairsW(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int tile, int B)
{
	int tilesize = 2*B;
	// draw the left half of an U face at [2B-1,B]
	TopFaceIterator tdstit(drect.x + 2*B-1, drect.y + B, tilesize);
	int tcutoff = tilesize * B;
	bool textra = false;
	// if B is odd, we need to skip the last pixel of the last left-half column, and add the very first
	//  pixel of the first right-half column
	if (B % 2 == 1)
	{
		tcutoff--;
		textra = true;
	}
	for (FaceIterator srcit((tile%16)*tilesize, (tile/16)*tilesize, 0, tilesize); !srcit.end; srcit.advance(), tdstit.advance())
	{
		if (tdstit.pos < tcutoff || (textra && tdstit.pos == tcutoff + 1))
		{
			dest(tdstit.x, tdstit.y) = tiles(srcit.x, srcit.y);
		}
	}
	// draw the right half of the normal U face at [2B-1,0]
	tdstit = TopFaceIterator(drect.x + 2*B-1, drect.y, tilesize);
	tcutoff = tilesize * B;
	textra = false;
	// if B is odd, do the reverse of what we did with the top half
	if (B % 2 == 1)
	{
		tcutoff++;
		textra = true;
	}
	for (FaceIterator srcit((tile%16)*tilesize, (tile/16)*tilesize, 0, tilesize); !srcit.end; srcit.advance(), tdstit.advance())
	{
		if (tdstit.pos >= tcutoff || (textra && tdstit.pos == tcutoff - 2))
		{
			dest(tdstit.x, tdstit.y) = tiles(srcit.x, srcit.y);
		}
	}
	// normal N face starts at [0,B]; draw all but the upper-left quarter of it
	for (FaceIterator srcit((tile%16)*tilesize, (tile/16)*tilesize, 0, tilesize),
	     dstit(drect.x, drect.y + B, 1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		if (dstit.pos % tilesize >= B || dstit.pos / tilesize >= B)
		{
			dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y), 0.9, 0.9, 0.9);
		}
	}
	// normal W face starts at [2B,2B]; draw the whole thing
	for (FaceIterator srcit((tile%16)*tilesize, (tile/16)*tilesize, 0, tilesize),
	     dstit(drect.x + 2*B, drect.y + 2*B, -1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y);
		darken(dest(dstit.x, dstit.y), 0.8, 0.8, 0.8);
	}
}

// draw crappy fence post
void drawFencePost(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int tile, int B)
{
	int tilesize = 2*B;
	int tilex = (tile%16)*tilesize, tiley = (tile/16)*tilesize;

	// draw a 2x2 top at [2B-1,B-1]
	for (int y = 0; y < 2; y++)
		for (int x = 0; x < 2; x++)
			dest(drect.x + 2*B - 1 + x, drect.y + B - 1 + y) = tiles(tilex + x, tiley + y);

	// draw a 1x2B side at [2B-1,B+1]
	for (int y = 0; y < 2*B; y++)
		dest(drect.x + 2*B - 1, drect.y + B + 1 + y) = tiles(tilex, tiley + y);

	// draw a 1x2B side at [2B,B+1]
	for (int y = 0; y < 2*B; y++)
		dest(drect.x + 2*B, drect.y + B + 1 + y) = tiles(tilex, tiley + y);
}

// draw fence: post, plus maybe some rails
void drawFence(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int tile, bool N, bool S, bool E, bool W, int B)
{
	// first, E and S rails, since the post should be in front of them
	int tilesize = 2*B;
	if (E)
	{
		// N/S face starting at [B,0.5B]; left half, one strip
		for (FaceIterator srcit((tile%16)*tilesize, (tile/16)*tilesize, 0, tilesize),
			dstit(drect.x + B, drect.y + B/2, 1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
		{
			if (dstit.pos / tilesize < B && (((dstit.pos % tilesize) * 2 / B) % 4) == 1)
				dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y);
		}
	}
	if (S)
	{
		// E/W face starting at [B,1.5B]; right half, one strip
		for (FaceIterator srcit((tile%16)*tilesize, (tile/16)*tilesize, 0, tilesize),
			dstit(drect.x + B, drect.y + B*3/2, -1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
		{
			if (dstit.pos / tilesize >= B && (((dstit.pos % tilesize) * 2 / B) % 4) == 1)
				dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y);
		}
	}

	// now the post
	drawFencePost(dest, drect, tiles, tile, B);

	// now the N and W rails
	if (W)
	{
		// N/S face starting at [B,0.5B]; right half, one strip
		for (FaceIterator srcit((tile%16)*tilesize, (tile/16)*tilesize, 0, tilesize),
			dstit(drect.x + B, drect.y + B/2, 1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
		{
			if (dstit.pos / tilesize >= B && (((dstit.pos % tilesize) * 2 / B) % 4) == 1)
				dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y);
		}
	}
	if (N)
	{
		// E/W face starting at [B,1.5B]; left half, one strip
		for (FaceIterator srcit((tile%16)*tilesize, (tile/16)*tilesize, 0, tilesize),
			dstit(drect.x + B, drect.y + B*3/2, -1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
		{
			if (dstit.pos / tilesize < B && (((dstit.pos % tilesize) * 2 / B) % 4) == 1)
				dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y);
		}
	}
}

// draw crappy sign facing out towards the viewer
void drawSign(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int tile, int B)
{
	// start with fence post
	drawFencePost(dest, drect, tiles, tile, B);

	int tilesize = 2*B;
	// draw the top half of a tile at [B,B]
	for (FaceIterator srcit((tile%16)*tilesize, (tile/16)*tilesize, 0, tilesize),
	     dstit(drect.x + B, drect.y + B, 0, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		if (dstit.pos % tilesize < B)
			dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y);
	}
}

// draw crappy wall lever
void drawWallLever(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int face, int B)
{
	drawPartialSingleFaceBlockImage(dest, drect, tiles, 16, face, B, 0.5, 1, 0.35, 0.65);
	drawSingleFaceBlockImage(dest, drect, tiles, 96, face, B);
}

void drawFloorLeverNS(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int B)
{
	drawPartialFloorBlockImage(dest, drect, tiles, 16, B, 0.25, 0.75, 0.35, 0.65);
	drawItemBlockImage(dest, drect, tiles, 96, B);
}

void drawFloorLeverEW(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int B)
{
	drawPartialFloorBlockImage(dest, drect, tiles, 16, B, 0.35, 0.65, 0.25, 0.75);
	drawItemBlockImage(dest, drect, tiles, 96, B);
}

void drawRepeater(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int tile, int rot, int B)
{
	drawFloorBlockImage(dest, drect, tiles, tile, rot, B);
	drawItemBlockImage(dest, drect, tiles, 99, B);
}

void drawFire(RGBAImage& dest, const ImageRect& drect, const RGBAImage& firetile, int B)
{
	drawSingleFaceBlockImage(dest, drect, firetile, 0, 0, B);
	drawSingleFaceBlockImage(dest, drect, firetile, 0, 3, B);
	drawSingleFaceBlockImage(dest, drect, firetile, 0, 1, B);
	drawSingleFaceBlockImage(dest, drect, firetile, 0, 2, B);
}




int offsetIdx(uint8_t blockID, uint8_t blockData)
{
	return blockID * 16 + blockData;
}

void setOffsetsForID(uint8_t blockID, int offset, BlockImages& bi)
{
	int start = blockID * 16;
	int end = start + 16;
	fill(bi.blockOffsets + start, bi.blockOffsets + end, offset);
}

void BlockImages::setOffsets()
{
	// default is the dummy image
	fill(blockOffsets, blockOffsets + 256*16, 0);

	//!!!!!!! put water levels back in?
	//!!!!!! might want to use darker redstone wire for lower strength, just for some visual variety?

	setOffsetsForID(1, 1, *this);
	setOffsetsForID(2, 2, *this);
	setOffsetsForID(3, 3, *this);
	setOffsetsForID(4, 4, *this);
	setOffsetsForID(5, 5, *this);
	setOffsetsForID(6, 6, *this);
	blockOffsets[offsetIdx(6, 1)] = 250;
	blockOffsets[offsetIdx(6, 5)] = 250;
	blockOffsets[offsetIdx(6, 9)] = 250;
	blockOffsets[offsetIdx(6, 13)] = 250;
	blockOffsets[offsetIdx(6, 2)] = 251;
	blockOffsets[offsetIdx(6, 6)] = 251;
	blockOffsets[offsetIdx(6, 10)] = 251;
	blockOffsets[offsetIdx(6, 14)] = 251;
	setOffsetsForID(7, 7, *this);
	setOffsetsForID(8, 8, *this);
	//blockOffsets[offsetIdx(8, 1)] = 9;
	//blockOffsets[offsetIdx(8, 2)] = 10;
	//blockOffsets[offsetIdx(8, 3)] = 11;
	//blockOffsets[offsetIdx(8, 4)] = 12;
	//blockOffsets[offsetIdx(8, 5)] = 13;
	//blockOffsets[offsetIdx(8, 6)] = 14;
	//blockOffsets[offsetIdx(8, 7)] = 15;
	setOffsetsForID(9, 8, *this);
	//blockOffsets[offsetIdx(9, 1)] = 9;
	//blockOffsets[offsetIdx(9, 2)] = 10;
	//blockOffsets[offsetIdx(9, 3)] = 11;
	//blockOffsets[offsetIdx(9, 4)] = 12;
	//blockOffsets[offsetIdx(9, 5)] = 13;
	//blockOffsets[offsetIdx(9, 6)] = 14;
	//blockOffsets[offsetIdx(9, 7)] = 15;
	setOffsetsForID(10, 16, *this);
	blockOffsets[offsetIdx(10, 1)] = 19;
	blockOffsets[offsetIdx(10, 2)] = 18;
	blockOffsets[offsetIdx(10, 3)] = 17;
	setOffsetsForID(11, 16, *this);
	blockOffsets[offsetIdx(11, 1)] = 19;
	blockOffsets[offsetIdx(11, 2)] = 18;
	blockOffsets[offsetIdx(11, 3)] = 17;
	setOffsetsForID(12, 20, *this);
	setOffsetsForID(13, 21, *this);
	setOffsetsForID(14, 22, *this);
	setOffsetsForID(15, 23, *this);
	setOffsetsForID(16, 24, *this);
	setOffsetsForID(17, 25, *this);
	blockOffsets[offsetIdx(17, 1)] = 219;
	blockOffsets[offsetIdx(17, 2)] = 220;
	setOffsetsForID(18, 26, *this);
	blockOffsets[offsetIdx(18, 1)] = 248;
	blockOffsets[offsetIdx(18, 5)] = 248;
	blockOffsets[offsetIdx(18, 2)] = 249;
	blockOffsets[offsetIdx(18, 6)] = 249;
	setOffsetsForID(19, 27, *this);
	setOffsetsForID(20, 28, *this);
	setOffsetsForID(21, 221, *this);
	setOffsetsForID(22, 222, *this);
	setOffsetsForID(23, 223, *this);
	blockOffsets[offsetIdx(23, 2)] = 225;
	blockOffsets[offsetIdx(23, 4)] = 224;
	blockOffsets[offsetIdx(23, 5)] = 225;
	setOffsetsForID(24, 226, *this);
	setOffsetsForID(25, 227, *this);
	setOffsetsForID(26, 236, *this);
	blockOffsets[offsetIdx(26, 1)] = 237;
	blockOffsets[offsetIdx(26, 2)] = 238;
	blockOffsets[offsetIdx(26, 3)] = 239;
	blockOffsets[offsetIdx(26, 8)] = 232;
	blockOffsets[offsetIdx(26, 9)] = 233;
	blockOffsets[offsetIdx(26, 10)] = 234;
	blockOffsets[offsetIdx(26, 11)] = 235;
	setOffsetsForID(27, 258, *this);
	blockOffsets[offsetIdx(27, 1)] = 259;
	blockOffsets[offsetIdx(27, 2)] = 260;
	blockOffsets[offsetIdx(27, 3)] = 261;
	blockOffsets[offsetIdx(27, 4)] = 262;
	blockOffsets[offsetIdx(27, 5)] = 263;
	blockOffsets[offsetIdx(27, 8)] = 252;
	blockOffsets[offsetIdx(27, 9)] = 253;
	blockOffsets[offsetIdx(27, 10)] = 254;
	blockOffsets[offsetIdx(27, 11)] = 255;
	blockOffsets[offsetIdx(27, 12)] = 256;
	blockOffsets[offsetIdx(27, 13)] = 257;
	setOffsetsForID(28, 264, *this);
	blockOffsets[offsetIdx(28, 1)] = 265;
	blockOffsets[offsetIdx(28, 2)] = 266;
	blockOffsets[offsetIdx(28, 3)] = 267;
	blockOffsets[offsetIdx(28, 4)] = 268;
	blockOffsets[offsetIdx(28, 5)] = 269;
	setOffsetsForID(30, 272, *this);
	setOffsetsForID(31, 273, *this);
	blockOffsets[offsetIdx(31, 0)] = 275;
	blockOffsets[offsetIdx(31, 2)] = 274;
	setOffsetsForID(32, 275, *this);
	blockOffsets[offsetIdx(35, 0)] = 29;
	blockOffsets[offsetIdx(35, 1)] = 204;
	blockOffsets[offsetIdx(35, 2)] = 205;
	blockOffsets[offsetIdx(35, 3)] = 206;
	blockOffsets[offsetIdx(35, 4)] = 207;
	blockOffsets[offsetIdx(35, 5)] = 208;
	blockOffsets[offsetIdx(35, 6)] = 209;
	blockOffsets[offsetIdx(35, 7)] = 210;
	blockOffsets[offsetIdx(35, 8)] = 211;
	blockOffsets[offsetIdx(35, 9)] = 212;
	blockOffsets[offsetIdx(35, 10)] = 213;
	blockOffsets[offsetIdx(35, 11)] = 214;
	blockOffsets[offsetIdx(35, 12)] = 215;
	blockOffsets[offsetIdx(35, 13)] = 216;
	blockOffsets[offsetIdx(35, 14)] = 217;
	blockOffsets[offsetIdx(35, 15)] = 218;
	setOffsetsForID(37, 30, *this);
	setOffsetsForID(38, 31, *this);
	setOffsetsForID(39, 32, *this);
	setOffsetsForID(40, 33, *this);
	setOffsetsForID(41, 34, *this);
	setOffsetsForID(42, 35, *this);
	setOffsetsForID(43, 36, *this);
	blockOffsets[offsetIdx(43, 1)] = 226;
	blockOffsets[offsetIdx(43, 2)] = 5;
	blockOffsets[offsetIdx(43, 3)] = 4;
	setOffsetsForID(44, 37, *this);
	blockOffsets[offsetIdx(44, 1)] = 229;
	blockOffsets[offsetIdx(44, 2)] = 230;
	blockOffsets[offsetIdx(44, 3)] = 231;
	setOffsetsForID(45, 38, *this);
	setOffsetsForID(46, 39, *this);
	setOffsetsForID(47, 40, *this);
	setOffsetsForID(48, 41, *this);
	setOffsetsForID(49, 42, *this);
	setOffsetsForID(50, 43, *this);
	blockOffsets[offsetIdx(50, 1)] = 44;
	blockOffsets[offsetIdx(50, 2)] = 45;
	blockOffsets[offsetIdx(50, 3)] = 46;
	blockOffsets[offsetIdx(50, 4)] = 47;
	setOffsetsForID(51, 189, *this);
	setOffsetsForID(52, 49, *this);
	setOffsetsForID(53, 50, *this);
	blockOffsets[offsetIdx(53, 1)] = 51;
	blockOffsets[offsetIdx(53, 2)] = 52;
	blockOffsets[offsetIdx(53, 3)] = 53;
	setOffsetsForID(54, 54, *this);
	setOffsetsForID(55, 55, *this);
	setOffsetsForID(56, 56, *this);
	setOffsetsForID(57, 57, *this);
	setOffsetsForID(58, 58, *this);
	setOffsetsForID(59, 59, *this);
	blockOffsets[offsetIdx(59, 6)] = 60;
	blockOffsets[offsetIdx(59, 5)] = 61;
	blockOffsets[offsetIdx(59, 4)] = 62;
	blockOffsets[offsetIdx(59, 3)] = 63;
	blockOffsets[offsetIdx(59, 2)] = 64;
	blockOffsets[offsetIdx(59, 1)] = 65;
	blockOffsets[offsetIdx(59, 0)] = 66;
	setOffsetsForID(60, 67, *this);
	setOffsetsForID(61, 183, *this);
	blockOffsets[offsetIdx(61, 2)] = 185;
	blockOffsets[offsetIdx(61, 4)] = 184;
	blockOffsets[offsetIdx(61, 5)] = 185;
	setOffsetsForID(62, 186, *this);
	blockOffsets[offsetIdx(62, 2)] = 188;
	blockOffsets[offsetIdx(62, 4)] = 187;
	blockOffsets[offsetIdx(62, 5)] = 188;
	setOffsetsForID(63, 73, *this);
	blockOffsets[offsetIdx(63, 0)] = 72;
	blockOffsets[offsetIdx(63, 1)] = 72;
	blockOffsets[offsetIdx(63, 4)] = 70;
	blockOffsets[offsetIdx(63, 5)] = 70;
	blockOffsets[offsetIdx(63, 6)] = 71;
	blockOffsets[offsetIdx(63, 7)] = 71;
	blockOffsets[offsetIdx(63, 8)] = 72;
	blockOffsets[offsetIdx(63, 9)] = 72;
	blockOffsets[offsetIdx(63, 12)] = 70;
	blockOffsets[offsetIdx(63, 13)] = 70;
	blockOffsets[offsetIdx(63, 14)] = 71;
	blockOffsets[offsetIdx(63, 15)] = 71;
	blockOffsets[offsetIdx(64, 1)] = 74;
	blockOffsets[offsetIdx(64, 5)] = 74;
	blockOffsets[offsetIdx(64, 3)] = 75;
	blockOffsets[offsetIdx(64, 7)] = 75;
	blockOffsets[offsetIdx(64, 2)] = 76;
	blockOffsets[offsetIdx(64, 6)] = 76;
	blockOffsets[offsetIdx(64, 0)] = 77;
	blockOffsets[offsetIdx(64, 4)] = 77;
	blockOffsets[offsetIdx(64, 9)] = 78;
	blockOffsets[offsetIdx(64, 13)] = 78;
	blockOffsets[offsetIdx(64, 11)] = 79;
	blockOffsets[offsetIdx(64, 15)] = 79;
	blockOffsets[offsetIdx(64, 10)] = 80;
	blockOffsets[offsetIdx(64, 14)] = 80;
	blockOffsets[offsetIdx(64, 8)] = 81;
	blockOffsets[offsetIdx(64, 12)] = 81;
	setOffsetsForID(65, 82, *this);
	blockOffsets[offsetIdx(65, 3)] = 83;
	blockOffsets[offsetIdx(65, 4)] = 84;
	blockOffsets[offsetIdx(65, 5)] = 85;
	setOffsetsForID(66, 86, *this);
	blockOffsets[offsetIdx(66, 1)] = 87;
	blockOffsets[offsetIdx(66, 2)] = 200;
	blockOffsets[offsetIdx(66, 3)] = 201;
	blockOffsets[offsetIdx(66, 4)] = 202;
	blockOffsets[offsetIdx(66, 5)] = 203;
	blockOffsets[offsetIdx(66, 6)] = 92;
	blockOffsets[offsetIdx(66, 7)] = 93;
	blockOffsets[offsetIdx(66, 8)] = 94;
	blockOffsets[offsetIdx(66, 9)] = 95;
	setOffsetsForID(67, 96, *this);
	blockOffsets[offsetIdx(67, 1)] = 97;
	blockOffsets[offsetIdx(67, 2)] = 98;
	blockOffsets[offsetIdx(67, 3)] = 99;
	setOffsetsForID(68, 100, *this);
	blockOffsets[offsetIdx(68, 3)] = 101;
	blockOffsets[offsetIdx(68, 4)] = 102;
	blockOffsets[offsetIdx(68, 5)] = 103;
	setOffsetsForID(69, 194, *this);
	blockOffsets[offsetIdx(69, 2)] = 195;
	blockOffsets[offsetIdx(69, 3)] = 196;
	blockOffsets[offsetIdx(69, 4)] = 197;
	blockOffsets[offsetIdx(69, 5)] = 198;
	blockOffsets[offsetIdx(69, 6)] = 199;
	blockOffsets[offsetIdx(69, 10)] = 195;
	blockOffsets[offsetIdx(69, 11)] = 196;
	blockOffsets[offsetIdx(69, 12)] = 197;
	blockOffsets[offsetIdx(69, 13)] = 198;
	blockOffsets[offsetIdx(69, 14)] = 199;
	setOffsetsForID(70, 110, *this);
	blockOffsets[offsetIdx(71, 1)] = 111;
	blockOffsets[offsetIdx(71, 5)] = 111;
	blockOffsets[offsetIdx(71, 3)] = 112;
	blockOffsets[offsetIdx(71, 7)] = 112;
	blockOffsets[offsetIdx(71, 2)] = 113;
	blockOffsets[offsetIdx(71, 6)] = 113;
	blockOffsets[offsetIdx(71, 0)] = 114;
	blockOffsets[offsetIdx(71, 4)] = 114;
	blockOffsets[offsetIdx(71, 9)] = 115;
	blockOffsets[offsetIdx(71, 13)] = 115;
	blockOffsets[offsetIdx(71, 11)] = 116;
	blockOffsets[offsetIdx(71, 15)] = 116;
	blockOffsets[offsetIdx(71, 10)] = 117;
	blockOffsets[offsetIdx(71, 14)] = 117;
	blockOffsets[offsetIdx(71, 8)] = 118;
	blockOffsets[offsetIdx(71, 12)] = 118;
	setOffsetsForID(72, 119, *this);
	setOffsetsForID(73, 120, *this);
	setOffsetsForID(74, 120, *this);
	setOffsetsForID(75, 121, *this);
	blockOffsets[offsetIdx(75, 1)] = 145;
	blockOffsets[offsetIdx(75, 2)] = 146;
	blockOffsets[offsetIdx(75, 3)] = 147;
	blockOffsets[offsetIdx(75, 4)] = 148;
	setOffsetsForID(76, 122, *this);
	blockOffsets[offsetIdx(76, 1)] = 141;
	blockOffsets[offsetIdx(76, 2)] = 142;
	blockOffsets[offsetIdx(76, 3)] = 143;
	blockOffsets[offsetIdx(76, 4)] = 144;
	setOffsetsForID(77, 190, *this);
	blockOffsets[offsetIdx(77, 2)] = 191;
	blockOffsets[offsetIdx(77, 3)] = 192;
	blockOffsets[offsetIdx(77, 4)] = 193;
	blockOffsets[offsetIdx(77, 10)] = 191;
	blockOffsets[offsetIdx(77, 11)] = 192;
	blockOffsets[offsetIdx(77, 12)] = 193;
	setOffsetsForID(78, 127, *this);
	setOffsetsForID(79, 128, *this);
	setOffsetsForID(80, 129, *this);
	setOffsetsForID(81, 130, *this);
	setOffsetsForID(82, 131, *this);
	setOffsetsForID(83, 132, *this);
	setOffsetsForID(84, 133, *this);
	setOffsetsForID(85, 134, *this);
	setOffsetsForID(86, 135, *this);
	blockOffsets[offsetIdx(86, 0)] = 153;
	blockOffsets[offsetIdx(86, 1)] = 153;
	blockOffsets[offsetIdx(86, 3)] = 154;
	setOffsetsForID(87, 136, *this);
	setOffsetsForID(88, 137, *this);
	setOffsetsForID(89, 138, *this);
	setOffsetsForID(90, 139, *this);
	setOffsetsForID(91, 140, *this);
	blockOffsets[offsetIdx(91, 0)] = 155;
	blockOffsets[offsetIdx(91, 1)] = 155;
	blockOffsets[offsetIdx(91, 3)] = 156;
	setOffsetsForID(92, 228, *this);
	setOffsetsForID(93, 247, *this);
	blockOffsets[offsetIdx(93, 1)] = 244;
	blockOffsets[offsetIdx(93, 5)] = 244;
	blockOffsets[offsetIdx(93, 9)] = 244;
	blockOffsets[offsetIdx(93, 13)] = 244;
	blockOffsets[offsetIdx(93, 2)] = 246;
	blockOffsets[offsetIdx(93, 6)] = 246;
	blockOffsets[offsetIdx(93, 10)] = 246;
	blockOffsets[offsetIdx(93, 14)] = 246;
	blockOffsets[offsetIdx(93, 3)] = 245;
	blockOffsets[offsetIdx(93, 7)] = 245;
	blockOffsets[offsetIdx(93, 11)] = 245;
	blockOffsets[offsetIdx(93, 15)] = 245;
	setOffsetsForID(94, 243, *this);
	blockOffsets[offsetIdx(94, 1)] = 240;
	blockOffsets[offsetIdx(94, 5)] = 240;
	blockOffsets[offsetIdx(94, 9)] = 240;
	blockOffsets[offsetIdx(94, 13)] = 240;
	blockOffsets[offsetIdx(94, 2)] = 242;
	blockOffsets[offsetIdx(94, 6)] = 242;
	blockOffsets[offsetIdx(94, 10)] = 242;
	blockOffsets[offsetIdx(94, 14)] = 242;
	blockOffsets[offsetIdx(94, 3)] = 241;
	blockOffsets[offsetIdx(94, 7)] = 241;
	blockOffsets[offsetIdx(94, 11)] = 241;
	blockOffsets[offsetIdx(94, 15)] = 241;
	setOffsetsForID(95, 270, *this);
	setOffsetsForID(96, 276, *this);
	blockOffsets[offsetIdx(96, 4)] = 277;
	blockOffsets[offsetIdx(96, 5)] = 278;
	blockOffsets[offsetIdx(96, 6)] = 279;
	blockOffsets[offsetIdx(96, 7)] = 280;
}

void BlockImages::checkOpacityAndTransparency(int B)
{
	opacity.clear();
	opacity.resize(NUMBLOCKIMAGES, true);
	transparency.clear();
	transparency.resize(NUMBLOCKIMAGES, true);

	for (int i = 0; i < NUMBLOCKIMAGES; i++)
	{
		ImageRect rect = getRect(i);
		// use the face iterators to examine the N, W, and U faces; any non-100% alpha makes
		//  the block non-opaque, and any non-0% alpha makes the block non-transparent
		int tilesize = 2*B;
		// N face starts at [0,B]
		for (FaceIterator it(rect.x, rect.y + B, 1, tilesize); !it.end; it.advance())
		{
			int a = ALPHA(img(it.x, it.y));
			if (a < 255)
				opacity[i] = false;
			if (a > 0)
				transparency[i] = false;
			if (!opacity[i] && !transparency[i])
				break;
		}
		if (!opacity[i] && !transparency[i])
			continue;
		// W face starts at [2B,2B]
		for (FaceIterator it(rect.x + 2*B, rect.y + 2*B, -1, tilesize); !it.end; it.advance())
		{
			int a = ALPHA(img(it.x, it.y));
			if (a < 255)
				opacity[i] = false;
			if (a > 0)
				transparency[i] = false;
			if (!opacity[i] && !transparency[i])
				break;
		}
		if (!opacity[i] && !transparency[i])
			continue;
		// U face starts at [2B-1,0]
		for (TopFaceIterator it(rect.x + 2*B-1, rect.y, tilesize); !it.end; it.advance())
		{
			int a = ALPHA(img(it.x, it.y));
			if (a < 255)
				opacity[i] = false;
			if (a > 0)
				transparency[i] = false;
			if (!opacity[i] && !transparency[i])
				break;
		}
	}
}

void BlockImages::retouchAlphas(int B)
{
	for (int i = 0; i < NUMBLOCKIMAGES; i++)
	{
		ImageRect rect = getRect(i);
		// use the face iterators to examine the N, W, and U faces; any alpha under 10 is changed
		//  to 0, and any alpha above 245 is changed to 255
		int tilesize = 2*B;
		// N face starts at [0,B]
		for (FaceIterator it(rect.x, rect.y + B, 1, tilesize); !it.end; it.advance())
		{
			int a = ALPHA(img(it.x, it.y));
			if (a < 10)
				setAlpha(img(it.x, it.y), 0);
			else if (a > 245)
				setAlpha(img(it.x, it.y), 255);
		}
		// W face starts at [2B,2B]
		for (FaceIterator it(rect.x + 2*B, rect.y + 2*B, -1, tilesize); !it.end; it.advance())
		{
			int a = ALPHA(img(it.x, it.y));
			if (a < 10)
				setAlpha(img(it.x, it.y), 0);
			else if (a > 245)
				setAlpha(img(it.x, it.y), 255);
		}
		// U face starts at [2B-1,0]
		for (TopFaceIterator it(rect.x + 2*B-1, rect.y, tilesize); !it.end; it.advance())
		{
			int a = ALPHA(img(it.x, it.y));
			if (a < 10)
				setAlpha(img(it.x, it.y), 0);
			else if (a > 245)
				setAlpha(img(it.x, it.y), 255);
		}
	}
}

bool BlockImages::construct(int B, const string& terrainfile, const string& firefile)
{
	if (B < 2)
		return false;

	// read the terrain file, check that it's okay, and get a resized copy for use
	RGBAImage terrain;
	if (!terrain.readPNG(terrainfile))
		return false;
	if (terrain.w % 16 != 0 || terrain.h != terrain.w)
		return false;
	int terrainSize = terrain.w / 16;
	RGBAImage tiles = getResizedTerrain(terrain, terrainSize, B);

	// read fire.png, make sure it's okay, and get a resized copy
	RGBAImage fire;
	if (!fire.readPNG(firefile))
		return false;
	if (fire.w != fire.h)
		return false;
	RGBAImage firetile;
	firetile.create(2*B, 2*B);
	resize(fire, ImageRect(0, 0, fire.w, fire.h), firetile, ImageRect(0, 0, 2*B, 2*B));

	// colorize various tiles
	darken(tiles, ImageRect(0, 0, 2*B, 2*B), 0.6, 0.95, 0.3);  // tile 0 = grass top
	darken(tiles, ImageRect(14*B, 4*B, 2*B, 2*B), 0.6, 0.95, 0.3);  // tile 39 = tall grass
	darken(tiles, ImageRect(16*B, 6*B, 2*B, 2*B), 0.6, 0.95, 0.3);  // tile 56 = fern
	darken(tiles, ImageRect(8*B, 20*B, 2*B, 2*B), 0.9, 0.1, 0.1);  // tile 164 = redstone dust

	// create colorized copies of leaf tiles (can't colorize in place because normal and
	//  birch leaves use the same texture)
	RGBAImage leaftiles;
	leaftiles.create(6*B, 2*B);
	// normal
	blit(tiles, ImageRect(8*B, 6*B, 2*B, 2*B), leaftiles, 0, 0);
	darken(leaftiles, ImageRect(0, 0, 2*B, 2*B), 0.3, 1.0, 0.1);
	// pine
	blit(tiles, ImageRect(8*B, 16*B, 2*B, 2*B), leaftiles, 2*B, 0);
	darken(leaftiles, ImageRect(2*B, 0, 2*B, 2*B), 0.3, 1.0, 0.45);
	// birch
	blit(tiles, ImageRect(8*B, 6*B, 2*B, 2*B), leaftiles, 4*B, 0);
	darken(leaftiles, ImageRect(4*B, 0, 2*B, 2*B), 0.55, 0.9, 0.1);

	// calculate the pixel offset used for cactus/cake; represents one pixel of the default
	//  16x16 texture size
	int smallOffset = (terrainSize + 15) / 16;  // ceil(terrainSize/16)

	// resize the cactus tiles again, this time taking a smaller portion of the terrain
	//  image (to drop the transparent border)
	resize(terrain, ImageRect(5*terrainSize + smallOffset, 4*terrainSize + smallOffset, terrainSize - 2*smallOffset, terrainSize - 2*smallOffset),
	       tiles, ImageRect(5*2*B, 4*2*B, 2*B, 2*B));
	resize(terrain, ImageRect(6*terrainSize + smallOffset, 4*terrainSize, terrainSize - 2*smallOffset, terrainSize),
	       tiles, ImageRect(6*2*B, 4*2*B, 2*B, 2*B));

	// ...and the same thing for the cake tiles
	resize(terrain, ImageRect(9*terrainSize + smallOffset, 7*terrainSize + smallOffset, terrainSize - 2*smallOffset, terrainSize - 2*smallOffset),
	       tiles, ImageRect(9*2*B, 7*2*B, 2*B, 2*B));
	resize(terrain, ImageRect(10*terrainSize + smallOffset, 7*terrainSize, terrainSize - 2*smallOffset, terrainSize),
	       tiles, ImageRect(10*2*B, 7*2*B, 2*B, 2*B));


	// initialize image
	img.create(rectsize * 16, (NUMBLOCKIMAGES/16 + 1) * rectsize);

	// build all block images

	drawBlockImage(img, getRect(1), tiles, 1, 1, 1, B);  // stone
	drawBlockImage(img, getRect(2), tiles, 3, 3, 0, B);  // grass
	drawBlockImage(img, getRect(3), tiles, 2, 2, 2, B);  // dirt
	drawBlockImage(img, getRect(4), tiles, 16, 16, 16, B);  // cobblestone
	drawBlockImage(img, getRect(5), tiles, 4, 4, 4, B);  // planks
	drawBlockImage(img, getRect(7), tiles, 17, 17, 17, B);  // bedrock
	drawBlockImage(img, getRect(8), tiles, 205, 205, 205, B);  // full water
	drawBlockImage(img, getRect(157), tiles, -1, -1, 205, B);  // water surface
	drawBlockImage(img, getRect(178), tiles, 205, -1, 205, B);  // water missing W
	drawBlockImage(img, getRect(179), tiles, -1, 205, 205, B);  // water missing N
	drawBlockImage(img, getRect(16), tiles, 237, 237, 237, B);  // full lava
	drawBlockImage(img, getRect(20), tiles, 18, 18, 18, B);  // sand
	drawBlockImage(img, getRect(21), tiles, 19, 19, 19, B);  // gravel
	drawBlockImage(img, getRect(22), tiles, 32, 32, 32, B);  // gold ore
	drawBlockImage(img, getRect(23), tiles, 33, 33, 33, B);  // iron ore
	drawBlockImage(img, getRect(24), tiles, 34, 34, 34, B);  // coal ore
	drawBlockImage(img, getRect(25), tiles, 20, 20, 21, B);  // log
	drawBlockImage(img, getRect(219), tiles, 116, 116, 21, B);  // dark log
	drawBlockImage(img, getRect(220), tiles, 117, 117, 21, B);  // birch log
	drawBlockImage(img, getRect(26), leaftiles, 0, 0, 0, B);  // leaves
	drawBlockImage(img, getRect(248), leaftiles, 1, 1, 1, B);  // pine leaves
	drawBlockImage(img, getRect(249), leaftiles, 2, 2, 2, B);  // birch leaves
	drawBlockImage(img, getRect(27), tiles, 48, 48, 48, B);  // sponge
	drawBlockImage(img, getRect(28), tiles, 49, 49, 49, B);  // glass
	drawBlockImage(img, getRect(29), tiles, 64, 64, 64, B);  // white wool
	drawBlockImage(img, getRect(204), tiles, 210, 210, 210, B);  // orange wool
	drawBlockImage(img, getRect(205), tiles, 194, 194, 194, B);  // magenta wool
	drawBlockImage(img, getRect(206), tiles, 178, 178, 178, B);  // light blue wool
	drawBlockImage(img, getRect(207), tiles, 162, 162, 162, B);  // yellow wool
	drawBlockImage(img, getRect(208), tiles, 146, 146, 146, B);  // lime wool
	drawBlockImage(img, getRect(209), tiles, 130, 130, 130, B);  // pink wool
	drawBlockImage(img, getRect(210), tiles, 114, 114, 114, B);  // gray wool
	drawBlockImage(img, getRect(211), tiles, 225, 225, 225, B);  // light gray wool
	drawBlockImage(img, getRect(212), tiles, 209, 209, 209, B);  // cyan wool
	drawBlockImage(img, getRect(213), tiles, 193, 193, 193, B);  // purple wool
	drawBlockImage(img, getRect(214), tiles, 177, 177, 177, B);  // blue wool
	drawBlockImage(img, getRect(215), tiles, 161, 161, 161, B);  // brown wool
	drawBlockImage(img, getRect(216), tiles, 145, 145, 145, B);  // green wool
	drawBlockImage(img, getRect(217), tiles, 129, 129, 129, B);  // red wool
	drawBlockImage(img, getRect(218), tiles, 113, 113, 113, B);  // black wool
	drawBlockImage(img, getRect(34), tiles, 23, 23, 23, B);  // gold block
	drawBlockImage(img, getRect(35), tiles, 22, 22, 22, B);  // iron block
	drawBlockImage(img, getRect(36), tiles, 5, 5, 6, B);  // double stone slab
	drawBlockImage(img, getRect(38), tiles, 7, 7, 7, B);  // brick
	drawBlockImage(img, getRect(39), tiles, 8, 8, 9, B);  // TNT
	drawBlockImage(img, getRect(40), tiles, 35, 35, 4, B);  // bookshelf
	drawBlockImage(img, getRect(41), tiles, 36, 36, 36, B);  // mossy cobblestone
	drawBlockImage(img, getRect(42), tiles, 37, 37, 37, B);  // obsidian
	drawBlockImage(img, getRect(49), tiles, 65, 65, 65, B);  // spawner
	drawBlockImage(img, getRect(54), tiles, 26, 27, 25, B);  // chest facing W
	drawBlockImage(img, getRect(177), tiles, 27, 26, 25, B);  // chest facing N
	drawBlockImage(img, getRect(173), tiles, 26, 41, 25, B);  // double chest N
	drawBlockImage(img, getRect(174), tiles, 26, 42, 25, B);  // double chest S
	drawBlockImage(img, getRect(175), tiles, 41, 26, 25, B);  // double chest E
	drawBlockImage(img, getRect(176), tiles, 42, 26, 25, B);  // double chest W
	drawBlockImage(img, getRect(270), tiles, 26, 27, 25, B);  // locked chest facing W
	drawBlockImage(img, getRect(271), tiles, 27, 26, 25, B);  // locked chest facing N
	drawBlockImage(img, getRect(56), tiles, 50, 50, 50, B);  // diamond ore
	drawBlockImage(img, getRect(57), tiles, 24, 24, 24, B);  // diamond block
	drawBlockImage(img, getRect(58), tiles, 59, 60, 43, B);  // workbench
	drawBlockImage(img, getRect(67), tiles, 2, 2, 87, B);  // farmland
	drawBlockImage(img, getRect(183), tiles, 45, 44, 62, B);  // furnace W
	drawBlockImage(img, getRect(184), tiles, 44, 45, 62, B);  // furnace N
	drawBlockImage(img, getRect(185), tiles, 45, 45, 62, B);  // furnace E/S
	drawBlockImage(img, getRect(186), tiles, 45, 61, 62, B);  // lit furnace W
	drawBlockImage(img, getRect(187), tiles, 61, 45, 62, B);  // lit furnace N
	drawBlockImage(img, getRect(188), tiles, 45, 45, 62, B);  // lit furnace E/S
	drawBlockImage(img, getRect(120), tiles, 51, 51, 51, B);  // redstone ore
	drawBlockImage(img, getRect(128), tiles, 67, 67, 67, B);  // ice
	drawBlockImage(img, getRect(180), tiles, -1, -1, 67, B);  // ice surface
	drawBlockImage(img, getRect(181), tiles, 67, -1, 67, B);  // ice missing W
	drawBlockImage(img, getRect(182), tiles, -1, 67, 67, B);  // ice missing N
	drawBlockImage(img, getRect(129), tiles, 66, 66, 66, B);  // snow block
	drawBlockImage(img, getRect(130), tiles, 70, 70, 69, B);  // cactus
	drawBlockImage(img, getRect(131), tiles, 72, 72, 72, B);  // clay
	drawBlockImage(img, getRect(133), tiles, 74, 74, 75, B);  // jukebox
	drawBlockImage(img, getRect(135), tiles, 118, 119, 102, B);  // pumpkin facing W
	drawBlockImage(img, getRect(153), tiles, 118, 118, 102, B);  // pumpkin facing E/S
	drawBlockImage(img, getRect(154), tiles, 119, 118, 102, B);  // pumpkin facing N
	drawBlockImage(img, getRect(136), tiles, 103, 103, 103, B);  // netherrack
	drawBlockImage(img, getRect(137), tiles, 104, 104, 104, B);  // soul sand
	drawBlockImage(img, getRect(138), tiles, 105, 105, 105, B);  // glowstone
	drawBlockImage(img, getRect(140), tiles, 118, 120, 102, B);  // jack-o-lantern W
	drawBlockImage(img, getRect(155), tiles, 118, 118, 102, B);  // jack-o-lantern E/S
	drawBlockImage(img, getRect(156), tiles, 120, 118, 102, B);  // jack-o-lantern N
	drawBlockImage(img, getRect(221), tiles, 160, 160, 160, B);  // lapis ore
	drawBlockImage(img, getRect(222), tiles, 144, 144, 144, B);  // lapis block
	drawBlockImage(img, getRect(223), tiles, 45, 46, 62, B);  // dispenser W
	drawBlockImage(img, getRect(224), tiles, 46, 45, 62, B);  // dispenser N
	drawBlockImage(img, getRect(225), tiles, 45, 45, 62, B);  // dispenser E/S
	drawBlockImage(img, getRect(226), tiles, 192, 192, 176, B);  // sandstone
	drawBlockImage(img, getRect(227), tiles, 74, 74, 74, B);  // note block

	drawPartialBlockImage(img, getRect(9), tiles, 205, 205, 205, B, 0.125, 0, 0);  // water level 7
	drawPartialBlockImage(img, getRect(10), tiles, 205, 205, 205, B, 0.25, 0, 0);  // water level 6
	drawPartialBlockImage(img, getRect(11), tiles, 205, 205, 205, B, 0.375, 0, 0);  // water level 5
	drawPartialBlockImage(img, getRect(12), tiles, 205, 205, 205, B, 0.5, 0, 0);  // water level 4
	drawPartialBlockImage(img, getRect(13), tiles, 205, 205, 205, B, 0.625, 0, 0);  // water level 3
	drawPartialBlockImage(img, getRect(14), tiles, 205, 205, 205, B, 0.75, 0, 0);  // water level 2
	drawPartialBlockImage(img, getRect(15), tiles, 205, 205, 205, B, 0.875, 0, 0);  // water level 1
	drawPartialBlockImage(img, getRect(17), tiles, 237, 237, 237, B, 0.25, 0, 0);  // lava level 3
	drawPartialBlockImage(img, getRect(18), tiles, 237, 237, 237, B, 0.5, 0, 0);  // lava level 2
	drawPartialBlockImage(img, getRect(19), tiles, 237, 237, 237, B, 0.75, 0, 0);  // lava level 1
	drawPartialBlockImage(img, getRect(37), tiles, 5, 5, 6, B, 0.5, 0, 0);  // stone slab
	drawPartialBlockImage(img, getRect(229), tiles, 192, 192, 176, B, 0.5, 0, 0);  // sandstone slab
	drawPartialBlockImage(img, getRect(230), tiles, 4, 4, 4, B, 0.5, 0, 0);  // wooden slab
	drawPartialBlockImage(img, getRect(231), tiles, 16, 16, 16, B, 0.5, 0, 0);  // cobble slab
	drawPartialBlockImage(img, getRect(110), tiles, 1, 1, 1, B, 0.875, 0, 0);  // stone pressure plate
	drawPartialBlockImage(img, getRect(119), tiles, 4, 4, 4, B, 0.875, 0, 0);  // wood pressure plate
	drawPartialBlockImage(img, getRect(127), tiles, 66, 66, 66, B, 0.75, 0, 0);  // snow
	drawPartialBlockImage(img, getRect(228), tiles, 122, 122, 121, B, 0.5, 0, 0);  // cake
	drawPartialBlockImage(img, getRect(232), tiles, 151, 152, 135, B, 0.5, 0, 0);  // bed head W
	drawPartialBlockImage(img, getRect(233), tiles, 152, 151, 135, B, 0.5, 3, 2);  // bed head N
	drawPartialBlockImage(img, getRect(234), tiles, 151, -1, 135, B, 0.5, 2, 1);  // bed head E
	drawPartialBlockImage(img, getRect(235), tiles, -1, 151, 135, B, 0.5, 1, 0);  // bed head S
	drawPartialBlockImage(img, getRect(236), tiles, 150, -1, 134, B, 0.5, 0, 0);  // bed foot W
	drawPartialBlockImage(img, getRect(237), tiles, -1, 150, 134, B, 0.5, 3, 2);  // bed foot N
	drawPartialBlockImage(img, getRect(238), tiles, 150, 149, 134, B, 0.5, 2, 1);  // bed foot E
	drawPartialBlockImage(img, getRect(239), tiles, 149, 150, 134, B, 0.5, 1, 0);  // bed foot S

	drawItemBlockImage(img, getRect(6), tiles, 15, B);  // sapling
	drawItemBlockImage(img, getRect(30), tiles, 13, B);  // yellow flower
	drawItemBlockImage(img, getRect(31), tiles, 12, B);  // red rose
	drawItemBlockImage(img, getRect(32), tiles, 29, B);  // brown mushroom
	drawItemBlockImage(img, getRect(33), tiles, 28, B);  // red mushroom
	drawItemBlockImage(img, getRect(43), tiles, 80, B);  // torch floor
	drawItemBlockImage(img, getRect(59), tiles, 95, B);  // wheat level 7
	drawItemBlockImage(img, getRect(60), tiles, 94, B);  // wheat level 6
	drawItemBlockImage(img, getRect(61), tiles, 93, B);  // wheat level 5
	drawItemBlockImage(img, getRect(62), tiles, 92, B);  // wheat level 4
	drawItemBlockImage(img, getRect(63), tiles, 91, B);  // wheat level 3
	drawItemBlockImage(img, getRect(64), tiles, 90, B);  // wheat level 2
	drawItemBlockImage(img, getRect(65), tiles, 89, B);  // wheat level 1
	drawItemBlockImage(img, getRect(66), tiles, 88, B);  // wheat level 0
	drawItemBlockImage(img, getRect(121), tiles, 115, B);  // red torch floor off
	drawItemBlockImage(img, getRect(122), tiles, 99, B);  // red torch floor on
	drawItemBlockImage(img, getRect(132), tiles, 73, B);  // reeds
	drawItemBlockImage(img, getRect(250), tiles, 63, B);  // pine sapling
	drawItemBlockImage(img, getRect(251), tiles, 79, B);  // birch sapling
	drawItemBlockImage(img, getRect(272), tiles, 11, B);  // web
	drawItemBlockImage(img, getRect(273), tiles, 39, B);  // tall grass
	drawItemBlockImage(img, getRect(274), tiles, 56, B);  // fern
	drawItemBlockImage(img, getRect(275), tiles, 55, B);  // dead shrub

	drawSingleFaceBlockImage(img, getRect(44), tiles, 80, 1, B);  // torch pointing S
	drawSingleFaceBlockImage(img, getRect(45), tiles, 80, 0, B);  // torch pointing N
	drawSingleFaceBlockImage(img, getRect(46), tiles, 80, 3, B);  // torch pointing W
	drawSingleFaceBlockImage(img, getRect(47), tiles, 80, 2, B);  // torch pointing E
	drawSingleFaceBlockImage(img, getRect(74), tiles, 97, 3, B);  // wood door S side
	drawSingleFaceBlockImage(img, getRect(75), tiles, 97, 2, B);  // wood door N side
	drawSingleFaceBlockImage(img, getRect(76), tiles, 97, 0, B);  // wood door W side
	drawSingleFaceBlockImage(img, getRect(77), tiles, 97, 1, B);  // wood door E side
	drawSingleFaceBlockImage(img, getRect(78), tiles, 81, 3, B);  // wood door top S
	drawSingleFaceBlockImage(img, getRect(79), tiles, 81, 2, B);  // wood door top N
	drawSingleFaceBlockImage(img, getRect(80), tiles, 81, 0, B);  // wood door top W
	drawSingleFaceBlockImage(img, getRect(81), tiles, 81, 1, B);  // wood door top E
	drawSingleFaceBlockImage(img, getRect(82), tiles, 83, 2, B);  // ladder E side
	drawSingleFaceBlockImage(img, getRect(83), tiles, 83, 3, B);  // ladder W side
	drawSingleFaceBlockImage(img, getRect(84), tiles, 83, 0, B);  // ladder N side
	drawSingleFaceBlockImage(img, getRect(85), tiles, 83, 1, B);  // ladder S side
	drawSingleFaceBlockImage(img, getRect(111), tiles, 98, 3, B);  // iron door S side
	drawSingleFaceBlockImage(img, getRect(112), tiles, 98, 2, B);  // iron door N side
	drawSingleFaceBlockImage(img, getRect(113), tiles, 98, 0, B);  // iron door W side
	drawSingleFaceBlockImage(img, getRect(114), tiles, 98, 1, B);  // iron door E side
	drawSingleFaceBlockImage(img, getRect(115), tiles, 82, 3, B);  // iron door top S
	drawSingleFaceBlockImage(img, getRect(116), tiles, 82, 2, B);  // iron door top N
	drawSingleFaceBlockImage(img, getRect(117), tiles, 82, 0, B);  // iron door top W
	drawSingleFaceBlockImage(img, getRect(118), tiles, 82, 1, B);  // iron door top E
	drawSingleFaceBlockImage(img, getRect(141), tiles, 99, 1, B);  // red torch S on
	drawSingleFaceBlockImage(img, getRect(142), tiles, 99, 0, B);  // red torch N on
	drawSingleFaceBlockImage(img, getRect(143), tiles, 99, 3, B);  // red torch W on
	drawSingleFaceBlockImage(img, getRect(144), tiles, 99, 2, B);  // red torch E on
	drawSingleFaceBlockImage(img, getRect(145), tiles, 115, 1, B);  // red torch S off
	drawSingleFaceBlockImage(img, getRect(146), tiles, 115, 0, B);  // red torch N off
	drawSingleFaceBlockImage(img, getRect(147), tiles, 115, 3, B);  // red torch W off
	drawSingleFaceBlockImage(img, getRect(148), tiles, 115, 2, B);  // red torch E off
	drawSingleFaceBlockImage(img, getRect(277), tiles, 84, 2, B);  // trapdoor open W
	drawSingleFaceBlockImage(img, getRect(278), tiles, 84, 3, B);  // trapdoor open E
	drawSingleFaceBlockImage(img, getRect(279), tiles, 84, 0, B);  // trapdoor open S
	drawSingleFaceBlockImage(img, getRect(280), tiles, 84, 1, B);  // trapdoor open N

	drawPartialSingleFaceBlockImage(img, getRect(100), tiles, 4, 2, B, 0.25, 0.75, 0, 1);  // wall sign facing E
	drawPartialSingleFaceBlockImage(img, getRect(101), tiles, 4, 3, B, 0.25, 0.75, 0, 1);  // wall sign facing W
	drawPartialSingleFaceBlockImage(img, getRect(102), tiles, 4, 0, B, 0.25, 0.75, 0, 1);  // wall sign facing N
	drawPartialSingleFaceBlockImage(img, getRect(103), tiles, 4, 1, B, 0.25, 0.75, 0, 1);  // wall sign facing S
	drawPartialSingleFaceBlockImage(img, getRect(190), tiles, 1, 1, B, 0.35, 0.65, 0.35, 0.65);  // stone button facing S
	drawPartialSingleFaceBlockImage(img, getRect(191), tiles, 1, 0, B, 0.35, 0.65, 0.35, 0.65);  // stone button facing N
	drawPartialSingleFaceBlockImage(img, getRect(192), tiles, 1, 3, B, 0.35, 0.65, 0.35, 0.65);  // stone button facing W
	drawPartialSingleFaceBlockImage(img, getRect(193), tiles, 1, 2, B, 0.35, 0.65, 0.35, 0.65);  // stone button facing E

	drawSolidColorBlockImage(img, getRect(139), 0xd07b2748, B);  // portal

	drawStairsS(img, getRect(50), tiles, 4, B);  // wood stairs asc S
	drawStairsN(img, getRect(51), tiles, 4, B);  // wood stairs asc N
	drawStairsW(img, getRect(52), tiles, 4, B);  // wood stairs asc W
	drawStairsE(img, getRect(53), tiles, 4, B);  // wood stairs asc E
	drawStairsS(img, getRect(96), tiles, 16, B);  // cobble stairs asc S
	drawStairsN(img, getRect(97), tiles, 16, B);  // cobble stairs asc N
	drawStairsW(img, getRect(98), tiles, 16, B);  // cobble stairs asc W
	drawStairsE(img, getRect(99), tiles, 16, B);  // cobble stairs asc E

	drawFloorBlockImage(img, getRect(55), tiles, 164, 0, B);  // redstone wire NSEW
	drawFloorBlockImage(img, getRect(86), tiles, 128, 1, B);  // track EW
	drawFloorBlockImage(img, getRect(87), tiles, 128, 0, B);  // track NS
	drawFloorBlockImage(img, getRect(92), tiles, 112, 1, B);  // track NE corner
	drawFloorBlockImage(img, getRect(93), tiles, 112, 0, B);  // track SE corner
	drawFloorBlockImage(img, getRect(94), tiles, 112, 3, B);  // track SW corner
	drawFloorBlockImage(img, getRect(95), tiles, 112, 2, B);  // track NW corner
	drawFloorBlockImage(img, getRect(252), tiles, 179, 1, B);  // booster on EW
	drawFloorBlockImage(img, getRect(253), tiles, 179, 0, B);  // booster on NS
	drawFloorBlockImage(img, getRect(258), tiles, 163, 1, B);  // booster off EW
	drawFloorBlockImage(img, getRect(259), tiles, 163, 0, B);  // booster off NS
	drawFloorBlockImage(img, getRect(264), tiles, 195, 1, B);  // detector EW
	drawFloorBlockImage(img, getRect(265), tiles, 195, 0, B);  // detector NS
	drawFloorBlockImage(img, getRect(276), tiles, 84, 0, B);  // trapdoor closed

	drawAngledFloorBlockImage(img, getRect(200), tiles, 128, 0, 0, B);  // track asc S
	drawAngledFloorBlockImage(img, getRect(201), tiles, 128, 0, 2, B);  // track asc N
	drawAngledFloorBlockImage(img, getRect(202), tiles, 128, 1, 3, B);  // track asc E
	drawAngledFloorBlockImage(img, getRect(203), tiles, 128, 1, 1, B);  // track asc W
	drawAngledFloorBlockImage(img, getRect(254), tiles, 179, 0, 0, B);  // booster on asc S
	drawAngledFloorBlockImage(img, getRect(255), tiles, 179, 0, 2, B);  // booster on asc N
	drawAngledFloorBlockImage(img, getRect(256), tiles, 179, 1, 3, B);  // booster on asc E
	drawAngledFloorBlockImage(img, getRect(257), tiles, 179, 1, 1, B);  // booster on asc W
	drawAngledFloorBlockImage(img, getRect(260), tiles, 163, 0, 0, B);  // booster off asc S
	drawAngledFloorBlockImage(img, getRect(261), tiles, 163, 0, 2, B);  // booster off asc N
	drawAngledFloorBlockImage(img, getRect(262), tiles, 163, 1, 3, B);  // booster off asc E
	drawAngledFloorBlockImage(img, getRect(263), tiles, 163, 1, 1, B);  // booster off asc W
	drawAngledFloorBlockImage(img, getRect(266), tiles, 195, 0, 0, B);  // detector asc S
	drawAngledFloorBlockImage(img, getRect(267), tiles, 195, 0, 2, B);  // detector asc N
	drawAngledFloorBlockImage(img, getRect(268), tiles, 195, 1, 3, B);  // detector asc E
	drawAngledFloorBlockImage(img, getRect(269), tiles, 195, 1, 1, B);  // detector asc W

	drawFencePost(img, getRect(134), tiles, 4, B);  // fence post
	drawFence(img, getRect(158), tiles, 4, true, false, false, false, B);  // fence N
	drawFence(img, getRect(159), tiles, 4, false, true, false, false, B);  // fence S
	drawFence(img, getRect(160), tiles, 4, true, true, false, false, B);  // fence NS
	drawFence(img, getRect(161), tiles, 4, false, false, true, false, B);  // fence E
	drawFence(img, getRect(162), tiles, 4, true, false, true, false, B);  // fence NE
	drawFence(img, getRect(163), tiles, 4, false, true, true, false, B);  // fence SE
	drawFence(img, getRect(164), tiles, 4, true, true, true, false, B);  // fence NSE
	drawFence(img, getRect(165), tiles, 4, false, false, false, true, B);  // fence W
	drawFence(img, getRect(166), tiles, 4, true, false, false, true, B);  // fence NW
	drawFence(img, getRect(167), tiles, 4, false, true, false, true, B);  // fence SW
	drawFence(img, getRect(168), tiles, 4, true, true, false, true, B);  // fence NSW
	drawFence(img, getRect(169), tiles, 4, false, false, true, true, B);  // fence EW
	drawFence(img, getRect(170), tiles, 4, true, false, true, true, B);  // fence NEW
	drawFence(img, getRect(171), tiles, 4, false, true, true, true, B);  // fence SEW
	drawFence(img, getRect(172), tiles, 4, true, true, true, true, B);  // fence NSEW

	drawSign(img, getRect(70), tiles, 4, B);  // sign facing N/S
	drawSign(img, getRect(71), tiles, 4, B);  // sign facing NE/SW
	drawSign(img, getRect(72), tiles, 4, B);  // sign facing E/W
	drawSign(img, getRect(73), tiles, 4, B);  // sign facing SE/NW

	drawWallLever(img, getRect(194), tiles, 1, B);  // wall lever facing S
	drawWallLever(img, getRect(195), tiles, 0, B);  // wall lever facing N
	drawWallLever(img, getRect(196), tiles, 3, B);  // wall lever facing W
	drawWallLever(img, getRect(197), tiles, 2, B);  // wall lever facing E
	drawFloorLeverEW(img, getRect(198), tiles, B);  // ground lever EW
	drawFloorLeverNS(img, getRect(199), tiles, B);  // ground lever NS

	drawRepeater(img, getRect(240), tiles, 147, 0, B);  // repeater on N
	drawRepeater(img, getRect(241), tiles, 147, 2, B);  // repeater on S
	drawRepeater(img, getRect(242), tiles, 147, 3, B);  // repeater on E
	drawRepeater(img, getRect(243), tiles, 147, 1, B);  // repeater on W
	drawRepeater(img, getRect(244), tiles, 131, 0, B);  // repeater on N
	drawRepeater(img, getRect(245), tiles, 131, 2, B);  // repeater on S
	drawRepeater(img, getRect(246), tiles, 131, 3, B);  // repeater on E
	drawRepeater(img, getRect(247), tiles, 131, 1, B);  // repeater on W

	drawFire(img, getRect(189), firetile, B);  // fire

	return true;
}

