#include "physics/backend/bake/BakedAssetFile.h"
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#if defined(__linux__)
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#endif
namespace eve::physics {
std::string bakedAssetContentKey(std::string_view source) {
    std::uint64_t a = 14695981039346656037ull, b = 0x9e3779b97f4a7c15ull;
    for (unsigned char c : source) {
        a = (a ^ c) * 1099511628211ull;
        b = (b ^ c) * 0x100000001b3ull;
        b ^= b >> 29;
    }
    constexpr char digits[] = "0123456789abcdef";
    std::string    key(32, '0');
    for (unsigned i = 0; i < 16; ++i) {
        key[i]      = digits[(a >> ((15 - i) * 4)) & 15];
        key[16 + i] = digits[(b >> ((15 - i) * 4)) & 15];
    }
    return key;
}

namespace {
// The directory is exclusively created before opening its child. Cleanup never touches the destination.
struct TemporaryAnimation {
    std::filesystem::path directory;
    ~TemporaryAnimation() {
        if (directory.empty()) return;
        std::error_code ignored;
        std::filesystem::remove(directory / "clip", ignored);
        std::filesystem::remove(directory, ignored);
    }
};
#if defined(__linux__)
// Linux descriptors pin the directory across publication and are never retained by the clip.
struct PublicationDescriptor {
    int value = -1;
    ~PublicationDescriptor() {
        if (value >= 0) ::close(value);
    }
};
Result<void> syncPublicationDescriptor(int descriptor, std::string_view path, std::string_view publication) {
    int result;
    do {
        result = ::fsync(descriptor);
    } while (result < 0 && errno == EINTR);
    if (result < 0)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, "animation durability barrier failed",
                                                       std::string(path),
                                                       {{"publication", std::string(publication)},
                                                        {"durability", "unconfirmed"},
                                                        {"system_error", std::to_string(errno)}}));
    return Result<void>::success();
}
#endif
}  // namespace
Result<void> publishBakedAsset(std::string_view path, std::string_view bytes) {
    if (path.empty() || path.find('\0') != std::string_view::npos)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "asset path is empty or contains NUL"));
    const std::filesystem::path target(path);
    if (target.filename().empty())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "asset filename is empty"));
#if defined(__linux__)
    const auto            parent = target.parent_path().empty() ? std::filesystem::path(".") : target.parent_path();
    PublicationDescriptor directory{::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
    if (directory.value < 0)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "cannot open asset publication directory", std::string(path)));
    // No visible staging name: the kernel reclaims unpublished data even after SIGKILL.
    PublicationDescriptor data{::openat(directory.value, ".", O_TMPFILE | O_WRONLY | O_CLOEXEC, 0666)};
    if (data.value < 0) {
        const int  error = errno;
        const auto code  = error == EOPNOTSUPP || error == EINVAL || error == EISDIR ? DiagnosticCode::Unsupported
                                                                                     : DiagnosticCode::Failed;
        return Result<void>::failure(Diagnostic::error(code, "anonymous asset staging unavailable", std::string(path),
                                                       {{"system_error", std::to_string(error)}}));
    }
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto count =
            std::min(bytes.size() - offset, static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
        const auto written = ::write(data.value, bytes.data() + offset, count);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::Failed, "asset staging write failed", std::string(path)));
        offset += static_cast<std::size_t>(written);
    }
    auto synced = syncPublicationDescriptor(data.value, path, "not_published");
    if (!synced) return synced;
    // procfs descriptor linking works without CAP_DAC_READ_SEARCH (unlike AT_EMPTY_PATH).
    const auto source = "/proc/self/fd/" + std::to_string(data.value);
    if (::linkat(AT_FDCWD, source.c_str(), directory.value, target.filename().c_str(), AT_SYMLINK_FOLLOW) < 0) {
        const int error = errno;
        return Result<void>::failure(
            Diagnostic::error(error == EEXIST ? DiagnosticCode::Conflict : DiagnosticCode::Failed,
                              "asset publication failed (requires procfs descriptor links and a new path)",
                              std::string(path), {{"system_error", std::to_string(error)}}));
    }
    // Never roll back a complete target that readers may already have observed.
    return syncPublicationDescriptor(directory.value, path, "published");
#else
    static std::atomic<std::uint64_t> sequence{0};
    TemporaryAnimation                temporary;
    // This entropy names temporary storage only; it never enters simulation or content identity.
    std::random_device entropy;
    for (unsigned attempt = 0; attempt < 16; ++attempt) {
        const auto name = ".eve-collapse-" + std::to_string(entropy()) + "-" +
                          std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
        auto            candidate = target.parent_path() / name;
        std::error_code error;
        if (std::filesystem::create_directory(candidate, error)) {
            temporary.directory = std::move(candidate);
            break;
        }
        if (error && error != std::errc::file_exists)
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::Failed, "cannot create animation temporary directory", std::string(path)));
    }
    if (temporary.directory.empty())
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Conflict, "cannot reserve animation temporary directory", std::string(path)));
    const auto    staging = temporary.directory / "clip";
    std::ofstream file(staging, std::ios::binary);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.close();
    if (!file)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "animation temporary write failed", std::string(path)));
    std::error_code error;
    std::filesystem::create_hard_link(staging, target, error);
    if (error)
        return Result<void>::failure(Diagnostic::error(
            error == std::errc::file_exists ? DiagnosticCode::Conflict : DiagnosticCode::Failed,
            "animation publication failed (requires same-filesystem hard links and a new path)", std::string(path)));
    return Result<void>::success();
#endif
}
}  // namespace eve::physics
