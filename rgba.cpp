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

#include <png.h>
#include <errno.h>

#include "rgba.h"
#include "utils.h"

using namespace std;

# ifndef UINT64_C
#  if __WORDSIZE == 64
#   define UINT64_C(c)	c ## UL
#  else
#   define UINT64_C(c)	c ## ULL
#  endif
# endif


RGBAPixel makeRGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	return (a << 24) | (b << 16) | (g << 8) | r;
}

void setAlpha(RGBAPixel& p, int a)
{
	p &= 0xffffff;
	p |= (a & 0xff) << 24;
}

void setBlue(RGBAPixel& p, int b)
{
	p &= 0xff00ffff;
	p |= (b & 0xff) << 16;
}

void setGreen(RGBAPixel& p, int g)
{
	p &= 0xffff00ff;
	p |= (g & 0xff) << 8;
}

void setRed(RGBAPixel& p, int r)
{
	p &= 0xffffff00;
	p |= r & 0xff;
}


void RGBAImage::create(int32_t ww, int32_t hh)
{
	w = ww;
	h = hh;
	data.clear();
	data.resize(w*h, 0);
}



struct fcloser
{
	FILE *f;
	fcloser(FILE *ff) : f(ff) {}
	~fcloser() {fclose(f);}
};

struct PNGReadCleaner
{
	png_structp png;
	png_infop info;
	png_infop endinfo;
	PNGReadCleaner() : png(NULL), info(NULL), endinfo(NULL) {}
	~PNGReadCleaner() {png_destroy_read_struct(&png, &info, &endinfo);}
};

struct PNGWriteCleaner
{
	png_structp png;
	png_infop info;
	PNGWriteCleaner() : png(NULL), info(NULL) {}
	~PNGWriteCleaner() {png_destroy_write_struct(&png, &info);}
};



bool RGBAImage::readPNG(const string& filename)
{
	FILE *f = fopen(filename.c_str(), "rb");
	if (f == NULL)
		return false;
	fcloser fc(f);

	uint8_t header[8];
	fread(header, 1, 8, f);
	if (0 != png_sig_cmp(header, 0, 8))
		return false;

	PNGReadCleaner cleaner;

	png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (png == NULL)
		return false;
	cleaner.png = png;

	png_infop info = png_create_info_struct(png);
	if (info == NULL)
		return false;
	cleaner.info = info;

	if (setjmp(png_jmpbuf(png)))
		return false;

	png_init_io(png, f);
	png_set_sig_bytes(png, 8);

	png_read_info(png, info);
	if (PNG_COLOR_TYPE_RGB_ALPHA != png_get_color_type(png, info) || 8 != png_get_bit_depth(png, info))
		return false;
	w = png_get_image_width(png, info);
	h = png_get_image_height(png, info);
	data.resize(w*h);

	png_set_interlace_handling(png);
	png_read_update_info(png, info);

	png_bytep *rowPointers = new png_bytep[h];
	arrayDeleter<png_bytep> ad(rowPointers);
	RGBAPixel *p = &data[0];
	for (int32_t i = 0; i < h; i++, p += w)
		rowPointers[i] = (png_bytep)p;

	if (isBigEndian())
	{
		png_set_bgr(png);
		png_set_swap_alpha(png);
	}

	png_read_image(png, rowPointers);

	png_read_end(png, NULL);
	return true;
}

