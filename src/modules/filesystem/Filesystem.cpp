
#include "filesystem/Filesystem.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <string_view>
#include <simplesquirrel/simplesquirrel.hpp>

#include "common/Assert.h"
#include "filesystem/physfs/Filesystem.h"

#if defined(EVENGINE_MACOSX)
#include "macosx/macosx.h"
#elif defined(EVENGINE_IOS)
#include "ios/ios.h"
#elif defined(EVENGINE_ANDROID)
#include "android/android.h"
#elif defined(EVENGINE_WEBGPU)
#include "webgpu/webplatform.h"
#elif defined(EVENGINE_WINDOWS)
#include <windows.h>

#include "common/utf8.h"
#elif defined(EVENGINE_LINUX)
#include <unistd.h>
#endif

#if !defined(EVENGINE_WINDOWS) && !defined(EVENGINE_WEBGPU)
#include <fcntl.h>
#include <unistd.h>
#endif

#if defined(EVENGINE_WEBGPU) && defined(EVENGINE_WINDOWS)
#include <windows.h>

#include "common/utf8.h"
#endif

namespace eve {
namespace filesystem {

namespace {

eve::Result<void> atomicWriteFailure(eve::DiagnosticCode code, std::string message,
                                     std::string path) {
    return eve::Result<void>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "filesystem.atomic-write"));
}

std::filesystem::path pathFromUtf8(std::string_view text) {
    const auto *data = reinterpret_cast<const char8_t *>(text.data());
    return std::filesystem::path(std::u8string_view(data, text.size()));
}

bool validRelativeSavePath(const std::filesystem::path &path) {
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) return false;
    for (const auto &part : path) {
        if (part == ".." || part == ".") return false;
    }
    return true;
}

std::atomic<std::uint64_t> &atomicWriteSequence() {
    static std::atomic<std::uint64_t> sequence{1};
    return sequence;
}

}  // namespace

Module_IMPL(Filesystem, new physfs::Filesystem());

bool Filesystem::writeText(const std::string &filename, const std::string &text) const {
    try {
        write(filename, text.data(), static_cast<int64_t>(text.size()));
        return true;
    } catch (const eve::Exception &) {
        return false;
    }
}

eve::Result<void> Filesystem::writeTextAtomic(std::string_view filename, std::string_view text) {
#if defined(EVENGINE_WEBGPU)
    (void)filename;
    (void)text;
    return atomicWriteFailure(eve::DiagnosticCode::Unsupported,
                              "atomic save replacement is unavailable on the WebGPU filesystem", {});
#else
    const std::string relativeText(filename);
    const std::filesystem::path relative = pathFromUtf8(relativeText).lexically_normal();
    if (!validRelativeSavePath(relative))
        return atomicWriteFailure(eve::DiagnosticCode::InvalidArgument,
                                  "atomic save path must be relative and cannot traverse parents", relativeText);
    const std::string saveDirectory = getSaveDirectory();
    if (saveDirectory.empty())
        return atomicWriteFailure(eve::DiagnosticCode::PreconditionViolation,
                                  "filesystem write directory is not configured", relativeText);
    const std::filesystem::path base = pathFromUtf8(saveDirectory).lexically_normal();
    const std::filesystem::path target = (base / relative).lexically_normal();
    if (target.parent_path().empty() || !std::filesystem::exists(target.parent_path()))
        return atomicWriteFailure(eve::DiagnosticCode::NotFound,
                                  "atomic save parent directory does not exist", relativeText);

    const auto sequence = atomicWriteSequence().fetch_add(1, std::memory_order_relaxed);
#if defined(EVENGINE_WINDOWS)
    const std::wstring targetPath = target.wstring();
    const std::wstring temporaryPath = targetPath + L".tmp." + std::to_wstring(GetCurrentProcessId()) +
                                       L"." + std::to_wstring(sequence);
    HANDLE file = CreateFileW(temporaryPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return atomicWriteFailure(eve::DiagnosticCode::Failed,
                                  "could not create atomic save temporary file", relativeText);
    bool written = true;
    std::size_t offset = 0;
    while (offset < text.size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(text.size() - offset, 0x7fffffffu));
        DWORD completed = 0;
        if (!WriteFile(file, text.data() + offset, chunk, &completed, nullptr) || completed != chunk) {
            written = false;
            break;
        }
        offset += completed;
    }
    if (written && !FlushFileBuffers(file)) written = false;
    if (!CloseHandle(file)) written = false;
    if (!written) {
        DeleteFileW(temporaryPath.c_str());
        return atomicWriteFailure(eve::DiagnosticCode::Failed,
                                  "could not write or flush atomic save temporary file", relativeText);
    }
    if (!MoveFileExW(temporaryPath.c_str(), targetPath.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporaryPath.c_str());
        return atomicWriteFailure(eve::DiagnosticCode::Failed,
                                  "could not atomically replace save file", relativeText);
    }
#else
    const std::string targetPath = target.string();
    const std::string temporaryPath = targetPath + ".tmp." + std::to_string(getpid()) + "." +
                                      std::to_string(sequence);
    const int file = ::open(temporaryPath.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (file < 0)
        return atomicWriteFailure(eve::DiagnosticCode::Failed,
                                  "could not create atomic save temporary file", relativeText);
    bool written = true;
    std::size_t offset = 0;
    while (offset < text.size()) {
        const ssize_t completed = ::write(file, text.data() + offset, text.size() - offset);
        if (completed < 0) {
            if (errno == EINTR) continue;
            written = false;
            break;
        }
        offset += static_cast<std::size_t>(completed);
    }
    if (written && ::fsync(file) != 0) written = false;
    if (::close(file) != 0) written = false;
    if (!written) {
        ::unlink(temporaryPath.c_str());
        return atomicWriteFailure(eve::DiagnosticCode::Failed,
                                  "could not write or flush atomic save temporary file", relativeText);
    }
    if (::rename(temporaryPath.c_str(), targetPath.c_str()) != 0) {
        ::unlink(temporaryPath.c_str());
        return atomicWriteFailure(eve::DiagnosticCode::Failed,
                                  "could not atomically replace save file", relativeText);
    }
#ifdef O_DIRECTORY
    const int directory = ::open(target.parent_path().string().c_str(), O_RDONLY | O_DIRECTORY);
    if (directory >= 0) {
        (void)::fsync(directory);
        (void)::close(directory);
    }
#endif
#endif
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
#endif
}

std::string Filesystem::readText(const std::string &filename) const {
    try {
        FileData *data = read(filename);
        if (!data) return {};
        const auto *bytes = static_cast<const char *>(data->getData());
        std::string text(bytes, static_cast<size_t>(data->getSize()));
        delete data;
        return text;
    } catch (const eve::Exception &) {
        return {};
    }
}

void Filesystem::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Filesystem::create, false);
    expose(cls);
}

