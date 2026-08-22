
#include "FileData.h"

#include <iostream>
#include <limits>
#include <cstring>

namespace eve {
namespace filesystem {

FileData::FileData(const std::string &filename, uint64_t size)
    : data(nullptr), size((size_t)size), filename(filename) {
    try {
        data = new uint8_t[size];
    } catch (std::bad_alloc &) {
        throw eve::Exception("Out of memory.");
    }

    size_t dotpos = filename.rfind('.');

    if (dotpos != std::string::npos) {
        extension = filename.substr(dotpos + 1);
        name      = filename.substr(0, dotpos);
    } else
        name = filename;
}

FileData::FileData(const FileData &c)
    : data(nullptr), size(c.size), filename(c.filename), extension(c.extension), name(c.name) {
    try {
        data = new uint8_t[size];
    } catch (std::bad_alloc &) {
        throw eve::Exception("Out of memory.");
    }
    memcpy(data, c.data, size);
}

FileData::~FileData() { delete[] data; }

FileData *FileData::clone() const { return new FileData(*this); }


}  // namespace filesystem
}  // namespace eve
