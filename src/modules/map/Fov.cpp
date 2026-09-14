#include "map/Fov.h"

#include "graphics/Graphics.h"
#include "graphics/Texture.h"
#include "map/TileOrientation.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace eve::map {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kExploredMask = 0.35f;

enum class Algorithm : uint8_t { Shadowcast, Raycast, Permissive, Rectangle };
enum class RadiusMetric : uint8_t { Euclidean, Chebyshev, Manhattan };
enum class Mode : uint8_t { Grid2D, Heightmap, Volume };
enum class Topology : uint8_t { Ortho, Hex };
enum class CellState : uint8_t { Unknown = 0, Explored = 1, Visible = 2 };

Algorithm parseAlgorithm(const std::string &name, Algorithm fallback) {
    if (name == "shadowcast") return Algorithm::Shadowcast;
    if (name == "raycast") return Algorithm::Raycast;
    if (name == "permissive") return Algorithm::Permissive;
    if (name == "rectangle") return Algorithm::Rectangle;
    return fallback;
}

std::string algorithmName(Algorithm a) {
    switch (a) {
    case Algorithm::Raycast:
        return "raycast";
    case Algorithm::Permissive:
        return "permissive";
    case Algorithm::Rectangle:
        return "rectangle";
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

std::string topologyName(Topology t) { return t == Topology::Hex ? "hex" : "ortho"; }

void offsetToCube(int x, int y, int &q, int &r, int &s) {
    // odd-r staggered (matches Pathfinder hex)
    q = x - (y - (y & 1)) / 2;
    r = y;
    s = -q - r;
}

void cubeToOffset(int q, int r, int &x, int &y) {
    y = r;
    x = q + (r - (r & 1)) / 2;
}

int cubeDistance(int q0, int r0, int s0, int q1, int r1, int s1) {
    return std::max({std::abs(q0 - q1), std::abs(r0 - r1), std::abs(s0 - s1)});
}

int hexDistance(int x0, int y0, int x1, int y1) {
    int q0, r0, s0, q1, r1, s1;
    offsetToCube(x0, y0, q0, r0, s0);
    offsetToCube(x1, y1, q1, r1, s1);
    return cubeDistance(q0, r0, s0, q1, r1, s1);
}

bool inRadiusOrtho(RadiusMetric metric, int dx, int dy, int radius) {
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

bool inRadius3Ortho(RadiusMetric metric, int dx, int dy, int dz, int radius) {
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

float normalizeAngle(float a) {
    while (a <= -kPi) a += 2.f * kPi;
    while (a > kPi) a -= 2.f * kPi;
    return a;
}

constexpr int kMultXX[8] = {1, 0, 0, -1, -1, 0, 0, 1};
constexpr int kMultXY[8] = {0, 1, -1, 0, 0, -1, 1, 0};
constexpr int kMultYX[8] = {0, 1, 1, 0, 0, -1, -1, 0};
constexpr int kMultYY[8] = {1, 0, 0, 1, -1, 0, 0, -1};

constexpr int kCubeDirs[6][3] = {{1, -1, 0}, {1, 0, -1}, {0, 1, -1}, {-1, 1, 0}, {-1, 0, 1}, {0, -1, 1}};

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
        float perception = 0.f;
    };

    struct Rect {
        int x0, y0, x1, y1;
    };

    struct AngleShadow {
        float start;
        float end;
    };

    int width = 0;
    int height = 0;
    int depth = 1;
    TileLayer *layer = nullptr;
    uint64_t layerRevision = 0;
    Algorithm algorithm = Algorithm::Shadowcast;
    RadiusMetric metric = RadiusMetric::Euclidean;
    Mode mode = Mode::Grid2D;
    Topology topology = Topology::Ortho;
    bool topologyManual = false;
    bool cornerPeek = false;
    bool blockEmpty = true;
    bool dirty = true;
    float cliffBlock = 1.f;
    float eyeOffset = 0.f;
    int verticalRange = -1;
    float perceptionRadiusScale = 0.f;
    float detectionMargin = 0.f;

    std::vector<uint8_t> opaque;
    std::vector<float> elevation;
    std::vector<CellState> state;
    std::vector<int> visibleList;
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

    int effectiveRadiusOf(const Revealer &r) const {
        const int bonus = int(std::floor(r.perception * perceptionRadiusScale));
        return std::max(0, r.radius + bonus);
    }

    bool inRadius2(int ox, int oy, int x, int y, int radius) const {
        if (topology == Topology::Hex) return hexDistance(ox, oy, x, y) <= radius;
        return inRadiusOrtho(metric, x - ox, y - oy, radius);
    }

    bool inRadius3(int ox, int oy, int oz, int x, int y, int z, int radius) const {
        if (topology == Topology::Hex) {
            return hexDistance(ox, oy, x, y) + std::abs(z - oz) <= radius;
        }
        return inRadius3Ortho(metric, x - ox, y - oy, z - oz, radius);
    }

    void applyAutoTopologyFromLayer() {
        if (!layer || topologyManual) return;
        const auto o = layer->config()->orientation;
        topology = (o == MapOrientation::Hexagonal || o == MapOrientation::Staggered) ? Topology::Hex
                                                                                     : Topology::Ortho;
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
        applyAutoTopologyFromLayer();
        syncFromLayer();
    }

    void syncFromLayer() {
        if (!layer) return;
        auto cfg = layer->config();
        auto tiles = layer->tiles();
        auto tileset = layer->tileset();
        if (cfg->mapW != width || cfg->mapH != height || depth != 1) {
            resize(cfg->mapW, cfg->mapH, 1);
        }
        applyAutoTopologyFromLayer();
        const int n = width * height;
        for (int i = 0; i < n; ++i) {
            const uint32_t gid = (i < int(tiles->gids.size())) ? tileGid(tiles->gids[size_t(i)]) : 0u;
            bool isOpaque = false;
            if (blockEmpty && gid == 0u) isOpaque = true;
            if (opaqueGids.count(gid)) isOpaque = true;
            const auto visual =
                std::find_if(tileset->visuals.begin(), tileset->visuals.end(),
                             [gid](const TileLayer::Tileset::Visual &candidate) { return candidate.gid == int(gid); });
            if (visual != tileset->visuals.end()) isOpaque = isOpaque || visual->opaque;
            opaque[size_t(i)] = isOpaque ? 1u : 0u;
        }
        layerRevision = tiles->revision;
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

    const Revealer *findRevealerConst(int id) const {
        for (const auto &r : revealers) {
            if (r.id == id) return &r;
        }
        return nullptr;
    }

    bool losBresenham2(int x0, int y0, int x1, int y1, int zSlice, bool applyHeight) const {
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
            const bool atEnd = (nx == x1 && ny == y1);
            if (!atEnd && cellOpaqueOnSlice(nx, ny, zSlice)) return false;
            if (applyHeight && mode == Mode::Heightmap && !atEnd) {
                if (elevAt(nx, ny) >= viewerElev + cliffBlock) return false;
            }
            if (!cornerPeek && nx != x && ny != y) {
                if (cellOpaqueOnSlice(nx, y, zSlice) && cellOpaqueOnSlice(x, ny, zSlice)) {
                    return false;
                }
            }
            x = nx;
            y = ny;
        }
    }

    bool losHexCube(int x0, int y0, int x1, int y1, int zSlice, bool applyHeight) const {
        int q0, r0, s0, q1, r1, s1;
        offsetToCube(x0, y0, q0, r0, s0);
        offsetToCube(x1, y1, q1, r1, s1);
        const int n = cubeDistance(q0, r0, s0, q1, r1, s1);
        if (n == 0) return true;
        const float viewerElev = elevAt(x0, y0) + eyeOffset;
        for (int i = 1; i <= n; ++i) {
            const float t = float(i) / float(n);
            const float qf = float(q0) + (float(q1) - float(q0)) * t;
            const float rf = float(r0) + (float(r1) - float(r0)) * t;
            const float sf = float(s0) + (float(s1) - float(s0)) * t;
            // cube round
            int rq = int(std::lround(qf));
            int rr = int(std::lround(rf));
            int rs = int(std::lround(sf));
            const float qdiff = std::fabs(rq - qf);
            const float rdiff = std::fabs(rr - rf);
            const float sdiff = std::fabs(rs - sf);
            if (qdiff > rdiff && qdiff > sdiff) rq = -rr - rs;
            else if (rdiff > sdiff) rr = -rq - rs;
            else rs = -rq - rr;
            (void)rs;
            int x, y;
            cubeToOffset(rq, rr, x, y);
            const bool atEnd = (i == n);
            if (!atEnd && cellOpaqueOnSlice(x, y, zSlice)) return false;
            if (applyHeight && mode == Mode::Heightmap && !atEnd) {
                if (elevAt(x, y) >= viewerElev + cliffBlock) return false;
            }
        }
        return true;
    }

    bool los2(int x0, int y0, int x1, int y1, int zSlice, bool applyHeight) const {
        if (topology == Topology::Hex) return losHexCube(x0, y0, x1, y1, zSlice, applyHeight);
        return losBresenham2(x0, y0, x1, y1, zSlice, applyHeight);
    }

    bool passesHeightToTarget(int ox, int oy, int tx, int ty) const {
        if (mode != Mode::Heightmap) return true;
        return los2(ox, oy, tx, ty, 0, true);
    }

    void tryMark2(int ox, int oy, int x, int y, int zSlice, bool useCone, float facingDeg,
                  float halfAngleDeg, int radius) {
        if (!inBounds2(x, y)) return;
        if (!inRadius2(ox, oy, x, y, radius)) return;
        if (!inCone(ox, oy, x, y, useCone, facingDeg, halfAngleDeg)) return;
        if (!passesHeightToTarget(ox, oy, x, y)) return;
        if (mode == Mode::Volume) markVisible3(x, y, zSlice);
        else markVisible2(x, y);
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

    void computeOrthoShadowcast(const Revealer &r, int zSlice, int radius) {
        tryMark2(r.x, r.y, r.x, r.y, zSlice, r.useCone, r.facingDeg, r.halfAngleDeg, radius);
        if (radius == 0) return;
        for (int oct = 0; oct < 8; ++oct) {
            castLight(r.x, r.y, radius, 1, 1.f, 0.f, kMultXX[oct], kMultXY[oct], kMultYX[oct],
                      kMultYY[oct], zSlice, r.useCone, r.facingDeg, r.halfAngleDeg);
        }
    }

    float hexCellAngle(int ox, int oy, int x, int y) const {
        const float px = float(x) + ((y & 1) ? 0.5f : 0.f);
        const float py = float(y) * 0.86602540378f;  // ≈ √3/2
        const float opx = float(ox) + ((oy & 1) ? 0.5f : 0.f);
        const float opy = float(oy) * 0.86602540378f;
        return std::atan2(py - opy, px - opx);
    }

    bool angleFullyCovered(float a0, float a1, const std::vector<AngleShadow> &shadows) const {
        // Conservative: sample mid angle; for small cells this is enough for FOV.
        float mid = normalizeAngle(0.5f * (a0 + a1));
        // handle wrap when a0/a1 straddle ±π
        if (std::fabs(a1 - a0) > kPi) mid = normalizeAngle(mid + kPi);
        for (const auto &sh : shadows) {
            float s = sh.start, e = sh.end;
            if (s <= e) {
                if (mid >= s && mid <= e) return true;
            } else {
                if (mid >= s || mid <= e) return true;
            }
        }
        return false;
    }

    void addAngleShadow(std::vector<AngleShadow> &shadows, float a0, float a1) const {
        a0 = normalizeAngle(a0);
        a1 = normalizeAngle(a1);
        // Ensure [start,end] covers the short arc from a0 to a1 going the opaque wedge way:
        // use unordered span expanded slightly.
        float start = a0;
        float end = a1;
        float diff = normalizeAngle(end - start);
        if (diff < 0.f) std::swap(start, end);
        shadows.push_back(AngleShadow{normalizeAngle(start), normalizeAngle(end)});
    }

    void computeHexShadowcast(const Revealer &r, int zSlice, int radius) {
        tryMark2(r.x, r.y, r.x, r.y, zSlice, r.useCone, r.facingDeg, r.halfAngleDeg, radius);
        if (radius == 0) return;
        std::vector<AngleShadow> shadows;
        shadows.reserve(64);

        int cq, cr, cs;
        offsetToCube(r.x, r.y, cq, cr, cs);

        for (int ring = 1; ring <= radius; ++ring) {
            // Start at cube +dir0 * ring, walk 6 edges
            int q = cq + kCubeDirs[4][0] * ring;
            int rr = cr + kCubeDirs[4][1] * ring;
            int s = cs + kCubeDirs[4][2] * ring;
            for (int side = 0; side < 6; ++side) {
                for (int step = 0; step < ring; ++step) {
                    int x, y;
                    cubeToOffset(q, rr, x, y);
                    if (inBounds2(x, y) && inRadius2(r.x, r.y, x, y, radius) &&
                        inCone(r.x, r.y, x, y, r.useCone, r.facingDeg, r.halfAngleDeg)) {
                        const float ang = hexCellAngle(r.x, r.y, x, y);
                        const float half =
                            (ring <= 0) ? kPi : (0.55f / float(ring));  // angular half-width
                        const float a0 = normalizeAngle(ang - half);
                        const float a1 = normalizeAngle(ang + half);
                        if (!angleFullyCovered(a0, a1, shadows) &&
                            passesHeightToTarget(r.x, r.y, x, y)) {
                            tryMark2(r.x, r.y, x, y, zSlice, false, 0.f, 180.f, radius);
                        }
                        if (cellOpaqueOnSlice(x, y, zSlice)) {
                            addAngleShadow(shadows, a0, a1);
                        }
                    }
                    q += kCubeDirs[side][0];
                    rr += kCubeDirs[side][1];
                    s += kCubeDirs[side][2];
                    (void)s;
                }
            }
        }
    }

    void computeShadowcast(const Revealer &r, int zSlice, int radius) {
        if (topology == Topology::Hex) computeHexShadowcast(r, zSlice, radius);
        else computeOrthoShadowcast(r, zSlice, radius);
    }

    void castRayLine(int ox, int oy, int tx, int ty, int zSlice, bool useCone, float facingDeg,
                     float halfAngleDeg, int radius) {
        if (topology == Topology::Hex) {
            // Follow cube line, marking until blocked.
            int q0, r0, s0, q1, r1, s1;
            offsetToCube(ox, oy, q0, r0, s0);
            offsetToCube(tx, ty, q1, r1, s1);
            const int n = std::max(1, cubeDistance(q0, r0, s0, q1, r1, s1));
            for (int i = 0; i <= n; ++i) {
                const float t = float(i) / float(n);
                int rq = int(std::lround(float(q0) + (float(q1) - float(q0)) * t));
                int rr = int(std::lround(float(r0) + (float(r1) - float(r0)) * t));
                int rs = -rq - rr;
                (void)rs;
                int x, y;
                cubeToOffset(rq, rr, x, y);
                tryMark2(ox, oy, x, y, zSlice, useCone, facingDeg, halfAngleDeg, radius);
                if (i > 0 && cellOpaqueOnSlice(x, y, zSlice)) break;
            }
            return;
        }
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

    void computeRaycast(const Revealer &r, int zSlice, int radius) {
        tryMark2(r.x, r.y, r.x, r.y, zSlice, r.useCone, r.facingDeg, r.halfAngleDeg, radius);
        if (radius == 0) return;
        if (topology == Topology::Hex) {
            int cq, cr, cs;
            offsetToCube(r.x, r.y, cq, cr, cs);
            for (int ring = 1; ring <= radius; ++ring) {
                int q = cq + kCubeDirs[4][0] * ring;
                int rr = cr + kCubeDirs[4][1] * ring;
                for (int side = 0; side < 6; ++side) {
                    for (int step = 0; step < ring; ++step) {
                        int x, y;
                        cubeToOffset(q, rr, x, y);
                        castRayLine(r.x, r.y, x, y, zSlice, r.useCone, r.facingDeg, r.halfAngleDeg,
                                    radius);
                        q += kCubeDirs[side][0];
                        rr += kCubeDirs[side][1];
                    }
                }
            }
            return;
        }
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

    void computePermissive(const Revealer &r, int zSlice, int radius) {
        tryMark2(r.x, r.y, r.x, r.y, zSlice, r.useCone, r.facingDeg, r.halfAngleDeg, radius);
        if (radius == 0) return;
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (dx == 0 && dy == 0) continue;
                const int tx = r.x + dx;
                const int ty = r.y + dy;
                if (!inBounds2(tx, ty)) continue;
                if (!inRadius2(r.x, r.y, tx, ty, radius)) continue;
                if (!inCone(r.x, r.y, tx, ty, r.useCone, r.facingDeg, r.halfAngleDeg)) continue;
                bool see = los2(r.x, r.y, tx, ty, zSlice, mode == Mode::Heightmap);
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
                        if (!inRadius2(r.x, r.y, ax, ay, radius)) continue;
                        if (los2(r.x, r.y, ax, ay, zSlice, mode == Mode::Heightmap)) {
                            see = true;
                            break;
                        }
                    }
                }
                if (see) tryMark2(r.x, r.y, tx, ty, zSlice, false, 0.f, 180.f, radius);
            }
        }
    }

    std::vector<Rect> buildOpaqueRects(int ox, int oy, int radius, int zSlice) const {
        const int xMin = std::max(0, ox - radius);
        const int xMax = std::min(width - 1, ox + radius);
        const int yMin = std::max(0, oy - radius);
        const int yMax = std::min(height - 1, oy + radius);
        const int bw = xMax - xMin + 1;
        const int bh = yMax - yMin + 1;
        std::vector<uint8_t> used(size_t(bw * bh), 0);
        auto usedAt = [&](int x, int y) -> uint8_t & {
            return used[size_t((y - yMin) * bw + (x - xMin))];
        };

        std::vector<Rect> rects;
        for (int y = yMin; y <= yMax; ++y) {
            for (int x = xMin; x <= xMax; ++x) {
                if (usedAt(x, y)) continue;
                if (!cellOpaqueOnSlice(x, y, zSlice)) continue;
                if (!inRadius2(ox, oy, x, y, radius)) continue;
                int x1 = x;
                while (x1 + 1 <= xMax && !usedAt(x1 + 1, y) && cellOpaqueOnSlice(x1 + 1, y, zSlice) &&
                       inRadius2(ox, oy, x1 + 1, y, radius)) {
                    ++x1;
                }
                int y1 = y;
                bool grow = true;
                while (grow && y1 + 1 <= yMax) {
                    for (int xx = x; xx <= x1; ++xx) {
                        if (usedAt(xx, y1 + 1) || !cellOpaqueOnSlice(xx, y1 + 1, zSlice) ||
                            !inRadius2(ox, oy, xx, y1 + 1, radius)) {
                            grow = false;
                            break;
                        }
                    }
                    if (grow) ++y1;
                }
                for (int yy = y; yy <= y1; ++yy)
                    for (int xx = x; xx <= x1; ++xx) usedAt(xx, yy) = 1;
                rects.push_back(Rect{x, y, x1, y1});
            }
        }
        return rects;
    }

    void computeRectangle(const Revealer &r, int zSlice, int radius) {
        // Mark all in-radius cells, then carve umbras behind opaque rectangles.
        std::vector<uint8_t> lit(size_t(width * height), 0);
        auto litAt = [&](int x, int y) -> uint8_t & { return lit[size_t(index2(x, y))]; };

        for (int y = std::max(0, r.y - radius); y <= std::min(height - 1, r.y + radius); ++y) {
            for (int x = std::max(0, r.x - radius); x <= std::min(width - 1, r.x + radius); ++x) {
                if (!inRadius2(r.x, r.y, x, y, radius)) continue;
                if (!inCone(r.x, r.y, x, y, r.useCone, r.facingDeg, r.halfAngleDeg)) continue;
                if (!passesHeightToTarget(r.x, r.y, x, y)) continue;
                litAt(x, y) = 1;
            }
        }

        auto rects = buildOpaqueRects(r.x, r.y, radius, zSlice);
        const float ox = float(r.x) + 0.5f;
        const float oy = float(r.y) + 0.5f;
        std::sort(rects.begin(), rects.end(), [&](const Rect &a, const Rect &b) {
            const int acx = (a.x0 + a.x1) / 2;
            const int acy = (a.y0 + a.y1) / 2;
            const int bcx = (b.x0 + b.x1) / 2;
            const int bcy = (b.y0 + b.y1) / 2;
            return inRadius2(r.x, r.y, acx, acy, radius) &&
                   (std::abs(acx - r.x) + std::abs(acy - r.y)) <
                       (std::abs(bcx - r.x) + std::abs(bcy - r.y));
        });

        for (const auto &rc : rects) {
            // Tangents from origin to rectangle corners → umbra angles.
            const float corners[4][2] = {
                {float(rc.x0), float(rc.y0)},
                {float(rc.x1) + 1.f, float(rc.y0)},
                {float(rc.x0), float(rc.y1) + 1.f},
                {float(rc.x1) + 1.f, float(rc.y1) + 1.f},
            };
            float angles[4];
            for (int i = 0; i < 4; ++i) {
                angles[i] = std::atan2(corners[i][1] - oy, corners[i][0] - ox);
            }
            float aMin = angles[0], aMax = angles[0];
            for (int i = 1; i < 4; ++i) {
                const float dMin = normalizeAngle(angles[i] - aMin);
                const float dMax = normalizeAngle(angles[i] - aMax);
                if (dMin < 0.f) aMin = angles[i];
                if (dMax > 0.f) aMax = angles[i];
            }
            const float nearest2 =
                float(std::min({(rc.x0 - r.x) * (rc.x0 - r.x) + (rc.y0 - r.y) * (rc.y0 - r.y),
                                (rc.x1 - r.x) * (rc.x1 - r.x) + (rc.y0 - r.y) * (rc.y0 - r.y),
                                (rc.x0 - r.x) * (rc.x0 - r.x) + (rc.y1 - r.y) * (rc.y1 - r.y),
                                (rc.x1 - r.x) * (rc.x1 - r.x) + (rc.y1 - r.y) * (rc.y1 - r.y)}));

            for (int y = std::max(0, r.y - radius); y <= std::min(height - 1, r.y + radius); ++y) {
                for (int x = std::max(0, r.x - radius); x <= std::min(width - 1, r.x + radius); ++x) {
                    if (!litAt(x, y)) continue;
                    // Keep the blocking rectangle itself lit.
                    if (x >= rc.x0 && x <= rc.x1 && y >= rc.y0 && y <= rc.y1) continue;
                    const float d2 = float((x - r.x) * (x - r.x) + (y - r.y) * (y - r.y));
                    if (d2 <= nearest2 + 0.01f) continue;
                    const float ang = std::atan2(float(y) + 0.5f - oy, float(x) + 0.5f - ox);
                    bool inside = false;
                    if (aMin <= aMax) inside = (ang >= aMin && ang <= aMax);
                    else inside = (ang >= aMin || ang <= aMax);
                    // Use normalized compare for wrapped wedges.
                    if (normalizeAngle(aMax - aMin) < 0.f) {
                        inside = normalizeAngle(ang - aMin) >= 0.f ||
                                 normalizeAngle(aMax - ang) >= 0.f;
                    } else {
                        const float da = normalizeAngle(ang - aMin);
                        const float span = normalizeAngle(aMax - aMin);
                        inside = da >= 0.f && da <= span;
                    }
                    if (inside) litAt(x, y) = 0;
                }
            }
        }

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                if (litAt(x, y)) tryMark2(r.x, r.y, x, y, zSlice, false, 0.f, 180.f, radius);
            }
        }
    }

    void extendVertical(const Revealer &r, int zSlice, int radius) {
        if (mode != Mode::Volume) return;
        const int vRange = effectiveVerticalRange();
        for (int y = std::max(0, r.y - radius); y <= std::min(height - 1, r.y + radius); ++y) {
            for (int x = std::max(0, r.x - radius); x <= std::min(width - 1, r.x + radius); ++x) {
                const int idx = index3(x, y, zSlice);
                if (state[size_t(idx)] != CellState::Visible) continue;
                for (int dir = -1; dir <= 1; dir += 2) {
                    for (int step = 1; step <= vRange; ++step) {
                        const int z = zSlice + dir * step;
                        if (z < 0 || z >= depth) break;
                        if (!inRadius3(r.x, r.y, r.z, x, y, z, radius)) break;
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
        const int radius = effectiveRadiusOf(r);

        switch (algorithm) {
        case Algorithm::Raycast:
            computeRaycast(r, zSlice, radius);
            break;
        case Algorithm::Permissive:
            computePermissive(r, zSlice, radius);
            break;
        case Algorithm::Rectangle:
            computeRectangle(r, zSlice, radius);
            break;
        case Algorithm::Shadowcast:
        default:
            computeShadowcast(r, zSlice, radius);
            break;
        }
        extendVertical(r, zSlice, radius);
    }

    void compute() {
        if (layer && layerRevision != layer->tiles()->revision) syncFromLayer();
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
    if (impl_->mode == Mode::Volume) impl_->mode = Mode::Grid2D;
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
    impl_->mode = next;
    if (next != Mode::Volume && impl_->depth != 1) {
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

void Fov::setTopology(const std::string &name) {
    if (name == "auto") {
        impl_->topologyManual = false;
        impl_->applyAutoTopologyFromLayer();
        if (!impl_->layer) impl_->topology = Topology::Ortho;
    } else if (name == "hex" || name == "hexagonal" || name == "staggered") {
        impl_->topologyManual = true;
        impl_->topology = Topology::Hex;
    } else if (name == "ortho" || name == "orthogonal") {
        impl_->topologyManual = true;
        impl_->topology = Topology::Ortho;
    }
    impl_->dirty = true;
}

std::string Fov::getTopology() const { return topologyName(impl_->topology); }

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
    if (impl_->mode == Mode::Volume) {
        if (!impl_->inBounds3(x, y, 0)) return;
        impl_->opaque[size_t(impl_->index3(x, y, 0))] = opaque ? 1u : 0u;
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
    if (auto *r = impl_->findRevealer(id)) setRevealerPosition3(id, x, y, r->z);
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

void Fov::setRevealerPerception(int id, float perception) {
    if (auto *r = impl_->findRevealer(id)) {
        r->perception = perception;
        impl_->dirty = true;
    }
}

float Fov::getRevealerPerception(int id) const {
    if (const auto *r = impl_->findRevealerConst(id)) return r->perception;
    return 0.f;
}

void Fov::setPerceptionRadiusScale(float scale) {
    impl_->perceptionRadiusScale = scale;
    impl_->dirty = true;
}
float Fov::getPerceptionRadiusScale() const { return impl_->perceptionRadiusScale; }

void Fov::setDetectionMargin(float margin) { impl_->detectionMargin = margin; }
float Fov::getDetectionMargin() const { return impl_->detectionMargin; }

int Fov::getEffectiveRadius(int id) const {
    if (const auto *r = impl_->findRevealerConst(id)) return impl_->effectiveRadiusOf(*r);
    return 0;
}

bool Fov::canDetect(int revealerId, int x, int y, float targetStealth) const {
    if (impl_->mode == Mode::Volume) return canDetect3(revealerId, x, y, 0, targetStealth);
    const auto *r = impl_->findRevealerConst(revealerId);
    if (!r || !r->enabled) return false;
    if (!isVisible(x, y)) return false;
    return (r->perception + impl_->detectionMargin) >= targetStealth;
}

bool Fov::canDetect3(int revealerId, int x, int y, int z, float targetStealth) const {
    const auto *r = impl_->findRevealerConst(revealerId);
    if (!r || !r->enabled) return false;
    if (!isVisible3(x, y, z)) return false;
    return (r->perception + impl_->detectionMargin) >= targetStealth;
}

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
    default:
        return "unknown";
    }
}

void Fov::clearMemory() { impl_->clearAllMemory(); }

void Fov::resetVisibleOnly() {
    impl_->resetVisibleKeepExplored();
    impl_->dirty = true;
}

Fov::Snapshot Fov::snapshot() const {
    Snapshot result;
    result.width = impl_->width;
    result.height = impl_->height;
    result.depth = impl_->depth;
    result.states.reserve(impl_->state.size());
    for (const auto state : impl_->state) result.states.push_back(stateToMaskByte(state));
    return result;
}

Result<void> Fov::restore(const Snapshot& snapshotValue) {
    if (snapshotValue.width != impl_->width || snapshotValue.height != impl_->height ||
        snapshotValue.depth != impl_->depth || snapshotValue.states.size() != impl_->state.size())
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Fov snapshot dimensions or payload size do not match", "snapshot"));
    for (std::size_t index = 0; index < snapshotValue.states.size(); ++index) {
        const auto value = snapshotValue.states[index];
        impl_->state[index] = value == 0 ? CellState::Unknown :
                              value >= 192 ? CellState::Visible : CellState::Explored;
    }
    impl_->visibleList.clear();
    for (std::size_t index = 0; index < impl_->state.size(); ++index)
        if (impl_->state[index] == CellState::Visible) impl_->visibleList.push_back(static_cast<int>(index));
    impl_->dirty = true;
    return Result<void>::success(Status::success(StatusCode::Applied));
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
            CellState s = (impl_->mode == Mode::Volume)
                              ? impl_->state[size_t(impl_->index3(x, y, sliceZ))]
                              : impl_->state[size_t(impl_->index2(x, y))];
            out[size_t(impl_->index2(x, y))] = stateToMaskByte(s);
        }
    }
    return true;
}

graphics::Texture *Fov::buildMaskTexture(graphics::Graphics *gfx) const {
    return buildMaskTextureSlice(gfx, 0);
}

graphics::Texture *Fov::buildMaskTextureSlice(graphics::Graphics *gfx, int sliceZ) const {
    if (!gfx) return nullptr;
    std::vector<uint8_t> r8;
    if (!fillMaskR8Slice(r8, sliceZ)) return nullptr;
    std::vector<uint8_t> rgba(size_t(impl_->width * impl_->height * 4));
    for (size_t i = 0; i < r8.size(); ++i) {
        const uint8_t v = r8[i];
        rgba[i * 4 + 0] = v;
        rgba[i * 4 + 1] = v;
        rgba[i * 4 + 2] = v;
        rgba[i * 4 + 3] = v;
    }
    return gfx->newTexture(impl_->width, impl_->height, rgba.data());
}

}  // namespace eve::map
