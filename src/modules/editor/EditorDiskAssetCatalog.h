#pragma once

#include "editor/EditorAssetDatabase.h"

#include <filesystem>
#include <map>

namespace eve::editor {

/** @brief Result of one disk asset sidecar scan/poll. */
struct DiskAssetScanResult {
    std::size_t                   indexed = 0;
    std::size_t                   changed = 0;
    std::vector<EditorDiagnostic> diagnostics;
};

/**
 * @brief Project Content scanner using stable `.evmeta` GUID sidecars.
 *
 * A sidecar named `Tree.png.evmeta` describes `Tree.png`. Moving both files
 * changes the logical URI while retaining identity. poll() is deliberately
 * host-driven so desktop editors, games and MCP hosts can choose their own
 * watcher/event loop without changing indexing semantics.
 */
class DiskAssetCatalog {
public:
    /**
     * @brief Create a host-driven disk asset catalog.
     * @param projectRoot Project directory containing Content/.
     * @param database Non-owning destination index; it must outlive the catalog.
     */
    DiskAssetCatalog(std::filesystem::path projectRoot, MemoryAssetDatabase* database);
    /** @brief Create/replace a sidecar for a Content-relative source file. */
    EditorResult<void> writeSidecar(const std::filesystem::path& contentRelativePath, AssetGuid guid,
                                    std::string typeId, std::uint32_t schemaVersion = 1);
    /** @brief Scan every sidecar and atomically publish valid records. */
    EditorResult<DiskAssetScanResult> scan();
    /** @brief Rescan and report only content/sidecar fingerprints that changed. */
    EditorResult<DiskAssetScanResult> poll();

private:
    std::filesystem::path resolveContent(const std::filesystem::path& relative) const;
    static std::string    fileHash(const std::filesystem::path& path);

    std::filesystem::path            contentRoot_;
    MemoryAssetDatabase*             database_ = nullptr;
    std::map<AssetGuid, std::string> fingerprints_;
};

}  // namespace eve::editor
