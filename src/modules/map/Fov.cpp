#include "map/Fov.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace eve::map {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kExploredMask = 0.35f;

enum class Algorithm : uint8_t { Shadowcast, Raycast, Permissive };
enum class RadiusMetric : uint8_t { Euclidean, Chebyshev, Manhattan };
enum class Mode : uint8_t { Grid2D, Heightmap, Volume };
enum class CellState : uint8_t { Unknown = 0, Explored = 1, Visible = 2 };

Algorithm parseAlgorithm(const std::string &name, Algorithm fallback) {
    if (name == "shadowcast") return Algorithm::Shadowcast;
    if (name == "raycast") return Algorithm::Raycast;
    if (name == "permissive") return Algorithm::Permissive;
    return fallback;
}

std::string algorithmName(Algorithm a) {
    switch (a) {
    case Algorithm::Raycast:
        return "raycast";
    case Algorithm::Permissive:
        return "permissive";
    case Algorithm::Shadowcast:
    default:
        return "shadowcast";
    }
}

Mode parseMode(const std::string &name, Mode fallback) {
    if (name == "grid2d") return Mode::Grid2D;
    if (name == "heightmap") return Mode::Heightmap;
    if (name == "volume") return Mode::Volume;
    return fallback;
}

std::string modeName(Mode m) {
    switch (m) {
    case Mode::Heightmap:
        return "heightmap";
    case Mode::Volume:
        return "volume";
    case Mode::Grid2D:
    default:
        return "grid2d";
    }
}

RadiusMetric parseMetric(const std::string &name, RadiusMetric fallback) {
    if (name == "euclidean") return RadiusMetric::Euclidean;
    if (name == "chebyshev") return RadiusMetric::Chebyshev;
    if (name == "manhattan") return RadiusMetric::Manhattan;
    return fallback;
}

std::string metricName(RadiusMetric m) {
    switch (m) {
    case RadiusMetric::Euclidean:
        return "euclidean";
    case RadiusMetric::Chebyshev:
        return "chebyshev";
    case RadiusMetric::Manhattan:
        return "manhattan";
    }
    return "euclidean";
}

bool inRadius(RadiusMetric metric, int dx, int dy, int radius) {
    if (radius < 0) return false;
    const int adx = std::abs(dx);
    const int ady = std::abs(dy);
    switch (metric) {
    case RadiusMetric::Chebyshev:
        return std::max(adx, ady) <= radius;
    case RadiusMetric::Manhattan:
        return adx + ady <= radius;
    case RadiusMetric::Euclidean:
    default:
        return adx * adx + ady * ady <= radius * radius;
    }
}

bool inRadius3(RadiusMetric metric, int dx, int dy, int dz, int radius) {
    if (radius < 0) return false;
    const int adx = std::abs(dx);
    const int ady = std::abs(dy);
    const int adz = std::abs(dz);
    switch (metric) {
    case RadiusMetric::Chebyshev:
        return std::max({adx, ady, adz}) <= radius;
    case RadiusMetric::Manhattan:
        return adx + ady + adz <= radius;
    case RadiusMetric::Euclidean:
    default:
        return adx * adx + ady * ady + adz * adz <= radius * radius;
    }
}

float angleDiffDeg(float a, float b) {
    float d = a - b;
    while (d > 180.f) d -= 360.f;
    while (d < -180.f) d += 360.f;
    return d;
}

bool inCone(int ox, int oy, int x, int y, bool useCone, float facingDeg, float halfAngleDeg) {
    if (!useCone || halfAngleDeg >= 180.f) return true;
    if (x == ox && y == oy) return true;
    const float ang = std::atan2(float(y - oy), float(x - ox)) * (180.f / kPi);
    return std::fabs(angleDiffDeg(ang, facingDeg)) <= halfAngleDeg;
}

uint8_t stateToMaskByte(CellState s) {
    switch (s) {
    case CellState::Visible:
        return 255;
    case CellState::Explored:
        return uint8_t(kExploredMask * 255.f + 0.5f);
    case CellState::Unknown:
    default:
        return 0;
    }
}

