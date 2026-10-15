#pragma once
#include "common/Export.h"

#include "archspace/ArchSpaceTypes.h"

#include <optional>
#include <string>
#include <vector>

namespace eve::archspace {

class Document;

/**
 * @brief Presentation-agnostic query facade over an ArchSpace document.
 *
 * @ownership Borrows the document; does not own or mutate it.
 * @lifetime Valid while the borrowed document remains alive and is not mutated.
 * @thread Owner thread only.
 * @cost Building a SpaceQuery is O(nodes). Individual queries are O(regions) or
 *       BFS over the portal graph unless noted.
 */
class EVENGINE_API_WORLD SpaceQuery {
public:
    /**
     * @brief Bind to an existing document.
     * @ownership Non-owning borrow of @p document; caller retains ownership.
     * @lifetime Must outlive every SpaceQuery call on this instance.
     */
    explicit SpaceQuery(const Document& document) noexcept;

    /** @brief Document topology dialect. */
    [[nodiscard]] TopologyKind topology() const noexcept;

    /** @brief Level (stratum) ids in document iteration order. */
    [[nodiscard]] std::vector<std::string> strata() const;

    /** @brief All Zone regions as SpaceRegion snapshots. */
    [[nodiscard]] std::vector<SpaceRegion> regions() const;

    /**
     * @brief First region whose polygon contains the XZ point on the given level.
     * @return Region snapshot, or nullopt when none match.
     * @cost O(regions on level) point-in-polygon tests.
     */
    [[nodiscard]] std::optional<SpaceRegion> regionAt(Vec2 xz, const std::string& levelId) const;

    /** @brief Wall openings and level portals as SpacePortal snapshots. */
    [[nodiscard]] std::vector<SpacePortal> portals() const;

    /** @brief Portals that touch the given region id. */
    [[nodiscard]] std::vector<SpacePortal> portalsFrom(const std::string& regionId) const;

    /**
     * @brief Whether two regions share a portal path (undirected, same connected component).
     * @cost BFS over portal adjacency.
     */
    [[nodiscard]] bool regionsConnected(const std::string& regionAId, const std::string& regionBId) const;

    /** @brief Item anchors whose position lies inside the region polygon (same level). */
    [[nodiscard]] std::vector<SpaceAnchor> anchorsIn(const std::string& regionId) const;

private:
    const Document* document_ = nullptr;
};

}  // namespace eve::archspace
