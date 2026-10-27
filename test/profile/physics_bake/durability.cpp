// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "role": "probe",
  "default": "platform-file",
  "module": "physics_destruction",
  "task": "house-collapse"
}
*/
// clang-format on

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <array>
#include <bit>
#include <cerrno>
#include <cstdarg>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "physics/backend/bake/BakedAssetFile.h"
#include "physics/destruction/CollapseAnimation.h"

namespace {
int  calls         = 0;
int  failAt        = 0;
bool interruptOnce = false;
int  crashAt       = 0;
int  writeFault    = 0;
bool unsupported   = false;
void check(bool value) {
    if (!value) throw std::runtime_error("durability contract failed");
}
void word(std::ofstream& out, unsigned value) {
    for (unsigned i = 0; i < 4; ++i) out.put(static_cast<char>((value >> (8 * i)) & 255));
}
bool detail(const eve::Status& status, const std::string& value) {
    for (const auto& diagnostic : status.diagnostics())
        for (const auto& [key, text] : diagnostic.details())
            if (key == "publication" && text == value) return true;
    return false;
}
struct Directory {
    std::filesystem::path path;
    ~Directory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};
}  // namespace
extern "C" int     __real_fsync(int descriptor);
extern "C" ssize_t __real_write(int descriptor, const void* data, size_t count);
extern "C" int     __real_openat(int directory, const char* path, int flags, ...);
extern "C" int     __wrap_openat(int directory, const char* path, int flags, ...) {
    va_list args;
    va_start(args, flags);
    const int mode = ((flags & O_CREAT) || (flags & O_TMPFILE) == O_TMPFILE) ? va_arg(args, int) : 0;
    va_end(args);
    if (unsupported) {
        errno = EOPNOTSUPP;
        return -1;
    }
    return __real_openat(directory, path, flags, mode);
}
extern "C" ssize_t __wrap_write(int descriptor, const void* data, size_t count) {
    if (writeFault == 1) {
        writeFault = 2;
        errno      = EINTR;
        return -1;
    }
    if (writeFault == 2 && count > 7) count = 7;
    if (writeFault == 3) {
        errno = ENOSPC;
        return -1;
    }
    if (writeFault == 4) return 0;
    return __real_write(descriptor, data, count);
}
extern "C" int __wrap_fsync(int descriptor) {
    if (interruptOnce) {
        interruptOnce = false;
        errno         = EINTR;
        return -1;
    }
    ++calls;
    if (calls == crashAt) {
        ::kill(::getpid(), SIGKILL);
        ::_exit(78);
    }
    struct stat info {};
    if (::fstat(descriptor, &info) != 0) return -1;
    // Prove the ordering and object types, not just the number of calls.
    if ((calls == 1 && !S_ISREG(info.st_mode)) || (calls == 2 && !S_ISDIR(info.st_mode))) {
        errno = EBADF;
        return -1;
    }
    if (calls == failAt) {
        errno = EIO;
        return -1;
    }
    return __real_fsync(descriptor);
}
int main() {
    try {
        auto pattern = (std::filesystem::temp_directory_path() / "eve-durability-XXXXXX").string();
        check(::mkdtemp(pattern.data()) != nullptr);
        Directory  directory{pattern};
        const auto source = directory.path / "source";
        {
            std::ofstream out(source, std::ios::binary);
            out.write("EVECOLL1", 8);
            for (unsigned v : {1u, 1u, 1u}) word(out, v);
            word(out, 0);
            for (float v : {0.5f, 0.5f, 0.5f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f}) word(out, std::bit_cast<unsigned>(v));
            for (unsigned v : {0u, 0u, 0u}) word(out, v);
        }
        auto clip = eve::physics::CollapseAnimation::load(source.string()).expect("fixture");
        for (int failure : {1, 2, 0}) {
            calls             = 0;
            failAt            = failure;
            interruptOnce     = true;
            const auto target = directory.path / ("clip" + std::to_string(failure));
            auto       result = clip.save(target.string());
            check(result.ok() == (failure == 0));
            check(calls == (failure == 1 ? 1 : 2));
            check(std::filesystem::exists(target) == (failure != 1));
            if (failure != 0) check(detail(result.status(), failure == 1 ? "not_published" : "published"));
            if (failure != 1) {
                check(eve::physics::CollapseAnimation::load(target.string()).ok());
                calls      = 0;
                failAt     = 0;
                auto retry = clip.save(target.string());
                check(!retry.ok());
                check(retry.status().code() == eve::StatusCode::Conflict);
                check(eve::physics::CollapseAnimation::load(target.string()).ok());
            }
            for (const auto& entry : std::filesystem::directory_iterator(directory.path))
                check(!entry.path().filename().string().starts_with(".eve-collapse-"));
        }
        for (int fault : {1, 3, 4}) {
            writeFault        = fault;
            calls             = 0;
            const auto target = directory.path / ("write" + std::to_string(fault));
            auto       result = clip.save(target.string());
            check(result.ok() == (fault == 1));
            check(std::filesystem::exists(target) == (fault == 1));
            if (fault == 1) check(eve::physics::CollapseAnimation::load(target.string()).ok());
        }
        writeFault                 = 0;
        unsupported                = true;
        const auto unsupportedPath = directory.path / "unsupported";
        auto       unavailable     = clip.save(unsupportedPath.string());
        check(!unavailable.ok());
        check(unavailable.status().code() == eve::StatusCode::Unsupported);
        check(!std::filesystem::exists(unsupportedPath));
        unsupported = false;
        for (int stage : {1, 2}) {
            const auto target = directory.path / ("crash" + std::to_string(stage));
            const auto child  = ::fork();
            check(child >= 0);
            if (child == 0) {
                calls       = 0;
                failAt      = 0;
                crashAt     = stage;
                auto result = clip.save(target.string());
                result.ignore();
                ::_exit(78);  // The injected interruption must occur before save returns.
            }
            int status = 0;
            check(::waitpid(child, &status, 0) == child);
            check(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
            check(std::filesystem::exists(target) == (stage == 2));
            if (stage == 2) check(eve::physics::CollapseAnimation::load(target.string()).ok());
            for (const auto& entry : std::filesystem::directory_iterator(directory.path))
                check(!entry.path().filename().string().starts_with(".eve-collapse-"));
        }
        // Independent writers race for one immutable cache key. Each stages different
        // complete bytes so the final payload identifies the winner and detects mixing.
        constexpr int Writers = 8;
        int           start[2];
        check(::pipe(start) == 0);
        std::array<pid_t, Writers> children{};
        const auto                 contested = directory.path / "contested";
        for (int writer = 0; writer < Writers; ++writer) {
            children[writer] = ::fork();
            check(children[writer] >= 0);
            if (children[writer] == 0) {
                ::close(start[1]);
                char    signal = 0;
                ssize_t readCount;
                do {
                    readCount = ::read(start[0], &signal, 1);
                } while (readCount < 0 && errno == EINTR);
                ::close(start[0]);
                if (readCount != 1) ::_exit(90);
                calls   = 0;
                failAt  = 0;
                crashAt = 0;
                const std::string bytes(65536, static_cast<char>('A' + writer));
                auto              published = eve::physics::publishBakedAsset(contested.string(), bytes);
                if (published.ok()) ::_exit(0);
                if (published.status().code() == eve::StatusCode::Conflict) ::_exit(10);
                ::_exit(91);
            }
        }
        ::close(start[0]);
        for (int writer = 0; writer < Writers; ++writer) {
            const char signal = 1;
            check(::write(start[1], &signal, 1) == 1);
        }
        ::close(start[1]);
        int winner = -1, winners = 0, conflicts = 0;
        for (int writer = 0; writer < Writers; ++writer) {
            int status = 0;
            check(::waitpid(children[writer], &status, 0) == children[writer]);
            check(WIFEXITED(status));
            const int code = WEXITSTATUS(status);
            if (code == 0) {
                winner = writer;
                ++winners;
            } else if (code == 10)
                ++conflicts;
            else
                check(false);
        }
        check(winners == 1 && conflicts == Writers - 1);
        std::ifstream     published(contested, std::ios::binary);
        const std::string actual{std::istreambuf_iterator<char>(published), std::istreambuf_iterator<char>()};
        check(actual == std::string(65536, static_cast<char>('A' + winner)));
        std::cout << "concurrent publication writers=" << Writers << " winners=" << winners
                  << " conflicts=" << conflicts << " verified_bytes=" << actual.size() << '\n';
        std::cout << "durability barriers, errors, publication state and process-interruption cleanup passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
