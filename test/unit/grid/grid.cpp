#include "grid/GridConfig.h"
#include "grid/GridProjection.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <cstring>
#include <tuple>
#include <utility>
#include <vector>

using namespace eve::grid;

namespace {
bool near(float actual, float expected) { return std::fabs(actual - expected) < 1e-4f; }

void expectWorld(const GridConfig& cfg, int col, int row, float expectedX, float expectedY) {
    float x = -999.f, y = -999.f;
    cellToWorld(cfg, col, row, x, y);
    REQUIRE(near(x, expectedX));
    REQUIRE(near(y, expectedY));
    REQUIRE(near(cellToDepthY(cfg, col, row), expectedY + cfg.cellH));
}

std::vector<std::pair<int, int>> rotated(int width, int height, const std::vector<uint8_t>& mask, int steps, bool hex) {
    std::vector<std::pair<int, int>> cells;
    foreachRotatedFootprint(width, height, mask, steps, hex, [&](int x, int y) { cells.emplace_back(x, y); });
    return cells;
}
}  // namespace

TEST_CASE("grid.spec.coordinateProjection") {
    SUB_CASE("rectangle: pitch, origin, and floor selection") {
        GridConfig cfg;
        cfg.cellW    = 32.f;
        cfg.cellH    = 16.f;
        cfg.cellGapX = 4.f;
        cfg.cellGapY = 2.f;
        cfg.originX  = 10.f;
        cfg.originY  = 20.f;
        expectWorld(cfg, 0, 0, 10.f, 20.f);
        expectWorld(cfg, 3, 2, 118.f, 56.f);
        int col = 99, row = 99;
        for (const auto& [x, y, expectedCol, expectedRow] :
             {std::tuple{10.f, 20.f, 0, 0}, std::tuple{45.9f, 37.9f, 0, 0}, std::tuple{46.f, 38.f, 1, 1},
              std::tuple{9.9f, 19.9f, -1, -1}}) {
            worldToCell(cfg, x, y, col, row, 8, 8);
            REQUIRE(col == expectedCol);
            REQUIRE(row == expectedRow);
        }
    };

    SUB_CASE("isometric and Z-as-Y: shared projection and interior inverse") {
        GridConfig cfg;
        cfg.cellW   = 64.f;
        cfg.cellH   = 32.f;
        cfg.originX = 10.f;
        cfg.originY = 20.f;
        for (auto layout : {GridLayout::Isometric, GridLayout::IsometricZAsY}) {
            cfg.layout = layout;
            expectWorld(cfg, 0, 0, 10.f, 20.f);
            expectWorld(cfg, 1, 0, 42.f, 36.f);
            expectWorld(cfg, 0, 1, -22.f, 36.f);
            expectWorld(cfg, 2, 3, -22.f, 100.f);
            int col = -1, row = -1;
            worldToCell(cfg, 10.f + 8.f, 20.f + 8.f, col, row, 8, 8);
            REQUIRE(col == 0);
            REQUIRE(row == 0);
        }
    };

    SUB_CASE("staggered: axis, parity, and nearest center") {
        GridConfig cfg;
        cfg.layout = GridLayout::Staggered;
        cfg.cellW = cfg.cellH = 32.f;
        cfg.staggerAxis       = StaggerAxis::Y;
        cfg.staggerIndex      = StaggerIndex::Odd;
        expectWorld(cfg, 0, 0, 0.f, 0.f);
        expectWorld(cfg, 0, 1, 16.f, 16.f);
        expectWorld(cfg, 1, 2, 32.f, 32.f);
        int col = -1, row = -1;
        worldToCell(cfg, 32.f, 32.f, col, row, 8, 8);
        REQUIRE(col == 0);
        REQUIRE(row == 1);
        cfg.staggerIndex = StaggerIndex::Even;
        expectWorld(cfg, 0, 0, 16.f, 0.f);
        expectWorld(cfg, 0, 1, 0.f, 16.f);
        cfg.staggerAxis  = StaggerAxis::X;
        cfg.staggerIndex = StaggerIndex::Odd;
        expectWorld(cfg, 0, 0, 0.f, 0.f);
        expectWorld(cfg, 1, 0, 16.f, 16.f);
    };

    SUB_CASE("hexagon: side length changes the major-axis pitch") {
        GridConfig cfg;
        cfg.layout = GridLayout::Hexagon;
        cfg.cellW = cfg.cellH = 32.f;
        cfg.hexSideLength     = 14.f;
        cfg.staggerAxis       = StaggerAxis::Y;
        cfg.staggerIndex      = StaggerIndex::Odd;
        expectWorld(cfg, 0, 0, 0.f, 0.f);
        expectWorld(cfg, 0, 1, 16.f, 23.f);
        cfg.staggerAxis = StaggerAxis::X;
        expectWorld(cfg, 1, 0, 23.f, 16.f);
    };
}

