#include "asset/AssetPackageStore.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <limits>

#if defined(_WIN32)
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace eve::asset {
namespace {

template <class T>
Result<T> storeFailure(DiagnosticCode code, std::string message, const std::filesystem::path& path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), path.string(), {},
                                                "asset.package.store"));
}

Result<void> writeExclusiveAndFlush(const std::filesystem::path& path,
                                    std::span<const std::uint8_t> bytes) {
#if defined(_WIN32)
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, "cannot create temporary package",
                                                       path.string(), {}, "asset.package.store"));
    std::size_t cursor = 0;
    bool written = true;
    while (cursor < bytes.size()) {
        const DWORD request = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - cursor, MAXDWORD));
        DWORD count = 0;
        if (!WriteFile(file, bytes.data() + cursor, request, &count, nullptr) || count != request) {
            written = false;
            break;
        }
        cursor += count;
    }
    const bool flushed = written && FlushFileBuffers(file) != 0;
    CloseHandle(file);
    if (!flushed)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, "cannot flush temporary package",
                                                       path.string(), {}, "asset.package.store"));
#else
    const int file = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (file < 0)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, "cannot create temporary package",
                                                       path.string(), {}, "asset.package.store"));
    std::size_t cursor = 0;
    bool written = true;
    while (cursor < bytes.size()) {
        const ssize_t count = ::write(file, bytes.data() + cursor, bytes.size() - cursor);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { written = false; break; }
        cursor += static_cast<std::size_t>(count);
    }
    const bool flushed = written && ::fsync(file) == 0;
    ::close(file);
    if (!flushed)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, "cannot flush temporary package",
                                                       path.string(), {}, "asset.package.store"));
#endif
    return Result<void>::success();
}

Result<std::vector<std::uint8_t>> readBounded(const std::filesystem::path& path, std::uint64_t maximumBytes) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size > maximumBytes || size > std::numeric_limits<std::size_t>::max())
        return storeFailure<std::vector<std::uint8_t>>(DiagnosticCode::InvalidArgument,
                                                       "temporary package size is outside limits", path);
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return storeFailure<std::vector<std::uint8_t>>(DiagnosticCode::Failed,
                                                       "cannot reopen temporary package", path);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input || input.peek() != std::ifstream::traits_type::eof())
        return storeFailure<std::vector<std::uint8_t>>(DiagnosticCode::Failed,
                                                       "temporary package changed while reading", path);
    return Result<std::vector<std::uint8_t>>::success(std::move(bytes));
}

Result<void> replaceFile(const std::filesystem::path& temporary,
                         const std::filesystem::path& destination) {
#if defined(_WIN32)
    if (!MoveFileExW(temporary.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed,
                                                       "atomic package replacement failed",
                                                       destination.string(), {}, "asset.package.store"));
#else
    std::error_code ec;
    std::filesystem::rename(temporary, destination, ec);
    if (ec)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed,
                                                       "atomic package replacement failed: " + ec.message(),
                                                       destination.string(), {}, "asset.package.store"));
#endif
    return Result<void>::success();
}

std::filesystem::path temporaryPath(const std::filesystem::path& destination, std::uint64_t sequence) {
#if defined(_WIN32)
    const auto process = static_cast<std::uint64_t>(GetCurrentProcessId());
#else
    const auto process = static_cast<std::uint64_t>(::getpid());
#endif
    return destination.parent_path() /
           ("." + destination.filename().string() + ".tmp." + std::to_string(process) + "." +
            std::to_string(sequence));
}

class TemporaryCleanup {
public:
    explicit TemporaryCleanup(std::filesystem::path path) : path_(std::move(path)) {}
    ~TemporaryCleanup() {
        if (!active_) return;
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }
    void release() noexcept { active_ = false; }
private:
    std::filesystem::path path_;
    bool active_ = true;
};

}  // namespace