float stateToMaskValue(CellState s) {
    switch (s) {
    case CellState::Visible:
        return 1.f;
    case CellState::Explored:
        return kExploredMask;
    case CellState::Unknown:
    default:
        return 0.f;
    }
}

constexpr int kMultXX[8] = {1, 0, 0, -1, -1, 0, 0, 1};
constexpr int kMultXY[8] = {0, 1, -1, 0, 0, -1, 1, 0};
constexpr int kMultYX[8] = {0, 1, 1, 0, 0, -1, -1, 0};
constexpr int kMultYY[8] = {1, 0, 0, 1, -1, 0, 0, -1};

}  // namespace

struct Fov::Impl {
    struct Revealer {
        int id = 0;
        int x = 0;
        int y = 0;
        int z = 0;
        int radius = 0;
        bool enabled = true;
        bool useCone = false;
        float facingDeg = 0.f;
        float halfAngleDeg = 180.f;
    };

    int width = 0;
    int height = 0;
    int depth = 1;
    TileLayer *layer = nullptr;
    Algorithm algorithm = Algorithm::Shadowcast;
    RadiusMetric metric = RadiusMetric::Euclidean;
    Mode mode = Mode::Grid2D;
    bool cornerPeek = false;
    bool blockEmpty = true;
    bool dirty = true;
    float cliffBlock = 1.f;
    float eyeOffset = 0.f;
    int verticalRange = -1;  // <0 => use depth

    std::vector<uint8_t> opaque;    // W*H*D
    std::vector<float> elevation;   // W*H (heightmap)
    std::vector<CellState> state;   // W*H*D
    std::vector<int> visibleList;   // indices currently Visible
    std::unordered_set<uint32_t> opaqueGids;
    std::vector<Revealer> revealers;
    int nextRevealerId = 1;

    int index2(int x, int y) const { return y * width + x; }
    int index3(int x, int y, int z) const { return (z * height + y) * width + x; }
    bool inBounds2(int x, int y) const { return x >= 0 && y >= 0 && x < width && y < height; }
    bool inBounds3(int x, int y, int z) const {
        return inBounds2(x, y) && z >= 0 && z < depth;
    }

    int effectiveVerticalRange() const {
        if (verticalRange < 0) return std::max(0, depth);
        return verticalRange;
    }

    void resize(int w, int h, int d) {
        width = w > 0 ? w : 0;
        height = h > 0 ? h : 0;
        depth = d > 0 ? d : 1;
        const size_t n = size_t(width * height * depth);
        opaque.assign(n, 0);
        state.assign(n, CellState::Unknown);
        elevation.assign(size_t(width * height), 0.f);
        visibleList.clear();
        dirty = true;
    }

    void bindLayer(TileLayer *l) {
        layer = l;
        if (!layer) return;
        auto cfg = layer->config();
        resize(cfg->mapW, cfg->mapH, 1);
        if (mode == Mode::Volume) mode = Mode::Grid2D;
        syncFromLayer();
    }

    void syncFromLayer() {
        if (!layer) return;
        auto cfg = layer->config();
        auto tiles = layer->tiles();
        if (cfg->mapW != width || cfg->mapH != height || depth != 1) {
            resize(cfg->mapW, cfg->mapH, 1);
        }
        const int n = width * height;
        for (int i = 0; i < n; ++i) {
            const uint32_t gid = (i < int(tiles->gids.size())) ? tileGid(tiles->gids[size_t(i)]) : 0u;
            bool isOpaque = false;
            if (blockEmpty && gid == 0u) isOpaque = true;
            if (opaqueGids.count(gid)) isOpaque = true;
            opaque[size_t(i)] = isOpaque ? 1u : 0u;
        }
        dirty = true;
    }

    bool cellOpaque2(int x, int y) const {
        if (!inBounds2(x, y)) return true;
        return opaque[size_t(index2(x, y))] != 0;
    }

