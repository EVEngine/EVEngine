#include "editor/EditorDiskAssetCatalog.h"

#include "editor/EditorValueJson.h"

#include <fstream>
#include <iomanip>
#include <iterator>
#include <set>
#include <sstream>

namespace eve::editor {
namespace {

EditorResult<DiskAssetScanResult> scanError(EditorStatus status, const char* rule, std::string message) {
    return EditorResult<DiskAssetScanResult>::error(status, RuleId(rule), std::move(message));
}

std::string textMember(const EditorValue::Object& object, const char* key) {
    const auto  found = object.find(key);
    const auto* value = found == object.end() ? nullptr : found->second.getIf<std::string>();
    return value ? *value : std::string{};
}

}  // namespace

DiskAssetCatalog::DiskAssetCatalog(std::filesystem::path projectRoot, MemoryAssetDatabase* database)
    : database_(database) {
    std::error_code ec;
    contentRoot_ = std::filesystem::weakly_canonical(std::move(projectRoot), ec) / "Content";
    if (ec) contentRoot_.clear();
}

std::filesystem::path DiskAssetCatalog::resolveContent(const std::filesystem::path& relative) const {
    if (contentRoot_.empty() || relative.empty() || relative.is_absolute()) return {};
    const auto candidate = (contentRoot_ / relative).lexically_normal();
    const auto scoped    = candidate.lexically_relative(contentRoot_);
    if (scoped.empty()) return {};
    for (const auto& part : scoped)
        if (part == "..") return {};
    return candidate;
}

EditorResult<void> DiskAssetCatalog::writeSidecar(const std::filesystem::path& contentRelativePath, AssetGuid guid,
                                                  std::string typeId, std::uint32_t schemaVersion) {
    const auto source = resolveContent(contentRelativePath);
    if (source.empty() || guid.empty() || typeId.empty())
        return EditorResult<void>::error(EditorStatus::Rejected, RuleId("editor.asset.sidecar-invalid"),
                                         "Content path, GUID and type are required");
    EditorValue::Object metadata;
    metadata["guid"]                    = EditorValue(guid.value());
    metadata["type"]                    = EditorValue(std::move(typeId));
    metadata["schemaVersion"]           = EditorValue(static_cast<std::int64_t>(schemaVersion));
    const std::filesystem::path sidecar = source.string() + ".evmeta";
    std::error_code             ec;
    std::filesystem::create_directories(sidecar.parent_path(), ec);
    if (ec)
        return EditorResult<void>::error(EditorStatus::Failed, RuleId("editor.asset.sidecar-directory"), ec.message());
    const std::filesystem::path temporary = sidecar.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output)
            return EditorResult<void>::error(EditorStatus::Failed, RuleId("editor.asset.sidecar-write"),
                                             "Cannot open asset sidecar temp file");
        output << editorValueToJson(EditorValue(std::move(metadata)));
    }
    std::filesystem::rename(temporary, sidecar, ec);
    if (ec) {
        std::filesystem::remove(sidecar, ec);
        ec.clear();
        std::filesystem::rename(temporary, sidecar, ec);
    }
    if (ec)
        return EditorResult<void>::error(EditorStatus::Failed, RuleId("editor.asset.sidecar-replace"), ec.message());
    return EditorResult<void>::applied();
}

EditorResult<DiskAssetScanResult> DiskAssetCatalog::scan() {
    if (!database_ || contentRoot_.empty())
        return scanError(EditorStatus::Rejected, "editor.asset.catalog-invalid",
                         "Asset database and Content root are required");
    DiskAssetScanResult result;
    std::error_code     ec;
    if (!std::filesystem::exists(contentRoot_)) return EditorResult<DiskAssetScanResult>::applied(std::move(result));
    std::set<AssetGuid> discovered;
    for (std::filesystem::recursive_directory_iterator iterator(contentRoot_, ec), end; iterator != end && !ec;
         iterator.increment(ec)) {
        if (!iterator->is_regular_file() || iterator->path().extension() != ".evmeta") continue;
        std::ifstream     input(iterator->path(), std::ios::binary);
        const std::string json{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        auto              parsed   = editorValueFromJson(json);
        const auto*       metadata = parsed.value ? parsed.value->getIf<EditorValue::Object>() : nullptr;
        const AssetGuid   guid(metadata ? textMember(*metadata, "guid") : std::string{});
        const std::string type = metadata ? textMember(*metadata, "type") : std::string{};
        if (!parsed.isAccepted() || !metadata || guid.empty() || type.empty()) {
            result.diagnostics.push_back({RuleId("editor.asset.sidecar-invalid"), DiagnosticSeverity::Error,
                                          "Invalid asset sidecar: " + iterator->path().generic_string()});
            continue;
        }
        if (!discovered.emplace(guid).second) {
            result.diagnostics.push_back({RuleId("editor.asset.duplicate-guid"), DiagnosticSeverity::Error,
                                          "Duplicate asset GUID: " + guid.value()});
            continue;
        }
        std::filesystem::path source = iterator->path();
        source.replace_extension();
        if (!std::filesystem::is_regular_file(source)) {
            result.diagnostics.push_back({RuleId("editor.asset.source-missing"), DiagnosticSeverity::Error,
                                          "Asset sidecar source is missing: " + source.generic_string()});
            continue;
        }
        const auto        relative    = source.lexically_relative(contentRoot_);
        const std::string hash        = fileHash(source);
        const std::string fingerprint = relative.generic_string() + ":" + hash + ":" + json;
        AssetRecord       record;
        record.guid          = guid;
        record.logicalUri    = "content://" + relative.generic_string();
        record.typeId        = type;
        record.sourceUri     = source.generic_string();
        record.sourceHash    = hash;
        record.schemaVersion = 1;
        auto published       = database_->publish(std::move(record));
        if (!published.isAccepted()) {
            result.diagnostics.insert(result.diagnostics.end(), published.diagnostics.begin(),
                                      published.diagnostics.end());
            continue;
        }
        ++result.indexed;
        const auto previous = fingerprints_.find(guid);
        if (previous == fingerprints_.end() || previous->second != fingerprint) ++result.changed;
        fingerprints_.insert_or_assign(guid, fingerprint);
    }
    if (ec) return scanError(EditorStatus::Failed, "editor.asset.scan-failed", ec.message());
    EditorResult<DiskAssetScanResult> completed = EditorResult<DiskAssetScanResult>::applied(std::move(result));
    completed.diagnostics                       = completed.value->diagnostics;
    return completed;
}

EditorResult<DiskAssetScanResult> DiskAssetCatalog::poll() { return scan(); }

std::string DiskAssetCatalog::fileHash(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::uint64_t hash = 1469598103934665603ULL;
    char          buffer[8192];
    while (input) {
        input.read(buffer, sizeof(buffer));
        for (std::streamsize index = 0; index < input.gcount(); ++index) {
            hash ^= static_cast<unsigned char>(buffer[index]);
            hash *= 1099511628211ULL;
        }
    }
    std::ostringstream text;
    text << std::hex << std::setfill('0') << std::setw(16) << hash;
    return text.str();
}

}  // namespace eve::editor
