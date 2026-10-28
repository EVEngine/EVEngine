#pragma once
#include "common/Export.h"

#include "archspace/map/ArchSpaceTileMapProjector.h"
#include "common/Result.h"
#include "hd2d/Hd2d.h"

#include <cstdint>
#include <string>

namespace eve::graphics {
class Graphics;
class Mesh;
class Renderable3D;
}  // namespace eve::graphics

namespace eve::archspace_hd2d {

/**
 * @brief Projects an ArchSpace level onto a TileLayer and HD-2D TileMap3D bake path.
 *
 * Sync rebuilds the shared grid → TileLayer via TileMapProjector. Mesh upload is
 * deferred: call buildMesh / buildRenderable when a Graphics host is available.
 * Unit tests can assert TileLayer GIDs without opening a GPU window.
 *
 * @ownership Owns its TileMapProjector (and thus an owned TileLayer unless setLayer
 *            was used on tiles()). TileMap3D is value-owned configuration only.
 * @thread Owner thread only.
 */
class EVENGINE_API_DOMAINS Hd2dSpaceProjector final : public archspace::IArchSpaceProjector {
public:
    Hd2dSpaceProjector()                                     = default;
    Hd2dSpaceProjector(const Hd2dSpaceProjector&)            = delete;
    Hd2dSpaceProjector& operator=(const Hd2dSpaceProjector&) = delete;
    ~Hd2dSpaceProjector() override                           = default;

    /**
     * @brief Stable projector kind.
     * @ownership Non-owning pointer to a static string literal; do not free.
     * @lifetime Valid for the process lifetime.
     */
    [[nodiscard]] const char* kind() const noexcept override { return "hd2d"; }

    /** @brief Level id to rasterize on sync (required, non-empty). */
    void setLevelId(std::string levelId);
    /** @brief Metres per cell / tile edge (must be finite and > 0). */
    void setCellSize(float cellSize);
    /** @brief GID mapping forwarded to the tile projector. */
    void setPalette(archspace_map::ArchSpaceTilePalette palette);
    /** @brief Downward side-wall depth for TileMap3D extrusion (world units). */
    void setSideDepth(float depth);
    /** @brief World units per unit of tile "height" metadata. */
    void setHeightScale(float scale);

    /**
     * @brief Borrow an existing TileLayer; projector will not delete it.
     * @ownership Non-owning; caller retains ownership and must outlive sync.
     */
    void setLayer(map::TileLayer* layer);

    [[nodiscard]] eve::Result<void> sync(const archspace::Document& document, std::uint64_t revision) override;
    void                            clear() override;

    /**
     * @brief Underlying tile projector (grid + TileLayer).
     * @ownership Borrowed from this projector; do not free.
     * @lifetime Until this projector is destroyed.
     */
    [[nodiscard]] archspace_map::TileMapProjector&       tiles() noexcept { return tiles_; }
    [[nodiscard]] const archspace_map::TileMapProjector& tiles() const noexcept { return tiles_; }

    /**
     * @brief Extrude the last synced TileLayer into a Graphics-owned mesh.
     * @param gfx Active Graphics; must outlive the returned mesh.
     * @return Mesh on success; Failed when no layer has been synced or build fails.
     * @cost Full terrain mesh bake; amortize per document revision / host upload.
     */
    [[nodiscard]] eve::Result<graphics::Mesh*> buildMesh(graphics::Graphics* gfx);

    /**
     * @brief Extrude into a scene-ready Renderable3D (ECS-registered).
     * @cost Full terrain mesh bake + renderable wrap; amortize per revision.
     */
    [[nodiscard]] eve::Result<graphics::Renderable3D*> buildRenderable(graphics::Graphics* gfx);

    /** @brief Tile count from the last successful buildMesh/buildRenderable; 0 otherwise. */
    [[nodiscard]] int lastTileCount() const noexcept { return lastTileCount_; }

    /** @brief Revision last successfully synced. */
    [[nodiscard]] std::uint64_t revision() const noexcept { return tiles_.revision(); }

private:
    archspace_map::TileMapProjector tiles_;
    hd2d::TileMap3D                 builder_{};
    int                             lastTileCount_ = 0;
};

}  // namespace eve::archspace_hd2d
