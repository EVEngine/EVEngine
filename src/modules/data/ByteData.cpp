#include "ByteData.h"
#include "common/Exception.h"

#include <string.h>

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