AtomicAssetPackageStore::AtomicAssetPackageStore(BeforeReplace beforeReplace)
    : beforeReplace_(std::move(beforeReplace)) {}

Result<PackagePublishReceipt> AtomicAssetPackageStore::publishEva(
    const std::filesystem::path& destination, const EvaManifest& manifest,
    std::vector<EvaArchiveEntry> entries, const EvaArchiveLimits& limits) {
    if (destination.empty() || destination.filename().empty())
        return storeFailure<PackagePublishReceipt>(DiagnosticCode::InvalidArgument,
                                                   "package destination is empty", destination);
    auto built = buildEvaArchive(manifest, std::move(entries), limits);
    if (!built) return Result<PackagePublishReceipt>::failure(built.status());
    std::error_code ec;
    if (!destination.parent_path().empty()) std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec) return storeFailure<PackagePublishReceipt>(DiagnosticCode::Failed, ec.message(), destination);
    const auto temporary = temporaryPath(destination, ++sequence_);
    TemporaryCleanup cleanup(temporary);
    auto written = writeExclusiveAndFlush(temporary, built.value());
    if (!written) return Result<PackagePublishReceipt>::failure(written.status());
    auto reopened = readBounded(temporary, limits.maximumArchiveBytes);
    if (!reopened) return Result<PackagePublishReceipt>::failure(reopened.status());
    auto verified = parseEvaArchive(reopened.value(), limits);
    if (!verified) return Result<PackagePublishReceipt>::failure(verified.status());
    if (beforeReplace_ && beforeReplace_(temporary, destination) == PackagePublishGateDecision::Reject)
        return storeFailure<PackagePublishReceipt>(DiagnosticCode::Cancelled,
                                                   "package replacement rejected before commit", destination);
    auto replaced = replaceFile(temporary, destination);
    if (!replaced) return Result<PackagePublishReceipt>::failure(replaced.status());
    cleanup.release();
    return Result<PackagePublishReceipt>::success(
        {destination, verified.value().manifest.packageId, std::nullopt, reopened.value().size()});
}

Result<PackagePublishReceipt> AtomicAssetPackageStore::publishEvpack(
    const std::filesystem::path& destination, std::span<const std::uint8_t> bytes,
    const EvpackLimits& limits, const EvpackTrust& trust) {
    if (destination.empty() || destination.filename().empty())
        return storeFailure<PackagePublishReceipt>(DiagnosticCode::InvalidArgument,
                                                   "package destination is empty", destination);
    auto admitted = parseEvpack(bytes, limits, trust);
    if (!admitted) return Result<PackagePublishReceipt>::failure(admitted.status());
    std::error_code ec;
    if (!destination.parent_path().empty()) std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec) return storeFailure<PackagePublishReceipt>(DiagnosticCode::Failed, ec.message(), destination);
    const auto temporary = temporaryPath(destination, ++sequence_);
    TemporaryCleanup cleanup(temporary);
    auto written = writeExclusiveAndFlush(temporary, bytes);
    if (!written) return Result<PackagePublishReceipt>::failure(written.status());
    auto reopened = readBounded(temporary, limits.maximumPackageBytes);
    if (!reopened) return Result<PackagePublishReceipt>::failure(reopened.status());
    auto verified = parseEvpack(reopened.value(), limits, trust);
    if (!verified) return Result<PackagePublishReceipt>::failure(verified.status());
    if (beforeReplace_ && beforeReplace_(temporary, destination) == PackagePublishGateDecision::Reject)
        return storeFailure<PackagePublishReceipt>(DiagnosticCode::Cancelled,
                                                   "package replacement rejected before commit", destination);
    auto replaced = replaceFile(temporary, destination);
    if (!replaced) return Result<PackagePublishReceipt>::failure(replaced.status());
    cleanup.release();
    return Result<PackagePublishReceipt>::success(
        {destination, verified.value().packageId(), verified.value().buildId(), reopened.value().size()});
}

}  // namespace eve::asset
