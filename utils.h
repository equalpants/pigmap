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

#ifndef UTILS_H
#define UTILS_H

#include <vector>
#include <string>
#include <stdint.h>


// ensure that a directory exists (create any missing directories on path)
void makePath(const std::string& path);

void renameFile(const std::string& oldpath, const std::string& newpath);
void copyFile(const std::string& oldpath, const std::string& newpath);

// read a text file and append each of its non-empty lines to a vector
bool readLines(const std::string& filename, std::vector<std::string>& lines);

// list names of entries in a directory, not including "." and ".."
// ...returns relative paths beginning with dirpath; appends results to vector
void listEntries(const std::string& dirpath, std::vector<std::string>& entries);

bool dirExists(const std::string& dirpath);

// -read a gzipped file into a vector, overwriting its contents, and expanding it if necessary
// -return 0 on success, -1 for nonexistent file, -2 for other errors
int readGzFile(const std::string& filename, std::vector<uint8_t>& data);

// extract gzip- or zlib-compressed data into a vector, overwriting its contents, and
//  expanding it if necessary
// (inbuf is not const only because zlib won't take const pointers for input)
bool readGzOrZlib(uint8_t* inbuf, size_t size, std::vector<uint8_t>& data);


#define USE_MALLINFO 0

uint64_t getHeapUsage();


// convert a big-endian int into whatever the current platform endianness is
uint32_t fromBigEndian(uint32_t i);
uint16_t fromBigEndian(uint16_t i);

// detect whether the platform is big-endian
bool isBigEndian();

// switch endianness of an int
void swapEndian(uint32_t& i);


// floored division; real value of a/b is floored instead of truncated toward 0
int64_t floordiv(int64_t a, int64_t b);
// ...same thing for ceiling
int64_t ceildiv(int64_t a, int64_t b);

// positive remainder mod 64, for chunk subdirectories
int64_t mod64pos(int64_t a);

// given i in [0,destrange), find j in [0,srcrange)
int64_t interpolate(int64_t i, int64_t destrange, int64_t srcrange);


// take a row-major index into a SIZExSIZE array and convert it to Z-order
uint32_t toZOrder(uint32_t i, const uint32_t SIZE);
// ...and vice versa
uint32_t fromZOrder(uint32_t i, const uint32_t SIZE);


bool fromBase36(const std::string& s, std::string::size_type pos, std::string::size_type n, int64_t& result);
int64_t fromBase36(const std::string& s);
std::string toBase36(int64_t i);

std::string tostring(int i);
std::string tostring(int64_t i);
bool fromstring(const std::string& s, int64_t& result);
bool fromstring(const std::string& s, int& result);


// replace all occurrences of oldstr in text with newstr; return false if none found
bool replace(std::string& text, const std::string& oldstr, const std::string& newstr);

std::vector<std::string> tokenize(const std::string& instr, char separator);


// find an assignment of costs to threads that attempts to minimize the difference
//  between the min and max total thread costs; return the difference by itself, and
//  also as a fraction of the max thread cost
std::pair<int64_t, double> schedule(const std::vector<int64_t>& costs, std::vector<int>& assignments, int threads);


class nocopy
{
protected:
	nocopy() {}
	~nocopy() {}
private:
	nocopy(const nocopy& n);
	const nocopy& operator=(const nocopy& n);
};


template <class T> struct arrayDeleter
{
	T *array;
	arrayDeleter(T *a) : array(a) {}
	~arrayDeleter() {delete[] array;}
};


template <class T> struct stackPusher
{
	std::vector<T>& vec;
	stackPusher(std::vector<T>& v, const T& item) : vec(v) {vec.push_back(item);}
	~stackPusher() {vec.pop_back();}
};


// fast version for dividing by 16 (important for BlockIdx::getChunkIdx, which is called very very frequently)
inline int64_t floordiv16(int64_t a)
{
	// right-shifting a negative is undefined, so just do the division--the compiler will probably know
	//  whether it can use a shift anyway
	if (a < 0)
		return (a - 15) / 16;
	return a >> 4;
}


#endif // UTILS_H