    bool cellOpaque3(int x, int y, int z) const {
        if (!inBounds3(x, y, z)) return true;
        return opaque[size_t(index3(x, y, z))] != 0;
    }

    bool cellOpaqueOnSlice(int x, int y, int zSlice) const {
        if (mode == Mode::Volume) return cellOpaque3(x, y, zSlice);
        return cellOpaque2(x, y);
    }

    float elevAt(int x, int y) const {
        if (!inBounds2(x, y)) return 0.f;
        return elevation[size_t(index2(x, y))];
    }

    void markVisibleIdx(int idx) {
        if (idx < 0 || idx >= int(state.size())) return;
        if (state[size_t(idx)] != CellState::Visible) {
            state[size_t(idx)] = CellState::Visible;
            visibleList.push_back(idx);
        }
    }

    void markVisible2(int x, int y) {
        if (!inBounds2(x, y)) return;
        markVisibleIdx(index2(x, y));
    }

    void markVisible3(int x, int y, int z) {
        if (!inBounds3(x, y, z)) return;
        markVisibleIdx(index3(x, y, z));
    }

    void resetVisibleKeepExplored() {
        for (int idx : visibleList) {
            if (idx >= 0 && idx < int(state.size()) && state[size_t(idx)] == CellState::Visible) {
                state[size_t(idx)] = CellState::Explored;
            }
        }
        visibleList.clear();
    }

    void clearAllMemory() {
        std::fill(state.begin(), state.end(), CellState::Unknown);
        visibleList.clear();
        dirty = true;
    }

    Revealer *findRevealer(int id) {
        for (auto &r : revealers) {
            if (r.id == id) return &r;
        }
        return nullptr;
    }

    /** Bresenham LOS: intermediate opaque cells block. Destination may be opaque. */
    bool losBresenham2(int x0, int y0, int x1, int y1, int zSlice,
                       bool applyHeight) const {
        int dx = std::abs(x1 - x0);
        int dy = std::abs(y1 - y0);
        const int sx = x0 < x1 ? 1 : -1;
        const int sy = y0 < y1 ? 1 : -1;
        int err = dx - dy;
        int x = x0;
        int y = y0;
        const float viewerElev = elevAt(x0, y0) + eyeOffset;

        while (true) {
            if (x == x1 && y == y1) return true;
            const int e2 = 2 * err;
            int nx = x;
            int ny = y;
            if (e2 > -dy) {
                err -= dy;
                nx += sx;
            }
            if (e2 < dx) {
                err += dx;
                ny += sy;
            }
            // Stepping onto next cell
            const bool atEnd = (nx == x1 && ny == y1);
            if (!atEnd && cellOpaqueOnSlice(nx, ny, zSlice)) return false;
            if (applyHeight && mode == Mode::Heightmap && !atEnd) {
                if (elevAt(nx, ny) >= viewerElev + cliffBlock) return false;
            }
            // Diagonal step: optional corner block when !cornerPeek
            if (!cornerPeek && nx != x && ny != y) {
                if (cellOpaqueOnSlice(nx, y, zSlice) && cellOpaqueOnSlice(x, ny, zSlice)) {
                    return false;
                }
            }
            x = nx;
            y = ny;
        }
    }

    bool passesHeightToTarget(int ox, int oy, int tx, int ty) const {
        if (mode != Mode::Heightmap) return true;
        return losBresenham2(ox, oy, tx, ty, 0, true);
    }

    void tryMark2(int ox, int oy, int x, int y, int zSlice, bool useCone, float facingDeg,
                  float halfAngleDeg, int radius) {
        if (!inBounds2(x, y)) return;
        if (!inRadius(metric, x - ox, y - oy, radius)) return;
        if (!inCone(ox, oy, x, y, useCone, facingDeg, halfAngleDeg)) return;
        if (!passesHeightToTarget(ox, oy, x, y)) return;
        if (mode == Mode::Volume) {
            markVisible3(x, y, zSlice);
        } else {
            markVisible2(x, y);
        }
    }

