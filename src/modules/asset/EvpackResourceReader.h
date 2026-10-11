#pragma once
#include "common/Export.h"


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
class EVENGINE_API_FOUNDATION EvpackResourceReader {
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

    /**
     * @brief List stable assets providing an exact type/version for the selected runtime variant.
     * @param expectedType Canonical type/version such as `eve.vegetation-scene/1`.
     * @param capabilities Actual runtime capabilities used for variant selection.
     * @param maximumAssets Owning result budget; exceeding it fails without a partial list.
     * @return Sorted unique asset references, which may be empty when the type is absent.
     * @thread Worker-safe while this immutable reader is read concurrently.
     */
    [[nodiscard]] Result<std::vector<AssetRef>> listAssets(
        std::string_view expectedType, const EvpackCapabilities& capabilities,
        std::uint32_t maximumAssets = 1'000'000) const;

private:
    std::shared_ptr<const Evpack> pack_;
};

}  // namespace eve::asset
