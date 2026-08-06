#pragma once

#include "medialoader/model/FileSystem.h"

namespace eve {
namespace filesystem {
class Filesystem;
}

namespace model3d {

/** medialoader::FileSystem adapter over eve::filesystem (physfs / VFS). */
class EveFileSystem : public medialoader::FileSystem {
public:
    explicit EveFileSystem(filesystem::Filesystem *fs);

    medialoader::FileHandle *open(const char *path, const char *mode) override;
    size_t read(medialoader::FileHandle *h, void *buf, size_t size) override;
    bool seek(medialoader::FileHandle *h, int64_t offset, int whence) override;
    int64_t tell(medialoader::FileHandle *h) override;
    int64_t size(medialoader::FileHandle *h) override;
    void close(medialoader::FileHandle *h) override;
    bool exists(const char *path) const override;

private:
    filesystem::Filesystem *fs_;
};

}  // namespace model3d
}  // namespace eve