    void castLight(int ox, int oy, int radius, int row, float startSlope, float endSlope, int xx,
                   int xy, int yx, int yy, int zSlice, bool useCone, float facingDeg,
                   float halfAngleDeg) {
        if (startSlope < endSlope) return;
        float newStart = 0.f;
        for (int j = row; j <= radius; ++j) {
            int dx = -j - 1;
            int dy = -j;
            bool blocked = false;
            while (dx <= 0) {
                ++dx;
                const int mapX = ox + dx * xx + dy * xy;
                const int mapY = oy + dx * yx + dy * yy;
                const float leftSlope = (float(dx) - 0.5f) / (float(dy) + 0.5f);
                const float rightSlope = (float(dx) + 0.5f) / (float(dy) - 0.5f);

                if (startSlope < rightSlope) continue;
                if (endSlope > leftSlope) break;

                tryMark2(ox, oy, mapX, mapY, zSlice, useCone, facingDeg, halfAngleDeg, radius);

                if (blocked) {
                    if (cellOpaqueOnSlice(mapX, mapY, zSlice)) {
                        newStart = rightSlope;
                        continue;
                    }
                    blocked = false;
                    startSlope = newStart;
                } else if (cellOpaqueOnSlice(mapX, mapY, zSlice) && j < radius) {
                    blocked = true;
                    castLight(ox, oy, radius, j + 1, startSlope, leftSlope, xx, xy, yx, yy, zSlice,
                              useCone, facingDeg, halfAngleDeg);
                    newStart = rightSlope;
                }
            }
            if (blocked) break;
        }
    }

    void computeShadowcast(const Revealer &r, int zSlice) {
        tryMark2(r.x, r.y, r.x, r.y, zSlice, r.useCone, r.facingDeg, r.halfAngleDeg, r.radius);
        const int radius = std::max(0, r.radius);
        if (radius == 0) return;
        for (int oct = 0; oct < 8; ++oct) {
            castLight(r.x, r.y, radius, 1, 1.f, 0.f, kMultXX[oct], kMultXY[oct], kMultYX[oct],
                      kMultYY[oct], zSlice, r.useCone, r.facingDeg, r.halfAngleDeg);
        }
    }

    void castRayLine(int ox, int oy, int tx, int ty, int zSlice, bool useCone, float facingDeg,
                     float halfAngleDeg, int radius) {
        int dx = std::abs(tx - ox);
        int dy = std::abs(ty - oy);
        const int sx = ox < tx ? 1 : -1;
        const int sy = oy < ty ? 1 : -1;
        int err = dx - dy;
        int x = ox;
        int y = oy;
        while (true) {
            tryMark2(ox, oy, x, y, zSlice, useCone, facingDeg, halfAngleDeg, radius);
            if (x == tx && y == ty) break;
            if (!(x == ox && y == oy) && cellOpaqueOnSlice(x, y, zSlice)) break;
            const int e2 = 2 * err;
            if (e2 > -dy) {
                err -= dy;
                x += sx;
            }
            if (e2 < dx) {
                err += dx;
                y += sy;
            }
        }
    }

    void computeRaycast(const Revealer &r, int zSlice) {
        tryMark2(r.x, r.y, r.x, r.y, zSlice, r.useCone, r.facingDeg, r.halfAngleDeg, r.radius);
        const int radius = std::max(0, r.radius);
        if (radius == 0) return;
        // Cast to every cell on the radius square perimeter (dense enough for gameplay).
        for (int i = -radius; i <= radius; ++i) {
            castRayLine(r.x, r.y, r.x + i, r.y - radius, zSlice, r.useCone, r.facingDeg,
                        r.halfAngleDeg, radius);
            castRayLine(r.x, r.y, r.x + i, r.y + radius, zSlice, r.useCone, r.facingDeg,
                        r.halfAngleDeg, radius);
            castRayLine(r.x, r.y, r.x - radius, r.y + i, zSlice, r.useCone, r.facingDeg,
                        r.halfAngleDeg, radius);
            castRayLine(r.x, r.y, r.x + radius, r.y + i, zSlice, r.useCone, r.facingDeg,
                        r.halfAngleDeg, radius);
        }
    }

