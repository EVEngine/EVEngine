#include "model3d/EveFileSystem.h"

#include "filesystem/File.h"
#include "filesystem/Filesystem.h"

#include <cstdio>

namespace eve {
namespace model3d {

namespace {

struct FileHandle {
    filesystem::File *file = nullptr;
};

}  // namespace

EveFileSystem::EveFileSystem(filesystem::Filesystem *fs) : fs_(fs) {}

medialoader::FileHandle *EveFileSystem::open(const char *path, const char *mode) {
    if (!fs_ || !path || !mode)
        return nullptr;
    try {
        filesystem::File *file = fs_->newFile(path);
        if (!file)
            return nullptr;
        if (!file->open(mode)) {
            delete file;
            return nullptr;
        }
        auto *h = new FileHandle();
        h->file = file;
        return reinterpret_cast<medialoader::FileHandle *>(h);
    } catch (...) {
        return nullptr;
    }
}

size_t EveFileSystem::read(medialoader::FileHandle *h, void *buf, size_t size) {
    auto *fh = reinterpret_cast<FileHandle *>(h);
    if (!fh || !fh->file || !buf || size == 0)
        return 0;
    try {
        int64_t n = fh->file->read(buf, static_cast<int64_t>(size));
        return n > 0 ? static_cast<size_t>(n) : 0;
    } catch (...) {
        return 0;
    }
}

bool EveFileSystem::seek(medialoader::FileHandle *h, int64_t offset, int whence) {
    auto *fh = reinterpret_cast<FileHandle *>(h);
    if (!fh || !fh->file)
        return false;
    try {
        int64_t pos = 0;
        if (whence == SEEK_SET)
            pos = offset;
        else if (whence == SEEK_CUR)
            pos = fh->file->tell() + offset;
        else if (whence == SEEK_END)
            pos = fh->file->getSize() + offset;
        else
            return false;
        if (pos < 0)
            return false;
        return fh->file->seek(static_cast<uint64_t>(pos));
    } catch (...) {
        return false;
    }
}

int64_t EveFileSystem::tell(medialoader::FileHandle *h) {
    auto *fh = reinterpret_cast<FileHandle *>(h);
    if (!fh || !fh->file)
        return -1;
    try {
        return fh->file->tell();
    } catch (...) {
        return -1;
    }
}

int64_t EveFileSystem::size(medialoader::FileHandle *h) {
    auto *fh = reinterpret_cast<FileHandle *>(h);
    if (!fh || !fh->file)
        return -1;
    try {
        return fh->file->getSize();
    } catch (...) {
        return -1;
    }
}

void EveFileSystem::close(medialoader::FileHandle *h) {
    auto *fh = reinterpret_cast<FileHandle *>(h);
    if (!fh)
        return;
    if (fh->file) {
        try {
            if (fh->file->isOpen())
                fh->file->close();
        } catch (...) {
        }
        delete fh->file;
        fh->file = nullptr;
    }
    delete fh;
}

bool EveFileSystem::exists(const char *path) const {
    if (!fs_ || !path)
        return false;
    try {
        filesystem::Filesystem::Info info;
        return fs_->getInfo(path, info);
    } catch (...) {
        return false;
    }
}

}  // namespace model3d
}  // namespace eve
