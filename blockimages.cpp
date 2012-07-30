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
	if (v < 0 || v > 10000)
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
	string endportalfile = imgpath + "/endportal.png";
	string chestfile = imgpath + "/chest.png";
	string largechestfile = imgpath + "/largechest.png";
	string enderchestfile = imgpath + "/enderchest.png";
	if (!construct(B, terrainfile, firefile, endportalfile, chestfile, largechestfile, enderchestfile))
	{
		cerr << "image path is missing at least one of these required files:" << endl;
		cerr << "terrain.png, chest.png, largechest.png, enderchest.png -- from minecraft.jar or your tile pack" << endl;
		cerr << "fire.png, endportal.png -- included with pigmap" << endl;
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

// take the various textures from chest.png and use them to construct "flat" 14x14 tiles (or whatever
//  the multiplied size is, if the textures are larger), then resize those flat images to 2Bx2B
// ...the resulting image will be a 3x1 array of 2Bx2B images: first the top, then the front, then
//  the side
RGBAImage getResizedChest(const RGBAImage& chest, int scale, int B)
{
	int chestSize = 14 * scale;
	RGBAImage chesttiles;
	chesttiles.create(chestSize*3, chestSize);
	
	// top texture just gets copied straight over
	blit(chest, ImageRect(14*scale, 0, 14*scale, 14*scale), chesttiles, 0, 0);
	
	// front tile gets the front lid texture plus the front bottom texture, then the latch on
	//  top of that
	blit(chest, ImageRect(14*scale, 14*scale, 14*scale, 4*scale), chesttiles, chestSize, 0);
	blit(chest, ImageRect(14*scale, 33*scale, 14*scale, 10*scale), chesttiles, chestSize, 4*scale);
	blit(chest, ImageRect(scale, scale, 2*scale, 4*scale), chesttiles, chestSize + 6*scale, 2*scale);
	
	// side tile gets the side lid texture plus the side bottom texture
	blit(chest, ImageRect(28*scale, 14*scale, 14*scale, 4*scale), chesttiles, chestSize*2, 0);
	blit(chest, ImageRect(28*scale, 33*scale, 14*scale, 10*scale), chesttiles, chestSize*2, 4*scale);

	int newsize = 2*B;
	RGBAImage img;
	img.create(3*newsize, newsize);
	for (int x = 0; x < 3; x++)
		resize(chesttiles, ImageRect(x*chestSize, 0, chestSize, chestSize),
		       img, ImageRect(x*newsize, 0, newsize, newsize));
	return img;
}

// same thing for largechest.png--construct flat tiles, then resize
// ...resulting image is a 7x1 array of 2Bx2B images:
//  -left half of top
//  -right half of top
//  -left half of front
//  -right half of front
//  -left half of back
//  -right half of back
//  -side
RGBAImage getResizedLargeChest(const RGBAImage& chest, int scale, int B)
{
	int newsize = 2*B;
	RGBAImage img;
	img.create(7*newsize, newsize);
	
	// top texture gets copied straight over--note that the original texture is 30x14, but
	//  we're putting it into two squares
	resize(chest, ImageRect(14*scale, 0, 30*scale, 14*scale), img, ImageRect(0, 0, newsize*2, newsize));
	
	// front tile gets the front lid texture plus the front bottom texture, then the latch
	//  on top of that
	RGBAImage fronttiles;
	fronttiles.create(30*scale, 14*scale);
	blit(chest, ImageRect(14*scale, 14*scale, 30*scale, 4*scale), fronttiles, 0, 0);
	blit(chest, ImageRect(14*scale, 33*scale, 30*scale, 10*scale), fronttiles, 0, 4*scale);
	blit(chest, ImageRect(scale, scale, 2*scale, 4*scale), fronttiles, 14*scale, 2*scale);
	// do two resizes, to make sure the special end processing picks up the latch
	resize(fronttiles, ImageRect(0, 0, 15*scale, 14*scale), img, ImageRect(2*newsize, 0, newsize, newsize));
	resize(fronttiles, ImageRect(15*scale, 0, 15*scale, 14*scale), img, ImageRect(3*newsize, 0, newsize, newsize));
	
	// back tile gets the back lid texture plus the back bottom texture
	RGBAImage backtiles;
	backtiles.create(30*scale, 14*scale);
	blit(chest, ImageRect(58*scale, 14*scale, 30*scale, 4*scale), backtiles, 0, 0);
	blit(chest, ImageRect(58*scale, 33*scale, 30*scale, 10*scale), backtiles, 0, 4*scale);
	resize(backtiles, ImageRect(0, 0, 30*scale, 14*scale), img, ImageRect(4*newsize, 0, 2*newsize, newsize));
	
	// side tile gets the side lid texture plus the side bottom texture
	RGBAImage sidetile;
	sidetile.create(14*scale, 14*scale);
	blit(chest, ImageRect(44*scale, 14*scale, 14*scale, 4*scale), sidetile, 0, 0);
	blit(chest, ImageRect(44*scale, 33*scale, 14*scale, 10*scale), sidetile, 0, 4*scale);
	resize(sidetile, ImageRect(0, 0, 14*scale, 14*scale), img, ImageRect(6*newsize, 0, newsize, newsize));
	
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


struct SourceTile
{
	const RGBAImage *image;  // or NULL for no tile
	int xpos, ypos;  // tile offset within the image
	int rot;
	bool flipX;
	
	SourceTile(const RGBAImage *img, int x, int y, int r, bool f) : image(img), xpos(x), ypos(y), rot(r), flipX(f) {}
	SourceTile() : image(NULL), xpos(0), ypos(0), rot(0), flipX(false) {}
	bool valid() const {return image != NULL;}
};

// iterate over a square source tile, with possible rotation and flip
struct SourceIterator
{
	SourceIterator(const SourceTile& tile, int tilesize)
		: image(*(tile.image)), faceit(tile.xpos*tilesize, tile.ypos*tilesize, tile.rot, tilesize, tile.flipX) {}
	
	void advance() {faceit.advance();}
	bool end() {return faceit.end;}
	RGBAPixel pixel() {return image(faceit.x, faceit.y);}

	const RGBAImage& image;
	RotatedFaceIterator faceit;
};

// construct a source iterator for a given terrain.png tile with rotation and/or flip
SourceTile terrainTile(const RGBAImage& tiles, int tile, int rot, bool flipX)
{
	if (tile < 0)
		return SourceTile();
	return SourceTile(&tiles, tile%16, tile/16, rot, flipX);
}

// construct a source iterator for a terrain.png tile with no rotation/flip
SourceTile terrainTile(const RGBAImage& tiles, int tile)
{
	return terrainTile(tiles, tile, 0, false);
}



// draw a normal block image, using three terrain tiles (which may be flipped/rotated/missing), and adding a bit of shadow
//  to the N and W faces
void drawRotatedBlockImage(RGBAImage& dest, const ImageRect& drect, const SourceTile& Nface, const SourceTile& Wface, const SourceTile& Uface, int B)
{
	int tilesize = 2*B;
	// N face starts at [0,B]
	if (Nface.valid())
	{
		FaceIterator dstit(drect.x, drect.y + B, 1, tilesize);
		for (SourceIterator srcit(Nface, tilesize); !srcit.end(); srcit.advance(), dstit.advance())
		{
			dest(dstit.x, dstit.y) = srcit.pixel();
			darken(dest(dstit.x, dstit.y), 0.9, 0.9, 0.9);
		}
	}
	// W face starts at [2B,2B]
	if (Wface.valid())
	{
		FaceIterator dstit(drect.x + 2*B, drect.y + 2*B, -1, tilesize);
		for (SourceIterator srcit(Wface, tilesize); !srcit.end(); srcit.advance(), dstit.advance())
		{
			dest(dstit.x, dstit.y) = srcit.pixel();
			darken(dest(dstit.x, dstit.y), 0.8, 0.8, 0.8);
		}
	}
	// U face starts at [2B-1,0]
	if (Uface.valid())
	{
		TopFaceIterator dstit(drect.x + 2*B-1, drect.y, tilesize);
		for (SourceIterator srcit(Uface, tilesize); !srcit.end(); srcit.advance(), dstit.advance())
		{
			dest(dstit.x, dstit.y) = srcit.pixel();
		}
	}
}

// overload of drawRotatedBlockImage taking three terrain.png tiles
void drawRotatedBlockImage(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int Nface, int Wface, int Uface, int rotN, bool flipN, int rotW, bool flipW, int rotU, bool flipU, int B)
{
	drawRotatedBlockImage(dest, drect, terrainTile(tiles, Nface, rotN, flipN), terrainTile(tiles, Wface, rotW, flipW), terrainTile(tiles, Uface, rotU, flipU), B);
}

void drawBlockImage(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int Nface, int Wface, int Uface, int B)
{
	drawRotatedBlockImage(dest, drect, terrainTile(tiles, Nface), terrainTile(tiles, Wface), terrainTile(tiles, Uface), B);
}

// draw a block image where the block isn't full height (half-steps, snow, etc.)
// topcutoff is the number of pixels (out of 2B) to chop off the top of the N and W faces
// bottomcutoff is the number of pixels (out of 2B) to chop off the bottom
// if shift is true, we start copying pixels from the very top of the source tile, even if there's a topcutoff
// U face can also be rotated, and N/W faces can be X-flipped (set 0x1 for N, 0x2 for W)
void drawPartialBlockImage(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int Nface, int Wface, int Uface, int B, int topcutoff, int bottomcutoff, int rot, int flip, bool shift)
{
	int tilesize = 2*B;
	if (topcutoff + bottomcutoff >= tilesize)
		return;
	int end = tilesize - bottomcutoff;
	// N face starts at [0,B]
	if (Nface != -1)
	{
		FaceIterator dstit(drect.x, drect.y + B, 1, tilesize);
		for (RotatedFaceIterator srcit((Nface%16)*tilesize, (Nface/16)*tilesize, 0, tilesize, flip & 0x1); !srcit.end; srcit.advance(), dstit.advance())
		{
			if (dstit.pos % tilesize >= topcutoff && dstit.pos % tilesize < end)
			{
				dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y - (shift ? topcutoff : 0));
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
			if (dstit.pos % tilesize >= topcutoff && dstit.pos % tilesize < end)
			{
				dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y - (shift ? topcutoff : 0));
				darken(dest(dstit.x, dstit.y), 0.8, 0.8, 0.8);
			}
		}
	}
	// U face starts at [2B-1,topcutoff]
	if (Uface != -1)
	{
		TopFaceIterator dstit(drect.x + 2*B-1, drect.y + topcutoff, tilesize);
		for (RotatedFaceIterator srcit((Uface%16)*tilesize, (Uface/16)*tilesize, rot, tilesize, false); !srcit.end; srcit.advance(), dstit.advance())
		{
			dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y);
		}
	}
}

// draw two flat copies of a tile intersecting at the block center (saplings, etc.)
void drawItemBlockImage(RGBAImage& dest, const ImageRect& drect, const SourceTile& tile, int B, bool N, bool S, bool E, bool W)
{
	if (!tile.valid())
		return;
	int tilesize = 2*B;
	int cutoff = tilesize/2;
	// E/W face starting at [B,1.5B] -- southern half only
	if (S)
	{
		FaceIterator dstit(drect.x + B, drect.y + B*3/2, -1, tilesize);
		for (SourceIterator srcit(tile, tilesize); !srcit.end(); srcit.advance(), dstit.advance())
		{
			if (dstit.pos / tilesize >= cutoff)
				blend(dest(dstit.x, dstit.y), srcit.pixel());
		}
	}
	// N/S face starting at [B,0.5B]
	if (E || W)
	{
		FaceIterator dstit(drect.x + B, drect.y + B/2, 1, tilesize);
		for (SourceIterator srcit(tile, tilesize); !srcit.end(); srcit.advance(), dstit.advance())
		{
			if ((W && dstit.pos / tilesize >= cutoff) || (E && dstit.pos / tilesize < cutoff))
				blend(dest(dstit.x, dstit.y), srcit.pixel());
		}
	}
	// E/W face starting at [B,1.5B] -- northern half only
	if (N)
	{
		FaceIterator dstit(drect.x + B, drect.y + B*3/2, -1, tilesize);
		for (SourceIterator srcit(tile, tilesize); !srcit.end(); srcit.advance(), dstit.advance())
		{
			if (dstit.pos / tilesize < cutoff)
				blend(dest(dstit.x, dstit.y), srcit.pixel());
		}
	}
}

void drawItemBlockImage(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int tile, int B)
{
	drawItemBlockImage(dest, drect, terrainTile(tiles, tile), B, true, true, true, true);
}


// draw an item block image possibly missing some edges (iron bars, etc.)
void drawPartialItemBlockImage(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int tile, int rot, bool flipX, int B, bool N, bool S, bool E, bool W)
{
	drawItemBlockImage(dest, drect, terrainTile(tiles, tile, rot, flipX), B, N, S, E, W);
}