    void computePermissive(const Revealer &r, int zSlice) {
        tryMark2(r.x, r.y, r.x, r.y, zSlice, r.useCone, r.facingDeg, r.halfAngleDeg, r.radius);
        const int radius = std::max(0, r.radius);
        if (radius == 0) return;
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (dx == 0 && dy == 0) continue;
                const int tx = r.x + dx;
                const int ty = r.y + dy;
                if (!inBounds2(tx, ty)) continue;
                if (!inRadius(metric, dx, dy, radius)) continue;
                if (!inCone(r.x, r.y, tx, ty, r.useCone, r.facingDeg, r.halfAngleDeg)) continue;

                // Center-to-center LOS (destination may be opaque).
                bool see = losBresenham2(r.x, r.y, tx, ty, zSlice, mode == Mode::Heightmap);

                // Permissive peek: also accept if an orthogonally adjacent open cell
                // on the near side has LOS, then the shared edge/corner does not fully seal.
                if (!see) {
                    const int sx = (dx > 0) ? -1 : (dx < 0 ? 1 : 0);
                    const int sy = (dy > 0) ? -1 : (dy < 0 ? 1 : 0);
                    const int cands[3][2] = {{tx + sx, ty}, {tx, ty + sy}, {tx + sx, ty + sy}};
                    for (auto &c : cands) {
                        const int ax = c[0];
                        const int ay = c[1];
                        if (ax == tx && ay == ty) continue;
                        if (!inBounds2(ax, ay)) continue;
                        if (cellOpaqueOnSlice(ax, ay, zSlice)) continue;
                        if (!inRadius(metric, ax - r.x, ay - r.y, radius)) continue;
                        if (losBresenham2(r.x, r.y, ax, ay, zSlice, mode == Mode::Heightmap)) {
                            see = true;
                            break;
                        }
                    }
                }

                if (see) {
                    tryMark2(r.x, r.y, tx, ty, zSlice, false, 0.f, 180.f, radius);
                }
            }
        }
    }

    void extendVertical(const Revealer &r, int zSlice) {
        if (mode != Mode::Volume) return;
        const int vRange = effectiveVerticalRange();
        const int radius = std::max(0, r.radius);
        // Walk all cells that were marked visible on this slice within radius.
        for (int y = std::max(0, r.y - radius); y <= std::min(height - 1, r.y + radius); ++y) {
            for (int x = std::max(0, r.x - radius); x <= std::min(width - 1, r.x + radius); ++x) {
                const int idx = index3(x, y, zSlice);
                if (state[size_t(idx)] != CellState::Visible) continue;
                for (int dir = -1; dir <= 1; dir += 2) {
                    for (int step = 1; step <= vRange; ++step) {
                        const int z = zSlice + dir * step;
                        if (z < 0 || z >= depth) break;
                        if (!inRadius3(metric, x - r.x, y - r.y, z - r.z, radius)) break;
                        markVisible3(x, y, z);
                        if (cellOpaque3(x, y, z)) break;
                    }
                }
            }
        }
    }

    void computeRevealer(const Revealer &r) {
        if (!r.enabled) return;
        if (!inBounds2(r.x, r.y)) return;
        int zSlice = 0;
        if (mode == Mode::Volume) {
            if (!inBounds3(r.x, r.y, r.z)) return;
            zSlice = r.z;
        }

        switch (algorithm) {
        case Algorithm::Raycast:
            computeRaycast(r, zSlice);
            break;
        case Algorithm::Permissive:
            computePermissive(r, zSlice);
            break;
        case Algorithm::Shadowcast:
        default:
            computeShadowcast(r, zSlice);
            break;
        }
        extendVertical(r, zSlice);
    }

    void compute() {
        if (!dirty) return;
        resetVisibleKeepExplored();
        for (const auto &r : revealers) computeRevealer(r);
        dirty = false;
    }
};

Fov::Fov() : impl_(std::make_unique<Impl>()) {}

Fov::Fov(TileLayer *layer) : Fov() { bindLayer(layer); }

