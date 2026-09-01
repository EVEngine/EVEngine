#pragma once

/** @file EvpackResourceReader.h @brief Typed capability-aware runtime access to admitted packs. */

#include "asset/Evpack.h"
#include "common/ResourceRef.h"

namespace eve::asset {

/** @brief One decoded runtime chunk with its admitted semantic metadata. */
struct RuntimeAssetChunk {
    EvpackChunkKind           kind = EvpackChunkKind::Definition;
    std::uint32_t             chunkId = 0;
    std::vector<std::uint8_t> bytes;
};

/** @brief Owning backend-neutral payload returned to a domain decoder. */
struct RuntimeAssetPayload {
    AssetRef                       asset;
    std::string                    type;
    SchemaVersion                  schemaVersion;
    EvpackVariantSelection         variant;
    std::vector<RuntimeAssetChunk> chunks;
};

/** @brief Immutable reader retaining an admitted pack by shared ownership. */
class EvpackResourceReader {
public:
    /** @brief Bind an admitted package; null is rejected by `read`. */
    explicit EvpackResourceReader(std::shared_ptr<const Evpack> pack) : pack_(std::move(pack)) {}

    /**
     * @brief Resolve, type-check, variant-select and decode a canonical runtime asset.
     * @param asset Stable source identity.
     * @param expectedType Canonical type/version such as `eve.mesh/1`.
     * @param capabilities Actual runtime capabilities.
     * @param maximumDecodedBytes Aggregate owning-output budget.
     */
    [[nodiscard]] Result<RuntimeAssetPayload> read(
        const AssetRef& asset, std::string_view expectedType,
        const EvpackCapabilities& capabilities, std::uint64_t maximumDecodedBytes) const;

private:
    std::shared_ptr<const Evpack> pack_;
};

}  // namespace eve::asset
