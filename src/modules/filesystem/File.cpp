#include "File.h"
#include <cstring>
#include <iostream>
#include <limits>
#include "FileData.h"

// File.cpp
namespace eve {
namespace filesystem {

File::~File() {}

FileData *File::read(int64_t size) {
    bool isopen = isOpen();

    // PhysFS backend expects binary modes ("rb"); plain "r" never opens a handle.
    if (!isopen && !open("rb")) throw eve::Exception("Could not read file %s.", getFilename().c_str());

    int64_t max = getSize();
    int64_t cur = tell();
    size        = (size == ALL) ? max : size;

    if (size < 0) throw eve::Exception("Invalid read size.");
    if (cur + size > max) size = max - cur;

    FileData *fileData  = new FileData(getFilename(), size);
    int64_t   bytesRead = read(fileData->getData(), size);

    if (bytesRead < 0 || (bytesRead == 0 && bytesRead != size)) {
        delete fileData;
        throw eve::Exception("Could not read from file.");
    }

    if (bytesRead < size) {
        FileData *tmpFileData = new FileData(getFilename(), bytesRead);
        memcpy(tmpFileData->getData(), fileData->getData(), (size_t)bytesRead);
        delete fileData;
        fileData = tmpFileData;
    }

    if (!isopen) close();

    return fileData;
}

bool File::write(const Data *data, int64_t size) {
    return write(data->getData(), (size == ALL) ? data->getSize() : size);
}

std::string File::getExtension() const {
    const std::string     &filename = getFilename();
    std::string::size_type idx      = filename.rfind('.');

    if (idx != std::string::npos)
        return filename.substr(idx + 1);
    else
        return std::string();
}

}  // namespace filesystem
}  // namespace eve

// FileData.cpp
namespace eve {
namespace filesystem {

FileData::FileData(const std::string &filename, uint64_t size) : data(nullptr), size((size_t)size), filename(filename) {
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
