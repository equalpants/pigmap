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

#include <zlib.h>
#include <dirent.h>
#include <errno.h>
#include <algorithm>
#include <sstream>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <stdio.h>

#include "utils.h"

using namespace std;


//!!!!!!! do this more carefully, like actually checking return values, etc.
void makePath(const string& path)
{
	if (path.empty())
		return;
	string::size_type pos = path.find('/');
	mkdir(path.substr(0, pos).c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
	while (pos != string::npos)
	{
		pos = path.find('/', pos+1);
		mkdir(path.substr(0, pos).c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
	}
}

//!!!!!! same here
void renameFile(const string& oldpath, const string& newpath)
{
	if (oldpath.empty() || newpath.empty())
		return;
	rename(oldpath.c_str(), newpath.c_str());
}

void copyFile(const string& oldpath, const string& newpath)
{
	ifstream infile(oldpath.c_str());
	ofstream outfile(newpath.c_str());
	outfile << infile.rdbuf();
}


void listEntries(const string& dirpath, vector<string>& entries)
{
	DIR *dir = opendir(dirpath.c_str());
	if (dir == NULL)
		return;
	dirent *de = readdir(dir);
	while (de != NULL)
	{
		string e(de->d_name);
		if (e != "." && e != "..")
			entries.push_back(dirpath + "/" + e);
		de = readdir(dir);
	}
	closedir(dir);
}

bool dirExists(const string& dirpath)
{
	DIR *dir = opendir(dirpath.c_str());
	if (dir == NULL)
		return false;
	closedir(dir);
	return true;
}


struct gzCloser
{
	gzFile gzfile;
	gzCloser(gzFile gzf) : gzfile(gzf) {}
	~gzCloser() {gzclose(gzfile);}
};

int readGzFile(const string& filename, vector<uint8_t>& data)
{
	gzFile gzf = gzopen(filename.c_str(), "rb");
	if (gzf == NULL)
	{
		if (errno == ENOENT)
			return -1;
		return -2;
	}
	gzCloser gc(gzf);
	// start by resizing vector to entire capacity; we'll shrink back down to the
	//  proper size later
	data.resize(data.capacity());
	if (data.empty())
	{
		data.resize(131072);
		data.resize(data.capacity());  // just in case extra space was allocated
	}
	// read as much as we can
	vector<uint8_t>::size_type pos = 0;
	unsigned requestSize = data.size() - pos;  // this is plain old unsigned to match the zlib call
	int bytesRead = gzread(gzf, &data[pos], requestSize);
	if (bytesRead == -1)
		return -2;
	pos += bytesRead;
	while (bytesRead == requestSize)
	{
		// if there's still more, reallocate and read more
		data.resize(data.size() * 2);
		data.resize(data.capacity());  // just in case extra space was allocated
		requestSize = data.size() - pos;
		bytesRead = gzread(gzf, &data[pos], requestSize);
		if (bytesRead == -1)
			return -2;
		pos += bytesRead;
	}
	// resize buffer back down to the end of the actual data
	data.resize(pos);
	return 0;
}

struct inflateEnder
{
	z_stream *zstr;
	inflateEnder(z_stream *zs) : zstr(zs) {}
	~inflateEnder() {inflateEnd(zstr);}
};

bool readGzOrZlib(uint8_t *inbuf, size_t size, vector<uint8_t>& data)
{
	// start by resizing vector to entire capacity; we'll shrink back down to the
	//  proper size later
	data.resize(data.capacity());
	if (data.empty())
	{
		data.resize(131072);
		data.resize(data.capacity());  // just in case extra space was allocated
	}
	// initialize zlib stream
	z_stream zstr;
	zstr.next_in = inbuf;
	zstr.avail_in = size;
	zstr.next_out = &(data[0]);
	zstr.avail_out = data.size();
	zstr.zalloc = Z_NULL;
	zstr.zfree = Z_NULL;
	int result = inflateInit2(&zstr, 15 + 32);  // adding 32 to window size means "detect both gzip and zlib"
	if (result != Z_OK)
		return false;
	inflateEnder ie(&zstr);
	// read as much as we can
	result = inflate(&zstr, Z_SYNC_FLUSH);
	while (result != Z_STREAM_END)
	{
		// if we failed for some reason other than not having enough room to read into, abort
		if (result != Z_OK)
			return false;
		// reallocate and read more
		ptrdiff_t diff = zstr.next_out - &(data[0]);
		size_t addedsize = data.size();
		data.resize(data.size() + addedsize);
		data.resize(data.capacity());  // just in case more was allocated
		zstr.next_out = &(data[0]) + diff;
		zstr.avail_out += addedsize;
		result = inflate(&zstr, Z_SYNC_FLUSH);
	}
	// resize buffer back down to end of the actual data
	data.resize(zstr.total_out);
	return true;
}



uint32_t fromBigEndian(uint32_t i)
{
	uint8_t *b = (uint8_t*)(&i);
	return (*b << 24) | (*(b+1) << 16) | (*(b+2) << 8) | (*(b+3));
}

bool isBigEndian()
{
	uint32_t i = 0xff000000;
	uint8_t *b = (uint8_t*)(&i);
	return *b == 0xff;
}

void swapEndian(uint32_t& i)
{
	uint8_t *b = (uint8_t*)(&i);
	swap(b[0], b[3]);
	swap(b[1], b[2]);
}



int64_t floordiv(int64_t a, int64_t b)
{
	if (b < 0)
	{
		a = -a;
		b = -b;
	}
	if (a < 0)
		return (a - b + 1) / b;
	return a / b;
}

int64_t ceildiv(int64_t a, int64_t b)
{
	if (b < 0)
	{
		a = -a;
		b = -b;
	}
	if (a > 0)
		return (a + b - 1) / b;
	return a / b;
}

int64_t mod64pos(int64_t a)
{
	if (a >= 0)
		return a % 64;
	int64_t m = a % 64;
	return (m == 0) ? 0 : (64 + m);
}



// technically, these use "upside-down-N-order", not Z-order--that is, the Y-coord is incremented
//  first, not the X-coord--because that way, no special way to detect the end of the array is
//  needed; advancing past the final valid element leads to the index one past the end of the
//  array, as usual

uint32_t toZOrder(uint32_t i, const uint32_t SIZE)
{
	// get x and y coords
	uint32_t x = i % SIZE, y = i / SIZE;
	// interleave bits; this (public domain) code taken from Sean Eron Anderson's website
	// ...this assumes that x and y are <= 0xffff; this is safe because if they weren't,
	//  SIZE would have to be > 0x10000, so 32 bits wouldn't have been enough to hold an
	//  index into a SIZExSIZE array
	x = (x | (x << 8)) & 0xff00ff;
	x = (x | (x << 4)) & 0xf0f0f0f;
	x = (x | (x << 2)) & 0x33333333;
	x = (x | (x << 1)) & 0x55555555;
	y = (y | (y << 8)) & 0xff00ff;
	y = (y | (y << 4)) & 0xf0f0f0f;
	y = (y | (y << 2)) & 0x33333333;
	y = (y | (y << 1)) & 0x55555555;
	return (x << 1) | y;
}

uint32_t fromZOrder(uint32_t i, const uint32_t SIZE)
{
	// de-interleave
	uint32_t x = (i >> 1) & 0x55555555;
	x = (x | (x >> 1)) & 0x33333333;
	x = (x | (x >> 2)) & 0xf0f0f0f;
	x = (x | (x >> 4)) & 0xff00ff;
	x = (x | (x >> 8)) & 0xffff;
	uint32_t y = i & 0x55555555;
	y = (y | (y >> 1)) & 0x33333333;
	y = (y | (y >> 2)) & 0xf0f0f0f;
	y = (y | (y >> 4)) & 0xff00ff;
	y = (y | (y >> 8)) & 0xffff;
	// convert to row-major
	return y*SIZE + x;

}



bool fromBase36(const string& s, string::size_type pos, string::size_type n, int64_t& result)
{
	if (s.empty())
		return false;
	if (n == string::npos)
		n = s.size();
	string::size_type i = pos;
	int64_t sign = 1;
	if (s[i] == '-')
	{
		sign = -1;
		i++;
	}
	int64_t total = 0;
	while (i != pos + n)
	{
		total *= 36;
		if (s[i] >= '0' && s[i] <= '9')
			total += s[i] - '0';
		else if (s[i] >= 'a' && s[i] <= 'z')
			total += s[i] - 'a' + 10;
		else if (s[i] >= 'A' && s[i] <= 'Z')
			total += s[i] - 'A' + 10;
		else
			return false;
		i++;
	}
	result = total * sign;
	return true;
}

int64_t fromBase36(const string& s)
{
	int64_t result;
	if (fromBase36(s, 0, string::npos, result))
		return result;
	return 0;
}

string toBase36(int64_t i)
{
	bool neg = false;
	if (i < 0)
	{
		neg = true;
		i = -i;
	}
	string s;
	while (i > 0)
	{
		int64_t d = i % 36;
		if (d < 10)
			s += ('0' + d);
		else
			s += ('a' + d - 10);
		i /= 36;
	}
	if (s.empty())
		return "0";
	if (neg)
		s += '-';
	reverse(s.begin(), s.end());
	return s;
}


string tostring(int i)
{
	ostringstream ss;
	ss << i;
	return ss.str();
}

string tostring(int64_t i)
{
	ostringstream ss;
	ss << i;
	return ss.str();
}

bool fromstring(const string& s, int64_t& result)
{
	istringstream ss(s);
	ss >> result;
	return !ss.fail();
}

bool replace(string& text, const string& oldstr, const string& newstr)
{
	string::size_type pos = text.find(oldstr);
	if (pos == string::npos)
		return false;
	while (pos != string::npos)
	{
		text.replace(pos, oldstr.size(), newstr);
		pos = text.find(oldstr, pos + 1);
	}
	return true;
}



pair<int64_t, double> schedule(const vector<int64_t>& costs, vector<int>& assignments, int threads)
{
	// simple scheduler: go through the costs in descending order, assigning
	//  each to the thread with the lowest cost so far

	// sort costs
	vector<pair<int64_t, int> > sortedcosts;  // first is cost, second is index in original vector
	for (int i = 0; i < costs.size(); i++)
		sortedcosts.push_back(make_pair(costs[i], i));
	sort(sortedcosts.begin(), sortedcosts.end(), greater<pair<int64_t, int> >());

	vector<int64_t> totals(threads, 0);
	assignments.resize(costs.size(), -1);

	// go through sorted costs
	int next = 0;
	for (vector<pair<int64_t, int> >::const_iterator it = sortedcosts.begin(); it != sortedcosts.end(); it++)
	{
		// assign to waiting (min-cost) thread
		assignments[it->second] = next;
		totals[next] += it->first;
		// find the new min-cost thread
		for (int i = 0; i < threads; i++)
			if (totals[i] < totals[next])
				next = i;
	}

	// compute error fraction
	int mintotal = totals[0], maxtotal = totals[0];
	for (int i = 1; i < threads; i++)
	{
		if (totals[i] < mintotal)
			mintotal = totals[i];
		else if (totals[i] > maxtotal)
			maxtotal = totals[i];
	}
	return make_pair(maxtotal - mintotal, (double)(maxtotal - mintotal) / (double)maxtotal);
}



