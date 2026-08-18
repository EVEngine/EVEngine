#pragma once

#include "common/Data.h"

#include <stddef.h>

namespace eve {
namespace data {

/** @brief In-memory byte buffer implementing eve::Data (ref-counted). */
class ByteData : public eve::Data {
public:
    /** @brief Allocates an uninitialized buffer of the given size. */
    ByteData(size_t size);
    /** @brief Copies `size` bytes from `d` into a new buffer. */
    ByteData(const void *d, size_t size);
    /** @brief Wraps `d`; when `own` is true the buffer is freed on destruction. */
    ByteData(void *d, size_t size, bool own);
    /** @brief Deep-copies another buffer. */
    ByteData(const ByteData &d);
    virtual ~ByteData();

    /** @brief Implements eve::Data. */
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
