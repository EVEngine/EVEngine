#pragma once

#include <cstdio>

#include "filesystem/File.h"

namespace eve {
namespace filesystem {

namespace cppfs {

/**
 * @brief File which is created when a user drags and drops an actual file onto the
 * eve game. Uses C++ stdio & filesystem. Filenames are system-dependent full paths.
 **/
class File : public eve::filesystem::File {
public:
    File(std::string filename);
    virtual ~File();

    // Implements eve::filesystem::File.
    using eve::filesystem::File::read;
    using eve::filesystem::File::write;
    bool        open(std::string newmode) override;
    bool        close() override;
    bool        isOpen() const override;
    int64_t     getSize() override;
    int64_t     read(void *dst, int64_t size) override;
    bool        write(const void *data, int64_t size) override;
    bool        flush() override;
    bool        isEOF() override;
    int64_t     tell() override;
    bool        seek(uint64_t pos) override;
    bool        setBuffer(std::string bufmode, int64_t size) override;
    std::string getBuffer(int64_t &size) const override;
    std::string getMode() const override;
    std::string getFilename() const override;

private:
    std::string filename;
    FILE *      file;

    std::string mode;
    std::string bufferMode;
    int64_t     bufferSize;
};
}  // namespace cppfs
}  // namespace filesystem
}  // namespace eve
