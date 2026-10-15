#pragma once
#include "common/Export.h"

#include "archspace/ArchSpaceProjector.h"
#include "common/Result.h"

#include <cstdint>
#include <string>

namespace eve::map {
class TileLayer;
}

namespace eve::archspace_map {

/** @brief GID palette written into a TileLayer by TileMapProjector. */
struct ArchSpaceTilePalette {
    int floorGid  = 1;
    int wallGid   = 2;
    int portalGid = 3;
    int solidGid  = 4;
};

/**
 * @brief Projects an ArchSpace level onto a map::TileLayer (orthogonal or isometric).
 *
 * Builds GridProjection via bakeGridProjection, then writes GIDs and navigation
 * metadata. Orientation is applied through TileLayer::applyConfig.
 *
 * @ownership Optionally owns a TileLayer created on first sync (released via
 *            Entity::release); borrowed layers set via setLayer are never released.
 * @thread Owner thread only.
 */
class EVENGINE_API_DOMAINS TileMapProjector final : public archspace::IArchSpaceProjector {
public:
    TileMapProjector()                                   = default;
    TileMapProjector(const TileMapProjector&)            = delete;
    TileMapProjector& operator=(const TileMapProjector&) = delete;
    ~TileMapProjector() override;

    /**
     * @brief Stable projector kind.
     * @ownership Non-owning pointer to a static string literal; do not free.
     * @lifetime Valid for the process lifetime.
     */
    [[nodiscard]] const char* kind() const noexcept override;

    /** @brief Level id to rasterize on sync (required, non-empty). */
    void setLevelId(std::string levelId);
    /** @brief Metres per cell / tile edge (must be finite and > 0). */
    void setCellSize(float cellSize);
    /** @brief "orthogonal" or "isometric". */
    [[nodiscard]] eve::Result<void> setOrientation(std::string orientation);
    /** @brief GID mapping for floor/wall/portal/solid. */
    void setPalette(ArchSpaceTilePalette palette);

    /**
     * @brief Borrow an existing layer; projector will not delete it.
     * @ownership Non-owning; caller retains ownership and must outlive sync.
     * @lifetime Until setLayer/clear or projector destruction.
     */
    void setLayer(map::TileLayer* layer);

    [[nodiscard]] eve::Result<void> sync(const archspace::Document& document, std::uint64_t revision) override;
    void                            clear() override;

    /**
     * @brief Last synced TileLayer (owned or borrowed).
     * @ownership Non-owning borrow; do not free unless caller owns it.
     * @lifetime Until clear/setLayer or projector destruction.
     */
    [[nodiscard]] map::TileLayer* layer() const noexcept { return layer_; }

    /** @brief Last baked grid; empty before the first successful sync. */
    [[nodiscard]] const archspace::GridProjection& grid() const noexcept { return grid_; }

    /** @brief Revision last successfully synced. */
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

    /** @brief Current orientation wire name. */
    [[nodiscard]] const std::string& orientation() const noexcept { return orientation_; }

private:
    [[nodiscard]] eve::Result<void> ensureLayer(int width, int height, float tileSize);

    std::string               levelId_;
    float                     cellSize_    = 1.f;
    std::string               orientation_ = "orthogonal";
    ArchSpaceTilePalette      palette_{};
    map::TileLayer*           layer_     = nullptr;
    bool                      ownsLayer_ = false;
    archspace::GridProjection grid_{};
    std::uint64_t             revision_ = 0;
};

}  // namespace eve::archspace_map
