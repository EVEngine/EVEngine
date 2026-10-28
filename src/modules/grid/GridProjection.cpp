#include "grid/GridProjection.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace eve::grid {
namespace {

bool staggerDoShift(const GridConfig &cfg, int cx, int cy) {
    const int major = (cfg.staggerAxis == StaggerAxis::Y) ? cy : cx;
    const bool odd = (major & 1) != 0;
    return cfg.staggerIndex == StaggerIndex::Odd ? odd : !odd;
}

// odd-r offset（staggerAxis=Y, odd）pointy-top hex <-> cube。
void offsetToCube(int col, int row, int &x, int &y, int &z) {
    x = col - (row - (row & 1)) / 2;
    y = row;
    z = -x - y;
}

void cubeToOffset(int x, int y, int &col, int &row) {
    row = y;
    col = x + (row - (row & 1)) / 2;
}

}  // namespace

void cellToWorld(const GridConfig &cfg, int cx, int cy, float &px, float &py) {
    const float tw = cellPitchX(cfg);
    const float th = cellPitchY(cfg);
    switch (cfg.layout) {
        case GridLayout::Rectangle:
            px = cfg.originX + float(cx) * tw;
            py = cfg.originY + float(cy) * th;
            break;
        case GridLayout::Isometric:
        case GridLayout::IsometricZAsY:
            // 菱形投影（IsometricZAsY 在 v1 与 Isometric 同构，Z-as-Y 由平面轴承担）。
            px = cfg.originX + float(cx - cy) * tw * 0.5f;
            py = cfg.originY + float(cx + cy) * th * 0.5f;
            break;
        case GridLayout::Staggered:
        case GridLayout::Hexagon: {
            const bool hex = cfg.layout == GridLayout::Hexagon && cfg.hexSideLength > 0.f;
            if (cfg.staggerAxis == StaggerAxis::Y) {
                const float pitchY = hex ? (th + cfg.hexSideLength) * 0.5f : th * 0.5f;
                px = cfg.originX + float(cx) * tw +
                     (staggerDoShift(cfg, cx, cy) ? tw * 0.5f : 0.f);
                py = cfg.originY + float(cy) * pitchY;
            } else {
                const float pitchX = hex ? (tw + cfg.hexSideLength) * 0.5f : tw * 0.5f;
                px = cfg.originX + float(cx) * pitchX;
                py = cfg.originY + float(cy) * th +
                     (staggerDoShift(cfg, cx, cy) ? th * 0.5f : 0.f);
            }
            break;
        }
    }
}

float cellToDepthY(const GridConfig &cfg, int cx, int cy) {
    float px = 0.f, py = 0.f;
    cellToWorld(cfg, cx, cy, px, py);
    return py + cfg.cellH;
}

void worldToCell(const GridConfig &cfg, float px, float py, int &cx, int &cy, int mapW,
                 int mapH) {
    switch (cfg.layout) {
        case GridLayout::Rectangle: {
            const float lx = px - cfg.originX;
            const float ly = py - cfg.originY;
            const float tw = cfg.cellW > 0.f ? cellPitchX(cfg) : 1.f;
            const float th = cfg.cellH > 0.f ? cellPitchY(cfg) : 1.f;
            cx = int(std::floor(lx / tw));
            cy = int(std::floor(ly / th));
            break;
        }
        case GridLayout::Isometric:
        case GridLayout::IsometricZAsY: {
            const float lx = px - cfg.originX;
            const float ly = py - cfg.originY;
            const float tw = cfg.cellW > 0.f ? cellPitchX(cfg) * 0.5f : 1.f;
            const float th = cfg.cellH > 0.f ? cellPitchY(cfg) * 0.5f : 1.f;
            const float a = lx / tw;
            const float b = ly / th;
            cx = int(std::floor((b + a) * 0.5f));
            cy = int(std::floor((b - a) * 0.5f));
            break;
        }
        default: {
            // 最近格中心（staggered / hex）。
            cx = 0;
            cy = 0;
            float best = 1e30f;
            const int w = std::max(1, mapW);
            const int h = std::max(1, mapH);
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    float wx = 0.f, wy = 0.f;
                    cellToWorld(cfg, x, y, wx, wy);
                    wx += cfg.cellW * 0.5f;
                    wy += cfg.cellH * 0.5f;
                    const float dx = wx - px;
                    const float dy = wy - py;
                    const float d = dx * dx + dy * dy;
                    if (d < best) {
                        best = d;
                        cx = x;
                        cy = y;
                    }
                }
            }
            break;
        }
    }
}

void foreachRotatedFootprint(int w, int h, const std::vector<uint8_t> &mask, int steps,
                             bool hexMode, const std::function<void(int lx, int ly)> &fn) {
    if (!fn) return;
    if (w <= 0) w = 1;
    if (h <= 0) h = 1;

    // 1. 收集实心局部格。
    std::vector<std::pair<int, int>> solid;
    for (int ly = 0; ly < h; ++ly) {
        for (int lx = 0; lx < w; ++lx) {
            bool solidCell = true;
            if (!mask.empty()) {
                const size_t idx = size_t(ly) * size_t(w) + size_t(lx);
                solidCell = idx < mask.size() && mask[idx] != 0;
            }
            if (solidCell) solid.emplace_back(lx, ly);
        }
    }
    if (solid.empty()) return;

    // 2. 旋转到归一化局部坐标。
    std::vector<std::pair<int, int>> rotated;
    rotated.reserve(solid.size());
    int minX = 1 << 30, minY = 1 << 30;
    for (const auto &c : solid) {
        int rx = c.first;
        int ry = c.second;
        if (hexMode) {
            int x = 0, y = 0, z = 0;
            offsetToCube(c.first, c.second, x, y, z);
            const int s = ((steps % 6) + 6) % 6;
            for (int i = 0; i < s; ++i) {
                const int nx = -z;
                const int ny = -x;
                const int nz = -y;
                x = nx;
                y = ny;
                z = nz;
            }
            cubeToOffset(x, y, rx, ry);
        } else {
            const int q = ((steps % 4) + 4) % 4;
            switch (q) {
                case 1:  // 90° CCW
                    rx = c.second;
                    ry = w - 1 - c.first;
                    break;
                case 2:  // 180°
                    rx = w - 1 - c.first;
                    ry = h - 1 - c.second;
                    break;
                case 3:  // 270° CCW / 90° CW
                    rx = h - 1 - c.second;
                    ry = c.first;
                    break;
                default:
                    break;
            }
        }
        rotated.emplace_back(rx, ry);
        minX = std::min(minX, rx);
        minY = std::min(minY, ry);
    }
    for (const auto &c : rotated) fn(c.first - minX, c.second - minY);
}

void rotatedFootprintSize(int w, int h, const std::vector<uint8_t> &mask, int steps,
                          bool hexMode, int &outW, int &outH) {
    int maxX = -1;
    int maxY = -1;
    foreachRotatedFootprint(w, h, mask, steps, hexMode, [&](int lx, int ly) {
        maxX = std::max(maxX, lx);
        maxY = std::max(maxY, ly);
    });
    outW = maxX + 1;
    outH = maxY + 1;
}

}  // namespace eve::grid
