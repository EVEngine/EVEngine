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
    /** @brief Constructs a File. */
    File(std::string filename);
    /** @brief Releases File resources. */
    virtual ~File();

    // Implements eve::filesystem::File.
    using eve::filesystem::File::read;
    using eve::filesystem::File::write;
    /** @brief Opens open. */
    bool        open(std::string newmode) override;
    /** @brief Closes close. */
    bool        close() override;
    /** @brief True when open. */
    bool        isOpen() const override;
    /** @brief Byte length of the owned buffer. */
    int64_t     getSize() override;
    /** @brief Reads read. */
    int64_t     read(void *dst, int64_t size) override;
    /** @brief Writes write. */
    bool        write(const void *data, int64_t size) override;
    /** @brief Flushes flush. */
    bool        flush() override;
    /** @brief True when eof. */
    bool        isEOF() override;
    /** @brief Returns the current tell. */
    int64_t     tell() override;
    /** @brief Seeks seek. */
    bool        seek(uint64_t pos) override;
    /** @brief Sets the buffer. */
    bool        setBuffer(std::string bufmode, int64_t size) override;
    /** @brief Returns the buffer. */
    std::string getBuffer(int64_t &size) const override;
    /** @brief Returns the mode. */
    std::string getMode() const override;
    /** @brief Full filename including extension. */
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