void Filesystem::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Filesystem::getName);
    cls.addFunc("setFused", &Filesystem::setFused);
    cls.addFunc("isFused", &Filesystem::isFused);
    cls.addFunc("setupWriteDirectory", &Filesystem::setupWriteDirectory);
    cls.addFunc("setAndroidSaveExternal", &Filesystem::setAndroidSaveExternal);
    cls.addFunc("isAndroidSaveExternal", &Filesystem::isAndroidSaveExternal);
    cls.addFunc("setIdentity", &Filesystem::setIdentity);
    cls.addFunc("getIdentity", &Filesystem::getIdentity);
    cls.addFunc("setSource", &Filesystem::setSource);
    cls.addFunc("getSource", &Filesystem::getSource);
    cls.addFunc("allowMountingForPath", &Filesystem::allowMountingForPath);
    cls.addFunc("mountExternalReadOnly", &Filesystem::mountExternalReadOnly);
    cls.addFunc("mountPath", [](Filesystem *self, const std::string &archive,
                                const std::string &mountpoint, bool appendToPath) {
        return self && self->mount(archive, mountpoint, appendToPath);
    });
    cls.addFunc("unmountPath", [](Filesystem *self, const std::string &archive) {
        return self && self->unmount(archive);
    });
    cls.addFunc("newFile", &Filesystem::newFile);
    cls.addFunc("newFileData", &Filesystem::newFileData);
    cls.addFunc("getWorkingDirectory", &Filesystem::getWorkingDirectory);
    cls.addFunc("getUserDirectory", &Filesystem::getUserDirectory);
    cls.addFunc("getAppdataDirectory", &Filesystem::getAppdataDirectory);
    cls.addFunc("getSaveDirectory", &Filesystem::getSaveDirectory);
    cls.addFunc("getSourceBaseDirectory", &Filesystem::getSourceBaseDirectory);
    cls.addFunc("getRealDirectory", &Filesystem::getRealDirectory);
    cls.addFunc("getExecutablePath", &Filesystem::getExecutablePath);
    cls.addFunc("createDirectory", &Filesystem::createDirectory);
    cls.addFunc("remove", &Filesystem::remove);
    cls.addFunc("read", &Filesystem::read);
    cls.addFunc("write", &Filesystem::write);
    cls.addFunc("append", &Filesystem::append);
    cls.addFunc("writeText", &Filesystem::writeText);
    cls.addFunc("writeTextAtomic", [](Filesystem *self, const std::string &filename,
                                      const std::string &text) -> int {
        return self && self->writeTextAtomic(filename, text).ok() ? 1 : 0;
    });
    cls.addFunc("readText", &Filesystem::readText);
    cls.addFunc("getDirectoryItems", &Filesystem::getDirectoryItems);
    cls.addFunc("setSymlinksEnabled", &Filesystem::setSymlinksEnabled);
    cls.addFunc("areSymlinksEnabled", &Filesystem::areSymlinksEnabled);
    cls.addFunc("getRequirePath", &Filesystem::getRequirePath);
    cls.addFunc("getCRequirePath", &Filesystem::getCRequirePath);
    cls.addFunc("getRequirePath", &Filesystem::getRequirePath);
    cls.addFunc("isRealDirectory", &Filesystem::isRealDirectory);
    cls.addFunc("watch", &Filesystem::watch);
    cls.addFunc("unwatch", &Filesystem::unwatch);
    cls.addFunc("unwatchAll", &Filesystem::unwatchAll);
    cls.addFunc("getWatchCount", &Filesystem::getWatchCount);
    cls.addFunc("pollWatch", &Filesystem::pollWatch);
    cls.addFunc("getLastWatchPath", &Filesystem::getLastWatchPath);
    cls.addFunc("getLastWatchRealPath", &Filesystem::getLastWatchRealPath);
}

bool Filesystem::mountExternalReadOnly(const std::string &path, const std::string &mountpoint) {
    if (path.empty() || mountpoint.empty())
        return false;
    allowMountingForPath(path);
    return mount(path, mountpoint, false);
}

FileData *Filesystem::newFileData(const void *data, std::string filename, size_t size) const {
    const bool validData = size == 0 || data != nullptr;
    EV_PARAM_CHECK(validData, "file data must not be null when size > 0");
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
#elif defined(EVENGINE_ANDROID)
    return android::getExecutablePath();
#elif defined(EVENGINE_WEBGPU)
    return eve::webgpu_platform::getExecutablePath();
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
