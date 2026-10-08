#pragma once
#include "common/Export.h"


#include "Compressor.h"
#include "common/Data.h"

namespace eve {
namespace data {

/**
 * @brief Stores byte data compressed via DataModule::compress.
 **/
class EVENGINE_API_FOUNDATION CompressedData : public eve::Data {
public:
    /**
     * @brief Constructor just stores already-compressed data in the object.
     **/
    CompressedData(std::string format, char *cdata, size_t compressedsize, size_t rawsize, bool own = true);
    /** @brief Compressed data. */
    CompressedData(const CompressedData &c);
    /** @brief Compressed data. */
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
    /** @brief Deep copy. @ownership Caller deletes. */
    CompressedData *clone() const override;
    /** @brief Returns the data. */
    void           *getData() const override;
    /** @brief Returns the size. */
    size_t          getSize() const override;

private:
    std::string format;

    char  *data;
    size_t dataSize;

    size_t originalSize;

};  // CompressedData

}  // namespace data
}  // namespace eve