// draw four flat copies of a tile intersecting in a square (netherwart, etc.)
void drawMultiItemBlockImage(RGBAImage& dest, const ImageRect& drect, const SourceTile& tile, int B)
{
	if (!tile.valid())
		return;
	int tilesize = 2*B;
	// E/W face starting at [0.5B,1.25B]
	{
		FaceIterator dstit(drect.x + B/2, drect.y + B*5/4, -1, tilesize);
		for (SourceIterator srcit(tile, tilesize); !srcit.end(); srcit.advance(), dstit.advance())
		{
			blend(dest(dstit.x, dstit.y), srcit.pixel());
		}
	}
	// E/W face starting at [1.5B,1.75B]
	{
		FaceIterator dstit(drect.x + 3*B/2, drect.y + B*7/4, -1, tilesize);
		for (SourceIterator srcit(tile, tilesize); !srcit.end(); srcit.advance(), dstit.advance())
		{
			blend(dest(dstit.x, dstit.y), srcit.pixel());
		}
	}
	// N/S face starting at [0.5B,0.75B]
	{
		FaceIterator dstit(drect.x + B/2, drect.y + B*3/4, 1, tilesize);
		for (SourceIterator srcit(tile, tilesize); !srcit.end(); srcit.advance(), dstit.advance())
		{
			blend(dest(dstit.x, dstit.y), srcit.pixel());
		}
	}
	// N/S face starting at [1.5B,0.25B]
	{
		FaceIterator dstit(drect.x + 3*B/2, drect.y + B/4, 1, tilesize);
		for (SourceIterator srcit(tile, tilesize); !srcit.end(); srcit.advance(), dstit.advance())
		{
			blend(dest(dstit.x, dstit.y), srcit.pixel());
		}
	}
}

void drawMultiItemBlockImage(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int tile, int B)
{
	drawMultiItemBlockImage(dest, drect, terrainTile(tiles, tile), B);
}

// draw a tile on a single upright face
// 0 = S, 1 = N, 2 = W, 3 = E
// ...handles transparency
void drawSingleFaceBlockImage(RGBAImage& dest, const ImageRect& drect, const SourceTile& tile, int face, int B)
{
	if (!tile.valid())
		return;
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
	FaceIterator dstit(drect.x + xoff, drect.y + yoff, deltaY, tilesize);
	for (SourceIterator srcit(tile, tilesize); !srcit.end(); srcit.advance(), dstit.advance())
	{
		blend(dest(dstit.x, dstit.y), srcit.pixel());
	}
}

void drawSingleFaceBlockImage(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int tile, int face, int B)
{
	drawSingleFaceBlockImage(dest, drect, terrainTile(tiles, tile), face, B);
}

// draw part of a tile on a single upright face
// 0 = S, 1 = N, 2 = W, 3 = E
// ...handles transparency
void drawPartialSingleFaceBlockImage(RGBAImage& dest, const ImageRect& drect, const SourceTile& tile, int face, int B, double fstartv, double fendv, double fstarth, double fendh)
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
	FaceIterator dstit(drect.x + xoff, drect.y + yoff, deltaY, tilesize);
	for (SourceIterator srcit(tile, tilesize); !srcit.end(); srcit.advance(), dstit.advance())
	{
		if (dstit.pos % tilesize >= vstartcutoff && dstit.pos % tilesize < vendcutoff &&
		    dstit.pos / tilesize >= hstartcutoff && dstit.pos / tilesize < hendcutoff)
			blend(dest(dstit.x, dstit.y), srcit.pixel());
	}
}

void drawPartialSingleFaceBlockImage(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int tile, int face, int B, double fstartv, double fendv, double fstarth, double fendh)
{
	drawPartialSingleFaceBlockImage(dest, drect, terrainTile(tiles, tile), face, B, fstartv, fendv, fstarth, fendh);
}

// draw a single tile on the floor, possibly with rotation
// 0 = top of tile is on S side; 1 = W, 2 = N, 3 = E
void drawFloorBlockImage(RGBAImage& dest, const ImageRect& drect, const SourceTile& tile, int B)
{
	int tilesize = 2*B;
	TopFaceIterator dstit(drect.x + 2*B-1, drect.y + 2*B, tilesize);
	for (SourceIterator srcit(tile, tilesize); !srcit.end(); srcit.advance(), dstit.advance())
	{
		dest(dstit.x, dstit.y) = srcit.pixel();
	}
}

void drawFloorBlockImage(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int tile, int rot, int B)
{
	drawFloorBlockImage(dest, drect, terrainTile(tiles, tile, rot, false), B);
}

// draw part of a single tile on the floor
void drawPartialFloorBlockImage(RGBAImage& dest, const ImageRect& drect, const SourceTile& tile, int B, double fstartv, double fendv, double fstarth, double fendh)
{
	int tilesize = 2*B;
	int vstartcutoff = max(0, min(tilesize, (int)(fstartv * tilesize)));
	int vendcutoff = max(0, min(tilesize, (int)(fendv * tilesize)));
	int hstartcutoff = max(0, min(tilesize, (int)(fstarth * tilesize)));
	int hendcutoff = max(0, min(tilesize, (int)(fendh * tilesize)));
	TopFaceIterator dstit(drect.x + 2*B-1, drect.y + 2*B, tilesize);
	for (SourceIterator srcit(tile, tilesize); !srcit.end(); srcit.advance(), dstit.advance())
	{
		if (dstit.pos % tilesize >= vstartcutoff && dstit.pos % tilesize < vendcutoff &&
		    dstit.pos / tilesize >= hstartcutoff && dstit.pos / tilesize < hendcutoff)
			dest(dstit.x, dstit.y) = srcit.pixel();
	}
}

void drawPartialFloorBlockImage(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int tile, int B, double fstartv, double fendv, double fstarth, double fendh)
{
	drawPartialFloorBlockImage(dest, drect, terrainTile(tiles, tile), B, fstartv, fendv, fstarth, fendh);
}

// draw a single tile on the floor, possibly with rotation, angled upwards
// rot: 0 = top of tile is on S side; 1 = W, 2 = N, 3 = E
// up: 0 = S side of tile is highest; 1 = W, 2 = N, 3 = E
void drawAngledFloorBlockImage(RGBAImage& dest, const ImageRect& drect, const SourceTile& tile, int up, int B)
{
	int tilesize = 2*B;
	TopFaceIterator dstit(drect.x + 2*B-1, drect.y + 2*B, tilesize);
	for (SourceIterator srcit(tile, tilesize); !srcit.end(); srcit.advance(), dstit.advance())
	{
		int yoff = 0;
		int row = dstit.pos % tilesize, col = dstit.pos / tilesize;
		if (up == 0)
			yoff = tilesize - 1 - row;
		else if (up == 1)
			yoff = col;
		else if (up == 2)
			yoff = row;
		else if (up == 3)
			yoff = tilesize - 1 - col;
		blend(dest(dstit.x, dstit.y - yoff), srcit.pixel());
		blend(dest(dstit.x, dstit.y - yoff + 1), srcit.pixel());
	}
}

void drawAngledFloorBlockImage(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int tile, int rot, int up, int B)
{
	drawAngledFloorBlockImage(dest, drect, terrainTile(tiles, tile, rot, false), up, B);
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
void drawStairsS(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int tileNW, int tileU, int B)
{
	int tilesize = 2*B;
	// normal N face starts at [0,B]; draw the bottom half of it
	for (FaceIterator srcit((tileNW%16)*tilesize, (tileNW/16)*tilesize, 0, tilesize),
	     dstit(drect.x, drect.y + B, 1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		if (dstit.pos % tilesize >= B)
		{
			dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y), 0.9, 0.9, 0.9);
		}
	}
	// normal W face starts at [2B,2B]; draw all but the upper-left quarter of it
	for (FaceIterator srcit((tileNW%16)*tilesize, (tileNW/16)*tilesize, 0, tilesize),
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
	for (FaceIterator srcit((tileU%16)*tilesize, (tileU/16)*tilesize, 0, tilesize); !srcit.end; srcit.advance(), tdstit.advance())
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
	for (FaceIterator srcit((tileNW%16)*tilesize, (tileNW/16)*tilesize, 0, tilesize),
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
	for (FaceIterator srcit((tileU%16)*tilesize, (tileU/16)*tilesize, 0, tilesize); !srcit.end; srcit.advance(), tdstit.advance())
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

// draw S-ascending stairs inverted
void drawInvStairsS(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int tileNW, int tileU, int B)
{
	int tilesize = 2*B;
	// draw the bottom half of a N face at [B,B/2]; do this first because the others will partially cover it
	for (FaceIterator srcit((tileNW%16)*tilesize, (tileNW/16)*tilesize, 0, tilesize),
	     dstit(drect.x + B, drect.y + B/2, 1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		// ...but if B is odd, we need to add an extra [0,1] to the even-numbered columns
		int adjust = 0;
		if (B % 2 == 1 && (dstit.pos / tilesize) % 2 == 0)
			adjust = 1;
		if (dstit.pos % tilesize >= B)
		{
			dest(dstit.x, dstit.y + adjust) = tiles(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y + adjust), 0.9, 0.9, 0.9);
		}
	}
	// normal N face starts at [0,B]; draw the top half of it
	for (FaceIterator srcit((tileNW%16)*tilesize, (tileNW/16)*tilesize, 0, tilesize),
	     dstit(drect.x, drect.y + B, 1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		if (dstit.pos % tilesize < B)
		{
			dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y), 0.9, 0.9, 0.9);
		}
	}
	// normal W face starts at [2B,2B]; draw all but the lower-left quarter of it
	for (FaceIterator srcit((tileNW%16)*tilesize, (tileNW/16)*tilesize, 0, tilesize),
	     dstit(drect.x + 2*B, drect.y + 2*B, -1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		if (dstit.pos % tilesize < B || dstit.pos / tilesize >= B)
		{
			dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y), 0.8, 0.8, 0.8);
		}
	}
	// normal U face starts at [2B-1,0]; draw the whole thing
	TopFaceIterator tdstit(drect.x + 2*B-1, drect.y, tilesize);
	for (FaceIterator srcit((tileU%16)*tilesize, (tileU/16)*tilesize, 0, tilesize); !srcit.end; srcit.advance(), tdstit.advance())
	{
		dest(tdstit.x, tdstit.y) = tiles(srcit.x, srcit.y);
	}
}

// draw N-ascending stairs
void drawStairsN(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int tileNW, int tileU, int B)
{
	int tilesize = 2*B;
	// draw the top half of an an U face at [2B-1,B]
	TopFaceIterator tdstit(drect.x + 2*B-1, drect.y + B, tilesize);
	for (FaceIterator srcit((tileU%16)*tilesize, (tileU/16)*tilesize, 0, tilesize); !srcit.end; srcit.advance(), tdstit.advance())
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
	for (FaceIterator srcit((tileU%16)*tilesize, (tileU/16)*tilesize, 0, tilesize); !srcit.end; srcit.advance(), tdstit.advance())
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
	for (FaceIterator srcit((tileNW%16)*tilesize, (tileNW/16)*tilesize, 0, tilesize),
	     dstit(drect.x, drect.y + B, 1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y);
		darken(dest(dstit.x, dstit.y), 0.9, 0.9, 0.9);
	}
	// normal W face starts at [2B,2B]; draw all but the upper-right quarter of it
	for (FaceIterator srcit((tileNW%16)*tilesize, (tileNW/16)*tilesize, 0, tilesize),
	     dstit(drect.x + 2*B, drect.y + 2*B, -1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		if (dstit.pos % tilesize >= B || dstit.pos / tilesize < B)
		{
			dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y), 0.8, 0.8, 0.8);
		}
	}
}

// draw N-ascending stairs inverted
void drawInvStairsN(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int tileNW, int tileU, int B)
{
	int tilesize = 2*B;
	// normal U face starts at [2B-1,0]; draw the whole thing
	TopFaceIterator tdstit(drect.x + 2*B-1, drect.y, tilesize);
	for (FaceIterator srcit((tileU%16)*tilesize, (tileU/16)*tilesize, 0, tilesize); !srcit.end; srcit.advance(), tdstit.advance())
	{
		dest(tdstit.x, tdstit.y) = tiles(srcit.x, srcit.y);
	}
	// normal N face starts at [0,B]; draw it all
	for (FaceIterator srcit((tileNW%16)*tilesize, (tileNW/16)*tilesize, 0, tilesize),
	     dstit(drect.x, drect.y + B, 1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y);
		darken(dest(dstit.x, dstit.y), 0.9, 0.9, 0.9);
	}
	// normal W face starts at [2B,2B]; draw all but the lower-right quarter of it
	for (FaceIterator srcit((tileNW%16)*tilesize, (tileNW/16)*tilesize, 0, tilesize),
	     dstit(drect.x + 2*B, drect.y + 2*B, -1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		if (dstit.pos % tilesize < B || dstit.pos / tilesize < B)
		{
			dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y), 0.8, 0.8, 0.8);
		}
	}
}

