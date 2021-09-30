#pragma once

#include <string>

#include "common/Data.h"
#include "common/Exception.h"

namespace eve {
namespace filesystem {

class FileData : public Data {
public:
    FileData(const std::string &filename, uint64_t size);
    FileData(const FileData &c);

    virtual ~FileData();

    // Implements Data.
    FileData *clone() const;
    void *    getData() const { return data; }
    size_t    getSize() const { return size; }

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