Fov::Fov(int width, int height) : Fov() { setSize(width, height); }

Fov::Fov(int width, int height, int depth) : Fov() { setVolumeSize(width, height, depth); }

Fov::~Fov() = default;
Fov::Fov(Fov &&) noexcept = default;
Fov &Fov::operator=(Fov &&) noexcept = default;

void Fov::bindLayer(TileLayer *layer) {
    if (!layer) return;
    impl_->bindLayer(layer);
}

void Fov::setSize(int width, int height) {
    impl_->layer = nullptr;
    impl_->mode = (impl_->mode == Mode::Volume) ? Mode::Grid2D : impl_->mode;
    impl_->resize(width, height, 1);
}

void Fov::setVolumeSize(int width, int height, int depth) {
    impl_->layer = nullptr;
    impl_->mode = Mode::Volume;
    impl_->resize(width, height, depth);
}

int Fov::getWidth() const { return impl_->width; }
int Fov::getHeight() const { return impl_->height; }
int Fov::getDepth() const { return impl_->depth; }

void Fov::setMode(const std::string &name) {
    const Mode next = parseMode(name, impl_->mode);
    if (next == Mode::Volume && impl_->depth < 2) {
        // Keep current footprint; ensure at least depth 1 volume semantics.
        impl_->mode = Mode::Volume;
    } else {
        impl_->mode = next;
    }
    if (next != Mode::Volume && impl_->depth != 1) {
        // Collapse to 2D footprint preserving z=0 opaque/state via resize wipe.
        impl_->resize(impl_->width, impl_->height, 1);
    }
    impl_->dirty = true;
}

std::string Fov::getMode() const { return modeName(impl_->mode); }

void Fov::setAlgorithm(const std::string &name) {
    impl_->algorithm = parseAlgorithm(name, impl_->algorithm);
    impl_->dirty = true;
}

std::string Fov::getAlgorithm() const { return algorithmName(impl_->algorithm); }

void Fov::setRadiusMetric(const std::string &name) {
    impl_->metric = parseMetric(name, impl_->metric);
    impl_->dirty = true;
}

std::string Fov::getRadiusMetric() const { return metricName(impl_->metric); }

void Fov::setCornerPeek(bool enable) {
    impl_->cornerPeek = enable;
    impl_->dirty = true;
}
bool Fov::getCornerPeek() const { return impl_->cornerPeek; }

void Fov::blockOpaqueGid(int gid) {
    if (gid < 0) return;
    impl_->opaqueGids.insert(uint32_t(gid));
    if (impl_->layer) impl_->syncFromLayer();
    else impl_->dirty = true;
}

void Fov::unblockOpaqueGid(int gid) {
    if (gid < 0) return;
    impl_->opaqueGids.erase(uint32_t(gid));
    if (impl_->layer) impl_->syncFromLayer();
    else impl_->dirty = true;
}

void Fov::clearOpaqueGids() {
    impl_->opaqueGids.clear();
    if (impl_->layer) impl_->syncFromLayer();
    else impl_->dirty = true;
}

void Fov::setBlockEmpty(bool enable) {
    impl_->blockEmpty = enable;
    if (impl_->layer) impl_->syncFromLayer();
    else impl_->dirty = true;
}

bool Fov::getBlockEmpty() const { return impl_->blockEmpty; }

void Fov::setOpaque(int x, int y, bool opaque) {
    if (!impl_->inBounds2(x, y)) return;
    // 2D API writes z=0 (or sole layer).
    const int z = 0;
    if (impl_->mode == Mode::Volume) {
        if (!impl_->inBounds3(x, y, z)) return;
        impl_->opaque[size_t(impl_->index3(x, y, z))] = opaque ? 1u : 0u;
    } else {
        impl_->opaque[size_t(impl_->index2(x, y))] = opaque ? 1u : 0u;
    }
    impl_->dirty = true;
}

bool Fov::isOpaque(int x, int y) const {
    if (impl_->mode == Mode::Volume) return impl_->cellOpaque3(x, y, 0);
    return impl_->cellOpaque2(x, y);
}