// draw E-ascending stairs
void drawStairsE(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int tileNW, int tileU, int B)
{
	int tilesize = 2*B;
	// normal N face starts at [0,B]; draw all but the upper-right quarter of it
	for (FaceIterator srcit((tileNW%16)*tilesize, (tileNW/16)*tilesize, 0, tilesize),
	     dstit(drect.x, drect.y + B, 1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		if (dstit.pos % tilesize >= B || dstit.pos / tilesize < B)
		{
			dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y), 0.9, 0.9, 0.9);
		}
	}
	// normal W face starts at [2B,2B]; draw the bottom half of it
	for (FaceIterator srcit((tileNW%16)*tilesize, (tileNW/16)*tilesize, 0, tilesize),
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
	for (FaceIterator srcit((tileU%16)*tilesize, (tileU/16)*tilesize, 0, tilesize); !srcit.end; srcit.advance(), tdstit.advance())
	{
		if (tdstit.pos < tcutoff || (textra && tdstit.pos == tcutoff + 1))
		{
			dest(tdstit.x, tdstit.y) = tiles(srcit.x, srcit.y);
		}
	}
	// draw the top half of another W face at [B,1.5B]
	for (FaceIterator srcit((tileNW%16)*tilesize, (tileNW/16)*tilesize, 0, tilesize),
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
	for (FaceIterator srcit((tileU%16)*tilesize, (tileU/16)*tilesize, 0, tilesize); !srcit.end; srcit.advance(), tdstit.advance())
	{
		if (tdstit.pos >= tcutoff || (textra && tdstit.pos == tcutoff - 2))
		{
			dest(tdstit.x, tdstit.y) = tiles(srcit.x, srcit.y);
		}
	}
}

// draw E-ascending stairs inverted
void drawInvStairsE(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int tileNW, int tileU, int B)
{
	int tilesize = 2*B;
	// draw the bottom half of a W face at [B,1.5B]; do this first because the others will partially cover it
	for (FaceIterator srcit((tileNW%16)*tilesize, (tileNW/16)*tilesize, 0, tilesize),
	     dstit(drect.x + B, drect.y + 3*B/2, -1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		// ...but if B is odd, we need to add an extra [0,1] to the odd-numbered columns
		int adjust = 0;
		if (B % 2 == 1 && (dstit.pos / tilesize) % 2 == 1)
			adjust = 1;
		if (dstit.pos % tilesize >= B)
		{
			dest(dstit.x, dstit.y + adjust) = tiles(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y + adjust), 0.8, 0.8, 0.8);
		}
	}
	// normal W face starts at [2B,2B]; draw the top half of it
	for (FaceIterator srcit((tileNW%16)*tilesize, (tileNW/16)*tilesize, 0, tilesize),
	     dstit(drect.x + 2*B, drect.y + 2*B, -1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		if (dstit.pos % tilesize < B)
		{
			dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y), 0.8, 0.8, 0.8);
		}
	}
	// normal N face starts at [0,B]; draw all but the lower-right quarter of it
	for (FaceIterator srcit((tileNW%16)*tilesize, (tileNW/16)*tilesize, 0, tilesize),
	     dstit(drect.x, drect.y + B, 1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		if (dstit.pos % tilesize < B || dstit.pos / tilesize < B)
		{
			dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y), 0.9, 0.9, 0.9);
		}
	}
	// normal U face starts at [2B-1,0]; draw the whole thing
	TopFaceIterator tdstit(drect.x + 2*B-1, drect.y, tilesize);
	for (FaceIterator srcit((tileU%16)*tilesize, (tileU/16)*tilesize, 0, tilesize); !srcit.end; srcit.advance(), tdstit.advance())
	{
		dest(tdstit.x, tdstit.y) = tiles(srcit.x, srcit.y);
	}
}

// draw W-ascending stairs
void drawStairsW(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int tileNW, int tileU, int B)
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
	for (FaceIterator srcit((tileU%16)*tilesize, (tileU/16)*tilesize, 0, tilesize); !srcit.end; srcit.advance(), tdstit.advance())
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
	for (FaceIterator srcit((tileU%16)*tilesize, (tileU/16)*tilesize, 0, tilesize); !srcit.end; srcit.advance(), tdstit.advance())
	{
		if (tdstit.pos >= tcutoff || (textra && tdstit.pos == tcutoff - 2))
		{
			dest(tdstit.x, tdstit.y) = tiles(srcit.x, srcit.y);
		}
	}
	// normal N face starts at [0,B]; draw all but the upper-left quarter of it
	for (FaceIterator srcit((tileNW%16)*tilesize, (tileNW/16)*tilesize, 0, tilesize),
	     dstit(drect.x, drect.y + B, 1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		if (dstit.pos % tilesize >= B || dstit.pos / tilesize >= B)
		{
			dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y), 0.9, 0.9, 0.9);
		}
	}
	// normal W face starts at [2B,2B]; draw the whole thing
	for (FaceIterator srcit((tileNW%16)*tilesize, (tileNW/16)*tilesize, 0, tilesize),
	     dstit(drect.x + 2*B, drect.y + 2*B, -1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y);
		darken(dest(dstit.x, dstit.y), 0.8, 0.8, 0.8);
	}
}

// draw W-ascending stairs inverted
void drawInvStairsW(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int tileNW, int tileU, int B)
{
	int tilesize = 2*B;
	// normal U face starts at [2B-1,0]; draw the whole thing
	TopFaceIterator tdstit(drect.x + 2*B-1, drect.y, tilesize);
	for (FaceIterator srcit((tileU%16)*tilesize, (tileU/16)*tilesize, 0, tilesize); !srcit.end; srcit.advance(), tdstit.advance())
	{
		dest(tdstit.x, tdstit.y) = tiles(srcit.x, srcit.y);
	}
	// normal W face starts at [2B,2B]; draw the whole thing
	for (FaceIterator srcit((tileNW%16)*tilesize, (tileNW/16)*tilesize, 0, tilesize),
	     dstit(drect.x + 2*B, drect.y + 2*B, -1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y);
		darken(dest(dstit.x, dstit.y), 0.8, 0.8, 0.8);
	}
	// normal N face starts at [0,B]; draw all but the lower-left quarter of it
	for (FaceIterator srcit((tileNW%16)*tilesize, (tileNW/16)*tilesize, 0, tilesize),
	     dstit(drect.x, drect.y + B, 1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		if (dstit.pos % tilesize < B || dstit.pos / tilesize >= B)
		{
			dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y), 0.9, 0.9, 0.9);
		}
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

// draw fence: post and four rails, each optional
void drawFence(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int tile, bool N, bool S, bool E, bool W, bool post, int B)
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
	if (post)
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

// draw crappy brewing stand: full base tile plus item-shaped stand
void drawBrewingStand(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int base, int stand, int B)
{
	drawFloorBlockImage(dest, drect, tiles, base, 0, B);
	drawItemBlockImage(dest, drect, tiles, stand, B);
}

void drawCauldron(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int side, int liquid, int cutoff, int B)
{
	// start with E/S sides, since liquid goes in front of them
	drawSingleFaceBlockImage(dest, drect, tiles, side, 0, B);
	drawSingleFaceBlockImage(dest, drect, tiles, side, 3, B);
	
	// draw the liquid
	if (liquid != -1)
		drawPartialBlockImage(dest, drect, tiles, -1, -1, liquid, B, cutoff, 0, 0, 0, true);
	
	// now the N/W sides
	drawSingleFaceBlockImage(dest, drect, tiles, side, 1, B);
	drawSingleFaceBlockImage(dest, drect, tiles, side, 2, B);
}

void drawVines(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int tile, int B, bool N, bool S, bool E, bool W, bool top)
{
	if (S)
		drawSingleFaceBlockImage(dest, drect, tiles, tile, 0, B);
	if (E)
		drawSingleFaceBlockImage(dest, drect, tiles, tile, 3, B);
	if (N)
		drawSingleFaceBlockImage(dest, drect, tiles, tile, 1, B);
	if (W)
		drawSingleFaceBlockImage(dest, drect, tiles, tile, 2, B);
	if (top)
		drawCeilBlockImage(dest, drect, tiles, tile, 0, B);
}

// draw crappy dragon egg--just a half-size block
void drawDragonEgg(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tiles, int tile, int B)
{
	int tilesize = 2*B;
	// N face at [0,0.5B]; draw the bottom-right quarter of it
	for (FaceIterator srcit((tile%16)*tilesize, (tile/16)*tilesize, 0, tilesize),
	     dstit(drect.x, drect.y + B/2, 1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		if (dstit.pos % tilesize >= B && dstit.pos / tilesize >= B)
		{
			dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y), 0.9, 0.9, 0.9);
		}
	}
	// W face at [2B,1.5B]; draw the bottom-left quarter of it
	for (FaceIterator srcit((tile%16)*tilesize, (tile/16)*tilesize, 0, tilesize),
	     dstit(drect.x + 2*B, drect.y + 3*B/2, -1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		if (dstit.pos % tilesize >= B && dstit.pos / tilesize < B)
		{
			dest(dstit.x, dstit.y) = tiles(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y), 0.8, 0.8, 0.8);
		}
	}
	// draw the bottom-right quarter of a U face at [2B-1,0.5B]
	TopFaceIterator tdstit(drect.x + 2*B-1, drect.y + B/2, tilesize);
	for (FaceIterator srcit((tile%16)*tilesize, (tile/16)*tilesize, 0, tilesize); !srcit.end; srcit.advance(), tdstit.advance())
	{
		// again, if B is odd, take B pixels from each column; if even, take B-1 or B+1
		int cutoff = B;
		if (B % 2 == 0)
			cutoff += ((tdstit.pos / tilesize) % 2 == 0) ? -1 : 1;
		if (tdstit.pos % tilesize >= cutoff && tdstit.pos / tilesize >= cutoff)
		{
			dest(tdstit.x, tdstit.y) = tiles(srcit.x, srcit.y);
		}
	}
}




int offsetIdx(uint16_t blockID, uint8_t blockData)
{
	return blockID * 16 + blockData;
}

void setOffsetsForID(uint16_t blockID, int offset, BlockImages& bi)
{
	int start = blockID * 16;
	int end = start + 16;
	fill(bi.blockOffsets + start, bi.blockOffsets + end, offset);
}

