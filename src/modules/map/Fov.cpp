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

enum class Algorithm : uint8_t { Shadowcast };
enum class RadiusMetric : uint8_t { Euclidean, Chebyshev, Manhattan };
enum class CellState : uint8_t { Unknown = 0, Explored = 1, Visible = 2 };

Algorithm parseAlgorithm(const std::string &name) {
    (void)name;
    return Algorithm::Shadowcast;
}

std::string algorithmName(Algorithm a) {
    (void)a;
    return "shadowcast";
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

/** Octant transform multipliers (libtcod / RogueBasin style). */
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
        int radius = 0;
        bool enabled = true;
        bool useCone = false;
        float facingDeg = 0.f;
        float halfAngleDeg = 180.f;
    };

    int width = 0;
    int height = 0;
    TileLayer *layer = nullptr;
    Algorithm algorithm = Algorithm::Shadowcast;
    RadiusMetric metric = RadiusMetric::Euclidean;
    bool cornerPeek = false;
    bool blockEmpty = true;
    std::vector<uint8_t> opaque;  // 0/1
    std::vector<CellState> state;
    std::unordered_set<uint32_t> opaqueGids;
    std::vector<Revealer> revealers;
    int nextRevealerId = 1;

    int index(int x, int y) const { return y * width + x; }
    bool inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < width && y < height; }

    void resize(int w, int h) {
        width = w > 0 ? w : 0;
        height = h > 0 ? h : 0;
        const size_t n = size_t(width * height);
        opaque.assign(n, 0);
        state.assign(n, CellState::Unknown);
    }

    void bindLayer(TileLayer *l) {
        layer = l;
        if (!layer) return;
        auto cfg = layer->config();
        resize(cfg->mapW, cfg->mapH);
        syncFromLayer();
    }

    void syncFromLayer() {
        if (!layer) return;
        auto cfg = layer->config();
        auto tiles = layer->tiles();
        if (cfg->mapW != width || cfg->mapH != height) {
            // Preserve explored memory only when size unchanged; resize clears.
            resize(cfg->mapW, cfg->mapH);
        }
        const int n = width * height;
        for (int i = 0; i < n; ++i) {
            const uint32_t gid = (i < int(tiles->gids.size())) ? tileGid(tiles->gids[size_t(i)]) : 0u;
            bool isOpaque = false;
            if (blockEmpty && gid == 0u) isOpaque = true;
            if (opaqueGids.count(gid)) isOpaque = true;
            opaque[size_t(i)] = isOpaque ? 1u : 0u;
        }
    }

    bool cellOpaque(int x, int y) const {
        if (!inBounds(x, y)) return true;
        return opaque[size_t(index(x, y))] != 0;
    }

    void markVisible(int x, int y) {
        if (!inBounds(x, y)) return;
        state[size_t(index(x, y))] = CellState::Visible;
    }

    void resetVisibleKeepExplored() {
        for (auto &s : state) {
            if (s == CellState::Visible) s = CellState::Explored;
        }
    }

    void clearAllMemory() { std::fill(state.begin(), state.end(), CellState::Unknown); }

    Revealer *findRevealer(int id) {
        for (auto &r : revealers) {
            if (r.id == id) return &r;
        }
        return nullptr;
    }

    void castLight(int ox, int oy, int radius, int row, float startSlope, float endSlope, int xx,
                   int xy, int yx, int yy, bool useCone, float facingDeg, float halfAngleDeg) {
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

                if (startSlope < rightSlope) {
                    continue;
                }
                if (endSlope > leftSlope) {
                    break;
                }

                if (inRadius(metric, mapX - ox, mapY - oy, radius) &&
                    inCone(ox, oy, mapX, mapY, useCone, facingDeg, halfAngleDeg)) {
                    markVisible(mapX, mapY);
                }

                if (blocked) {
                    if (cellOpaque(mapX, mapY)) {
                        newStart = rightSlope;
                        continue;
                    }
                    blocked = false;
                    startSlope = newStart;
                } else if (cellOpaque(mapX, mapY) && j < radius) {
                    blocked = true;
                    castLight(ox, oy, radius, j + 1, startSlope, leftSlope, xx, xy, yx, yy, useCone,
                              facingDeg, halfAngleDeg);
                    newStart = rightSlope;
                }
            }
            if (blocked) break;
        }
    }

    void computeRevealer(const Revealer &r) {
        if (!r.enabled) return;
        if (!inBounds(r.x, r.y)) return;

        if (inCone(r.x, r.y, r.x, r.y, r.useCone, r.facingDeg, r.halfAngleDeg)) {
            markVisible(r.x, r.y);
        }

        const int radius = std::max(0, r.radius);
        if (radius == 0) return;

        for (int oct = 0; oct < 8; ++oct) {
            castLight(r.x, r.y, radius, 1, 1.f, 0.f, kMultXX[oct], kMultXY[oct], kMultYX[oct],
                      kMultYY[oct], r.useCone, r.facingDeg, r.halfAngleDeg);
        }
        (void)cornerPeek;
    }

    void compute() {
        resetVisibleKeepExplored();
        for (const auto &r : revealers) computeRevealer(r);
    }
};

