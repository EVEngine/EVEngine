#pragma once

#include "common/Data.h"

#include <stddef.h>

namespace eve {
namespace data {

class ByteData : public eve::Data {
public:
    ByteData(size_t size);
    ByteData(const void *d, size_t size);
    ByteData(void *d, size_t size, bool own);
    ByteData(const ByteData &d);
    virtual ~ByteData();

    // Implements Data.
    ByteData *clone() const override;
    void     *getData() const override;
    size_t    getSize() const override;

private:
    void create();

    char  *data;
    size_t size;

};  // ByteData

}  // namespace data
}  // namespace eve
