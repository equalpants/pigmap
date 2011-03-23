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

#ifndef RGBA_H
#define RGBA_H

#include <vector>
#include <string>
#include <stdint.h>


typedef uint32_t RGBAPixel;
#define ALPHA(x) ((x & 0xff000000) >> 24)
#define BLUE(x) ((x & 0xff0000) >> 16)
#define GREEN(x) ((x & 0xff00) >> 8)
#define RED(x) (x & 0xff)

RGBAPixel makeRGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a);

void setAlpha(RGBAPixel& p, int a);
void setBlue(RGBAPixel& p, int b);
void setGreen(RGBAPixel& p, int g);
void setRed(RGBAPixel& p, int r);

struct RGBAImage
{
	std::vector<RGBAPixel> data;
	int32_t w, h;

	// get pixel
	RGBAPixel& operator()(int32_t x, int32_t y) {return data[y*w+x];}
	const RGBAPixel& operator()(int32_t x, int32_t y) const {return data[y*w+x];}

	// resize data and initialize to 0 (clear out any existing data)
	void create(int32_t ww, int32_t hh);

	bool readPNG(const std::string& filename);
	bool writePNG(const std::string& filename);
};

struct ImageRect
{
	int32_t x, y, w, h;

	ImageRect(int32_t xx, int32_t yy, int32_t ww, int32_t hh) : x(xx), y(yy), w(ww), h(hh) {}
};

//------- these are used by the inner rendering loops and must be fast

// alpha-blend source pixel onto destination pixel
// ...note that the alpha channel of the result is not computed the same way as the RGB channels:
//  instead of interpolating between ALPHA(source) and ALPHA(dest), it is the inverse product of the
//  inverses of ALPHA(source) and ALPHA(dest), so that when you draw a translucent pixel on top of an
//  opaque one, the result stays opaque
void blend(RGBAPixel& dest, const RGBAPixel& source);

// alpha-blend source rect onto destination rect of same size
void alphablit(const RGBAImage& source, const ImageRect& srect, RGBAImage& dest, int32_t dxstart, int32_t dystart);

// reduce source image into destination rect half its size
// (does nothing if the ImageRect isn't exactly half the size of the source image)
void reduceHalf(RGBAImage& dest, const ImageRect& drect, const RGBAImage& source);


//--------- these are used only to generate block images from terrain.png and may be crappy

// copy source rect into destination rect of possibly different size
void resize(const RGBAImage& source, const ImageRect& srect, RGBAImage& dest, const ImageRect& drect);

// darken a pixel by multiplying its RGB components by some number from 0 to 1
void darken(RGBAPixel& dest, double r, double g, double b);
void darken(RGBAImage& img, const ImageRect& rect, double r, double g, double b);

// copy source rect into destination rect of same size
void blit(const RGBAImage& source, const ImageRect& srect, RGBAImage& dest, int32_t dxstart, int32_t dystart);

#endif // RGBA_H