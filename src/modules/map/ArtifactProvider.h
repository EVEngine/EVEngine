#pragma once

/**
 * @file ArtifactProvider.h
 * @brief Map-owned CPU registry provider for generated grid artifacts.
 */

#include "common/ArtifactPublication.h"
#include "common/RuntimeHandle.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace eve::map {

/** @brief Owner tag for map artifact runtime handles. */
struct MapArtifactHandleTag {};
/** @brief Generation-qualified handle for a map artifact record. */
using MapArtifactHandle = eve::RuntimeHandle<MapArtifactHandleTag>;

/** @brief Queryable map record containing the copied semantic topology. */
struct MapArtifactRecord {
    eve::PersistentId          id;
    std::string                buildKey;
    MapArtifactHandle          handle;
    std::int32_t               width  = 0;
    std::int32_t               height = 0;
    std::vector<std::uint32_t> cells;
};

/**
 * @brief Real map-module provider for generated grid publication.
 *
 * The CPU registry is the backend-neutral map ownership point. A TileLayer
 * renderer can consume the same record later; semantic cells are never
 * reconstructed from rendered resources.
 */
class MapArtifactProvider final : public eve::artifact::IMapArtifactAdapter {
public:
    /** @brief Stage a topology grid and copy its cells before commit. */
    [[nodiscard]] eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>> prepare(
        const eve::artifact::PublicationView& publication) override;
    /** @brief Export map identity, build keys, handles and topology cells. */
    [[nodiscard]] eve::Result<eve::Value> snapshotState() const override;
    /** @brief Replace the registry from validated topology state. */
    [[nodiscard]] eve::Result<void> restoreState(const eve::Value& state) override;
    /** @brief Return whether the map registry has no committed records. */
    [[nodiscard]] bool emptyState() const noexcept override { return records_.empty(); }
    /** @brief Clear provider state through the common restore-cleanup hook. */
    void clearState() noexcept override { clear(); }

    /**
     * @brief Find a committed map artifact; the pointer is borrowed.
     * @ownership Borrowed from this provider; callers must not delete or retain it.
     * @nullable Null when no committed artifact has the requested identity.
     * @lifetime Until provider clear/restore/destruction or any later commit that
     *            reallocates the provider's record vector; callers must not cache it.
     * @thread Map-provider owner thread; external synchronization is required.
     * @reentrancy No callbacks are made.
     */
    [[nodiscard]] const MapArtifactRecord* find(eve::PersistentId id) const noexcept;
    /** @brief Return one semantic cell, or empty for an invalid coordinate. */
    [[nodiscard]] std::optional<std::uint32_t> cell(eve::PersistentId id, std::int32_t x,
                                                    std::int32_t y) const noexcept;
    /** @brief Return the number of committed map artifact records. */
    [[nodiscard]] std::size_t size() const noexcept { return records_.size(); }
    /** @brief Remove all records and reset the local handle allocator. */
    void clear() noexcept;
    /** @brief Inject a prepare failure for composition tests. */
    void setPrepareFailure(bool enabled) noexcept { failPrepare_ = enabled; }

private:
    friend class MapArtifactStage;
    void                           commit(MapArtifactRecord record) noexcept;
    std::vector<MapArtifactRecord> records_;
    std::uint32_t                  nextIndex_   = 0;
    bool                           failPrepare_ = false;
};

/** @brief Return the process-owned map artifact provider singleton. */
[[nodiscard]] MapArtifactProvider& mapArtifactProvider() noexcept;
/** @brief Register the map provider in the common capability registry. */
void registerMapArtifactProvider();

}  // namespace eve::map
