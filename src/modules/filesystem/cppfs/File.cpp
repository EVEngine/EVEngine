#include "filesystem/cppfs/File.h"

#include "common/Exception.h"

// Assume POSIX or Visual Studio.
#include <sys/stat.h>
#include <sys/types.h>

#include "common/utf8.h"
#ifdef EVENGINE_WINDOWS
#include <wchar.h>
#else
#include <unistd.h>  // POSIX.
#endif

namespace eve {
namespace filesystem {
namespace cppfs {

File::File(std::string filename) : filename(filename), file(nullptr), mode("c"), bufferMode("none"), bufferSize(0) {}

File::~File() {
    if (mode != "c") close();
}

bool File::open(std::string newmode) {
    if (newmode == "c") return true;

    // File already open?
    if (file != nullptr) return false;

#ifdef EVENGINE_WINDOWS
    // make sure non-ASCII filenames work.
    std::wstring wnewmode  = to_widestr(newmode);
    std::wstring wfilename = to_widestr(filename);

    file = _wfopen(wfilename.c_str(), wnewmode.c_str());
#else
    file = fopen(filename.c_str(), newmode.c_str());
#endif

    if (newmode.find('c') != std::string::npos && file == nullptr)
        throw Exception("Could not open file %s. Does not exist.", filename.c_str());

    mode = newmode;

    if (file != nullptr && !setBuffer(bufferMode, bufferSize)) {
        // Revert to buffer defaults if we don't successfully set the buffer.
        bufferMode = "none";
        bufferSize = 0;
    }

    return file != nullptr;
}

bool File::close() {
    if (file == nullptr || fclose(file) != 0) return false;

    mode = "c";
    file = nullptr;

    return true;
}

bool File::isOpen() const { return mode != "c" && file != nullptr; }

int64_t File::getSize() {
    int fd = file ? fileno(file) : -1;

#ifdef EVENGINE_WINDOWS

    struct _stat64 buf;

    if (fd != -1) {
        if (_fstat64(fd, &buf) != 0) return -1;
    } else {
        // make sure non-ASCII filenames work.
        std::wstring wfilename = to_widestr(filename);

        if (_wstat64(wfilename.c_str(), &buf) != 0) return -1;
    }

    return (int64_t)buf.st_size;

#else

    // Assume POSIX support...
    struct stat buf;

    if (fd != -1) {
        if (fstat(fd, &buf) != 0) return -1;
    } else if (stat(filename.c_str(), &buf) != 0)
        return -1;

    return (int64_t)buf.st_size;

#endif
}

int64_t File::read(void *dst, int64_t size) {
    if (!file || mode != "rb") throw Exception("File is not opened for reading.");

    if (size < 0) throw Exception("Invalid read size.");

    size_t read = fread(dst, 1, (size_t)size, file);

    return (int64_t)read;
}

bool File::write(const void *data, int64_t size) {
    if (!file || (mode != "wb" && mode != "ab")) throw Exception("File is not opened for writing.");

    if (size < 0) throw Exception("Invalid write size.");

    int64_t written = (int64_t)fwrite(data, 1, (size_t)size, file);

    return written == size;
}

bool File::flush() {
    if (!file || (mode != "wb" && mode != "ab")) throw Exception("File is not opened for writing.");

    return fflush(file) == 0;
}

bool File::isEOF() { return file == nullptr || tell() >= getSize(); }

int64_t File::tell() {
    if (file == nullptr) return -1;

#ifdef EVENGINE_WINDOWS
    return (int64_t)_ftelli64(file);
#else
    return (int64_t)ftello(file);
#endif
}

bool File::seek(uint64_t pos) {
    if (file == nullptr) return false;

#ifdef EVENGINE_WINDOWS
    return _fseeki64(file, (int64_t)pos, SEEK_SET) == 0;
#else
    return fseeko(file, (off_t)pos, SEEK_SET) == 0;
#endif
}

bool File::setBuffer(std::string bufmode, int64_t size) {
    if (size < 0) return false;

    if (bufmode == "none") size = 0;

    // If the file isn't open, we'll make sure the buffer values are set in
    // File::open.
    if (!isOpen()) {
        bufferMode = bufmode;
        bufferSize = size;
        return true;
    }

    int vbufmode;
    vbufmode = _IONBF;
    if (bufferMode == "newline") vbufmode = _IOLBF;
    if (bufferMode == "full") vbufmode = _IOFBF;

    if (setvbuf(file, nullptr, vbufmode, (size_t)size) != 0) return false;

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

}  // namespace cppfs
}  // namespace filesystem
}  // namespace eve
