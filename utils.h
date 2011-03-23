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

#ifndef UTILS_H
#define UTILS_H

#include <vector>
#include <string>
#include <stdint.h>


// ensure that a directory exists (create any missing directories on path)
void makePath(const std::string& path);

void renameFile(const std::string& oldpath, const std::string& newpath);
void copyFile(const std::string& oldpath, const std::string& newpath);

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


// convert a big-endian int into whatever the current platform endianness is
uint32_t fromBigEndian(uint32_t i);

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


// replace all occurrences of oldstr in text with newstr; return false if none found
bool replace(std::string& text, const std::string& oldstr, const std::string& newstr);


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


#endif // UTILS_H