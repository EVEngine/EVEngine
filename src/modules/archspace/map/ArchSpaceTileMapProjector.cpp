#include "archspace/map/ArchSpaceTileMapProjector.h"

#include "archspace/ArchSpaceDocument.h"
#include "map/TileLayer.h"

#include <cmath>
#include <utility>

namespace eve::archspace_map {
namespace {

eve::Result<void> ok() { return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied)); }

int gidFor(const ArchSpaceTilePalette& palette, archspace::GridCellKind kind) {
    switch (kind) {
        case archspace::GridCellKind::Floor: return palette.floorGid;
        case archspace::GridCellKind::Wall: return palette.wallGid;
        case archspace::GridCellKind::Portal: return palette.portalGid;
        case archspace::GridCellKind::Solid: return palette.solidGid;
        case archspace::GridCellKind::Empty: return 0;
    }
    return 0;
}

}  // namespace

TileMapProjector::~TileMapProjector() { clear(); }

const char* TileMapProjector::kind() const noexcept { return orientation_ == "isometric" ? "tile.iso" : "tile.ortho"; }

void TileMapProjector::setLevelId(std::string levelId) { levelId_ = std::move(levelId); }

void TileMapProjector::setCellSize(float cellSize) { cellSize_ = cellSize; }

eve::Result<void> TileMapProjector::setOrientation(std::string orientation) {
    if (orientation != "orthogonal" && orientation != "isometric")
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "ArchSpace tile orientation must be orthogonal or isometric",
            "orientation", {}, "archspace.map"));
    orientation_ = std::move(orientation);
    return ok();
}

void TileMapProjector::setPalette(ArchSpaceTilePalette palette) { palette_ = palette; }

void TileMapProjector::setLayer(map::TileLayer* layer) {
    if (ownsLayer_ && layer_ && layer_ != layer) {
        layer_->release();
        layer_ = nullptr;
    }
    layer_     = layer;
    ownsLayer_ = false;
}

eve::Result<void> TileMapProjector::ensureLayer(int width, int height, float tileSize) {
    if (width <= 0 || height <= 0)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "ArchSpace tile layer size must be positive", "size",
                                                                 {}, "archspace.map"));
    if (!layer_) {
        layer_     = map::TileLayer::createLayer(width, height, tileSize, tileSize);
        ownsLayer_ = true;
        if (!layer_)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Failed, "ArchSpace failed to allocate TileLayer", "layer", {}, "archspace.map"));
    } else {
        layer_->resize(width, height);
        layer_->setTileSize(tileSize, tileSize);
    }
    const std::string config = std::string("{\"orientation\":\"") + orientation_ + "\"}";
    if (!layer_->applyConfig(config))
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Failed,
                                                                 "ArchSpace failed to apply TileLayer orientation",
                                                                 "orientation", {}, "archspace.map"));
    return ok();
}

eve::Result<void> TileMapProjector::sync(const archspace::Document& document, std::uint64_t revision) {
    if (levelId_.empty())
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "ArchSpace TileMapProjector requires levelId",
                                                                 "levelId", {}, "archspace.map"));
    if (!std::isfinite(cellSize_) || cellSize_ <= 0.f)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "ArchSpace TileMapProjector cellSize must be finite and > 0",
            "cellSize", {}, "archspace.map"));

    auto baked = archspace::bakeGridProjection(document, levelId_, static_cast<double>(cellSize_), orientation_);
    if (!baked.ok()) return eve::Result<void>::failure(baked.status());
    grid_ = std::move(baked).takeValue();

    if (grid_.width == 0 || grid_.height == 0) {
        if (layer_) layer_->clear();
        revision_ = revision;
        return ok();
    }

    auto prepared = ensureLayer(grid_.width, grid_.height, cellSize_);
    if (!prepared.ok()) return prepared;

    layer_->setOrigin(static_cast<float>(grid_.originX), static_cast<float>(grid_.originZ));
    layer_->clear();
    layer_->setTileNavigationProfile(palette_.floorGid, true, 1.f, 15, 15, false);
    layer_->setTileNavigationProfile(palette_.portalGid, true, 1.f, 15, 15, false);
    layer_->setTileNavigationProfile(palette_.wallGid, false, 0.f, 0, 0, true);
    layer_->setTileNavigationProfile(palette_.solidGid, false, 0.f, 0, 0, true);

    for (int z = 0; z < grid_.height; ++z) {
        for (int x = 0; x < grid_.width; ++x) {
            const std::size_t idx = static_cast<std::size_t>(z * grid_.width + x);
            const int         gid = gidFor(palette_, grid_.cells[idx]);
            if (gid != 0) layer_->setTile(x, z, gid);
        }
    }
    revision_ = revision;
    return ok();
}

void TileMapProjector::clear() {
    if (ownsLayer_ && layer_) {
        layer_->release();
        layer_     = nullptr;
        ownsLayer_ = false;
    } else {
        layer_ = nullptr;
    }
    grid_     = {};
    revision_ = 0;
}

}  // namespace eve::archspace_map
