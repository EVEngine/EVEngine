#include "filesystem/PreparedFile.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/AsyncWork.h"
#include "common/Capability.h"
#include "common/Exception.h"
#include "common/Resource.h"
#include "common/SquirrelBinding.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"

namespace eve::filesystem {
namespace {
constexpr size_t           maximumBytes = 1024ull * 1024 * 1024;
constexpr std::string_view suffix       = "?filedata=1";
class FileSnapshot         final : public Resource {
public:
    std::shared_ptr<const FileData> bytes;
    FileSnapshot(std::string key, std::shared_ptr<const FileData> data)
        : Resource(std::move(key)), bytes(std::move(data)) {}
    void adopt(Resource& replacement) override { bytes.swap(static_cast<FileSnapshot&>(replacement).bytes); }
};
class Loader final : public caps::IAssetReloader {
public:
    const char*  reloadKind() const override { return "filedata"; }
    bool         handlesPath(const std::string& key) const override { return key.ends_with(suffix); }
    Result<bool> reload(const std::string&) override { return Result<bool>::success(false); }
    Resource*    load(const std::string& key) override {
        auto* fs = ModuleManager::getInstance<Filesystem>("Filesystem");
        if (!fs) throw Exception("Initialize Filesystem before reading prepared files");
        const auto       path = ResourceManager::pathOfKey(key);
        Filesystem::Info info{};
        if (!fs->getInfo(path, info) || info.size <= 0 || uint64_t(info.size) > maximumBytes)
            throw Exception("Prepared file missing or exceeds 1 GiB: %s", path.c_str());
        std::shared_ptr<const FileData> bytes(fs->read(path));
        if (!bytes || bytes->getSize() == 0 || bytes->getSize() > maximumBytes)
            throw Exception("Prepared file read failed: %s", path.c_str());
        return new FileSnapshot(key, std::move(bytes));
    }
};
Loader loader;
struct Register {
    Register() { cap::addListener<caps::IAssetReloader>(&loader, caps::IAssetReloader::kCache - 1); }
} registration;
std::string keyFor(const std::string& path) {
    if (path.empty() || path.find('?') != std::string::npos) throw Exception("Invalid prepared file path");
    return ResourceManager::makeKey(path, "filedata=1");
}
}  // namespace
Result<void> requestPreparedFile(const std::string& path) {
    if (!cap::query<caps::IAsyncWorkExecutor>() || !ModuleManager::getInstance<Filesystem>("Filesystem"))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported,
                                                       "File preparation requires Filesystem and a worker executor"));
    try {
        return ResourceManager::getInstance().request(keyFor(path));
    } catch (const std::exception& e) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, e.what()));
    }
}
Result<std::shared_ptr<const FileData>> readPreparedFile(const std::string& path, size_t limit) {
    using R = Result<std::shared_ptr<const FileData>>;
    try {
        if (!limit || limit > maximumBytes)
            return R::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid prepared file size limit"));
        auto result = ResourceManager::getInstance().waitFor(keyFor(path));
        if (!result) return R::failure(result.status());
        auto bytes = static_cast<FileSnapshot&>(result.value().get()).bytes;
        if (bytes->getSize() > limit)
            return R::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "Prepared file exceeds caller size limit", path));
        return R::success(std::move(bytes));
    } catch (const std::exception& e) {
        return R::failure(Diagnostic::error(DiagnosticCode::Failed, e.what(), path));
    }
}
void exposePreparedFileBindings(ssq::Class& cls) {
    cls.addFunc("requestPreparedFile", [vm = cls.getHandle()](Filesystem*, const std::string& path) {
        return script::projectResult(vm, requestPreparedFile(path));
    });
}
}  // namespace eve::filesystem
