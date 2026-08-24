#include "map/TileProjection.h"

#include "grid/GridConfig.h"
#include "grid/GridProjection.h"

namespace eve::map {
namespace {

grid::GridConfig toGridConfig(const TileLayer::Config &cfg) {
    grid::GridConfig g;
    switch (cfg.orientation) {
        case MapOrientation::Orthogonal:
            g.layout = grid::GridLayout::Rectangle;
            break;
        case MapOrientation::Isometric:
            g.layout = grid::GridLayout::Isometric;
            break;
        case MapOrientation::Staggered:
            g.layout = grid::GridLayout::Staggered;
            break;
        case MapOrientation::Hexagonal:
            g.layout = grid::GridLayout::Hexagon;
            break;
    }
    g.cellW = cfg.tileW;
    g.cellH = cfg.tileH;
    g.cellGapX = cfg.cellGapX;
    g.cellGapY = cfg.cellGapY;
    g.originX = cfg.originX;
    g.originY = cfg.originY;
    g.staggerAxis = cfg.staggerAxis == StaggerAxis::Y ? grid::StaggerAxis::Y
                                                      : grid::StaggerAxis::X;
    g.staggerIndex = cfg.staggerIndex == StaggerIndex::Odd ? grid::StaggerIndex::Odd
                                                           : grid::StaggerIndex::Even;
    g.hexSideLength = cfg.hexSideLength;
    return g;
}

}  // namespace

void tileToWorld(const TileLayer::Config &cfg, int tx, int ty, float &wx, float &wy) {
    grid::cellToWorld(toGridConfig(cfg), tx, ty, wx, wy);
}

float tileToDepthY(const TileLayer::Config &cfg, int tx, int ty) {
    return grid::cellToDepthY(toGridConfig(cfg), tx, ty);
}

void worldToTile(const TileLayer::Config &cfg, float wx, float wy, int &tx, int &ty) {
    grid::worldToCell(toGridConfig(cfg), wx, wy, tx, ty, cfg.mapW, cfg.mapH);
}

}  // namespace eve::map
