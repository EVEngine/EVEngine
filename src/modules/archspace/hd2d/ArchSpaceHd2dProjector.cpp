#include "archspace/hd2d/ArchSpaceHd2dProjector.h"

#include "map/TileLayer.h"

#include <utility>

namespace eve::archspace_hd2d {

void Hd2dSpaceProjector::setLevelId(std::string levelId) { tiles_.setLevelId(std::move(levelId)); }

void Hd2dSpaceProjector::setCellSize(float cellSize) { tiles_.setCellSize(cellSize); }

void Hd2dSpaceProjector::setPalette(archspace_map::ArchSpaceTilePalette palette) { tiles_.setPalette(palette); }

void Hd2dSpaceProjector::setSideDepth(float depth) { builder_.setSideDepth(depth); }

void Hd2dSpaceProjector::setHeightScale(float scale) { builder_.setHeightScale(scale); }

void Hd2dSpaceProjector::setLayer(map::TileLayer* layer) { tiles_.setLayer(layer); }

eve::Result<void> Hd2dSpaceProjector::sync(const archspace::Document& document, std::uint64_t revision) {
    // HD2D hosts use orthogonal tile footprints; isometric is a separate 2D projector.
    auto oriented = tiles_.setOrientation("orthogonal");
    if (!oriented.ok()) return oriented;
    return tiles_.sync(document, revision);
}

void Hd2dSpaceProjector::clear() {
    tiles_.clear();
    lastTileCount_ = 0;
}

eve::Result<graphics::Mesh*> Hd2dSpaceProjector::buildMesh(graphics::Graphics* gfx) {
    if (!gfx)
        return eve::Result<graphics::Mesh*>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "ArchSpace HD2D buildMesh requires Graphics",
                                   "gfx", {}, "archspace.hd2d"));
    map::TileLayer* layer = tiles_.layer();
    if (!layer)
        return eve::Result<graphics::Mesh*>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "ArchSpace HD2D buildMesh requires a synced TileLayer",
                                   "layer", {}, "archspace.hd2d"));
    graphics::Mesh* mesh = builder_.buildMesh(gfx, layer);
    lastTileCount_       = builder_.getTileCount();
    return eve::Result<graphics::Mesh*>::success(mesh, eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<graphics::Renderable3D*> Hd2dSpaceProjector::buildRenderable(graphics::Graphics* gfx) {
    if (!gfx)
        return eve::Result<graphics::Renderable3D*>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                   "ArchSpace HD2D buildRenderable requires Graphics", "gfx", {}, "archspace.hd2d"));
    map::TileLayer* layer = tiles_.layer();
    if (!layer)
        return eve::Result<graphics::Renderable3D*>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, "ArchSpace HD2D buildRenderable requires a synced TileLayer", "layer", {},
            "archspace.hd2d"));
    graphics::Renderable3D* renderable = builder_.buildRenderable(gfx, layer);
    lastTileCount_                     = builder_.getTileCount();
    return eve::Result<graphics::Renderable3D*>::success(renderable, eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace eve::archspace_hd2d
