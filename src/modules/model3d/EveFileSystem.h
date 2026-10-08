#pragma once

#include "medialoader/model/FileSystem.h"

namespace eve {
namespace filesystem {
class Filesystem;
}

namespace model3d {

/** @brief medialoader::FileSystem adapter over eve::filesystem (physfs / VFS). */
class EveFileSystem : public medialoader::FileSystem {
public:
    /** @brief Constructs a EveFileSystem. */
    explicit EveFileSystem(filesystem::Filesystem *fs);

    /** @brief Opens . */
    medialoader::FileHandle *open(const char *path, const char *mode) override;
    /** @brief Reads . */
    size_t read(medialoader::FileHandle *h, void *buf, size_t size) override;
    /** @brief Seeks . */
    bool seek(medialoader::FileHandle *h, int64_t offset, int whence) override;
    /** @brief Tell. */
    int64_t tell(medialoader::FileHandle *h) override;
    /** @brief Returns the size of . */
    int64_t size(medialoader::FileHandle *h) override;
    /** @brief Closes . */
    void close(medialoader::FileHandle *h) override;
    /** @brief True if active. */
    bool exists(const char *path) const override;

private:
    filesystem::Filesystem *fs_;
};

}  // namespace model3d
}  // namespace eve