bool RGBAImage::writePNG(const string& filename)
{
	FILE *f = fopen(filename.c_str(), "wb");
	if (f == NULL)
	{
		// if the directory didn't exist, create it and try again
		if (errno == ENOENT)
		{
			makePath(filename.substr(0, filename.rfind('/')));
			f = fopen(filename.c_str(), "wb");
		}
		if (f == NULL)
			return false;
	}
	fcloser fc(f);

	PNGWriteCleaner cleaner;

	png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (png == NULL)
		return false;
	cleaner.png = png;

	png_infop info = png_create_info_struct(png);
	if (info == NULL)
		return false;
	cleaner.info = info;

	if (setjmp(png_jmpbuf(png)))
		return false;

	png_init_io(png, f);

	png_set_IHDR(png, info, w, h, 8, PNG_COLOR_TYPE_RGB_ALPHA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

	png_bytep *rowPointers = new png_bytep[h];
	arrayDeleter<png_bytep> ad(rowPointers);
	RGBAPixel *p = &data[0];
	for (int32_t i = 0; i < h; i++, p += w)
		rowPointers[i] = (png_bytep)p;

	png_set_rows(png, info, rowPointers);

	if (isBigEndian())
		png_write_png(png, info, PNG_TRANSFORM_BGR | PNG_TRANSFORM_SWAP_ALPHA, NULL);
	else
		png_write_png(png, info, PNG_TRANSFORM_IDENTITY, NULL);

	return true;
}





void fullblend(RGBAPixel& dest, const RGBAPixel& source)
{
	// get sa and sainv in the range 1-256; this way, the possible results of blending 8-bit color channels sc and dc
	//  (using sc*sa + dc*sainv) span the range 0x0000-0xffff, so we can just truncate and shift
	int64_t sa = ALPHA(source) + 1;
	int64_t sainv = 257 - sa;
	// compute the new RGB channels
	int64_t d = dest, s = source;
	d = ((d << 16) & UINT64_C(0xff00000000)) | ((d << 8) & 0xff0000) | (d & 0xff);
	s = ((s << 16) & UINT64_C(0xff00000000)) | ((s << 8) & 0xff0000) | (s & 0xff);
	int64_t newrgb = s*sa + d*sainv;
	// compute the new alpha channel
	int64_t dainv = 256 - ALPHA(dest);
	int64_t newa = sainv * dainv;  // result is from 1-0x10000
	newa = (newa - 1) >> 8;  // result is from 0-0xff
	newa = 255 - newa;  // final result; if either input was 255, so is this, so opacity is preserved
	// combine everything and write it out
	dest = (newa << 24) | ((newrgb >> 24) & 0xff0000) | ((newrgb >> 16) & 0xff00) | ((newrgb >> 8) & 0xff);
}

// if destination pixel is already 100% opaque, no need to calculate its new alpha
void opaqueblend(RGBAPixel& dest, const RGBAPixel& source)
{
	// get sa and sainv in the range 1-256; this way, the possible results of blending 8-bit color channels sc and dc
	//  (using sc*sa + dc*sainv) span the range 0x0000-0xffff, so we can just truncate and shift
	int64_t sa = ALPHA(source) + 1;
	int64_t sainv = 257 - sa;
	// compute the new RGB channels
	int64_t d = dest, s = source;
	d = ((d << 16) & UINT64_C(0xff00000000)) | ((d << 8) & 0xff0000) | (d & 0xff);
	s = ((s << 16) & UINT64_C(0xff00000000)) | ((s << 8) & 0xff0000) | (s & 0xff);
	int64_t newrgb = s*sa + d*sainv;
	// destination alpha remains 100%; combine everything and write it out
	dest = 0xff000000 | ((newrgb >> 24) & 0xff0000) | ((newrgb >> 16) & 0xff00) | ((newrgb >> 8) & 0xff);
}

void blend(RGBAPixel& dest, const RGBAPixel& source)
{
	// if source is transparent, there's nothing to do
	if (source <= 0xffffff)
		return;
	// if source is opaque, or if destination is transparent, just copy it over
	else if (source >= 0xff000000 || dest <= 0xffffff)
		dest = source;
	// if source is translucent and dest is opaque, the color channels need to be blended,
	//  but the new pixel will be opaque
	else if (dest >= 0xff000000)
		opaqueblend(dest, source);
	// both source and dest are translucent; we need the whole deal
	else
		fullblend(dest, source);
}

void alphablit(const RGBAImage& source, const ImageRect& srect, RGBAImage& dest, int32_t dxstart, int32_t dystart)
{
	int32_t ybegin = max(0, max(-srect.y, -dystart));
	int32_t yend = min(srect.h, min(source.h-srect.y, dest.h-dystart));
	int32_t xbegin = max(0, max(-srect.x, -dxstart));
	int32_t xend = min(srect.w, min(source.w-srect.x, dest.w-dxstart));
	for (int32_t yoff = ybegin, sy = srect.y + ybegin, dy = dystart + ybegin; yoff < yend; yoff++, sy++, dy++)
		for (int32_t xoff = xbegin, sx = srect.x + xbegin, dx = dxstart + xbegin; xoff < xend; xoff++, sx++, dx++)
			blend(dest(dx,dy), source(sx,sy));
}

void reduceHalf(RGBAImage& dest, const ImageRect& drect, const RGBAImage& source)
{
	if (source.w != drect.w*2 || source.h != drect.h*2)
		return;
	for (int32_t dy = drect.y, sy = 0; sy < source.h; dy++, sy += 2)
		for (int32_t dx = drect.x, sx = 0; sx < source.w; dx++, sx += 2)
		{
			RGBAPixel p1 = (source(sx, sy) >> 2) & 0x3f3f3f3f;
			RGBAPixel p2 = (source(sx+1, sy) >> 2) & 0x3f3f3f3f;
			RGBAPixel p3 = (source(sx, sy+1) >> 2) & 0x3f3f3f3f;
			RGBAPixel p4 = (source(sx+1, sy+1) >> 2) & 0x3f3f3f3f;
			dest(dx, dy) = p1 + p2 + p3 + p4;
		}
}



//!!!!!!!!! replace this with something non-idiotic?  (it does surprisingly well, though!)
void resize(const RGBAImage& source, const ImageRect& srect, RGBAImage& dest, const ImageRect& drect)
{
	for (int y = drect.y; y < drect.y + drect.h; y++)
	{
		float ypct = (float)(y - drect.y) / (float)(drect.h - 1);
		int yoff = (int)(ypct * (float)(srect.h - 1));
		for (int x = drect.x; x < drect.x + drect.w; x++)
		{
			float xpct = (float)(x - drect.x) / (float)(drect.w - 1);
			int xoff = (int)(xpct * (float)(srect.w - 1));
			dest(x, y) = source(srect.x + xoff, srect.y + yoff);
		}
	}
}

void darken(RGBAPixel& dest, double r, double g, double b)
{
	uint8_t newr = (uint8_t)(r * (double)(RED(dest)));
	uint8_t newg = (uint8_t)(g * (double)(GREEN(dest)));
	uint8_t newb = (uint8_t)(b * (double)(BLUE(dest)));
	dest = makeRGBA(newr, newg, newb, ALPHA(dest));
}

void darken(RGBAImage& img, const ImageRect& rect, double r, double g, double b)
{
	for (int y = rect.y; y < rect.y + rect.h; y++)
		for (int x = rect.x; x < rect.x + rect.w; x++)
			darken(img(x, y), r, g, b);
}

void blit(const RGBAImage& source, const ImageRect& srect, RGBAImage& dest, int32_t dxstart, int32_t dystart)
{
	int32_t ybegin = max(0, max(-srect.y, -dystart));
	int32_t yend = min(srect.h, min(source.h-srect.y, dest.h-dystart));
	int32_t xbegin = max(0, max(-srect.x, -dxstart));
	int32_t xend = min(srect.w, min(source.w-srect.x, dest.w-dxstart));
	for (int32_t yoff = ybegin, sy = srect.y + ybegin, dy = dystart + ybegin; yoff < yend; yoff++, sy++, dy++)
		for (int32_t xoff = xbegin, sx = srect.x + xbegin, dx = dxstart + xbegin; xoff < xend; xoff++, sx++, dx++)
			dest(dx,dy) = source(sx,sy);
}
