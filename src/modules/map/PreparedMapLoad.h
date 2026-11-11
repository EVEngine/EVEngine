#pragma once
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include "common/Export.h"
#include "common/Result.h"
#include "map/TileOrientation.h"
namespace eve::service {
class IFileSystem;
}
namespace eve::map {
class Map;
class TileLayer;
class NavigationGridSnapshot;
class MapGridState;
/** @brief Opaque weak Map lifetime observation; copying it never retains the Map or its marker. */
class MapLease {
public:
    /** @brief Construct an invalid observation. */
    MapLease() = default;
    /** @brief Whether the originating Map still owns its lifetime marker; no pointers or callbacks escape. */
    [[nodiscard]] bool isCurrent() const noexcept { return !owner_.expired(); }
    /** @brief Whether two observations name the same Map owner; neither needs to be live. */
    [[nodiscard]] bool hasSameOwner(const MapLease& other) const noexcept {
        return !owner_.owner_before(other.owner_) && !other.owner_.owner_before(owner_);
    }

private:
    friend class Map;
    friend class PreparedMapLoad;
    explicit MapLease(std::weak_ptr<const std::byte> owner) : owner_(std::move(owner)) {}
    std::weak_ptr<const std::byte> owner_;
};
/** @brief Owning grid geometry in map pixel coordinates, derived by the canonical tile projection. */
struct MapGridGeometry {
    double         originX = 0, originY = 0, stepX = 0, stepY = 0, gapX = 0, gapY = 0;
    MapOrientation orientation                                                = MapOrientation::Orthogonal;
    friend bool    operator==(const MapGridGeometry&, const MapGridGeometry&) = default;
};
/**
 * @brief Read canonical grid geometry without materializing components or capturing the grid.
 * @ownership Borrows layer only for this call; result owns its values.
 * @return Geometry or missing/invalid Config diagnostic; no mutation or callbacks.
 * @thread Map owner thread. Constant arithmetic and component lookup cost.
 */
[[nodiscard]] EVENGINE_API_WORLD Result<MapGridGeometry> describeMapGrid(TileLayer& layer);
/**
 * @brief Map-owned unpublished candidate; its ECS table is isolated from live Views and render systems.
 * @ownership Owns all candidate entities/objects and weak-observes its Map owner; no asset service pointers retained.
 * @remarks Graphics resource owners follow the map module dependency lifetime. Owner destruction revokes this
 * candidate.
 * @thread Map owner thread; capture/commit are non-reentrant. Cancellation invokes no gameplay callbacks.
 */
class EVENGINE_API_WORLD PreparedMapLoad {
public:
    /** @brief Destroy isolated candidates only, without publishing a map or registering persistent watches. */
    ~PreparedMapLoad();
    /** @brief Transfer the entire unpublished candidate; the moved-from token is empty. */
    PreparedMapLoad(PreparedMapLoad&&) noexcept;
    /** @brief Transfer the candidate, cancelling prior unpublished resources. */
    PreparedMapLoad& operator=(PreparedMapLoad&&) noexcept;
    PreparedMapLoad(const PreparedMapLoad&)            = delete;
    PreparedMapLoad& operator=(const PreparedMapLoad&) = delete;
    /** @brief Observe the originating Map without retaining its lifetime; also valid after publication. */
    [[nodiscard]] MapLease ownerLease() const noexcept;
    /** @brief Number of candidate tile layers; no implicit work. */
    [[nodiscard]] std::size_t layerCount() const noexcept;
    /** @brief Read owning grid geometry, or reject an invalid index/revoked Map owner. */
    [[nodiscard]] Result<MapGridGeometry> grid(std::size_t layer) const;
    /** @brief Capture canonical walkability from an isolated candidate, retaining no entity pointer.
     * @cost Grid-sized capture, amortized per chosen navigation layer during preparation. */
    [[nodiscard]] Result<NavigationGridSnapshot> captureNavigation(std::size_t layer) const;
    /** @brief Apply admitted runtime cells/terrain/navigation facts to this isolated structural candidate.
     * @return Success or binding/shape failure preserving the whole candidate and live Map for retry.
     * @ownership State is borrowed synchronously; restores only matching layer count/source paths. Renderer
     * resources stay owned by the structural candidate; runtime renderer/object changes remain separate participants.
     * @thread Map owner thread, outside Views; no callbacks or live publication.
     * @cost Full per-layer staging, canonical chunk rebuild and navigation metadata merging per restore attempt.
     * @throws std::bad_alloc Staging allocation failed before the all-layer no-throw exchange.
     */
    [[nodiscard]] Result<void> applyGridState(const MapGridState& state);
    /**
     * @brief Publish all candidate layer components and object metadata together, then consume this candidate.
     * @return Layer count; failure preserves the previous map and a retryable candidate.
     * @ownership Borrows target only during this call; it must match the Map that prepared this token.
     * @thread Map owner thread outside ECS Views; no gameplay callbacks during publication.
     * @cost Copies candidate component storage and admits entities, amortized once per successful load.
     */
    [[nodiscard]] Result<int> commit(Map& target);

private:
    friend class Map;
    struct Impl;
    explicit PreparedMapLoad(std::unique_ptr<Impl> impl);
    [[nodiscard]] static Result<std::unique_ptr<PreparedMapLoad>> prepare(MapLease owner, std::string_view json,
                                                                          const std::string&    sourcePath,
                                                                          service::IFileSystem& files,
                                                                          std::string_view      contract);
    std::unique_ptr<Impl>                                         impl_;
};
}  // namespace eve::map
