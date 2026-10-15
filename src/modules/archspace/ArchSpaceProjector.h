#pragma once
#include "common/Export.h"

#include "archspace/ArchSpaceTypes.h"
#include "common/Result.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::archspace {

class Document;

/** @brief Semantic cell kind for grid projections (tilemap / HD2D hosts). */
enum class GridCellKind : std::uint8_t { Empty = 0, Floor, Wall, Portal, Solid };

/**
 * @brief Renderer-neutral occupancy grid rebuilt from an ArchSpace level.
 *
 * Does not depend on `map` or `hd2d`; hosts map cells onto TileLayer / TileMap3D.
 */
struct GridProjection {
    int                      width    = 0;
    int                      height   = 0;
    double                   originX  = 0.0;
    double                   originZ  = 0.0;
    double                   cellSize = 1.0;
    /** @brief Wire hint: "orthogonal" or "isometric". */
    std::string              orientation = "orthogonal";
    std::vector<GridCellKind> cells;
    /** @brief Region id per cell (empty when none). Parallel to cells. */
    std::vector<std::string> regionIds;
};

/**
 * @brief Presentation adapter contract for ArchSpace documents.
 *
 * Implementations must treat the document as read-only authority and rebuild
 * derived state on sync. Missing optional backends are expressed by not
 * registering a projector, not by silent no-ops inside gameplay code.
 *
 * @thread Owner thread only.
 * @reentrancy Must not invoke unknown callbacks while holding projector locks.
 */
class IArchSpaceProjector {
public:
    /** @brief Releases IArchSpaceProjector resources. */
    virtual ~IArchSpaceProjector() = default;

    /**
     * @brief Stable projector kind wire name.
     * @ownership Non-owning pointer to a static string literal; do not free.
     * @lifetime Valid for the process lifetime.
     */
    [[nodiscard]] virtual const char* kind() const noexcept = 0;

    /**
     * @brief Rebuild derived presentation from @p document at @p revision.
     * @param document Borrowed authority; must outlive the call.
     * @param revision Caller-owned monotonic revision token for cache skipping.
     * @return Applied on success; Rejected when inputs are invalid.
     * @cost Full rebuild of presentation state; amortize per document revision.
     */
    [[nodiscard]] virtual eve::Result<void> sync(const Document& document, std::uint64_t revision) = 0;

    /** @brief Drop derived presentation state. */
    virtual void clear() = 0;

    /**
     * @brief Pick a document node under a world-space ray when supported.
     * @return Node id on hit; NotFound when nothing is hit; Unsupported when picking is unavailable.
     */
    [[nodiscard]] virtual eve::Result<std::string> pickNode(Vec3 /*rayOrigin*/, Vec3 /*rayDir*/) const {
        return eve::Result<std::string>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Unsupported, "ArchSpace projector does not support picking", "pick", {},
            "archspace.projector"));
    }
};

/**
 * @brief Mesh bake projector for 3D / scene hosts.
 *
 * Stores the last MeshBake; hosts upload via bakeMeshArrays / gfx themselves.
 */
class EVENGINE_API_WORLD MeshSceneProjector final : public IArchSpaceProjector {
public:
    /** @brief Mesh scene projector. */
    MeshSceneProjector() = default;

    [[nodiscard]] const char* kind() const noexcept override { return "scene3d"; }

    [[nodiscard]] eve::Result<void> sync(const Document& document, std::uint64_t revision) override;

    void clear() override;

    /** @brief Last baked mesh; empty after clear or before the first successful sync. */
    [[nodiscard]] const MeshBake& mesh() const noexcept { return mesh_; }

    /** @brief Revision last successfully synced; 0 before the first sync. */
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

private:
    MeshBake      mesh_{};
    std::uint64_t revision_ = 0;
};

/**
 * @brief Rasterize one level into a grid for ortho/iso/HD2D hosts.
 *
 * Floor cells come from Zone polygons; Wall from wall segments; Portal from
 * openings and level portals. Orientation is recorded as metadata only — hosts
 * apply `map` TileLayer projection separately.
 *
 * @param document Borrowed authority.
 * @param levelId Level to rasterize.
 * @param cellSize Metres per cell; must be finite and > 0.
 * @param orientation Wire hint ("orthogonal" or "isometric").
 * @return Grid on success; InvalidArgument when level/cellSize is invalid.
 * @cost O(cells * regions + wall samples); amortize per document revision.
 */
[[nodiscard]] EVENGINE_API_WORLD eve::Result<GridProjection> bakeGridProjection(
    const Document& document, const std::string& levelId, double cellSize,
    const std::string& orientation = "orthogonal");

}  // namespace eve::archspace
