
#include "filesystem/Filesystem.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <cstring>
#include <simplesquirrel/simplesquirrel.hpp>

#include "filesystem/physfs/Filesystem.h"

#if defined(EVENGINE_MACOSX)
#include "macosx/macosx.h"
#elif defined(EVENGINE_IOS)
#include "ios/ios.h"
#elif defined(EVENGINE_WINDOWS)
#include <windows.h>

#include "common/utf8.h"
#elif defined(EVENGINE_LINUX)
#include <unistd.h>
#endif

namespace eve {
namespace filesystem {

Module_IMPL(Filesystem);

Filesystem *Filesystem::create() {
    auto p = registered_modules.find(name);
    if (p != registered_modules.end()) return (Filesystem *)(p->second);
    auto n                   = new physfs::Filesystem();
    registered_modules[name] = n;
    return n;
}

void Filesystem::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Filesystem::create, false);
    expose(cls);
}

void Filesystem::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Filesystem::getName);
    cls.addFunc("init", &Filesystem::init);
    cls.addFunc("setFused", &Filesystem::setFused);
    cls.addFunc("isFused", &Filesystem::isFused);
    cls.addFunc("setupWriteDirectory", &Filesystem::setupWriteDirectory);
    cls.addFunc("setAndroidSaveExternal", &Filesystem::setAndroidSaveExternal);
    cls.addFunc("isAndroidSaveExternal", &Filesystem::isAndroidSaveExternal);
}

FileData *Filesystem::newFileData(const void *data, std::string filename, size_t size) const {
    FileData *fd = new FileData(std::string(filename), size);
    memcpy(fd->getData(), data, size);
    return fd;
}

bool Filesystem::isRealDirectory(const std::string &path) const {
#ifdef EVENGINE_WINDOWS
    // make sure non-ASCII paths work.
    std::wstring wpath = to_widestr(path);

    struct _stat buf;
    if (_wstat(wpath.c_str(), &buf) != 0) return false;
    return (buf.st_mode & _S_IFDIR) == _S_IFDIR;
#else
    // Assume POSIX support...
    struct stat buf;
    if (stat(path.c_str(), &buf) != 0) return false;
    return S_ISDIR(buf.st_mode) != 0;
#endif
}

std::string Filesystem::getExecutablePath() const {
#if defined(EVENGINE_MACOSX)
    return macosx::getExecutablePath();
#elif defined(EVENGINE_IOS)
    return ios::getExecutablePath();
#elif defined(EVENGINE_WINDOWS)
    wchar_t buffer[MAX_PATH + 1] = {0};
    if (GetModuleFileNameW(nullptr, buffer, MAX_PATH) == 0) return "";
    return to_utf8(buffer);
#elif defined(EVENGINE_LINUX)
    char    buffer[2048] = {0};
    ssize_t len          = readlink("/proc/self/exe", buffer, 2048);
    if (len <= 0) return "";
    return std::string(buffer, len);
#else
#error Missing implementation for Filesystem::getExecutablePath!
#endif
}

}  // namespace filesystem
}  // namespace eve
