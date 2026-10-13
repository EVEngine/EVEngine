#include "ByteData.h"
#include <string.h>
#include <cstdint>
#include <cstring>
#include "CompressedData.h"
#include "DataView.h"
#include "common/Exception.h"

// ByteData.cpp
namespace eve {
namespace data {

ByteData::ByteData(size_t size) : size(size) {
    create();
    memset(data, 0, size);
}

ByteData::ByteData(const void *d, size_t size) : size(size) {
    create();
    memcpy(data, d, size);
}

ByteData::ByteData(void *d, size_t size, bool own) : size(size) {
    if (own)
        data = (char *)d;
    else {
        create();
        memcpy(data, d, size);
    }
}

ByteData::ByteData(const ByteData &d) : size(d.size) {
    create();
    memcpy(data, d.data, size);
}

ByteData::~ByteData() { delete[] data; }

void ByteData::create() {
    if (size == 0) throw eve::Exception("ByteData size must be greater than 0.");

    try {
        data = new char[size];
    } catch (std::exception &) {
        throw eve::Exception("Create ByteData run out of memory.");
    }
}

ByteData *ByteData::clone() const { return new ByteData(*this); }

void *ByteData::getData() const { return data; }

size_t ByteData::getSize() const { return size; }

}  // namespace data
}  // namespace eve

// DataView.cpp
namespace eve {
namespace data {


DataView::DataView(Data *data, size_t offset, size_t size) : data(data), offset(offset), size(size) {
    if (offset >= data->getSize() || size > data->getSize() || offset > data->getSize() - size)
        throw eve::Exception("Offset and size of Data View must fit within the original Data's size.");

    if (size == 0) throw eve::Exception("DataView size must be greater than 0.");
}

DataView::DataView(const DataView &d) : data(d.data), offset(d.offset), size(d.size) {}

DataView::~DataView() {}

DataView *DataView::clone() const { return new DataView(*this); }

void *DataView::getData() const { return (uint8_t *)data->getData() + offset; }

size_t DataView::getSize() const { return size; }

}  // namespace data
}  // namespace eve

// CompressedData.cpp
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
