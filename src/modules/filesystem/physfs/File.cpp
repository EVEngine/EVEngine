
#include "filesystem/physfs/File.h"

#include <cstring>

#include "common/Exception.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"

namespace eve {
namespace filesystem {

static bool hack_setupWriteDirectory() {
    if (auto* f = getModInst(filesystem,Filesystem))
		return f->setupWriteDirectory();
	return false;
}

namespace physfs {

File::File(std::string filename)
    : filename(filename), file(nullptr), mode("c"), bufferMode("none"), bufferSize(0) {}

File::~File() {
    if (mode != "c") close();
}

bool File::open(std::string mode) {
    if (mode == "c") return true;

    if (!PHYSFS_isInit()) throw Exception("PhysFS is not initialized.");

    // File must exist if read mode.
    if ((mode == "rb") && !PHYSFS_exists(filename.c_str()))
        throw Exception("Could not open file %s. Does not exist.", filename.c_str());

    // Check whether the write directory is set.
    if ((mode == "ab" || mode == "wb") && (PHYSFS_getWriteDir() == nullptr) && !hack_setupWriteDirectory())
        throw Exception("Could not set write directory.");

    // File already open?
    if (file != nullptr) return false;

    PHYSFS_getLastErrorCode();
    PHYSFS_File *handle = nullptr;

    if (mode == "rb") handle = PHYSFS_openRead(filename.c_str());

    if (mode == "ab") handle = PHYSFS_openAppend(filename.c_str());

    if (mode == "wb") handle = PHYSFS_openWrite(filename.c_str());

    if (handle == nullptr) {
        const char *err = PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode());
        if (err == nullptr) err = "unknown error";
        throw Exception("Could not open file %s (%s)", filename.c_str(), err);
    }

    file = handle;

    this->mode = mode;

    if (file != nullptr && !setBuffer(bufferMode, bufferSize)) {
        // Revert to buffer defaults if we don't successfully set the buffer.
        bufferMode = "none";
        bufferSize = 0;
    }

    return (file != nullptr);
}

bool File::close() {
    if (file == nullptr || !PHYSFS_close(file)) return false;

    mode = "c";
    file = nullptr;

    return true;
}

bool File::isOpen() const { return mode != "c" && file != nullptr; }

int64_t File::getSize() {
    // If the file is closed, open it to
    // check the size.
    if (file == nullptr) {
        open("rb");
        int64_t size = (int64_t)PHYSFS_fileLength(file);
        close();
        return size;
    }

    return (int64_t)PHYSFS_fileLength(file);
}

int64_t File::read(void *dst, int64_t size) {
    if (!file || mode != "rb") throw Exception("File is not opened for reading.");

    int64_t max = (int64_t)PHYSFS_fileLength(file);
    size        = (size == ALL) ? max : size;
    size        = (size > max) ? max : size;

    if (size < 0) throw Exception("Invalid read size.");

    return PHYSFS_readBytes(file, dst, (PHYSFS_uint64)size);
}

bool File::write(const void *data, int64_t size) {
    if (!file || (mode != "wb" && mode != "ab")) throw Exception("File is not opened for writing.");

    if (size < 0) throw Exception("Invalid write size.");

    // Try to write.
    int64_t written = PHYSFS_writeBytes(file, data, (PHYSFS_uint64)size);

    // Check that correct amount of data was written.
    if (written != size) return false;

    // Manually flush the buffer in BUFFER_LINE mode if we find a newline.
    if (bufferMode == "newline" && bufferSize > size) {
        if (memchr(data, '\n', (size_t)size) != nullptr) flush();
    }

    return true;
}

bool File::flush() {
    if (!file || (mode != "wb" && mode != "ab")) throw Exception("File is not opened for writing.");

    return PHYSFS_flush(file) != 0;
}

#ifdef EVENGINE_WINDOWS
inline bool test_eof(File *f, PHYSFS_File *) {
    int64_t pos  = f->tell();
    int64_t size = f->getSize();
    return pos == -1 || size == -1 || pos >= size;
}
#else
inline bool test_eof(File *, PHYSFS_File *file) { return PHYSFS_eof(file); }
#endif

bool File::isEOF() { return file == nullptr || test_eof(this, file); }

int64_t File::tell() {
    if (file == nullptr) return -1;

    return (int64_t)PHYSFS_tell(file);
}

bool File::seek(uint64_t pos) { return file != nullptr && PHYSFS_seek(file, (PHYSFS_uint64)pos) != 0; }

bool File::setBuffer(std::string bufmode, int64_t size) {
    if (size < 0) return false;

    // If the file isn't open, we'll make sure the buffer values are set in
    // File::open.
    if (!isOpen()) {
        bufferMode = bufmode;
        bufferSize = size;
        return true;
    }

    int ret = 1;
    if (bufferMode == "newline" || bufferMode == "full") {
        ret = PHYSFS_setBuffer(file, size);
    } else {
        ret  = PHYSFS_setBuffer(file, 0);
        size = 0;
    }

    if (ret == 0) return false;

    bufferMode = bufmode;
    bufferSize = size;

    return true;
}

std::string File::getBuffer(int64_t &size) const {
    size = bufferSize;
    return bufferMode;
}

std::string File::getFilename() const { return filename; }

std::string File::getMode() const { return mode; }

}  // namespace physfs
}  // namespace filesystem
}  // namespace eve
