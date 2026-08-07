#include "map/TileProjection.h"

#include <algorithm>
#include <cmath>

namespace eve::map {
namespace {

bool staggerDoShift(const TileLayer::Config &cfg, int tx, int ty) {
    const int major = (cfg.staggerAxis == StaggerAxis::Y) ? ty : tx;
    const bool odd = (major & 1) != 0;
    return cfg.staggerIndex == StaggerIndex::Odd ? odd : !odd;
}

}  // namespace

void tileToWorld(const TileLayer::Config &cfg, int tx, int ty, float &wx, float &wy) {
    const float tw = cfg.tileW;
    const float th = cfg.tileH;
    switch (cfg.orientation) {
    case MapOrientation::Orthogonal:
        wx = cfg.originX + float(tx) * tw;
        wy = cfg.originY + float(ty) * th;
        break;
    case MapOrientation::Isometric:
        wx = cfg.originX + float(tx - ty) * tw * 0.5f;
        wy = cfg.originY + float(tx + ty) * th * 0.5f;
        break;
    case MapOrientation::Staggered:
    case MapOrientation::Hexagonal: {
        // Match Tiled StaggeredRenderer / HexagonalRenderer.
        const bool hex = cfg.orientation == MapOrientation::Hexagonal && cfg.hexSideLength > 0.f;
        if (cfg.staggerAxis == StaggerAxis::Y) {
            const float pitchY = hex ? (th + cfg.hexSideLength) * 0.5f : th * 0.5f;
            wx = cfg.originX + float(tx) * tw + (staggerDoShift(cfg, tx, ty) ? tw * 0.5f : 0.f);
            wy = cfg.originY + float(ty) * pitchY;
        } else {
            const float pitchX = hex ? (tw + cfg.hexSideLength) * 0.5f : tw * 0.5f;
            wx = cfg.originX + float(tx) * pitchX;
            wy = cfg.originY + float(ty) * th + (staggerDoShift(cfg, tx, ty) ? th * 0.5f : 0.f);
        }
        break;
    }
    }
}

float tileToDepthY(const TileLayer::Config &cfg, int tx, int ty) {
    float wx = 0.f, wy = 0.f;
    tileToWorld(cfg, tx, ty, wx, wy);
    return wy + cfg.tileH;
}

void worldToTile(const TileLayer::Config &cfg, float wx, float wy, int &tx, int &ty) {
    switch (cfg.orientation) {
    case MapOrientation::Orthogonal: {
        const float lx = wx - cfg.originX;
        const float ly = wy - cfg.originY;
        tx = int(std::floor(lx / cfg.tileW));
        ty = int(std::floor(ly / cfg.tileH));
        break;
    }
    case MapOrientation::Isometric: {
        const float lx = wx - cfg.originX;
        const float ly = wy - cfg.originY;
        const float a = lx / (cfg.tileW * 0.5f);
        const float b = ly / (cfg.tileH * 0.5f);
        tx = int(std::floor((b + a) * 0.5f));
        ty = int(std::floor((b - a) * 0.5f));
        break;
    }
    default: {
        // Nearest tile center within map bounds (staggered / hex).
        tx = 0;
        ty = 0;
        float best = 1e30f;
        const int mapW = std::max(1, cfg.mapW);
        const int mapH = std::max(1, cfg.mapH);
        for (int y = 0; y < mapH; ++y) {
            for (int x = 0; x < mapW; ++x) {
                float cx = 0.f, cy = 0.f;
                tileToWorld(cfg, x, y, cx, cy);
                cx += cfg.tileW * 0.5f;
                cy += cfg.tileH * 0.5f;
                const float dx = cx - wx;
                const float dy = cy - wy;
                const float d = dx * dx + dy * dy;
                if (d < best) {
                    best = d;
                    tx = x;
                    ty = y;
                }
            }
        }
        break;
    }
    }
}

}  // namespace eve::map