void Fov::setOpaque3(int x, int y, int z, bool opaque) {
    if (!impl_->inBounds3(x, y, z)) return;
    impl_->opaque[size_t(impl_->index3(x, y, z))] = opaque ? 1u : 0u;
    impl_->dirty = true;
}

bool Fov::isOpaque3(int x, int y, int z) const { return impl_->cellOpaque3(x, y, z); }

void Fov::syncFromLayer() { impl_->syncFromLayer(); }

void Fov::setElevation(int x, int y, float elev) {
    if (!impl_->inBounds2(x, y)) return;
    impl_->elevation[size_t(impl_->index2(x, y))] = elev;
    impl_->dirty = true;
}

float Fov::getElevation(int x, int y) const { return impl_->elevAt(x, y); }

void Fov::setCliffBlock(float delta) {
    impl_->cliffBlock = delta;
    impl_->dirty = true;
}
float Fov::getCliffBlock() const { return impl_->cliffBlock; }

void Fov::setEyeOffset(float offset) {
    impl_->eyeOffset = offset;
    impl_->dirty = true;
}
float Fov::getEyeOffset() const { return impl_->eyeOffset; }

void Fov::setVerticalRange(int range) {
    impl_->verticalRange = range;
    impl_->dirty = true;
}
int Fov::getVerticalRange() const { return impl_->effectiveVerticalRange(); }

int Fov::addRevealer(int x, int y, int radius) { return addRevealer3(x, y, 0, radius); }

int Fov::addRevealer3(int x, int y, int z, int radius) {
    Impl::Revealer r;
    r.id = impl_->nextRevealerId++;
    r.x = x;
    r.y = y;
    r.z = z;
    r.radius = radius;
    impl_->revealers.push_back(r);
    impl_->dirty = true;
    return r.id;
}

void Fov::removeRevealer(int id) {
    auto &v = impl_->revealers;
    v.erase(std::remove_if(v.begin(), v.end(), [id](const Impl::Revealer &r) { return r.id == id; }),
            v.end());
    impl_->dirty = true;
}

void Fov::clearRevealers() {
    impl_->revealers.clear();
    impl_->dirty = true;
}

void Fov::setRevealerPosition(int id, int x, int y) {
    if (auto *r = impl_->findRevealer(id)) {
        setRevealerPosition3(id, x, y, r->z);
    }
}

void Fov::setRevealerPosition3(int id, int x, int y, int z) {
    if (auto *r = impl_->findRevealer(id)) {
        r->x = x;
        r->y = y;
        r->z = z;
        impl_->dirty = true;
    }
}

void Fov::setRevealerRadius(int id, int radius) {
    if (auto *r = impl_->findRevealer(id)) {
        r->radius = radius;
        impl_->dirty = true;
    }
}

void Fov::setRevealerFacing(int id, float facingDeg, float halfAngleDeg) {
    if (auto *r = impl_->findRevealer(id)) {
        r->useCone = true;
        r->facingDeg = facingDeg;
        r->halfAngleDeg = std::max(0.f, halfAngleDeg);
        impl_->dirty = true;
    }
}

void Fov::clearRevealerFacing(int id) {
    if (auto *r = impl_->findRevealer(id)) {
        r->useCone = false;
        r->halfAngleDeg = 180.f;
        impl_->dirty = true;
    }
}

void Fov::setRevealerEnabled(int id, bool enabled) {
    if (auto *r = impl_->findRevealer(id)) {
        r->enabled = enabled;
        impl_->dirty = true;
    }
}

int Fov::getRevealerCount() const { return int(impl_->revealers.size()); }

void Fov::markDirty() { impl_->dirty = true; }
bool Fov::isDirty() const { return impl_->dirty; }

void Fov::compute() { impl_->compute(); }

bool Fov::isVisible(int x, int y) const {
    if (impl_->mode == Mode::Volume) return isVisible3(x, y, 0);
    if (!impl_->inBounds2(x, y)) return false;
    return impl_->state[size_t(impl_->index2(x, y))] == CellState::Visible;
}

