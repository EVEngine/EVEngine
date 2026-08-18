#pragma once

#include "Compressor.h"
#include "common/Data.h"

namespace eve {
namespace data {

/**
 * @brief Stores byte data compressed via DataModule::compress.
 **/
class CompressedData : public eve::Data {
public:
    /**
     * @brief Constructor just stores already-compressed data in the object.
     **/
    CompressedData(std::string format, char *cdata, size_t compressedsize, size_t rawsize, bool own = true);
    CompressedData(const CompressedData &c);
    virtual ~CompressedData();

    /**
     * @brief Gets the format that was used to compress the data.
     **/
    std::string getFormat() const;

    /**
     * @brief Gets the original (uncompressed) size of the compressed data. May return
     * 0 if the uncompressed size is unknown.
     **/
    size_t getDecompressedSize() const;

    // Implements Data.
    CompressedData *clone() const override;
    void           *getData() const override;
    size_t          getSize() const override;

private:
    std::string format;

    char  *data;
    size_t dataSize;

    size_t originalSize;

};  // CompressedData

}  // namespace data
}  // namespace eve