Fov::Fov() : impl_(std::make_unique<Impl>()) {}

Fov::Fov(TileLayer *layer) : Fov() { bindLayer(layer); }

Fov::Fov(int width, int height) : Fov() { setSize(width, height); }

Fov::~Fov() = default;
Fov::Fov(Fov &&) noexcept = default;
Fov &Fov::operator=(Fov &&) noexcept = default;

void Fov::bindLayer(TileLayer *layer) {
    if (!layer) return;
    impl_->bindLayer(layer);
}

void Fov::setSize(int width, int height) {
    impl_->layer = nullptr;
    impl_->resize(width, height);
}

int Fov::getWidth() const { return impl_->width; }
int Fov::getHeight() const { return impl_->height; }

void Fov::setAlgorithm(const std::string &name) { impl_->algorithm = parseAlgorithm(name); }

std::string Fov::getAlgorithm() const { return algorithmName(impl_->algorithm); }

void Fov::setRadiusMetric(const std::string &name) {
    impl_->metric = parseMetric(name, impl_->metric);
}

std::string Fov::getRadiusMetric() const { return metricName(impl_->metric); }

void Fov::setCornerPeek(bool enable) { impl_->cornerPeek = enable; }
bool Fov::getCornerPeek() const { return impl_->cornerPeek; }

void Fov::blockOpaqueGid(int gid) {
    if (gid < 0) return;
    impl_->opaqueGids.insert(uint32_t(gid));
    if (impl_->layer) impl_->syncFromLayer();
}

void Fov::unblockOpaqueGid(int gid) {
    if (gid < 0) return;
    impl_->opaqueGids.erase(uint32_t(gid));
    if (impl_->layer) impl_->syncFromLayer();
}

void Fov::clearOpaqueGids() {
    impl_->opaqueGids.clear();
    if (impl_->layer) impl_->syncFromLayer();
}

void Fov::setBlockEmpty(bool enable) {
    impl_->blockEmpty = enable;
    if (impl_->layer) impl_->syncFromLayer();
}

bool Fov::getBlockEmpty() const { return impl_->blockEmpty; }

void Fov::setOpaque(int x, int y, bool opaque) {
    if (!impl_->inBounds(x, y)) return;
    impl_->opaque[size_t(impl_->index(x, y))] = opaque ? 1u : 0u;
}

bool Fov::isOpaque(int x, int y) const { return impl_->cellOpaque(x, y); }

void Fov::syncFromLayer() { impl_->syncFromLayer(); }

int Fov::addRevealer(int x, int y, int radius) {
    Impl::Revealer r;
    r.id = impl_->nextRevealerId++;
    r.x = x;
    r.y = y;
    r.radius = radius;
    impl_->revealers.push_back(r);
    return r.id;
}

void Fov::removeRevealer(int id) {
    auto &v = impl_->revealers;
    v.erase(std::remove_if(v.begin(), v.end(), [id](const Impl::Revealer &r) { return r.id == id; }),
            v.end());
}

void Fov::clearRevealers() { impl_->revealers.clear(); }

void Fov::setRevealerPosition(int id, int x, int y) {
    if (auto *r = impl_->findRevealer(id)) {
        r->x = x;
        r->y = y;
    }
}

void Fov::setRevealerRadius(int id, int radius) {
    if (auto *r = impl_->findRevealer(id)) r->radius = radius;
}

void Fov::setRevealerFacing(int id, float facingDeg, float halfAngleDeg) {
    if (auto *r = impl_->findRevealer(id)) {
        r->useCone = true;
        r->facingDeg = facingDeg;
        r->halfAngleDeg = std::max(0.f, halfAngleDeg);
    }
}

void Fov::clearRevealerFacing(int id) {
    if (auto *r = impl_->findRevealer(id)) {
        r->useCone = false;
        r->halfAngleDeg = 180.f;
    }
}

void Fov::setRevealerEnabled(int id, bool enabled) {
    if (auto *r = impl_->findRevealer(id)) r->enabled = enabled;
}

int Fov::getRevealerCount() const { return int(impl_->revealers.size()); }

void Fov::compute() { impl_->compute(); }

bool Fov::isVisible(int x, int y) const {
    if (!impl_->inBounds(x, y)) return false;
    return impl_->state[size_t(impl_->index(x, y))] == CellState::Visible;
}

bool Fov::isExplored(int x, int y) const {
    if (!impl_->inBounds(x, y)) return false;
    const auto s = impl_->state[size_t(impl_->index(x, y))];
    return s == CellState::Explored || s == CellState::Visible;
}

std::string Fov::getState(int x, int y) const {
    if (!impl_->inBounds(x, y)) return "unknown";
    switch (impl_->state[size_t(impl_->index(x, y))]) {
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

void Fov::resetVisibleOnly() { impl_->resetVisibleKeepExplored(); }

}  // namespace eve::map