bool Fov::isExplored(int x, int y) const {
    if (impl_->mode == Mode::Volume) return isExplored3(x, y, 0);
    if (!impl_->inBounds2(x, y)) return false;
    const auto s = impl_->state[size_t(impl_->index2(x, y))];
    return s == CellState::Explored || s == CellState::Visible;
}

bool Fov::isVisible3(int x, int y, int z) const {
    if (!impl_->inBounds3(x, y, z)) return false;
    return impl_->state[size_t(impl_->index3(x, y, z))] == CellState::Visible;
}

bool Fov::isExplored3(int x, int y, int z) const {
    if (!impl_->inBounds3(x, y, z)) return false;
    const auto s = impl_->state[size_t(impl_->index3(x, y, z))];
    return s == CellState::Explored || s == CellState::Visible;
}

std::string Fov::getState(int x, int y) const {
    if (impl_->mode == Mode::Volume) return getState3(x, y, 0);
    if (!impl_->inBounds2(x, y)) return "unknown";
    switch (impl_->state[size_t(impl_->index2(x, y))]) {
    case CellState::Visible:
        return "visible";
    case CellState::Explored:
        return "explored";
    case CellState::Unknown:
    default:
        return "unknown";
    }
}

std::string Fov::getState3(int x, int y, int z) const {
    if (!impl_->inBounds3(x, y, z)) return "unknown";
    switch (impl_->state[size_t(impl_->index3(x, y, z))]) {
    case CellState::Visible:
        return "visible";
    case CellState::Explored:
        return "explored";
    case CellState::Unknown:
    default:
        return "unknown";
    }
}

void Fov::clearMemory() { impl_->clearAllMemory(); }

void Fov::resetVisibleOnly() {
    impl_->resetVisibleKeepExplored();
    impl_->dirty = true;
}

float Fov::getMaskValue(int x, int y) const {
    if (impl_->mode == Mode::Volume) return getMaskValue3(x, y, 0);
    if (!impl_->inBounds2(x, y)) return 0.f;
    return stateToMaskValue(impl_->state[size_t(impl_->index2(x, y))]);
}

int Fov::getMaskByte(int x, int y) const {
    if (impl_->mode == Mode::Volume) return getMaskByte3(x, y, 0);
    if (!impl_->inBounds2(x, y)) return 0;
    return int(stateToMaskByte(impl_->state[size_t(impl_->index2(x, y))]));
}

float Fov::getMaskValue3(int x, int y, int z) const {
    if (!impl_->inBounds3(x, y, z)) return 0.f;
    return stateToMaskValue(impl_->state[size_t(impl_->index3(x, y, z))]);
}

int Fov::getMaskByte3(int x, int y, int z) const {
    if (!impl_->inBounds3(x, y, z)) return 0;
    return int(stateToMaskByte(impl_->state[size_t(impl_->index3(x, y, z))]));
}

bool Fov::fillMaskR8(std::vector<uint8_t> &out) const { return fillMaskR8Slice(out, 0); }

bool Fov::fillMaskR8Slice(std::vector<uint8_t> &out, int sliceZ) const {
    if (impl_->width <= 0 || impl_->height <= 0) {
        out.clear();
        return false;
    }
    if (impl_->mode == Mode::Volume) {
        if (sliceZ < 0 || sliceZ >= impl_->depth) return false;
    } else {
        sliceZ = 0;
    }
    out.resize(size_t(impl_->width * impl_->height));
    for (int y = 0; y < impl_->height; ++y) {
        for (int x = 0; x < impl_->width; ++x) {
            CellState s;
            if (impl_->mode == Mode::Volume) {
                s = impl_->state[size_t(impl_->index3(x, y, sliceZ))];
            } else {
                s = impl_->state[size_t(impl_->index2(x, y))];
            }
            out[size_t(impl_->index2(x, y))] = stateToMaskByte(s);
        }
    }
    return true;
}

}  // namespace eve::map