TEST_CASE("grid.spec.footprintRotation") {
    SUB_CASE("cardinal: sparse mask, normalized coordinates, and four-step period") {
        const std::vector<uint8_t> mask{1, 0, 1, 1};
        REQUIRE(rotated(2, 2, mask, 0, false) == (std::vector<std::pair<int, int>>{{0, 0}, {0, 1}, {1, 1}}));
        REQUIRE(rotated(2, 2, mask, 1, false) == (std::vector<std::pair<int, int>>{{0, 1}, {1, 1}, {1, 0}}));
        REQUIRE(rotated(2, 2, mask, 4, false) == rotated(2, 2, mask, 0, false));
        REQUIRE(rotated(2, 2, mask, -3, false) == rotated(2, 2, mask, 1, false));
        int width = -1, height = -1;
        rotatedFootprintSize(2, 2, mask, 1, false, width, height);
        REQUIRE(width == 2);
        REQUIRE(height == 2);
        REQUIRE(rotated(2, 1, {}, 1, false) == (std::vector<std::pair<int, int>>{{0, 1}, {0, 0}}));
    };

    SUB_CASE("hexagon: six-step period and empty mask") {
        const auto zero = rotated(2, 1, {}, 0, true);
        REQUIRE(zero == (std::vector<std::pair<int, int>>{{0, 0}, {1, 0}}));
        REQUIRE(rotated(2, 1, {}, 6, true) == zero);
        REQUIRE(rotated(2, 1, {}, -6, true) == zero);
        REQUIRE(rotated(2, 1, {}, 1, true).size() == 2);
        REQUIRE(rotated(2, 1, {0, 0}, 1, true).empty());
        int width = -1, height = -1;
        rotatedFootprintSize(2, 1, {0, 0}, 1, true, width, height);
        REQUIRE(width == 0);
        REQUIRE(height == 0);
    };
}

TEST_CASE("grid.spec.layoutAndPlaneNameContracts") {
    for (const auto& [name, layout] :
         {std::pair{"rectangle", GridLayout::Rectangle}, std::pair{"isometric", GridLayout::Isometric},
          std::pair{"hexagon", GridLayout::Hexagon}, std::pair{"staggered", GridLayout::Staggered},
          std::pair{"isometric-z-as-y", GridLayout::IsometricZAsY}}) {
        REQUIRE(GridConfig::layoutFromName(name) == layout);
        REQUIRE(std::strcmp(GridConfig::layoutName(layout), name) == 0);
    }
    REQUIRE(GridConfig::layoutFromName("iso") == GridLayout::Isometric);
    REQUIRE(GridConfig::layoutFromName("hex") == GridLayout::Hexagon);
    REQUIRE(GridConfig::layoutFromName("unknown") == GridLayout::Rectangle);
    REQUIRE(GridConfig::planeFromName("xz") == GridPlane::XZ);
    REQUIRE(GridConfig::planeFromName("unknown") == GridPlane::XY);
    REQUIRE(std::strcmp(GridConfig::planeName(GridPlane::XY), "xy") == 0);
}
