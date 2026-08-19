#pragma once

#include <string>
#include <cstdint>
#include "common/Data.h"
#include "common/Exception.h"

namespace eve {
namespace filesystem {

/** @brief Data buffer paired with a filename (used for type identification). */
class FileData : public Data {
public:
    /** @brief Allocates a buffer of the given size for the named file. */
    FileData(const std::string &filename, uint64_t size);
    /** @brief Deep-copies another FileData. */
    FileData(const FileData &c);

    virtual ~FileData();

    /** @brief Implements eve::Data. */
    FileData *clone() const;
    void *    getData() const { return data; }
    size_t    getSize() const { return size; }

    /** @brief Filename, extension (without dot) and base name. */
    const std::string &getFilename() const { return filename; }
    const std::string &getExtension() const { return extension; }
    const std::string &getName() const { return name; }

private:
    // The actual data.
    uint8_t *data;

    // Size of the data.
    uint64_t size;

    // The filename used for error purposes.
    std::string filename;

    // The extension (without dot). Used to identify file type.
    std::string extension;

    // The file name without the extension (and without the dot).
    std::string name;

};  // FileData

}  // namespace filesystem
}  // namespace eve