void BlockImages::setOffsets()
{
	// default is the dummy image
	fill(blockOffsets, blockOffsets + 4096*16, 0);

	//!!!!!! might want to use darker redstone wire for lower strength, just for some visual variety?

	setOffsetsForID(1, 1, *this);
	setOffsetsForID(2, 2, *this);
	setOffsetsForID(3, 3, *this);
	setOffsetsForID(4, 4, *this);
	setOffsetsForID(5, 5, *this);
	blockOffsets[offsetIdx(5, 1)] = 435;
	blockOffsets[offsetIdx(5, 2)] = 436;
	blockOffsets[offsetIdx(5, 3)] = 437;
	setOffsetsForID(6, 6, *this);
	blockOffsets[offsetIdx(6, 1)] = 250;
	blockOffsets[offsetIdx(6, 5)] = 250;
	blockOffsets[offsetIdx(6, 9)] = 250;
	blockOffsets[offsetIdx(6, 13)] = 250;
	blockOffsets[offsetIdx(6, 2)] = 251;
	blockOffsets[offsetIdx(6, 6)] = 251;
	blockOffsets[offsetIdx(6, 10)] = 251;
	blockOffsets[offsetIdx(6, 14)] = 251;
	blockOffsets[offsetIdx(6, 3)] = 429;
	blockOffsets[offsetIdx(6, 7)] = 429;
	blockOffsets[offsetIdx(6, 11)] = 429;
	blockOffsets[offsetIdx(6, 15)] = 429;
	setOffsetsForID(7, 7, *this);
	setOffsetsForID(8, 8, *this);
	blockOffsets[offsetIdx(8, 1)] = 9;
	blockOffsets[offsetIdx(8, 2)] = 10;
	blockOffsets[offsetIdx(8, 3)] = 11;
	blockOffsets[offsetIdx(8, 4)] = 12;
	blockOffsets[offsetIdx(8, 5)] = 13;
	blockOffsets[offsetIdx(8, 6)] = 14;
	blockOffsets[offsetIdx(8, 7)] = 15;
	setOffsetsForID(9, 8, *this);
	blockOffsets[offsetIdx(9, 1)] = 9;
	blockOffsets[offsetIdx(9, 2)] = 10;
	blockOffsets[offsetIdx(9, 3)] = 11;
	blockOffsets[offsetIdx(9, 4)] = 12;
	blockOffsets[offsetIdx(9, 5)] = 13;
	blockOffsets[offsetIdx(9, 6)] = 14;
	blockOffsets[offsetIdx(9, 7)] = 15;
	setOffsetsForID(10, 16, *this);
	blockOffsets[offsetIdx(10, 6)] = 19;
	blockOffsets[offsetIdx(10, 4)] = 18;
	blockOffsets[offsetIdx(10, 2)] = 17;
	setOffsetsForID(11, 16, *this);
	blockOffsets[offsetIdx(11, 6)] = 19;
	blockOffsets[offsetIdx(11, 4)] = 18;
	blockOffsets[offsetIdx(11, 2)] = 17;
	setOffsetsForID(12, 20, *this);
	setOffsetsForID(13, 483, *this);
	setOffsetsForID(14, 22, *this);
	setOffsetsForID(15, 23, *this);
	setOffsetsForID(16, 24, *this);
	setOffsetsForID(17, 25, *this);
	blockOffsets[offsetIdx(17, 1)] = 219;
	blockOffsets[offsetIdx(17, 2)] = 220;
	blockOffsets[offsetIdx(17, 3)] = 427;
	blockOffsets[offsetIdx(17, 4)] = 532;
	blockOffsets[offsetIdx(17, 5)] = 534;
	blockOffsets[offsetIdx(17, 6)] = 536;
	blockOffsets[offsetIdx(17, 7)] = 538;
	blockOffsets[offsetIdx(17, 8)] = 531;
	blockOffsets[offsetIdx(17, 9)] = 533;
	blockOffsets[offsetIdx(17, 10)] = 535;
	blockOffsets[offsetIdx(17, 11)] = 537;
	setOffsetsForID(18, 26, *this);
	blockOffsets[offsetIdx(18, 1)] = 248;
	blockOffsets[offsetIdx(18, 5)] = 248;
	blockOffsets[offsetIdx(18, 9)] = 248;
	blockOffsets[offsetIdx(18, 13)] = 248;
	blockOffsets[offsetIdx(18, 2)] = 249;
	blockOffsets[offsetIdx(18, 6)] = 249;
	blockOffsets[offsetIdx(18, 10)] = 249;
	blockOffsets[offsetIdx(18, 14)] = 249;
	blockOffsets[offsetIdx(18, 3)] = 428;
	blockOffsets[offsetIdx(18, 7)] = 428;
	blockOffsets[offsetIdx(18, 11)] = 428;
	blockOffsets[offsetIdx(18, 15)] = 428;
	setOffsetsForID(19, 27, *this);
	setOffsetsForID(20, 28, *this);
	setOffsetsForID(21, 221, *this);
	setOffsetsForID(22, 222, *this);
	setOffsetsForID(23, 223, *this);
	blockOffsets[offsetIdx(23, 2)] = 225;
	blockOffsets[offsetIdx(23, 4)] = 224;
	blockOffsets[offsetIdx(23, 5)] = 225;
	setOffsetsForID(24, 226, *this);
	blockOffsets[offsetIdx(24, 1)] = 431;
	blockOffsets[offsetIdx(24, 2)] = 432;
	setOffsetsForID(25, 227, *this);
	setOffsetsForID(26, 285, *this);
	blockOffsets[offsetIdx(26, 1)] = 286;
	blockOffsets[offsetIdx(26, 5)] = 286;
	blockOffsets[offsetIdx(26, 2)] = 287;
	blockOffsets[offsetIdx(26, 6)] = 287;
	blockOffsets[offsetIdx(26, 3)] = 288;
	blockOffsets[offsetIdx(26, 7)] = 288;
	blockOffsets[offsetIdx(26, 8)] = 281;
	blockOffsets[offsetIdx(26, 12)] = 281;
	blockOffsets[offsetIdx(26, 9)] = 282;
	blockOffsets[offsetIdx(26, 13)] = 282;
	blockOffsets[offsetIdx(26, 10)] = 283;
	blockOffsets[offsetIdx(26, 14)] = 283;
	blockOffsets[offsetIdx(26, 11)] = 284;
	blockOffsets[offsetIdx(26, 15)] = 284;
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
	setOffsetsForID(29, 413, *this);
	blockOffsets[offsetIdx(29, 1)] = 414;
	blockOffsets[offsetIdx(29, 9)] = 414;
	blockOffsets[offsetIdx(29, 4)] = 415;
	blockOffsets[offsetIdx(29, 12)] = 415;
	blockOffsets[offsetIdx(29, 5)] = 416;
	blockOffsets[offsetIdx(29, 13)] = 416;
	blockOffsets[offsetIdx(29, 3)] = 417;
	blockOffsets[offsetIdx(29, 11)] = 417;
	blockOffsets[offsetIdx(29, 2)] = 418;
	blockOffsets[offsetIdx(29, 10)] = 418;
	setOffsetsForID(30, 272, *this);
	setOffsetsForID(31, 273, *this);
	blockOffsets[offsetIdx(31, 0)] = 275;
	blockOffsets[offsetIdx(31, 2)] = 274;
	setOffsetsForID(32, 275, *this);
	setOffsetsForID(33, 407, *this);
	blockOffsets[offsetIdx(33, 1)] = 408;
	blockOffsets[offsetIdx(33, 9)] = 408;
	blockOffsets[offsetIdx(33, 4)] = 409;
	blockOffsets[offsetIdx(33, 12)] = 409;
	blockOffsets[offsetIdx(33, 5)] = 410;
	blockOffsets[offsetIdx(33, 13)] = 410;
	blockOffsets[offsetIdx(33, 3)] = 411;
	blockOffsets[offsetIdx(33, 11)] = 411;
	blockOffsets[offsetIdx(33, 2)] = 412;
	blockOffsets[offsetIdx(33, 10)] = 412;
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
	blockOffsets[offsetIdx(43, 4)] = 38;
	blockOffsets[offsetIdx(43, 5)] = 294;
	setOffsetsForID(44, 37, *this);
	blockOffsets[offsetIdx(44, 1)] = 229;
	blockOffsets[offsetIdx(44, 2)] = 230;
	blockOffsets[offsetIdx(44, 3)] = 231;
	blockOffsets[offsetIdx(44, 4)] = 302;
	blockOffsets[offsetIdx(44, 5)] = 303;
	blockOffsets[offsetIdx(44, 8)] = 458;
	blockOffsets[offsetIdx(44, 9)] = 459;
	blockOffsets[offsetIdx(44, 10)] = 460;
	blockOffsets[offsetIdx(44, 11)] = 461;
	blockOffsets[offsetIdx(44, 12)] = 462;
	blockOffsets[offsetIdx(44, 13)] = 463;
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
	blockOffsets[offsetIdx(53, 4)] = 438;
	blockOffsets[offsetIdx(53, 5)] = 439;
	blockOffsets[offsetIdx(53, 6)] = 440;
	blockOffsets[offsetIdx(53, 7)] = 441;
	setOffsetsForID(54, 484, *this);
	blockOffsets[offsetIdx(54, 4)] = 485;
	blockOffsets[offsetIdx(54, 2)] = 486;
	blockOffsets[offsetIdx(54, 5)] = 486;
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
	setOffsetsForID(64, 74, *this);
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
	blockOffsets[offsetIdx(67, 4)] = 442;
	blockOffsets[offsetIdx(67, 5)] = 443;
	blockOffsets[offsetIdx(67, 6)] = 444;
	blockOffsets[offsetIdx(67, 7)] = 445;
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
	setOffsetsForID(71, 111, *this);
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
	setOffsetsForID(86, 153, *this);
	blockOffsets[offsetIdx(86, 0)] = 135;
	blockOffsets[offsetIdx(86, 1)] = 154;
	blockOffsets[offsetIdx(86, 3)] = 153;
	setOffsetsForID(87, 136, *this);
	setOffsetsForID(88, 137, *this);
	setOffsetsForID(89, 138, *this);
	setOffsetsForID(90, 139, *this);
	setOffsetsForID(91, 155, *this);
	blockOffsets[offsetIdx(91, 0)] = 140;
	blockOffsets[offsetIdx(91, 1)] = 156;
	blockOffsets[offsetIdx(91, 3)] = 155;
	setOffsetsForID(92, 289, *this);
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
	setOffsetsForID(97, 1, *this);
	blockOffsets[offsetIdx(96, 1)] = 4;
	blockOffsets[offsetIdx(96, 2)] = 294;
	setOffsetsForID(98, 294, *this);
	blockOffsets[offsetIdx(98, 1)] = 295;
	blockOffsets[offsetIdx(98, 2)] = 296;
	blockOffsets[offsetIdx(98, 3)] = 430;
	setOffsetsForID(99, 336, *this);
	blockOffsets[offsetIdx(99, 1)] = 342;
	blockOffsets[offsetIdx(99, 2)] = 341;
	blockOffsets[offsetIdx(99, 3)] = 341;
	blockOffsets[offsetIdx(99, 4)] = 342;
	blockOffsets[offsetIdx(99, 5)] = 341;
	blockOffsets[offsetIdx(99, 6)] = 341;
	blockOffsets[offsetIdx(99, 7)] = 344;
	blockOffsets[offsetIdx(99, 8)] = 343;
	blockOffsets[offsetIdx(99, 9)] = 343;
	blockOffsets[offsetIdx(99, 10)] = 345;
	setOffsetsForID(100, 336, *this);
	blockOffsets[offsetIdx(100, 1)] = 338;
	blockOffsets[offsetIdx(100, 2)] = 337;
	blockOffsets[offsetIdx(100, 3)] = 337;
	blockOffsets[offsetIdx(100, 4)] = 338;
	blockOffsets[offsetIdx(100, 5)] = 337;
	blockOffsets[offsetIdx(100, 6)] = 337;
	blockOffsets[offsetIdx(100, 7)] = 340;
	blockOffsets[offsetIdx(100, 8)] = 339;
	blockOffsets[offsetIdx(100, 9)] = 339;
	blockOffsets[offsetIdx(100, 10)] = 345;
	setOffsetsForID(101, 355, *this);
	setOffsetsForID(102, 366, *this);
	setOffsetsForID(103, 290, *this);
	setOffsetsForID(104, 395, *this);
	blockOffsets[offsetIdx(104, 1)] = 396;
	blockOffsets[offsetIdx(104, 2)] = 397;
	blockOffsets[offsetIdx(104, 3)] = 398;
	blockOffsets[offsetIdx(104, 4)] = 399;
	blockOffsets[offsetIdx(104, 5)] = 400;
	blockOffsets[offsetIdx(104, 6)] = 401;
	blockOffsets[offsetIdx(104, 7)] = 402;
	setOffsetsForID(105, 395, *this);
	blockOffsets[offsetIdx(105, 1)] = 396;
	blockOffsets[offsetIdx(105, 2)] = 397;
	blockOffsets[offsetIdx(105, 3)] = 398;
	blockOffsets[offsetIdx(105, 4)] = 399;
	blockOffsets[offsetIdx(105, 5)] = 400;
	blockOffsets[offsetIdx(105, 6)] = 401;
	blockOffsets[offsetIdx(105, 7)] = 402;
	setOffsetsForID(106, 379, *this);
	blockOffsets[offsetIdx(106, 2)] = 380;
	blockOffsets[offsetIdx(106, 8)] = 381;
	blockOffsets[offsetIdx(106, 10)] = 382;
	blockOffsets[offsetIdx(106, 4)] = 383;
	blockOffsets[offsetIdx(106, 6)] = 384;
	blockOffsets[offsetIdx(106, 12)] = 385;
	blockOffsets[offsetIdx(106, 14)] = 386;
	blockOffsets[offsetIdx(106, 1)] = 387;
	blockOffsets[offsetIdx(106, 3)] = 388;
	blockOffsets[offsetIdx(106, 9)] = 389;
	blockOffsets[offsetIdx(106, 11)] = 390;
	blockOffsets[offsetIdx(106, 5)] = 391;
	blockOffsets[offsetIdx(106, 7)] = 392;
	blockOffsets[offsetIdx(106, 13)] = 393;
	blockOffsets[offsetIdx(106, 15)] = 394;
	setOffsetsForID(107, 347, *this);
	blockOffsets[offsetIdx(107, 1)] = 346;
	blockOffsets[offsetIdx(107, 3)] = 346;
	blockOffsets[offsetIdx(107, 5)] = 346;
	blockOffsets[offsetIdx(107, 7)] = 346;
	setOffsetsForID(108, 304, *this);
	blockOffsets[offsetIdx(108, 1)] = 305;
	blockOffsets[offsetIdx(108, 2)] = 306;
	blockOffsets[offsetIdx(108, 3)] = 307;
	blockOffsets[offsetIdx(108, 4)] = 446;
	blockOffsets[offsetIdx(108, 5)] = 447;
	blockOffsets[offsetIdx(108, 6)] = 448;
	blockOffsets[offsetIdx(108, 7)] = 449;
	setOffsetsForID(109, 308, *this);
	blockOffsets[offsetIdx(109, 1)] = 309;
	blockOffsets[offsetIdx(109, 2)] = 310;
	blockOffsets[offsetIdx(109, 3)] = 311;
	blockOffsets[offsetIdx(109, 4)] = 450;
	blockOffsets[offsetIdx(109, 5)] = 451;
	blockOffsets[offsetIdx(109, 6)] = 452;
	blockOffsets[offsetIdx(109, 7)] = 453;
	setOffsetsForID(110, 291, *this);
	setOffsetsForID(111, 316, *this);
	setOffsetsForID(112, 292, *this);
	setOffsetsForID(113, 332, *this);
	setOffsetsForID(114, 312, *this);
	blockOffsets[offsetIdx(114, 1)] = 313;
	blockOffsets[offsetIdx(114, 2)] = 314;
	blockOffsets[offsetIdx(114, 3)] = 315;
	blockOffsets[offsetIdx(114, 4)] = 454;
	blockOffsets[offsetIdx(114, 5)] = 455;
	blockOffsets[offsetIdx(114, 6)] = 456;
	blockOffsets[offsetIdx(114, 7)] = 457;
	setOffsetsForID(115, 333, *this);
	blockOffsets[offsetIdx(115, 1)] = 334;
	blockOffsets[offsetIdx(115, 2)] = 334;
	blockOffsets[offsetIdx(115, 3)] = 335;
	setOffsetsForID(116, 348, *this);
	setOffsetsForID(117, 350, *this);
	setOffsetsForID(118, 351, *this);
	blockOffsets[offsetIdx(118, 1)] = 352;
	blockOffsets[offsetIdx(118, 2)] = 353;
	blockOffsets[offsetIdx(118, 3)] = 354;
	setOffsetsForID(119, 377, *this);
	setOffsetsForID(120, 349, *this);
	setOffsetsForID(121, 293, *this);
	setOffsetsForID(122, 378, *this);
	setOffsetsForID(123, 434, *this);
	setOffsetsForID(124, 433, *this);
	setOffsetsForID(125, 5, *this);
	blockOffsets[offsetIdx(125, 1)] = 435;
	blockOffsets[offsetIdx(125, 2)] = 436;
	blockOffsets[offsetIdx(125, 3)] = 437;
	setOffsetsForID(126, 230, *this);
	blockOffsets[offsetIdx(126, 1)] = 464;
	blockOffsets[offsetIdx(126, 2)] = 466;
	blockOffsets[offsetIdx(126, 3)] = 468;
	blockOffsets[offsetIdx(126, 8)] = 460;
	blockOffsets[offsetIdx(126, 9)] = 465;
	blockOffsets[offsetIdx(126, 10)] = 467;
	blockOffsets[offsetIdx(126, 11)] = 469;
	setOffsetsForID(127, 522, *this);
	blockOffsets[offsetIdx(127, 1)] = 519;
	blockOffsets[offsetIdx(127, 2)] = 521;
	blockOffsets[offsetIdx(127, 3)] = 520;
	blockOffsets[offsetIdx(127, 4)] = 526;
	blockOffsets[offsetIdx(127, 5)] = 523;
	blockOffsets[offsetIdx(127, 6)] = 525;
	blockOffsets[offsetIdx(127, 7)] = 524;
	blockOffsets[offsetIdx(127, 8)] = 530;
	blockOffsets[offsetIdx(127, 9)] = 527;
	blockOffsets[offsetIdx(127, 10)] = 529;
	blockOffsets[offsetIdx(127, 11)] = 528;
	setOffsetsForID(128, 470, *this);
	blockOffsets[offsetIdx(128, 1)] = 471;
	blockOffsets[offsetIdx(128, 2)] = 472;
	blockOffsets[offsetIdx(128, 3)] = 473;
	blockOffsets[offsetIdx(128, 4)] = 474;
	blockOffsets[offsetIdx(128, 5)] = 475;
	blockOffsets[offsetIdx(128, 6)] = 476;
	blockOffsets[offsetIdx(128, 7)] = 477;
	setOffsetsForID(129, 478, *this);
	setOffsetsForID(130, 479, *this);
	blockOffsets[offsetIdx(130, 4)] = 480;
	blockOffsets[offsetIdx(130, 2)] = 481;
	blockOffsets[offsetIdx(130, 5)] = 481;
	blockOffsets[offsetIdx(131, 0)] = 542;
	blockOffsets[offsetIdx(131, 4)] = 542;
	blockOffsets[offsetIdx(131, 8)] = 542;
	blockOffsets[offsetIdx(131, 12)] = 542;
	blockOffsets[offsetIdx(131, 1)] = 539;
	blockOffsets[offsetIdx(131, 5)] = 539;
	blockOffsets[offsetIdx(131, 9)] = 539;
	blockOffsets[offsetIdx(131, 13)] = 539;
	blockOffsets[offsetIdx(131, 2)] = 541;
	blockOffsets[offsetIdx(131, 6)] = 541;
	blockOffsets[offsetIdx(131, 10)] = 541;
	blockOffsets[offsetIdx(131, 14)] = 541;
	blockOffsets[offsetIdx(131, 3)] = 540;
	blockOffsets[offsetIdx(131, 7)] = 540;
	blockOffsets[offsetIdx(131, 11)] = 540;
	blockOffsets[offsetIdx(131, 15)] = 540;
	setOffsetsForID(132, 543, *this);
	setOffsetsForID(133, 482, *this);
	setOffsetsForID(134, 495, *this);
	blockOffsets[offsetIdx(134, 1)] = 496;
	blockOffsets[offsetIdx(134, 2)] = 497;
	blockOffsets[offsetIdx(134, 3)] = 498;
	blockOffsets[offsetIdx(134, 4)] = 499;
	blockOffsets[offsetIdx(134, 5)] = 500;
	blockOffsets[offsetIdx(134, 6)] = 501;
	blockOffsets[offsetIdx(134, 7)] = 502;
	setOffsetsForID(135, 503, *this);
	blockOffsets[offsetIdx(135, 1)] = 504;
	blockOffsets[offsetIdx(135, 2)] = 505;
	blockOffsets[offsetIdx(135, 3)] = 506;
	blockOffsets[offsetIdx(135, 4)] = 507;
	blockOffsets[offsetIdx(135, 5)] = 508;
	blockOffsets[offsetIdx(135, 6)] = 509;
	blockOffsets[offsetIdx(135, 7)] = 510;
	setOffsetsForID(136, 511, *this);
	blockOffsets[offsetIdx(136, 1)] = 512;
	blockOffsets[offsetIdx(136, 2)] = 513;
	blockOffsets[offsetIdx(136, 3)] = 514;
	blockOffsets[offsetIdx(136, 4)] = 515;
	blockOffsets[offsetIdx(136, 5)] = 516;
	blockOffsets[offsetIdx(136, 6)] = 517;
	blockOffsets[offsetIdx(136, 7)] = 518;
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

int deinterpolate(int targetj, int srcrange, int destrange)
{
	for (int i = 0; i < destrange; i++)
	{
		int j = interpolate(i, destrange, srcrange);
		if (j >= targetj)
			return i;
	}
	return destrange - 1;
}

bool BlockImages::construct(int B, const string& terrainfile, const string& firefile, const string& endportalfile, const string& chestfile, const string& largechestfile, const string& enderchestfile)
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
	
	// read endportal.png, make sure it's okay, and get a resized copy
	RGBAImage endportal;
	if (!endportal.readPNG(endportalfile))
		return false;
	if (endportal.w != endportal.h)
		return false;
	RGBAImage endportaltile;
	endportaltile.create(2*B, 2*B);
	resize(endportal, ImageRect(0, 0, endportal.w, endportal.h), endportaltile, ImageRect(0, 0, 2*B, 2*B));
	
	// read chest.png, make sure it's okay, and build resized tiles
	RGBAImage chest;
	if (!chest.readPNG(chestfile))
		return false;
	if (chest.w % 64 != 0 || chest.h != chest.w)
		return false;
	int chestScale = chest.w / 64;
	RGBAImage chesttiles = getResizedChest(chest, chestScale, B);

	// read enderchest.png, make sure it's okay, and build resized tiles
	RGBAImage enderchest;
	if (!enderchest.readPNG(enderchestfile))
		return false;
	if (enderchest.w % 64 != 0 || enderchest.h != enderchest.w)
		return false;
	int enderchestScale = enderchest.w / 64;
	RGBAImage enderchesttiles = getResizedChest(enderchest, enderchestScale, B);
	
	// read largechest.png, make sure it's okay, and build resized tiles
	RGBAImage largechest;
	if (!largechest.readPNG(largechestfile))
		return false;
	if (largechest.w % 128 != 0 || largechest.h != largechest.w / 2)
		return false;
	int largechestScale = largechest.w / 128;
	RGBAImage largechesttiles = getResizedLargeChest(largechest, largechestScale, B);

	// colorize various tiles
	darken(tiles, ImageRect(0, 0, 2*B, 2*B), 0.6, 0.95, 0.3);  // tile 0 = grass top
	darken(tiles, ImageRect(14*B, 4*B, 2*B, 2*B), 0.6, 0.95, 0.3);  // tile 39 = tall grass
	darken(tiles, ImageRect(16*B, 6*B, 2*B, 2*B), 0.6, 0.95, 0.3);  // tile 56 = fern
	darken(tiles, ImageRect(8*B, 20*B, 2*B, 2*B), 0.9, 0.1, 0.1);  // tile 164 = redstone dust
	darken(tiles, ImageRect(24*B, 8*B, 2*B, 2*B), 0.3, 0.95, 0.3);  // tile 76 = lily pad
	darken(tiles, ImageRect(30*B, 16*B, 2*B, 2*B), 0.35, 1.0, 0.15);  // tile 143 = vines

	// create colorized copies of leaf tiles (can't colorize in place because normal and
	//  birch leaves use the same texture)
	RGBAImage leaftiles;
	leaftiles.create(8*B, 2*B);
	// normal
	blit(tiles, ImageRect(8*B, 6*B, 2*B, 2*B), leaftiles, 0, 0);
	darken(leaftiles, ImageRect(0, 0, 2*B, 2*B), 0.3, 1.0, 0.1);
	// pine
	blit(tiles, ImageRect(8*B, 16*B, 2*B, 2*B), leaftiles, 2*B, 0);
	darken(leaftiles, ImageRect(2*B, 0, 2*B, 2*B), 0.3, 1.0, 0.45);
	// birch
	blit(tiles, ImageRect(8*B, 6*B, 2*B, 2*B), leaftiles, 4*B, 0);
	darken(leaftiles, ImageRect(4*B, 0, 2*B, 2*B), 0.55, 0.9, 0.1);
	// jungle
	blit(tiles, ImageRect(8*B, 24*B, 2*B, 2*B), leaftiles, 6*B, 0);
	darken(leaftiles, ImageRect(6*B, 0, 2*B, 2*B), 0.35, 1.0, 0.05);
	
	// create colorized/shortened copies of stem tiles
	RGBAImage stemtiles;
	stemtiles.create(20*B, 2*B);
	// levels 0-7
	for (int i = 1; i <= 8; i++)
		blit(tiles, ImageRect(30*B, 12*B, 2*B, i*B/4), stemtiles, (i-1)*2*B, 2*B - i*B/4);
	// stem connecting to melon/pumpkin, and flipped version
	blit(tiles, ImageRect(30*B, 14*B, 2*B, 2*B), stemtiles, 16*B, 0);
	blit(tiles, ImageRect(30*B, 14*B, 2*B, 2*B), stemtiles, 18*B, 0);
	flipX(stemtiles, ImageRect(18*B, 0, 2*B, 2*B));
	// green for levels 0-6, brown for level 7 and the connectors
	darken(stemtiles, ImageRect(0, 0, 14*B, 2*B), 0.45, 0.95, 0.4);
	darken(stemtiles, ImageRect(14*B, 0, 6*B, 2*B), 0.75, 0.6, 0.3);

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

	// determine some cutoff values for partial block images: given a particular pixel offset in terrain.png--for
	//  example, the end portal frame texture is missing its top 3 (out of 16) pixels--we need to know which pixel
	//  in the resized tile is the first one past that offset
	// ...if the terrain tile size isn't a multiple of 16 for some reason, this may break down and be ugly
	int CUTOFF_2_16 = deinterpolate(2 * terrainSize/16, terrainSize, 2*B);
	int CUTOFF_3_16 = deinterpolate(3 * terrainSize/16, terrainSize, 2*B);
	int CUTOFF_4_16 = deinterpolate(4 * terrainSize/16, terrainSize, 2*B);
	int CUTOFF_6_16 = deinterpolate(6 * terrainSize/16, terrainSize, 2*B);
	int CUTOFF_8_16 = deinterpolate(8 * terrainSize/16, terrainSize, 2*B);
	int CUTOFF_10_16 = deinterpolate(10 * terrainSize/16, terrainSize, 2*B);
	int CUTOFF_12_16 = deinterpolate(12 * terrainSize/16, terrainSize, 2*B);
	int CUTOFF_14_16 = deinterpolate(14 * terrainSize/16, terrainSize, 2*B);

	// initialize image
	img.create(rectsize * 16, (NUMBLOCKIMAGES/16 + 1) * rectsize);

	// build all block images

	drawBlockImage(img, getRect(1), tiles, 1, 1, 1, B);  // stone
	drawBlockImage(img, getRect(2), tiles, 3, 3, 0, B);  // grass
	drawBlockImage(img, getRect(3), tiles, 2, 2, 2, B);  // dirt
	drawBlockImage(img, getRect(4), tiles, 16, 16, 16, B);  // cobblestone
	drawBlockImage(img, getRect(5), tiles, 4, 4, 4, B);  // planks
	drawBlockImage(img, getRect(435), tiles, 198, 198, 198, B);  // pine planks
	drawBlockImage(img, getRect(436), tiles, 214, 214, 214, B);  // birch planks
	drawBlockImage(img, getRect(437), tiles, 199, 199, 199, B);  // jungle planks
	drawBlockImage(img, getRect(7), tiles, 17, 17, 17, B);  // bedrock
	drawBlockImage(img, getRect(8), tiles, 205, 205, 205, B);  // full water
	drawBlockImage(img, getRect(157), tiles, -1, -1, 205, B);  // water surface
	drawBlockImage(img, getRect(178), tiles, 205, -1, 205, B);  // water missing W
	drawBlockImage(img, getRect(179), tiles, -1, 205, 205, B);  // water missing N
	drawBlockImage(img, getRect(16), tiles, 237, 237, 237, B);  // full lava
	drawBlockImage(img, getRect(20), tiles, 18, 18, 18, B);  // sand
	drawBlockImage(img, getRect(483), tiles, 19, 19, 19, B);  // gravel
	drawBlockImage(img, getRect(22), tiles, 32, 32, 32, B);  // gold ore
	drawBlockImage(img, getRect(23), tiles, 33, 33, 33, B);  // iron ore
	drawBlockImage(img, getRect(24), tiles, 34, 34, 34, B);  // coal ore
	drawBlockImage(img, getRect(25), tiles, 20, 20, 21, B);  // log
	drawBlockImage(img, getRect(219), tiles, 116, 116, 21, B);  // pine log
	drawBlockImage(img, getRect(220), tiles, 117, 117, 21, B);  // birch log
	drawBlockImage(img, getRect(427), tiles, 153, 153, 21, B);  // jungle log
	drawBlockImage(img, getRect(26), leaftiles, 0, 0, 0, B);  // leaves
	drawBlockImage(img, getRect(248), leaftiles, 1, 1, 1, B);  // pine leaves
	drawBlockImage(img, getRect(249), leaftiles, 2, 2, 2, B);  // birch leaves
	drawBlockImage(img, getRect(428), leaftiles, 3, 3, 3, B);  // jungle leaves
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
	drawBlockImage(img, getRect(484), chesttiles, 2, 1, 0, B);  // chest facing W
	drawBlockImage(img, getRect(485), chesttiles, 1, 2, 0, B);  // chest facing N
	drawBlockImage(img, getRect(486), chesttiles, 2, 2, 0, B);  // chest facing E/S
	drawBlockImage(img, getRect(479), enderchesttiles, 2, 1, 0, B);  // ender chest facing W
	drawBlockImage(img, getRect(480), enderchesttiles, 1, 2, 0, B);  // ender chest facing N
	drawBlockImage(img, getRect(481), enderchesttiles, 2, 2, 0, B);  // ender chest facing E/S
	drawBlockImage(img, getRect(489), largechesttiles, 2, 6, 0, B);  // double chest E facing N
	drawBlockImage(img, getRect(490), largechesttiles, 3, 6, 1, B);  // double chest W facing N
	drawBlockImage(img, getRect(493), largechesttiles, 4, 6, 0, B);  // double chest E facing S
	drawBlockImage(img, getRect(494), largechesttiles, 5, 6, 1, B);  // double chest W facing S
	drawBlockImage(img, getRect(270), chesttiles, 2, 1, 0, B);  // locked chest facing W
	drawBlockImage(img, getRect(271), chesttiles, 1, 2, 0, B);  // locked chest facing N
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
	drawBlockImage(img, getRect(431), tiles, 229, 229, 176, B);  // hieroglyphic sandstone
	drawBlockImage(img, getRect(432), tiles, 230, 230, 176, B);  // smooth sandstone
	drawBlockImage(img, getRect(227), tiles, 74, 74, 74, B);  // note block
	drawBlockImage(img, getRect(290), tiles, 136, 136, 137, B);  // melon
	drawBlockImage(img, getRect(291), tiles, 77, 77, 78, B);  // mycelium
	drawBlockImage(img, getRect(292), tiles, 224, 224, 224, B);  // nether brick
	drawBlockImage(img, getRect(293), tiles, 175, 175, 175, B);  // end stone
	drawBlockImage(img, getRect(294), tiles, 54, 54, 54, B);  // stone brick
	drawBlockImage(img, getRect(295), tiles, 100, 100, 100, B);  // mossy stone brick
	drawBlockImage(img, getRect(296), tiles, 101, 101, 101, B);  // cracked stone brick
	drawBlockImage(img, getRect(430), tiles, 213, 213, 213, B);  // circle stone brick
	drawBlockImage(img, getRect(336), tiles, 142, 142, 142, B);  // mushroom flesh
	drawBlockImage(img, getRect(337), tiles, 142, 142, 125, B);  // red cap top only
	drawBlockImage(img, getRect(338), tiles, 125, 142, 125, B);  // red cap N
	drawBlockImage(img, getRect(339), tiles, 142, 125, 125, B);  // red cap W
	drawBlockImage(img, getRect(340), tiles, 125, 125, 125, B);  // red cap NW
	drawBlockImage(img, getRect(341), tiles, 142, 142, 126, B);  // brown cap top only
	drawBlockImage(img, getRect(342), tiles, 126, 142, 126, B);  // brown cap N
	drawBlockImage(img, getRect(343), tiles, 142, 126, 126, B);  // brown cap W
	drawBlockImage(img, getRect(344), tiles, 126, 126, 126, B);  // brown cap NW
	drawBlockImage(img, getRect(345), tiles, 141, 141, 142, B);  // mushroom stem
	drawBlockImage(img, getRect(433), tiles, 212, 212, 212, B);  // redstone lamp on
	drawBlockImage(img, getRect(434), tiles, 211, 211, 211, B);  // redstone lamp off
	drawBlockImage(img, getRect(478), tiles, 171, 171, 171, B);  // emerald ore
	drawBlockImage(img, getRect(482), tiles, 25, 25, 25, B);  // emerald block
	drawRotatedBlockImage(img, getRect(407), tiles, 108, 108, 109, 2, false, 2, false, 0, false, B);  // closed piston D
	drawRotatedBlockImage(img, getRect(408), tiles, 108, 108, 107, 0, false, 0, false, 0, false, B);  // closed piston U
	drawRotatedBlockImage(img, getRect(409), tiles, 107, 108, 108, 0, false, 1, false, 2, false, B);  // closed piston N
	drawRotatedBlockImage(img, getRect(410), tiles, 109, 108, 108, 0, false, 3, false, 0, false, B);  // closed piston S
	drawRotatedBlockImage(img, getRect(411), tiles, 108, 107, 108, 3, false, 0, false, 3, false, B);  // closed piston W
	drawRotatedBlockImage(img, getRect(412), tiles, 108, 109, 108, 1, false, 0, false, 1, false, B);  // closed piston E
	drawRotatedBlockImage(img, getRect(413), tiles, 108, 108, 109, 2, false, 2, false, 0, false, B);  // closed sticky piston D
	drawRotatedBlockImage(img, getRect(414), tiles, 108, 108, 106, 0, false, 0, false, 0, false, B);  // closed sticky piston U
	drawRotatedBlockImage(img, getRect(415), tiles, 106, 108, 108, 0, false, 1, false, 2, false, B);  // closed sticky piston N
	drawRotatedBlockImage(img, getRect(416), tiles, 109, 108, 108, 0, false, 3, false, 0, false, B);  // closed sticky piston S
	drawRotatedBlockImage(img, getRect(417), tiles, 108, 106, 108, 3, false, 0, false, 3, false, B);  // closed sticky piston W
	drawRotatedBlockImage(img, getRect(418), tiles, 108, 109, 108, 1, false, 0, false, 1, false, B);  // closed sticky piston E
	drawRotatedBlockImage(img, getRect(487), largechesttiles, 6, 2, 0, 0, false, 0, false, 1, false, B);  // double chest N facing W
	drawRotatedBlockImage(img, getRect(488), largechesttiles, 6, 3, 1, 0, false, 0, false, 1, false, B);  // double chest S facing W
	drawRotatedBlockImage(img, getRect(491), largechesttiles, 6, 4, 0, 0, false, 0, false, 1, false, B);  // double chest N facing E
	drawRotatedBlockImage(img, getRect(492), largechesttiles, 6, 5, 1, 0, false, 0, false, 1, false, B);  // double chest S facing E
	drawRotatedBlockImage(img, getRect(531), tiles, 20, 21, 20, 1, false, 0, false, 1, false, B);  // log EW
	drawRotatedBlockImage(img, getRect(532), tiles, 21, 20, 20, 0, false, 3, false, 0, false, B);  // log NS
	drawRotatedBlockImage(img, getRect(533), tiles, 116, 21, 116, 1, false, 0, false, 1, false, B);  // pine log EW
	drawRotatedBlockImage(img, getRect(534), tiles, 21, 116, 116, 0, false, 3, false, 0, false, B);  // pine log NS
	drawRotatedBlockImage(img, getRect(535), tiles, 117, 21, 117, 1, false, 0, false, 1, false, B);  // birch log EW
	drawRotatedBlockImage(img, getRect(536), tiles, 21, 117, 117, 0, false, 3, false, 0, false, B);  // birch log NS
	drawRotatedBlockImage(img, getRect(537), tiles, 153, 21, 153, 1, false, 0, false, 1, false, B);  // jungle log EW
	drawRotatedBlockImage(img, getRect(538), tiles, 21, 153, 153, 0, false, 3, false, 0, false, B);  // jungle log NS

	drawPartialBlockImage(img, getRect(9), tiles, 205, 205, 205, B, CUTOFF_2_16, 0, 0, 0, true);  // water level 7
	drawPartialBlockImage(img, getRect(10), tiles, 205, 205, 205, B, CUTOFF_4_16, 0, 0, 0, true);  // water level 6
	drawPartialBlockImage(img, getRect(11), tiles, 205, 205, 205, B, CUTOFF_6_16, 0, 0, 0, true);  // water level 5
	drawPartialBlockImage(img, getRect(12), tiles, 205, 205, 205, B, CUTOFF_8_16, 0, 0, 0, true);  // water level 4
	drawPartialBlockImage(img, getRect(13), tiles, 205, 205, 205, B, CUTOFF_10_16, 0, 0, 0, true);  // water level 3
	drawPartialBlockImage(img, getRect(14), tiles, 205, 205, 205, B, CUTOFF_12_16, 0, 0, 0, true);  // water level 2
	drawPartialBlockImage(img, getRect(15), tiles, 205, 205, 205, B, CUTOFF_14_16, 0, 0, 0, true);  // water level 1
	drawPartialBlockImage(img, getRect(17), tiles, 237, 237, 237, B, CUTOFF_4_16, 0, 0, 0, true);  // lava level 3
	drawPartialBlockImage(img, getRect(18), tiles, 237, 237, 237, B, CUTOFF_8_16, 0, 0, 0, true);  // lava level 2
	drawPartialBlockImage(img, getRect(19), tiles, 237, 237, 237, B, CUTOFF_12_16, 0, 0, 0, true);  // lava level 1
	drawPartialBlockImage(img, getRect(37), tiles, 5, 5, 6, B, CUTOFF_8_16, 0, 0, 0, true);  // stone slab
	drawPartialBlockImage(img, getRect(229), tiles, 192, 192, 176, B, CUTOFF_8_16, 0, 0, 0, true);  // sandstone slab
	drawPartialBlockImage(img, getRect(230), tiles, 4, 4, 4, B, CUTOFF_8_16, 0, 0, 0, true);  // wooden slab
	drawPartialBlockImage(img, getRect(231), tiles, 16, 16, 16, B, CUTOFF_8_16, 0, 0, 0, true);  // cobble slab
	drawPartialBlockImage(img, getRect(302), tiles, 7, 7, 7, B, CUTOFF_8_16, 0, 0, 0, true);  // brick slab
	drawPartialBlockImage(img, getRect(303), tiles, 54, 54, 54, B, CUTOFF_8_16, 0, 0, 0, true);  // stone brick slab
	drawPartialBlockImage(img, getRect(464), tiles, 198, 198, 198, B, CUTOFF_8_16, 0, 0, 0, true);  // pine slab
	drawPartialBlockImage(img, getRect(466), tiles, 214, 214, 214, B, CUTOFF_8_16, 0, 0, 0, true);  // birch slab
	drawPartialBlockImage(img, getRect(468), tiles, 199, 199, 199, B, CUTOFF_8_16, 0, 0, 0, true);  // jungle slab
	drawPartialBlockImage(img, getRect(458), tiles, 5, 5, 6, B, 0, CUTOFF_8_16, 0, 0, false);  // stone slab inv
	drawPartialBlockImage(img, getRect(459), tiles, 192, 192, 176, B, 0, CUTOFF_8_16, 0, 0, false);  // sandstone slab inv
	drawPartialBlockImage(img, getRect(460), tiles, 4, 4, 4, B, 0, CUTOFF_8_16, 0, 0, false);  // wooden slab inv
	drawPartialBlockImage(img, getRect(461), tiles, 16, 16, 16, B, 0, CUTOFF_8_16, 0, 0, false);  // cobble slab inv
	drawPartialBlockImage(img, getRect(462), tiles, 7, 7, 7, B, 0, CUTOFF_8_16, 0, 0, false);  // brick slab inv
	drawPartialBlockImage(img, getRect(463), tiles, 54, 54, 54, B, 0, CUTOFF_8_16, 0, 0, false);  // stone brick slab inv
	drawPartialBlockImage(img, getRect(465), tiles, 198, 198, 198, B, 0, CUTOFF_8_16, 0, 0, false);  // pine slab inv
	drawPartialBlockImage(img, getRect(467), tiles, 214, 214, 214, B, 0, CUTOFF_8_16, 0, 0, false);  // birch slab inv
	drawPartialBlockImage(img, getRect(469), tiles, 199, 199, 199, B, 0, CUTOFF_8_16, 0, 0, false);  // jungle slab inv
	drawPartialBlockImage(img, getRect(110), tiles, 1, 1, 1, B, CUTOFF_14_16, 0, 0, 0, true);  // stone pressure plate
	drawPartialBlockImage(img, getRect(119), tiles, 4, 4, 4, B, CUTOFF_14_16, 0, 0, 0, true);  // wood pressure plate
	drawPartialBlockImage(img, getRect(127), tiles, 66, 66, 66, B, CUTOFF_12_16, 0, 0, 0, true);  // snow
	drawPartialBlockImage(img, getRect(289), tiles, 122, 122, 121, B, CUTOFF_8_16, 0, 0, 0, false);  // cake
	drawPartialBlockImage(img, getRect(281), tiles, 151, 152, 135, B, CUTOFF_8_16, 0, 0, 0, false);  // bed head W
	drawPartialBlockImage(img, getRect(282), tiles, 152, 151, 135, B, CUTOFF_8_16, 0, 3, 2, false);  // bed head N
	drawPartialBlockImage(img, getRect(283), tiles, 151, -1, 135, B, CUTOFF_8_16, 0, 2, 1, false);  // bed head E
	drawPartialBlockImage(img, getRect(284), tiles, -1, 151, 135, B, CUTOFF_8_16, 0, 1, 0, false);  // bed head S
	drawPartialBlockImage(img, getRect(285), tiles, 150, -1, 134, B, CUTOFF_8_16, 0, 0, 0, false);  // bed foot W
	drawPartialBlockImage(img, getRect(286), tiles, -1, 150, 134, B, CUTOFF_8_16, 0, 3, 2, false);  // bed foot N
	drawPartialBlockImage(img, getRect(287), tiles, 150, 149, 134, B, CUTOFF_8_16, 0, 2, 1, false);  // bed foot E
	drawPartialBlockImage(img, getRect(288), tiles, 149, 150, 134, B, CUTOFF_8_16, 0, 1, 0, false);  // bed foot S
	drawPartialBlockImage(img, getRect(348), tiles, 182, 182, 166, B, CUTOFF_4_16, 0, 0, 0, false);  // enchantment table
	drawPartialBlockImage(img, getRect(349), tiles, 159, 159, 158, B, CUTOFF_3_16, 0, 0, 0, false);  // end portal frame
	drawPartialBlockImage(img, getRect(377), endportaltile, 0, 0, 0, B, CUTOFF_4_16, 0, 0, 0, true);  // end portal

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
	drawItemBlockImage(img, getRect(429), tiles, 30, B);  // birch sapling
	drawItemBlockImage(img, getRect(272), tiles, 11, B);  // web
	drawItemBlockImage(img, getRect(273), tiles, 39, B);  // tall grass
	drawItemBlockImage(img, getRect(274), tiles, 56, B);  // fern
	drawItemBlockImage(img, getRect(275), tiles, 55, B);  // dead shrub
	drawMultiItemBlockImage(img, getRect(333), tiles, 226, B);  // netherwart small
	drawMultiItemBlockImage(img, getRect(334), tiles, 227, B);  // netherwart medium
	drawMultiItemBlockImage(img, getRect(335), tiles, 228, B);  // netherwart large
	drawItemBlockImage(img, getRect(355), tiles, 85, B);  // iron bars NSEW
	drawPartialItemBlockImage(img, getRect(356), tiles, 85, 0, false, B, true, true, false, false);  // iron bars NS
	drawPartialItemBlockImage(img, getRect(357), tiles, 85, 0, false, B, true, false, true, false);  // iron bars NE
	drawPartialItemBlockImage(img, getRect(358), tiles, 85, 0, false, B, true, false, false, true);  // iron bars NW
	drawPartialItemBlockImage(img, getRect(359), tiles, 85, 0, false, B, false, true, true, false);  // iron bars SE
	drawPartialItemBlockImage(img, getRect(360), tiles, 85, 0, false, B, false, true, false, true);  // iron bars SW
	drawPartialItemBlockImage(img, getRect(361), tiles, 85, 0, false, B, false, false, true, true);  // iron bars EW
	drawPartialItemBlockImage(img, getRect(362), tiles, 85, 0, false, B, false, true, true, true);  // iron bars SEW
	drawPartialItemBlockImage(img, getRect(363), tiles, 85, 0, false, B, true, false, true, true);  // iron bars NEW
	drawPartialItemBlockImage(img, getRect(364), tiles, 85, 0, false, B, true, true, false, true);  // iron bars NSW
	drawPartialItemBlockImage(img, getRect(365), tiles, 85, 0, false, B, true, true, true, false);  // iron bars NSE
	drawPartialItemBlockImage(img, getRect(419), tiles, 85, 0, false, B, true, false, false, false);  // iron bars N
	drawPartialItemBlockImage(img, getRect(420), tiles, 85, 0, false, B, false, true, false, false);  // iron bars S
	drawPartialItemBlockImage(img, getRect(421), tiles, 85, 0, false, B, false, false, true, false);  // iron bars E
	drawPartialItemBlockImage(img, getRect(422), tiles, 85, 0, false, B, false, false, false, true);  // iron bars W
	drawItemBlockImage(img, getRect(366), tiles, 49, B);  // glass pane NSEW
	drawPartialItemBlockImage(img, getRect(367), tiles, 49, 0, false, B, true, true, false, false);  // glass pane NS
	drawPartialItemBlockImage(img, getRect(368), tiles, 49, 0, false, B, true, false, true, false);  // glass pane NE
	drawPartialItemBlockImage(img, getRect(369), tiles, 49, 0, false, B, true, false, false, true);  // glass pane NW
	drawPartialItemBlockImage(img, getRect(370), tiles, 49, 0, false, B, false, true, true, false);  // glass pane SE
	drawPartialItemBlockImage(img, getRect(371), tiles, 49, 0, false, B, false, true, false, true);  // glass pane SW
	drawPartialItemBlockImage(img, getRect(372), tiles, 49, 0, false, B, false, false, true, true);  // glass pane EW
	drawPartialItemBlockImage(img, getRect(373), tiles, 49, 0, false, B, false, true, true, true);  // glass pane SEW
	drawPartialItemBlockImage(img, getRect(374), tiles, 49, 0, false, B, true, false, true, true);  // glass pane NEW
	drawPartialItemBlockImage(img, getRect(375), tiles, 49, 0, false, B, true, true, false, true);  // glass pane NSW
	drawPartialItemBlockImage(img, getRect(376), tiles, 49, 0, false, B, true, true, true, false);  // glass pane NSE
	drawPartialItemBlockImage(img, getRect(423), tiles, 49, 0, false, B, true, false, false, false);  // glass pane N
	drawPartialItemBlockImage(img, getRect(424), tiles, 49, 0, false, B, false, true, false, false);  // glass pane S
	drawPartialItemBlockImage(img, getRect(425), tiles, 49, 0, false, B, false, false, true, false);  // glass pane E
	drawPartialItemBlockImage(img, getRect(426), tiles, 49, 0, false, B, false, false, false, true);  // glass pane W
	drawItemBlockImage(img, getRect(395), stemtiles, 0, B);  // stem level 0
	drawItemBlockImage(img, getRect(396), stemtiles, 1, B);  // stem level 1
	drawItemBlockImage(img, getRect(397), stemtiles, 2, B);  // stem level 2
	drawItemBlockImage(img, getRect(398), stemtiles, 3, B);  // stem level 3
	drawItemBlockImage(img, getRect(399), stemtiles, 4, B);  // stem level 4
	drawItemBlockImage(img, getRect(400), stemtiles, 5, B);  // stem level 5
	drawItemBlockImage(img, getRect(401), stemtiles, 6, B);  // stem level 6
	drawItemBlockImage(img, getRect(402), stemtiles, 7, B);  // stem level 7
	drawPartialItemBlockImage(img, getRect(403), stemtiles, 8, 0, false, B, true, true, false, false);  // stem pointing N
	drawPartialItemBlockImage(img, getRect(404), stemtiles, 9, 0, false, B, true, true, false, false);  // stem pointing S
	drawPartialItemBlockImage(img, getRect(405), stemtiles, 8, 0, false, B, false, false, true, true);  // stem pointing E
	drawPartialItemBlockImage(img, getRect(406), stemtiles, 9, 0, false, B, false, false, true, true);  // stem pointing W
	drawPartialItemBlockImage(img, getRect(519), tiles, 170, 0, true, B, true, false, false, false);  // cocoa level 0 stem N
	drawPartialItemBlockImage(img, getRect(520), tiles, 170, 0, false, B, false, true, false, false);  // cocoa level 0 stem S
	drawPartialItemBlockImage(img, getRect(521), tiles, 170, 0, true, B, false, false, true, false);  // cocoa level 0 stem E
	drawPartialItemBlockImage(img, getRect(522), tiles, 170, 0, false, B, false, false, false, true);  // cocoa level 0 stem W
	drawPartialItemBlockImage(img, getRect(523), tiles, 169, 0, true, B, true, false, false, false);  // cocoa level 1 stem N
	drawPartialItemBlockImage(img, getRect(524), tiles, 169, 0, false, B, false, true, false, false);  // cocoa level 1 stem S
	drawPartialItemBlockImage(img, getRect(525), tiles, 169, 0, true, B, false, false, true, false);  // cocoa level 1 stem E
	drawPartialItemBlockImage(img, getRect(526), tiles, 169, 0, false, B, false, false, false, true);  // cocoa level 1 stem W
	drawPartialItemBlockImage(img, getRect(527), tiles, 168, 0, true, B, true, false, false, false);  // cocoa level 2 stem N
	drawPartialItemBlockImage(img, getRect(528), tiles, 168, 0, false, B, false, true, false, false);  // cocoa level 2 stem S
	drawPartialItemBlockImage(img, getRect(529), tiles, 168, 0, true, B, false, false, true, false);  // cocoa level 2 stem E
	drawPartialItemBlockImage(img, getRect(530), tiles, 168, 0, false, B, false, false, false, true);  // cocoa level 2 stem W
	drawPartialItemBlockImage(img, getRect(543), tiles, 173, 2, false, B, true, true, true, true);  // tripwire NSEW
	drawPartialItemBlockImage(img, getRect(544), tiles, 173, 2, false, B, true, true, false, false);  // tripwire NS
	drawPartialItemBlockImage(img, getRect(545), tiles, 173, 2, false, B, true, false, true, false);  // tripwire NE
	drawPartialItemBlockImage(img, getRect(546), tiles, 173, 2, false, B, true, false, false, true);  // tripwire NW
	drawPartialItemBlockImage(img, getRect(547), tiles, 173, 2, false, B, false, true, true, false);  // tripwire SE
	drawPartialItemBlockImage(img, getRect(548), tiles, 173, 2, false, B, false, true, false, true);  // tripwire SW
	drawPartialItemBlockImage(img, getRect(549), tiles, 173, 2, false, B, false, false, true, true);  // tripwire EW
	drawPartialItemBlockImage(img, getRect(550), tiles, 173, 2, false, B, false, true, true, true);  // tripwire SEW
	drawPartialItemBlockImage(img, getRect(551), tiles, 173, 2, false, B, true, false, true, true);  // tripwire NEW
	drawPartialItemBlockImage(img, getRect(552), tiles, 173, 2, false, B, true, true, false, true);  // tripwire NSW
	drawPartialItemBlockImage(img, getRect(553), tiles, 173, 2, false, B, true, true, true, false);  // tripwire NSE

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
	drawSingleFaceBlockImage(img, getRect(539), tiles, 172, 0, B);  // tripwire hook S
	drawSingleFaceBlockImage(img, getRect(540), tiles, 172, 1, B);  // tripwire hook N
	drawSingleFaceBlockImage(img, getRect(541), tiles, 172, 2, B);  // tripwire hook W
	drawSingleFaceBlockImage(img, getRect(542), tiles, 172, 3, B);  // tripwire hook E

	drawPartialSingleFaceBlockImage(img, getRect(100), tiles, 4, 2, B, 0.25, 0.75, 0, 1);  // wall sign facing E
	drawPartialSingleFaceBlockImage(img, getRect(101), tiles, 4, 3, B, 0.25, 0.75, 0, 1);  // wall sign facing W
	drawPartialSingleFaceBlockImage(img, getRect(102), tiles, 4, 0, B, 0.25, 0.75, 0, 1);  // wall sign facing N
	drawPartialSingleFaceBlockImage(img, getRect(103), tiles, 4, 1, B, 0.25, 0.75, 0, 1);  // wall sign facing S
	drawPartialSingleFaceBlockImage(img, getRect(190), tiles, 1, 1, B, 0.35, 0.65, 0.35, 0.65);  // stone button facing S
	drawPartialSingleFaceBlockImage(img, getRect(191), tiles, 1, 0, B, 0.35, 0.65, 0.35, 0.65);  // stone button facing N
	drawPartialSingleFaceBlockImage(img, getRect(192), tiles, 1, 3, B, 0.35, 0.65, 0.35, 0.65);  // stone button facing W
	drawPartialSingleFaceBlockImage(img, getRect(193), tiles, 1, 2, B, 0.35, 0.65, 0.35, 0.65);  // stone button facing E

	drawSolidColorBlockImage(img, getRect(139), 0xd07b2748, B);  // portal

	drawStairsS(img, getRect(50), tiles, 4, 4, B);  // wood stairs asc S
	drawStairsN(img, getRect(51), tiles, 4, 4, B);  // wood stairs asc N
	drawStairsW(img, getRect(52), tiles, 4, 4, B);  // wood stairs asc W
	drawStairsE(img, getRect(53), tiles, 4, 4, B);  // wood stairs asc E
	drawStairsS(img, getRect(96), tiles, 16, 16, B);  // cobble stairs asc S
	drawStairsN(img, getRect(97), tiles, 16, 16, B);  // cobble stairs asc N
	drawStairsW(img, getRect(98), tiles, 16, 16, B);  // cobble stairs asc W
	drawStairsE(img, getRect(99), tiles, 16, 16, B);  // cobble stairs asc E
	drawStairsS(img, getRect(304), tiles, 7, 7, B);  // brick stairs asc S
	drawStairsN(img, getRect(305), tiles, 7, 7, B);  // brick stairs asc N
	drawStairsW(img, getRect(306), tiles, 7, 7, B);  // brick stairs asc W
	drawStairsE(img, getRect(307), tiles, 7, 7, B);  // brick stairs asc E
	drawStairsS(img, getRect(308), tiles, 54, 54, B);  // stone brick stairs asc S
	drawStairsN(img, getRect(309), tiles, 54, 54, B);  // stone brick stairs asc N
	drawStairsW(img, getRect(310), tiles, 54, 54, B);  // stone brick stairs asc W
	drawStairsE(img, getRect(311), tiles, 54, 54, B);  // stone brick stairs asc E
	drawStairsS(img, getRect(312), tiles, 224, 224, B);  // nether brick stairs asc S
	drawStairsN(img, getRect(313), tiles, 224, 224, B);  // nether brick stairs asc N
	drawStairsW(img, getRect(314), tiles, 224, 224, B);  // nether brick stairs asc W
	drawStairsE(img, getRect(315), tiles, 224, 224, B);  // nether brick stairs asc E
	drawStairsS(img, getRect(470), tiles, 192, 176, B);  // sandstone stairs asc S
	drawStairsN(img, getRect(471), tiles, 192, 176, B);  // sandstone stairs asc N
	drawStairsW(img, getRect(472), tiles, 192, 176, B);  // sandstone stairs asc W
	drawStairsE(img, getRect(473), tiles, 192, 176, B);  // sandstone stairs asc E
	drawStairsS(img, getRect(495), tiles, 198, 198, B);  // pine stairs asc S
	drawStairsN(img, getRect(496), tiles, 198, 198, B);  // pine stairs asc N
	drawStairsW(img, getRect(497), tiles, 198, 198, B);  // pine stairs asc W
	drawStairsE(img, getRect(498), tiles, 198, 198, B);  // pine stairs asc E
	drawStairsS(img, getRect(503), tiles, 214, 214, B);  // birch stairs asc S
	drawStairsN(img, getRect(504), tiles, 214, 214, B);  // birch stairs asc N
	drawStairsW(img, getRect(505), tiles, 214, 214, B);  // birch stairs asc W
	drawStairsE(img, getRect(506), tiles, 214, 214, B);  // birch stairs asc E
	drawStairsS(img, getRect(511), tiles, 199, 199, B);  // jungle stairs asc S
	drawStairsN(img, getRect(512), tiles, 199, 199, B);  // jungle stairs asc N
	drawStairsW(img, getRect(513), tiles, 199, 199, B);  // jungle stairs asc W
	drawStairsE(img, getRect(514), tiles, 199, 199, B);  // jungle stairs asc E
	drawInvStairsS(img, getRect(438), tiles, 4, 4, B);  // wood stairs asc S inverted
	drawInvStairsN(img, getRect(439), tiles, 4, 4, B);  // wood stairs asc N inverted
	drawInvStairsW(img, getRect(440), tiles, 4, 4, B);  // wood stairs asc W inverted
	drawInvStairsE(img, getRect(441), tiles, 4, 4, B);  // wood stairs asc E inverted
	drawInvStairsS(img, getRect(442), tiles, 16, 16, B);  // cobble stairs asc S inverted
	drawInvStairsN(img, getRect(443), tiles, 16, 16, B);  // cobble stairs asc N inverted
	drawInvStairsW(img, getRect(444), tiles, 16, 16, B);  // cobble stairs asc W inverted
	drawInvStairsE(img, getRect(445), tiles, 16, 16, B);  // cobble stairs asc E inverted
	drawInvStairsS(img, getRect(446), tiles, 7, 7, B);  // brick stairs asc S inverted
	drawInvStairsN(img, getRect(447), tiles, 7, 7, B);  // brick stairs asc N inverted
	drawInvStairsW(img, getRect(448), tiles, 7, 7, B);  // brick stairs asc W inverted
	drawInvStairsE(img, getRect(449), tiles, 7, 7, B);  // brick stairs asc E inverted
	drawInvStairsS(img, getRect(450), tiles, 54, 54, B);  // stone brick stairs asc S inverted
	drawInvStairsN(img, getRect(451), tiles, 54, 54, B);  // stone brick stairs asc N inverted
	drawInvStairsW(img, getRect(452), tiles, 54, 54, B);  // stone brick stairs asc W inverted
	drawInvStairsE(img, getRect(453), tiles, 54, 54, B);  // stone brick stairs asc E inverted
	drawInvStairsS(img, getRect(454), tiles, 224, 224, B);  // nether brick stairs asc S inverted
	drawInvStairsN(img, getRect(455), tiles, 224, 224, B);  // nether brick stairs asc N inverted
	drawInvStairsW(img, getRect(456), tiles, 224, 224, B);  // nether brick stairs asc W inverted
	drawInvStairsE(img, getRect(457), tiles, 224, 224, B);  // nether brick stairs asc E inverted
	drawInvStairsS(img, getRect(474), tiles, 192, 176, B);  // sandstone stairs asc S inverted
	drawInvStairsN(img, getRect(475), tiles, 192, 176, B);  // sandstone stairs asc N inverted
	drawInvStairsW(img, getRect(476), tiles, 192, 176, B);  // sandstone stairs asc W inverted
	drawInvStairsE(img, getRect(477), tiles, 192, 176, B);  // sandstone stairs asc E inverted
	drawInvStairsS(img, getRect(499), tiles, 198, 198, B);  // pine stairs asc S inverted
	drawInvStairsN(img, getRect(500), tiles, 198, 198, B);  // pine stairs asc N inverted
	drawInvStairsW(img, getRect(501), tiles, 198, 198, B);  // pine stairs asc W inverted
	drawInvStairsE(img, getRect(502), tiles, 198, 198, B);  // pine stairs asc E inverted
	drawInvStairsS(img, getRect(507), tiles, 214, 214, B);  // birch stairs asc S inverted
	drawInvStairsN(img, getRect(508), tiles, 214, 214, B);  // birch stairs asc N inverted
	drawInvStairsW(img, getRect(509), tiles, 214, 214, B);  // birch stairs asc W inverted
	drawInvStairsE(img, getRect(510), tiles, 214, 214, B);  // birch stairs asc E inverted
	drawInvStairsS(img, getRect(515), tiles, 199, 199, B);  // jungle stairs asc S inverted
	drawInvStairsN(img, getRect(516), tiles, 199, 199, B);  // jungle stairs asc N inverted
	drawInvStairsW(img, getRect(517), tiles, 199, 199, B);  // jungle stairs asc W inverted
	drawInvStairsE(img, getRect(518), tiles, 199, 199, B);  // jungle stairs asc E inverted

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
	drawFloorBlockImage(img, getRect(316), tiles, 76, 0, B);  // lily pad

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
	drawFence(img, getRect(158), tiles, 4, true, false, false, false, true, B);  // fence N
	drawFence(img, getRect(159), tiles, 4, false, true, false, false, true, B);  // fence S
	drawFence(img, getRect(160), tiles, 4, true, true, false, false, true, B);  // fence NS
	drawFence(img, getRect(161), tiles, 4, false, false, true, false, true, B);  // fence E
	drawFence(img, getRect(162), tiles, 4, true, false, true, false, true, B);  // fence NE
	drawFence(img, getRect(163), tiles, 4, false, true, true, false, true, B);  // fence SE
	drawFence(img, getRect(164), tiles, 4, true, true, true, false, true, B);  // fence NSE
	drawFence(img, getRect(165), tiles, 4, false, false, false, true, true, B);  // fence W
	drawFence(img, getRect(166), tiles, 4, true, false, false, true, true, B);  // fence NW
	drawFence(img, getRect(167), tiles, 4, false, true, false, true, true, B);  // fence SW
	drawFence(img, getRect(168), tiles, 4, true, true, false, true, true, B);  // fence NSW
	drawFence(img, getRect(169), tiles, 4, false, false, true, true, true, B);  // fence EW
	drawFence(img, getRect(170), tiles, 4, true, false, true, true, true, B);  // fence NEW
	drawFence(img, getRect(171), tiles, 4, false, true, true, true, true, B);  // fence SEW
	drawFence(img, getRect(172), tiles, 4, true, true, true, true, true, B);  // fence NSEW
	drawFencePost(img, getRect(332), tiles, 224, B);  // nether fence post
	drawFence(img, getRect(317), tiles, 224, true, false, false, false, true, B);  // nether fence N
	drawFence(img, getRect(318), tiles, 224, false, true, false, false, true, B);  // nether fence S
	drawFence(img, getRect(319), tiles, 224, true, true, false, false, true, B);  // nether fence NS
	drawFence(img, getRect(320), tiles, 224, false, false, true, false, true, B);  // nether fence E
	drawFence(img, getRect(321), tiles, 224, true, false, true, false, true, B);  // nether fence NE
	drawFence(img, getRect(322), tiles, 224, false, true, true, false, true, B);  // nether fence SE
	drawFence(img, getRect(323), tiles, 224, true, true, true, false, true, B);  // nether fence NSE
	drawFence(img, getRect(324), tiles, 224, false, false, false, true, true, B);  // nether fence W
	drawFence(img, getRect(325), tiles, 224, true, false, false, true, true, B);  // nether fence NW
	drawFence(img, getRect(326), tiles, 224, false, true, false, true, true, B);  // nether fence SW
	drawFence(img, getRect(327), tiles, 224, true, true, false, true, true, B);  // nether fence NSW
	drawFence(img, getRect(328), tiles, 224, false, false, true, true, true, B);  // nether fence EW
	drawFence(img, getRect(329), tiles, 224, true, false, true, true, true, B);  // nether fence NEW
	drawFence(img, getRect(330), tiles, 224, false, true, true, true, true, B);  // nether fence SEW
	drawFence(img, getRect(331), tiles, 224, true, true, true, true, true, B);  // nether fence NSEW
	drawFence(img, getRect(346), tiles, 4, false, false, true, true, false, B);  // fence gate EW
	drawFence(img, getRect(347), tiles, 4, true, true, false, false, false, B);  // fence gate NS

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
	
	drawBrewingStand(img, getRect(350), tiles, 156, 157, B);  // brewing stand
	
	drawCauldron(img, getRect(351), tiles, 154, -1, 0, B);  // cauldron empty
	drawCauldron(img, getRect(352), tiles, 154, 205, CUTOFF_10_16, B);  // cauldron 1/3 full
	drawCauldron(img, getRect(353), tiles, 154, 205, CUTOFF_6_16, B);  // cauldron 2/3 full
	drawCauldron(img, getRect(354), tiles, 154, 205, CUTOFF_2_16, B);  // cauldron full
	
	drawDragonEgg(img, getRect(378), tiles, 167, B);  // dragon egg
	
	drawVines(img, getRect(379), tiles, 143, B, false, false, false, false, true);  // vines top only
	drawVines(img, getRect(380), tiles, 143, B, true, false, false, false, false);  // vines N
	drawVines(img, getRect(381), tiles, 143, B, false, true, false, false, false);  // vines S
	drawVines(img, getRect(382), tiles, 143, B, true, true, false, false, false);  // vines NS
	drawVines(img, getRect(383), tiles, 143, B, false, false, true, false, false);  // vines E
	drawVines(img, getRect(384), tiles, 143, B, true, false, true, false, false);  // vines NE
	drawVines(img, getRect(385), tiles, 143, B, false, true, true, false, false);  // vines SE
	drawVines(img, getRect(386), tiles, 143, B, true, true, true, false, false);  // vines NSE
	drawVines(img, getRect(387), tiles, 143, B, false, false, false, true, false);  // vines W
	drawVines(img, getRect(388), tiles, 143, B, true, false, false, true, false);  // vines NW
	drawVines(img, getRect(389), tiles, 143, B, false, true, false, true, false);  // vines SW
	drawVines(img, getRect(390), tiles, 143, B, true, true, false, true, false);  // vines NSW
	drawVines(img, getRect(391), tiles, 143, B, false, false, true, true, false);  // vines EW
	drawVines(img, getRect(392), tiles, 143, B, true, false, true, true, false);  // vines NEW
	drawVines(img, getRect(393), tiles, 143, B, false, true, true, true, false);  // vines SEW
	drawVines(img, getRect(394), tiles, 143, B, true, true, true, true, false);  // vines NSEW

	return true;
}

