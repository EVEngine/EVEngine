#include "CompressedData.h"
#include "common/Exception.h"

namespace eve {
namespace data {

CompressedData::CompressedData(std::string format, char *cdata, size_t compressedsize, size_t rawsize, bool own)
    : format(format), data(nullptr), dataSize(compressedsize), originalSize(rawsize) {
    if (own)
        data = cdata;
    else {
        try {
            data = new char[dataSize];
        } catch (std::bad_alloc &) {
            throw eve::Exception("Create CompressedData run out of memory.");
        }

        memcpy(data, cdata, dataSize);
    }
}

CompressedData::CompressedData(const CompressedData &c)
    : format(c.format), data(nullptr), dataSize(c.dataSize), originalSize(c.originalSize) {
    try {
        data = new char[dataSize];
    } catch (std::bad_alloc &) {
        throw eve::Exception("Copy CompressedData run out of memory.");
    }

    memcpy(data, c.data, dataSize);
}

CompressedData::~CompressedData() { delete[] data; }

CompressedData *CompressedData::clone() const { return new CompressedData(*this); }

std::string CompressedData::getFormat() const { return format; }

size_t CompressedData::getDecompressedSize() const { return originalSize; }

void *CompressedData::getData() const { return data; }

size_t CompressedData::getSize() const { return dataSize; }

}  // namespace data
}  // namespace eve
