#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "grid/GridConfig.h"
#include "grid/GridProjection.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

using namespace eve::grid;

namespace {

bool approxEq(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

void expectCellWorld(const GridConfig &cfg, int cx, int cy, float ex, float ey) {
    float wx = 0.f, wy = 0.f;
    cellToWorld(cfg, cx, cy, wx, wy);
    REQUIRE(approxEq(wx, ex));
    REQUIRE(approxEq(wy, ey));
    int rx = -999, ry = -999;
    worldToCell(cfg, wx + cfg.cellW * 0.25f, wy + cfg.cellH * 0.25f, rx, ry, 32, 32);
    REQUIRE_EQ(rx, cx);
    REQUIRE_EQ(ry, cy);
}

}  // namespace

TEST_CASE("grid.rectangle.roundtrip") {
    GridConfig cfg;
    cfg.cellW = 32.f;
    cfg.cellH = 16.f;
    cfg.originX = 10.f;
    cfg.originY = 20.f;

    expectCellWorld(cfg, 0, 0, 10.f, 20.f);
    expectCellWorld(cfg, 3, 2, 10.f + 3 * 32.f, 20.f + 2 * 16.f);

    cfg.cellGapX = 4.f;
    cfg.cellGapY = 2.f;
    expectCellWorld(cfg, 1, 1, 10.f + 36.f, 20.f + 18.f);
}

TEST_CASE("grid.isometric.roundtrip") {
    GridConfig cfg;
    cfg.layout = GridLayout::Isometric;
    cfg.cellW = 64.f;
    cfg.cellH = 32.f;
    cfg.originX = 0.f;
    cfg.originY = 0.f;

    expectCellWorld(cfg, 0, 0, 0.f, 0.f);
    expectCellWorld(cfg, 1, 0, 32.f, 16.f);
    expectCellWorld(cfg, 0, 1, -32.f, 16.f);
    expectCellWorld(cfg, 2, 3, -32.f, 80.f);
}

TEST_CASE("grid.staggered.cellToWorld") {
    GridConfig cfg;
    cfg.layout = GridLayout::Staggered;
    cfg.cellW = 32.f;
    cfg.cellH = 32.f;
    cfg.staggerAxis = StaggerAxis::Y;
    cfg.staggerIndex = StaggerIndex::Odd;

    float wx = 0.f, wy = 0.f;
    cellToWorld(cfg, 0, 0, wx, wy);
    REQUIRE(approxEq(wx, 0.f));
    REQUIRE(approxEq(wy, 0.f));

    // 奇行向右偏移半格。
    cellToWorld(cfg, 0, 1, wx, wy);
    REQUIRE(approxEq(wx, 16.f));
    REQUIRE(approxEq(wy, 16.f));

    cellToWorld(cfg, 1, 2, wx, wy);
    REQUIRE(approxEq(wx, 32.f));
    REQUIRE(approxEq(wy, 32.f));

    int cx = -1, cy = -1;
    worldToCell(cfg, 60.f, 30.f, cx, cy, 8, 8);
    REQUIRE_EQ(cx, 1);
    REQUIRE_EQ(cy, 1);
}

TEST_CASE("grid.hex.cellToWorldDepth") {
    GridConfig cfg;
    cfg.layout = GridLayout::Hexagon;
    cfg.cellW = 32.f;
    cfg.cellH = 32.f;
    cfg.hexSideLength = 14.f;
    cfg.staggerAxis = StaggerAxis::Y;
    cfg.staggerIndex = StaggerIndex::Odd;

    float wx = 0.f, wy = 0.f;
    cellToWorld(cfg, 0, 0, wx, wy);
    REQUIRE(approxEq(wx, 0.f));
    REQUIRE(approxEq(wy, 0.f));
    cellToWorld(cfg, 0, 1, wx, wy);
    REQUIRE(approxEq(wx, 16.f));
    REQUIRE(approxEq(wy, (32.f + 14.f) * 0.5f));
    REQUIRE(approxEq(cellToDepthY(cfg, 0, 1), wy + 32.f));
}

TEST_CASE("grid.footprint.cardinalRotate") {
    std::vector<uint8_t> mask;  // 实心 2x1
    std::vector<std::pair<int, int>> cells;
    foreachRotatedFootprint(2, 1, mask, 1, false, [&](int lx, int ly) {
        cells.emplace_back(lx, ly);
    });
    // 90° 后变为 1x2：{(0,0),(0,1)}
    REQUIRE_EQ(cells.size(), size_t(2));
    const bool has00 = (cells[0].first == 0 && cells[0].second == 0) ||
                       (cells[1].first == 0 && cells[1].second == 0);
    const bool has01 = (cells[0].first == 0 && cells[0].second == 1) ||
                       (cells[1].first == 0 && cells[1].second == 1);
    REQUIRE(has00);
    REQUIRE(has01);

    cells.clear();
    foreachRotatedFootprint(2, 1, mask, 4, false, [&](int lx, int ly) {
        cells.emplace_back(lx, ly);
    });
    // 360° = 恒等
    REQUIRE_EQ(cells.size(), size_t(2));
    REQUIRE_EQ(cells[0].first, 0);
    REQUIRE_EQ(cells[0].second, 0);
    REQUIRE_EQ(cells[1].first, 1);
    REQUIRE_EQ(cells[1].second, 0);
}

TEST_CASE("grid.footprint.hexRotate") {
    std::vector<uint8_t> mask;  // 实心 2x1
    std::vector<std::pair<int, int>> cells;
    foreachRotatedFootprint(2, 1, mask, 1, true, [&](int lx, int ly) {
        cells.emplace_back(lx, ly);
    });
    // 60° 旋转 2x1 在 offset 空间产生 2 个格（不再是 1x2 轴对齐）。
    REQUIRE_EQ(cells.size(), size_t(2));

    cells.clear();
    foreachRotatedFootprint(2, 1, mask, 6, true, [&](int lx, int ly) {
        cells.emplace_back(lx, ly);
    });
    REQUIRE_EQ(cells.size(), size_t(2));
    REQUIRE_EQ(cells[0].first, 0);
    REQUIRE_EQ(cells[0].second, 0);
    REQUIRE_EQ(cells[1].first, 1);
    REQUIRE_EQ(cells[1].second, 0);

    int w = 0, h = 0;
    rotatedFootprintSize(2, 1, mask, 3, true, w, h);
    const bool sizeOk = w >= 1 && h >= 1;
    REQUIRE(sizeOk);
}

TEST_CASE("grid.layoutNames") {
    const bool nameOk =
        std::strcmp(GridConfig::layoutName(GridLayout::Isometric), "isometric") == 0;
    REQUIRE(nameOk);
    REQUIRE_EQ(int(GridConfig::layoutFromName("hex")), int(GridLayout::Hexagon));
    REQUIRE_EQ(int(GridConfig::layoutFromName("bogus")), int(GridLayout::Rectangle));
    REQUIRE_EQ(int(GridConfig::planeFromName("xz")), int(GridPlane::XZ));
    const bool planeOk = std::strcmp(GridConfig::planeName(GridPlane::XY), "xy") == 0;
    REQUIRE(planeOk);
}
