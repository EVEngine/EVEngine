#include "map/Pathfinder.h"
#include "common/Exception.h"
#include "map/PathfinderState.h"

#include "map/TileOrientation.h"

#include "common/Diagnostic.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <queue>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

namespace eve::map {
namespace {

constexpr float kInf = std::numeric_limits<float>::infinity();
constexpr float kSqrt2 = 1.41421356237f;

enum class Topology { Ortho4, Ortho8, Hex };

using NeighborFn = std::function<void(int nx, int ny, float moveCost)>;

Topology parseTopology(const std::string &name, Topology fallback) {
    if (name == "ortho4" || name == "orthogonal4" || name == "4") return Topology::Ortho4;
    if (name == "ortho8" || name == "orthogonal8" || name == "8") return Topology::Ortho8;
    if (name == "hex" || name == "hexagonal" || name == "staggered") return Topology::Hex;
    if (name == "auto") return fallback;
    return fallback;
}

const char *topologyName(Topology t) {
    switch (t) {
    case Topology::Ortho4:
        return "ortho4";
    case Topology::Ortho8:
        return "ortho8";
    case Topology::Hex:
        return "hex";
    }
    return "ortho4";
}

Topology topologyFromOrientation(MapOrientation orientation, bool preferDiagonal) {
    switch (orientation) {
    case MapOrientation::Staggered:
    case MapOrientation::Hexagonal:
        return Topology::Hex;
    case MapOrientation::Orthogonal:
    case MapOrientation::Isometric:
    default:
        return preferDiagonal ? Topology::Ortho8 : Topology::Ortho4;
    }
}

void forEachNeighbor(Topology topology, int x, int y, bool staggerAxisY, bool staggerOdd,
                     const NeighborFn &fn) {
    if (!fn) return;
    switch (topology) {
    case Topology::Ortho4: {
        static const int dx[4] = {1, -1, 0, 0};
        static const int dy[4] = {0, 0, 1, -1};
        for (int i = 0; i < 4; ++i) fn(x + dx[i], y + dy[i], 1.f);
        break;
    }
    case Topology::Ortho8: {
        static const int dx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
        static const int dy[8] = {0, 0, 1, -1, 1, -1, 1, -1};
        for (int i = 0; i < 8; ++i) {
            const float c = (i < 4) ? 1.f : kSqrt2;
            fn(x + dx[i], y + dy[i], c);
        }
        break;
    }
    case Topology::Hex: {
        if (staggerAxisY) {
            const bool rowOdd = ((y & 1) != 0);
            const bool shifted = staggerOdd ? rowOdd : !rowOdd;
            if (shifted) {
                fn(x + 1, y, 1.f);
                fn(x - 1, y, 1.f);
                fn(x, y - 1, 1.f);
                fn(x + 1, y - 1, 1.f);
                fn(x, y + 1, 1.f);
                fn(x + 1, y + 1, 1.f);
            } else {
                fn(x + 1, y, 1.f);
                fn(x - 1, y, 1.f);
                fn(x - 1, y - 1, 1.f);
                fn(x, y - 1, 1.f);
                fn(x - 1, y + 1, 1.f);
                fn(x, y + 1, 1.f);
            }
        } else {
            const bool colOdd = ((x & 1) != 0);
            const bool shifted = staggerOdd ? colOdd : !colOdd;
            if (shifted) {
                fn(x, y + 1, 1.f);
                fn(x, y - 1, 1.f);
                fn(x - 1, y, 1.f);
                fn(x - 1, y + 1, 1.f);
                fn(x + 1, y, 1.f);
                fn(x + 1, y + 1, 1.f);
            } else {
                fn(x, y + 1, 1.f);
                fn(x, y - 1, 1.f);
                fn(x - 1, y - 1, 1.f);
                fn(x - 1, y, 1.f);
                fn(x + 1, y - 1, 1.f);
                fn(x + 1, y, 1.f);
            }
        }
        break;
    }
    }
}

float heuristic(Topology topology, int x0, int y0, int x1, int y1) {
    const int dx = std::abs(x1 - x0);
    const int dy = std::abs(y1 - y0);
    switch (topology) {
    case Topology::Ortho4:
        return float(dx + dy);
    case Topology::Ortho8:
        return float(std::max(dx, dy)) + (kSqrt2 - 1.f) * float(std::min(dx, dy));
    case Topology::Hex: {
        auto toCube = [](int x, int y) {
            const int q = x - (y - (y & 1)) / 2;
            const int r = y;
            const int s = -q - r;
            return std::tuple<int, int, int>{q, r, s};
        };
        auto [q0, r0, s0] = toCube(x0, y0);
        auto [q1, r1, s1] = toCube(x1, y1);
        return float(std::max({std::abs(q0 - q1), std::abs(r0 - r1), std::abs(s0 - s1)}));
    }
    }
    return float(dx + dy);
}

/** Navigable grid — internal linkage to keep Windows export table small. */
struct Grid {
    int width = 0;
    int height = 0;
    TileLayer *layer = nullptr;
    uint64_t layerRevision = 0;
    Topology topology = Topology::Ortho4;
    bool topologyManual = false;
    bool diagonal = false;
    bool blockEmpty = true;
    bool staggerAxisY = true;
    bool staggerOdd = true;
    bool dirty = true;
    std::vector<float> cost;  // ≤0 => blocked
    std::vector<uint8_t>         enterMask;
    std::vector<uint8_t>         exitMask;
    std::unordered_set<uint32_t> blockedGids;

    int index(int x, int y) const { return y * width + x; }
    bool inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < width && y < height; }

    void resize(int w, int h) {
        width = w > 0 ? w : 0;
        height = h > 0 ? h : 0;
        cost.assign(size_t(width * height), 1.f);
        enterMask.assign(size_t(width * height), 0xff);
        exitMask.assign(size_t(width * height), 0xff);
        dirty = true;
    }

    void applyAutoTopologyFromLayer() {
        if (!layer) return;
        topology = topologyFromOrientation(layer->config()->orientation, diagonal);
    }

    void bindLayer(TileLayer *l) {
        layer = l;
        if (!layer) return;
        auto cfg = layer->config();
        resize(cfg->mapW, cfg->mapH);
        staggerAxisY = cfg->staggerAxis == StaggerAxis::Y;
        staggerOdd = cfg->staggerIndex == StaggerIndex::Odd;
        if (!topologyManual) applyAutoTopologyFromLayer();
        syncFromLayer();
    }

    void clearLayer() { layer = nullptr; }

    void syncFromLayer() {
        if (!layer) return;
        auto cfg = layer->config();
        auto tiles = layer->tiles();
        if (cfg->mapW != width || cfg->mapH != height) resize(cfg->mapW, cfg->mapH);
        staggerAxisY = cfg->staggerAxis == StaggerAxis::Y;
        staggerOdd = cfg->staggerIndex == StaggerIndex::Odd;
        if (!topologyManual) applyAutoTopologyFromLayer();

        const auto tileset = layer->tileset();
        const int n = width * height;
        for (int i = 0; i < n; ++i) {
            const uint32_t gid = (i < int(tiles->gids.size())) ? tileGid(tiles->gids[size_t(i)]) : 0u;
            bool blocked = false;
            float          movementCost  = 1.f;
            uint8_t        cellEnterMask = 0xff;
            uint8_t        cellExitMask  = 0xff;
            if (blockEmpty && gid == 0u) blocked = true;
            if (blockedGids.count(gid)) blocked = true;
            if (gid != 0u) {
                const auto visual = std::find_if(
                    tileset->visuals.begin(), tileset->visuals.end(),
                    [gid](const TileLayer::Tileset::Visual &candidate) { return candidate.gid == int(gid); });
                if (visual != tileset->visuals.end()) {
                    blocked       = blocked || !visual->walkable;
                    movementCost  = std::max(0.001f, visual->cost);
                    cellEnterMask = visual->enterMask;
                    cellExitMask  = visual->exitMask;
                }
            }
            cost[size_t(i)]      = blocked ? 0.f : movementCost;
            enterMask[size_t(i)] = cellEnterMask;
            exitMask[size_t(i)]  = cellExitMask;
        }
        layerRevision = tiles->revision;
        dirty = true;
    }

    void setTopology(const std::string &name) {
        if (name == "auto") {
            topologyManual = false;
            if (layer) applyAutoTopologyFromLayer();
            else topology = diagonal ? Topology::Ortho8 : Topology::Ortho4;
        } else {
            topologyManual = true;
            topology = parseTopology(name, topology);
        }
        dirty = true;
    }

    void setDiagonal(bool enable) {
        diagonal = enable;
        if (!topologyManual && layer) applyAutoTopologyFromLayer();
        else if (!topologyManual) topology = diagonal ? Topology::Ortho8 : Topology::Ortho4;
        dirty = true;
    }

    void blockGid(int gid) {
        if (gid < 0) return;
        blockedGids.insert(uint32_t(gid));
        dirty = true;
        if (layer) syncFromLayer();
    }

    void unblockGid(int gid) {
        if (gid < 0) return;
        blockedGids.erase(uint32_t(gid));
        dirty = true;
        if (layer) syncFromLayer();
    }

    void clearBlockedGids() {
        blockedGids.clear();
        dirty = true;
        if (layer) syncFromLayer();
    }

    void setBlockEmpty(bool enable) {
        blockEmpty = enable;
        dirty = true;
        if (layer) syncFromLayer();
    }

    bool isWalkable(int x, int y) const {
        if (!inBounds(x, y)) return false;
        return cost[size_t(index(x, y))] > 0.f;
    }

    void setBlocked(int x, int y, bool blocked) {
        if (!inBounds(x, y)) return;
        cost[size_t(index(x, y))] = blocked ? 0.f : 1.f;
        dirty = true;
    }

    void setCellCost(int x, int y, float c) {
        if (!inBounds(x, y)) return;
        cost[size_t(index(x, y))] = c;
        dirty = true;
    }

    float getCellCost(int x, int y) const {
        if (!inBounds(x, y)) return 0.f;
        return cost[size_t(index(x, y))];
    }

    bool canTraverseCardinal(int x, int y, int nx, int ny) const {
        if (!isWalkable(x, y) || !isWalkable(nx, ny)) return false;
        uint8_t direction = 0;
        uint8_t opposite  = 0;
        if (nx == x && ny == y - 1) {
            direction = 1;
            opposite  = 4;
        } else if (nx == x + 1 && ny == y) {
            direction = 2;
            opposite  = 8;
        } else if (nx == x && ny == y + 1) {
            direction = 4;
            opposite  = 1;
        } else if (nx == x - 1 && ny == y) {
            direction = 8;
            opposite  = 2;
        } else {
            return true;
        }
        return (exitMask[size_t(index(x, y))] & direction) != 0 && (enterMask[size_t(index(nx, ny))] & opposite) != 0;
    }

    void forEachWalkableNeighbor(int x, int y, const NeighborFn &fn) const {
        if (!fn || !inBounds(x, y)) return;
        forEachNeighbor(topology, x, y, staggerAxisY, staggerOdd, [&](int nx, int ny, float moveCost) {
            if (!isWalkable(nx, ny)) return;
            if (!canTraverseCardinal(x, y, nx, ny)) return;
            if (topology == Topology::Ortho8) {
                const int dx = nx - x;
                const int dy = ny - y;
                if (dx != 0 && dy != 0) {
                    if (!canTraverseCardinal(x, y, x + dx, y) || !canTraverseCardinal(x, y, x, y + dy)) return;
                }
            }
            fn(nx, ny, moveCost * getCellCost(nx, ny));
        });
    }
};

struct HeapNode {
    float f = 0.f;
    int idx = 0;
    bool operator>(const HeapNode &o) const { return f > o.f; }
};

}  // namespace

struct NavigationGridSnapshot::Data {
    bool                      staggerAxisY = true, staggerOdd = true;
    int                       width = 0, height = 0;
    std::uint64_t             revision = 1;
    NavigationTopology        topology = NavigationTopology::Ortho4;
    std::vector<float>        costs;
    std::vector<std::uint8_t> enterMasks, exitMasks;
};
int                NavigationGridSnapshot::width() const noexcept { return data_ ? data_->width : 0; }
int                NavigationGridSnapshot::height() const noexcept { return data_ ? data_->height : 0; }
std::uint64_t      NavigationGridSnapshot::revision() const noexcept { return data_ ? data_->revision : 0; }
NavigationTopology NavigationGridSnapshot::topology() const noexcept {
    return data_ ? data_->topology : NavigationTopology::Ortho4;
}
bool                   NavigationGridSnapshot::isStaggerAxisY() const noexcept { return !data_ || data_->staggerAxisY; }
bool                   NavigationGridSnapshot::isStaggerOdd() const noexcept { return !data_ || data_->staggerOdd; }
std::span<const float> NavigationGridSnapshot::costs() const noexcept {
    return data_ ? std::span<const float>(data_->costs) : std::span<const float>{};
}
std::span<const std::uint8_t> NavigationGridSnapshot::enterMasks() const noexcept {
    return data_ ? std::span<const std::uint8_t>(data_->enterMasks) : std::span<const std::uint8_t>{};
}
std::span<const std::uint8_t> NavigationGridSnapshot::exitMasks() const noexcept {
    return data_ ? std::span<const std::uint8_t>(data_->exitMasks) : std::span<const std::uint8_t>{};
}

struct Pathfinder::Impl {
    std::weak_ptr<const PathfinderState::Data> restoredFrom;
    NavigationGridSnapshot lastCapture;
    std::uint64_t          changes = 1;
    Grid grid;
    bool hasCachedField = false;
    int cachedGoalX = 0;
    int cachedGoalY = 0;
    FlowField cachedField;
};

struct PathfinderState::Data {
    Pathfinder::Impl    saved;
    std::optional<Grid> binding;
};
PathfinderState::PathfinderState(std::shared_ptr<const Data> data) : data_(std::move(data)) {}
PathfinderState::~PathfinderState()                                      = default;
PathfinderState::PathfinderState(PathfinderState &&) noexcept            = default;
PathfinderState &PathfinderState::operator=(PathfinderState &&) noexcept = default;
bool             PathfinderState::bound() const noexcept { return data_ && data_->binding.has_value(); }

namespace {
namespace navigation_state {
struct Invalid {
    Status status;
};
[[noreturn]] void invalid(const char *message) {
    throw Invalid{Status::failure(Diagnostic::error(DiagnosticCode::ParseError, message, "map.pathfinder-state"))};
}
const Value::Array &array(const Value &v) {
    auto *a = v.getIf<Value::Array>();
    if (!a) invalid("Expected navigation array");
    return *a;
}
const Value::Array &tuple(const Value &v, std::size_t n) {
    const auto &a = array(v);
    if (a.size() != n) invalid("Unknown or missing navigation tuple fields");
    return a;
}
const std::string &text(const Value &v) {
    auto *s = v.getIf<std::string>();
    if (!s) invalid("Expected navigation text");
    return *s;
}
std::uint64_t counter(const Value &v) {
    const auto   &s = text(v);
    std::uint64_t n = 0;
    auto          p = std::from_chars(s.data(), s.data() + s.size(), n);
    if (p.ec != std::errc{} || p.ptr != s.data() + s.size() || std::to_string(n) != s)
        invalid("Invalid canonical navigation counter");
    return n;
}
std::int64_t integer(const Value &v, std::int64_t min, std::int64_t max) {
    if (!v.isInt64() || v.asInt() < min || v.asInt() > max) invalid("Navigation integer outside range");
    return v.asInt();
}
bool boolean(const Value &v) {
    auto *b = v.getIf<bool>();
    if (!b) invalid("Expected navigation boolean");
    return *b;
}
std::size_t cells(int width, int height) {
    constexpr int limit = 4 * 1024 * 1024;
    if (width < 0 || height < 0 || width > limit || height > limit || std::int64_t(width) * height > limit)
        invalid("Oversized navigation dimensions");
    return std::size_t(width) * std::size_t(height);
}
float number(const Value &v) {
    if (!v.isDouble() && !v.isInt64()) invalid("Expected navigation number");
    double n = v.isDouble() ? v.asDouble() : double(v.asInt());
    if (!std::isfinite(n) || std::abs(n) > std::numeric_limits<float>::max()) invalid("Invalid navigation float");
    return float(n);
}
Value floats(std::span<const float> values) {
    Value::Array a;
    a.reserve(values.size());
    for (float x : values) {
        if (!std::isfinite(x)) invalid("Nonfinite grid cost");
        a.push_back(x);
    }
    return a;
}
std::vector<float> floats(const Value &v, std::size_t count) {
    const auto &a = array(v);
    if (a.size() != count) invalid("Navigation cost length mismatch");
    std::vector<float> r;
    r.reserve(count);
    for (const auto &x : a) r.push_back(number(x));
    return r;
}
Value masks(std::span<const std::uint8_t> values) {
    Value::Array a;
    a.reserve(values.size());
    for (auto x : values) a.push_back(int(x));
    return a;
}
std::vector<std::uint8_t> masks(const Value &v, std::size_t count) {
    const auto &a = array(v);
    if (a.size() != count) invalid("Navigation mask length mismatch");
    std::vector<std::uint8_t> r;
    r.reserve(count);
    for (const auto &x : a) r.push_back(std::uint8_t(integer(x, 0, 255)));
    return r;
}
Value grid(const Grid &g) {
    const auto n = cells(g.width, g.height);
    if (g.cost.size() != n || g.enterMask.size() != n || g.exitMask.size() != n || g.layer)
        invalid("Invalid detached navigation grid");
    std::vector<std::uint32_t> gids(g.blockedGids.begin(), g.blockedGids.end());
    std::sort(gids.begin(), gids.end());
    Value::Array blocked;
    for (auto gid : gids) blocked.push_back(std::int64_t(gid));
    return Value::Array{g.width,           g.height,          std::to_string(g.layerRevision),
                        int(g.topology),   g.topologyManual,  g.diagonal,
                        g.blockEmpty,      g.staggerAxisY,    g.staggerOdd,
                        g.dirty,           floats(g.cost),    masks(g.enterMask),
                        masks(g.exitMask), std::move(blocked)};
}
Grid grid(const Value &v) {
    const auto &a = tuple(v, 14);
    Grid        g;
    g.width          = int(integer(a[0], 0, 4 * 1024 * 1024));
    g.height         = int(integer(a[1], 0, 4 * 1024 * 1024));
    const auto n     = cells(g.width, g.height);
    g.layerRevision  = counter(a[2]);
    g.topology       = Topology(integer(a[3], 0, 2));
    g.topologyManual = boolean(a[4]);
    g.diagonal       = boolean(a[5]);
    g.blockEmpty     = boolean(a[6]);
    g.staggerAxisY   = boolean(a[7]);
    g.staggerOdd     = boolean(a[8]);
    g.dirty          = boolean(a[9]);
    g.cost           = floats(a[10], n);
    g.enterMask      = masks(a[11], n);
    g.exitMask       = masks(a[12], n);
    std::optional<std::uint32_t> prior;
    for (const auto &item : array(a[13])) {
        auto gid = std::uint32_t(integer(item, 0, std::numeric_limits<std::uint32_t>::max()));
        if (prior && gid <= *prior) invalid("Blocked gids must be ordered unique");
        prior = gid;
        g.blockedGids.insert(gid);
    }
    return g;
}
Value field(const FlowField &f) {
    const auto   count = cells(f.getWidth(), f.getHeight());
    Value::Array data;
    data.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        int   x = int(i % std::size_t(f.getWidth())), y = int(i / std::size_t(f.getWidth()));
        float cost = f.costAt(x, y);
        if (std::isnan(cost) || cost < 0) invalid("Invalid flow integration cost");
        data.push_back(Value::Array{std::isinf(cost) ? Value{} : Value(cost), f.nextX(x, y), f.nextY(x, y)});
    }
    return Value::Array{f.getWidth(), f.getHeight(), f.getGoalX(), f.getGoalY(), std::move(data)};
}
FlowField field(const Value &v) {
    const auto &a = tuple(v, 5);
    int         w = int(integer(a[0], 0, 4 * 1024 * 1024)), h = int(integer(a[1], 0, 4 * 1024 * 1024));
    const auto  n    = cells(w, h);
    const auto &data = array(a[4]);
    if (data.size() != n) invalid("Flow cell length mismatch");
    FlowField f;
    f.resize(w, h);
    f.setGoal(int(integer(a[2], std::numeric_limits<int>::min(), std::numeric_limits<int>::max())),
              int(integer(a[3], std::numeric_limits<int>::min(), std::numeric_limits<int>::max())));
    for (std::size_t i = 0; i < n; ++i) {
        const auto &row  = tuple(data[i], 3);
        float       cost = row[0].isNull() ? FlowField::kUnreachable : number(row[0]);
        if (cost < 0) invalid("Negative flow integration cost");
        int x = int(i % std::size_t(w)), y = int(i / std::size_t(w));
        f.setCost(x, y, cost);
        f.setNext(x, y, int(integer(row[1], 0, w - 1)), int(integer(row[2], 0, h - 1)));
    }
    return f;
}
}  // namespace navigation_state
}  // namespace
Result<Value> PathfinderState::encodeState() const {
    using R = Result<Value>;
    if (!data_) return R::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation, "Moved navigation state"));
    try {
        const auto &s = data_->saved;
        Value       capture;
        if (s.lastCapture.valid()) {
            const auto &c = s.lastCapture;
            capture       = Value::Array{c.width(),
                                   c.height(),
                                   std::to_string(c.revision()),
                                   int(c.topology()),
                                   c.isStaggerAxisY(),
                                   c.isStaggerOdd(),
                                   navigation_state::floats(c.costs()),
                                   navigation_state::masks(c.enterMasks()),
                                   navigation_state::masks(c.exitMasks())};
        }
        return R::success(
            Value::object({{"schema", "map.pathfinder-state"},
                           {"version", 1},
                           {"grid", navigation_state::grid(s.grid)},
                           {"binding", data_->binding ? navigation_state::grid(*data_->binding) : Value{}},
                           {"changes", std::to_string(s.changes)},
                           {"lastCapture", std::move(capture)},
                           {"hasCachedField", s.hasCachedField},
                           {"cachedGoal", Value::Array{s.cachedGoalX, s.cachedGoalY}},
                           {"cachedField", navigation_state::field(s.cachedField)}}));
    } catch (const navigation_state::Invalid &error) {
        return R::failure(error.status);
    }
}
Result<PathfinderState> PathfinderState::prepareState(const Value &value) {
    using R = Result<PathfinderState>;
    try {
        const auto *root = value.getIf<Value::Object>();
        if (!root || root->size() != 9) navigation_state::invalid("Unknown or missing navigation state fields");
        for (const auto *key : {"schema", "version", "grid", "binding", "changes", "lastCapture", "hasCachedField",
                                "cachedGoal", "cachedField"})
            if (!root->contains(key)) navigation_state::invalid("Missing navigation state field");
        if (navigation_state::text(root->at("schema")) != "map.pathfinder-state")
            navigation_state::invalid("Wrong navigation schema");
        if (navigation_state::integer(root->at("version"), 0, std::numeric_limits<std::int64_t>::max()) != 1)
            return R::failure(Diagnostic::error(DiagnosticCode::UnknownVersion, "Unknown navigation state version"));
        auto  data = std::make_shared<Data>();
        auto &s    = data->saved;
        s.grid     = navigation_state::grid(root->at("grid"));
        if (!root->at("binding").isNull()) {
            data->binding = navigation_state::grid(root->at("binding"));
            const auto &b = *data->binding;
            if (!b.dirty || b.topologyManual != s.grid.topologyManual || b.diagonal != s.grid.diagonal ||
                b.blockEmpty != s.grid.blockEmpty || b.blockedGids != s.grid.blockedGids ||
                (b.topologyManual && b.topology != s.grid.topology))
                navigation_state::invalid("Pending binding policy differs from captured grid");
        }
        s.changes = navigation_state::counter(root->at("changes"));
        if (!s.changes) navigation_state::invalid("Zero navigation configuration watermark");
        const auto &last = root->at("lastCapture");
        if (!last.isNull()) {
            const auto &a = navigation_state::tuple(last, 9);
            auto        c = std::make_shared<NavigationGridSnapshot::Data>();
            c->width      = int(navigation_state::integer(a[0], 1, 4 * 1024 * 1024));
            c->height     = int(navigation_state::integer(a[1], 1, 4 * 1024 * 1024));
            const auto n  = navigation_state::cells(c->width, c->height);
            c->revision   = navigation_state::counter(a[2]);
            if (!c->revision) navigation_state::invalid("Zero published navigation revision");
            c->topology     = NavigationTopology(navigation_state::integer(a[3], 0, 2));
            c->staggerAxisY = navigation_state::boolean(a[4]);
            c->staggerOdd   = navigation_state::boolean(a[5]);
            if (c->topology != NavigationTopology::Hex && (!c->staggerAxisY || !c->staggerOdd))
                navigation_state::invalid("Noncanonical orthogonal capture flags");
            c->costs            = navigation_state::floats(a[6], n);
            c->enterMasks       = navigation_state::masks(a[7], n);
            c->exitMasks        = navigation_state::masks(a[8], n);
            s.lastCapture.data_ = std::move(c);
        }
        s.hasCachedField = navigation_state::boolean(root->at("hasCachedField"));
        const auto &goal = navigation_state::tuple(root->at("cachedGoal"), 2);
        s.cachedGoalX =
            int(navigation_state::integer(goal[0], std::numeric_limits<int>::min(), std::numeric_limits<int>::max()));
        s.cachedGoalY =
            int(navigation_state::integer(goal[1], std::numeric_limits<int>::min(), std::numeric_limits<int>::max()));
        s.cachedField = navigation_state::field(root->at("cachedField"));
        if (s.hasCachedField) {
            if (s.cachedField.getGoalX() != s.cachedGoalX || s.cachedField.getGoalY() != s.cachedGoalY ||
                (!s.grid.dirty &&
                 (s.cachedField.getWidth() != s.grid.width || s.cachedField.getHeight() != s.grid.height)))
                navigation_state::invalid("Cached flow identity differs from grid/cache goal");
        } else if (s.cachedField.getWidth() != 0 || s.cachedField.getHeight() != 0 || s.cachedField.getGoalX() != 0 ||
                   s.cachedField.getGoalY() != 0)
            navigation_state::invalid("Inactive flow cache contains a field");
        return R::success(PathfinderState(std::move(data)));
    } catch (const navigation_state::Invalid &error) {
        return R::failure(error.status);
    }
}
namespace {
template <class T>
const T *existingNavigationComponent(TileLayer &layer) noexcept {
    auto *buffer = layer.getComponentManager().template getComponentBuffer<T>();
    return buffer && buffer->has(layer.id) ? buffer->ptr(layer.id) : nullptr;
}
bool completeNavigationLayer(TileLayer &layer) noexcept {
    const auto *config = existingNavigationComponent<TileLayer::Config>(layer);
    return config && config->mapW >= 0 && config->mapH >= 0 &&
           std::int64_t(config->mapW) * config->mapH <= 4 * 1024 * 1024 &&
           existingNavigationComponent<TileLayer::Tiles>(layer) &&
           existingNavigationComponent<TileLayer::Tileset>(layer);
}
bool sameBindingInputs(const Grid &a, const Grid &b) noexcept {
    return a.width == b.width && a.height == b.height && a.layerRevision == b.layerRevision &&
           a.topology == b.topology && a.staggerAxisY == b.staggerAxisY && a.staggerOdd == b.staggerOdd &&
           a.cost == b.cost && a.enterMask == b.enterMask && a.exitMask == b.exitMask;
}
}  // namespace

Result<PathfinderState> Pathfinder::captureState() const {
    if (!impl_)
        return Result<PathfinderState>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Moved-from Pathfinder", "map.navigation-state"));
    if (impl_->grid.layer && !completeNavigationLayer(*impl_->grid.layer))
        return Result<PathfinderState>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "Incomplete navigation layer", "map.navigation-state"));
    auto data   = std::make_shared<PathfinderState::Data>();
    data->saved = *impl_;
    data->saved.restoredFrom.reset();
    // Observe pending layer changes through a copy, leaving the actual stale cache untouched.
    if (impl_->grid.layer) {
        data->binding = impl_->grid;
        data->binding->syncFromLayer();
        data->binding->layer = nullptr;
    }
    data->saved.grid.layer = nullptr;
    return Result<PathfinderState>::success(PathfinderState(std::move(data)));
}

Result<std::unique_ptr<Pathfinder>> Pathfinder::prepareRestored(const PathfinderState &state,
                                                                OptionalRef<TileLayer> binding) {
    using Outcome = Result<std::unique_ptr<Pathfinder>>;
    if (!state.data_ || state.data_->binding.has_value() != binding.has_value())
        return Outcome::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Native navigation binding mismatch",
                                                  "map.navigation-state"));
    if (binding) {
        auto &layer = binding->get();
        if (!completeNavigationLayer(layer))
            return Outcome::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                      "Incomplete restored navigation layer", "map.navigation-state"));
        Grid actual  = state.data_->saved.grid;
        actual.layer = &layer;
        actual.syncFromLayer();
        if (!sameBindingInputs(actual, *state.data_->binding))
            return Outcome::failure(Diagnostic::error(
                DiagnosticCode::Conflict, "Restored navigation layer facts differ", "map.navigation-state"));
    }
    auto result                 = std::make_unique<Pathfinder>();
    *result->impl_              = state.data_->saved;
    result->impl_->grid.layer   = binding ? &binding->get() : nullptr;
    result->impl_->restoredFrom = state.data_;
    return Outcome::success(std::move(result));
}

bool Pathfinder::isRestoredFrom(const PathfinderState &state) const noexcept {
    if (!impl_ || !state.data_ || impl_->restoredFrom.lock() != state.data_) return false;
    if (!impl_->grid.layer) return !state.data_->binding;
    const auto *config = existingNavigationComponent<TileLayer::Config>(*impl_->grid.layer);
    const auto *tiles  = existingNavigationComponent<TileLayer::Tiles>(*impl_->grid.layer);
    if (!config || !tiles || !state.data_->binding) return false;
    const auto &saved    = *state.data_->binding;
    const auto  topology = impl_->grid.topologyManual
                               ? impl_->grid.topology
                               : topologyFromOrientation(config->orientation, impl_->grid.diagonal);
    return tiles->revision == saved.layerRevision && config->mapW == saved.width && config->mapH == saved.height &&
           topology == saved.topology && (config->staggerAxis == StaggerAxis::Y) == saved.staggerAxisY &&
           (config->staggerIndex == StaggerIndex::Odd) == saved.staggerOdd;
}

Pathfinder::Pathfinder() : impl_(std::make_unique<Impl>()) {}

Pathfinder::Pathfinder(TileLayer *layer) : Pathfinder() { bindLayer(layer); }

Pathfinder::Pathfinder(int width, int height) : Pathfinder() { setSize(width, height); }

Pathfinder::~Pathfinder() = default;
Pathfinder::Pathfinder(Pathfinder &&) noexcept = default;
Pathfinder &Pathfinder::operator=(Pathfinder &&) noexcept = default;

NavigationChangeStamp Pathfinder::getNavigationChangeStamp() const noexcept {
    auto *layer = impl_->grid.layer;
    return {impl_->changes, layer ? layer->tiles()->revision : 0};
}

void Pathfinder::bindLayer(TileLayer *layer) {
    invalidateCache();
    impl_->grid.bindLayer(layer);
}

void Pathfinder::setSize(int width, int height) {
    invalidateCache();
    impl_->grid.clearLayer();
    impl_->grid.resize(width, height);
}

void Pathfinder::setTopology(const std::string &name) {
    invalidateCache();
    impl_->grid.setTopology(name);
}

std::string Pathfinder::getTopology() const { return topologyName(impl_->grid.topology); }

void Pathfinder::setDiagonal(bool enable) {
    invalidateCache();
    impl_->grid.setDiagonal(enable);
}

bool Pathfinder::getDiagonal() const { return impl_->grid.diagonal; }

void Pathfinder::blockGid(int gid) {
    invalidateCache();
    impl_->grid.blockGid(gid);
}

void Pathfinder::unblockGid(int gid) {
    invalidateCache();
    impl_->grid.unblockGid(gid);
}

void Pathfinder::clearBlockedGids() {
    invalidateCache();
    impl_->grid.clearBlockedGids();
}

void Pathfinder::setBlockEmpty(bool enable) {
    invalidateCache();
    impl_->grid.setBlockEmpty(enable);
}

bool Pathfinder::getBlockEmpty() const { return impl_->grid.blockEmpty; }

void Pathfinder::setBlocked(int x, int y, bool blocked) {
    invalidateCache();
    impl_->grid.setBlocked(x, y, blocked);
}

bool Pathfinder::isWalkable(int x, int y) const { return impl_->grid.isWalkable(x, y); }

void Pathfinder::setCellCost(int x, int y, float cost) {
    invalidateCache();
    impl_->grid.setCellCost(x, y, cost);
}

float Pathfinder::getCellCost(int x, int y) const { return impl_->grid.getCellCost(x, y); }

void Pathfinder::syncFromLayer() {
    invalidateCache();
    impl_->grid.syncFromLayer();
}

void Pathfinder::invalidateCache() {
    if (impl_->changes == std::numeric_limits<std::uint64_t>::max())
        throw eve::Exception("Navigation input revision exhausted");
    ++impl_->changes;
    impl_->restoredFrom.reset();
    impl_->hasCachedField = false;
    impl_->cachedField.clear();
}

namespace {

bool ensureSynced(Grid &grid) {
    if (grid.layer && (grid.dirty || grid.layerRevision != grid.layer->tiles()->revision))
        grid.syncFromLayer();
    return grid.width > 0 && grid.height > 0;
}

FlowField *buildFlowFieldUncached(Grid &grid, int gx, int gy) {
    auto *field = new FlowField();
    if (!ensureSynced(grid) || !grid.isWalkable(gx, gy)) {
        field->resize(grid.width, grid.height);
        field->setGoal(gx, gy);
        return field;
    }

    const int w = grid.width;
    const int h = grid.height;
    field->resize(w, h);
    field->setGoal(gx, gy);

    const auto idx = [w](int x, int y) { return y * w + x; };
    std::vector<float> dist(size_t(w * h), kInf);
    std::priority_queue<HeapNode, std::vector<HeapNode>, std::greater<HeapNode>> open;

    dist[size_t(idx(gx, gy))] = 0.f;
    field->setCost(gx, gy, 0.f);
    field->setNext(gx, gy, gx, gy);
    open.push(HeapNode{0.f, idx(gx, gy)});

    while (!open.empty()) {
        const HeapNode cur = open.top();
        open.pop();
        if (cur.f > dist[size_t(cur.idx)]) continue;
        const int cx = cur.idx % w;
        const int cy = cur.idx / w;
        const float enterCur = grid.getCellCost(cx, cy);
        forEachNeighbor(grid.topology, cx, cy, grid.staggerAxisY, grid.staggerOdd,
                        [&](int nx, int ny, float moveCost) {
                            if (!grid.isWalkable(nx, ny)) return;
                            if (grid.topology == Topology::Ortho8) {
                                const int dx = nx - cx;
                                const int dy = ny - cy;
                                if (dx != 0 && dy != 0) {
                                    if (!grid.isWalkable(cx + dx, cy) || !grid.isWalkable(cx, cy + dy))
                                        return;
                                }
                            }
                            const int ni = idx(nx, ny);
                            const float tentative = dist[size_t(cur.idx)] + moveCost * enterCur;
                            if (tentative + 1e-6f >= dist[size_t(ni)]) return;
                            dist[size_t(ni)] = tentative;
                            field->setCost(nx, ny, tentative);
                            field->setNext(nx, ny, cx, cy);
                            open.push(HeapNode{tentative, ni});
                        });
    }
    return field;
}

}  // namespace

Result<NavigationGridSnapshot> Pathfinder::captureNavigationGrid() try {
    if (!impl_)
        return Result<NavigationGridSnapshot>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Moved-from Pathfinder", "navigationGrid"));
    Grid staged = impl_->grid;
    if (!ensureSynced(staged) || std::int64_t(staged.width) * staged.height > 4 * 1024 * 1024)
        return Result<NavigationGridSnapshot>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Unconfigured or oversized navigation grid", "navigationGrid"));
    const auto count = static_cast<std::size_t>(std::int64_t(staged.width) * staged.height);
    if (staged.cost.size() != count || staged.enterMask.size() != count || staged.exitMask.size() != count)
        return Result<NavigationGridSnapshot>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "Inconsistent navigation grid buffers", "navigationGrid"));
    for (const float cost : staged.cost)
        if (!std::isfinite(cost))
            return Result<NavigationGridSnapshot>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "Nonfinite map navigation cost", "navigationGrid"));
    const auto  topology = static_cast<NavigationTopology>(staged.topology);
    const bool  axisY    = topology != NavigationTopology::Hex || staged.staggerAxisY;
    const bool  odd      = topology != NavigationTopology::Hex || staged.staggerOdd;
    const auto &previous = impl_->lastCapture.data_;
    if (previous && previous->width == staged.width && previous->height == staged.height &&
        previous->topology == topology && previous->staggerAxisY == axisY && previous->staggerOdd == odd &&
        previous->costs == staged.cost && previous->enterMasks == staged.enterMask &&
        previous->exitMasks == staged.exitMask)
        return Result<NavigationGridSnapshot>::success(impl_->lastCapture, Status::success(StatusCode::NoOp));
    if (previous && previous->revision == std::numeric_limits<std::uint64_t>::max())
        return Result<NavigationGridSnapshot>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Navigation capture revision exhausted", "navigationGrid"));
    auto data          = std::make_shared<NavigationGridSnapshot::Data>();
    data->width        = staged.width;
    data->height       = staged.height;
    data->topology     = topology;
    data->revision     = previous ? previous->revision + 1 : 1;
    data->staggerAxisY = axisY;
    data->staggerOdd   = odd;
    data->costs        = std::move(staged.cost);
    data->enterMasks   = std::move(staged.enterMask);
    data->exitMasks    = std::move(staged.exitMask);
    NavigationGridSnapshot result;
    result.data_       = std::move(data);
    impl_->restoredFrom.reset();
    impl_->lastCapture = result;
    return Result<NavigationGridSnapshot>::success(std::move(result));
} catch (const std::exception &error) {
    return Result<NavigationGridSnapshot>::failure(
        Diagnostic::error(DiagnosticCode::Failed, error.what(), "navigationGrid"));
}


Path *Pathfinder::findPath(int sx, int sy, int gx, int gy) {
    return findPath(sx, sy, gx, gy, {});
}

namespace {

struct CellHash {
    std::size_t operator()(const std::pair<int, int> &cell) const noexcept {
        return (static_cast<std::size_t>(static_cast<std::uint32_t>(cell.first)) << 32) ^
               static_cast<std::uint32_t>(cell.second);
    }
};

Result<bool> keepsPathOpenWithBlocks(Pathfinder &finder, Grid &grid, int sx, int sy, int gx, int gy,
                                     const std::unordered_set<std::pair<int, int>, CellHash> &blocks) {
    if (!ensureSynced(grid) || grid.width <= 0 || grid.height <= 0) {
        return Result<bool>::failure(eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                                            "pathfinder grid is empty", "pathfinder.grid", {}, "map"));
    }
    for (const auto &cell : blocks) {
        if (cell.first < 0 || cell.second < 0 || cell.first >= grid.width || cell.second >= grid.height) {
            return Result<bool>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                "temporary block cell is out of bounds",
                                                                "pathfinder.block", {}, "map"));
        }
        if ((cell.first == sx && cell.second == sy) || (cell.first == gx && cell.second == gy))
            return Result<bool>::success(false);
    }
    if (!grid.isWalkable(sx, sy) || !grid.isWalkable(gx, gy)) return Result<bool>::success(false);
    std::unique_ptr<Path> path(finder.findPath(sx, sy, gx, gy, [&blocks](int x, int y) {
        return blocks.count({x, y}) ? kInf : 0.f;
    }));
    return Result<bool>::success(path && path->getLength() > 0);
}

}  // namespace

Result<bool> Pathfinder::keepsPathOpen(int sx, int sy, int gx, int gy, int blockX, int blockY) {
    std::unordered_set<std::pair<int, int>, CellHash> blocks;
    blocks.emplace(blockX, blockY);
    return keepsPathOpenWithBlocks(*this, impl_->grid, sx, sy, gx, gy, blocks);
}

Result<bool> Pathfinder::keepsPathOpenFootprint(int sx, int sy, int gx, int gy, int originX, int originY,
                                                int footprintW, int footprintH) {
    if (footprintW <= 0 || footprintH <= 0) {
        return Result<bool>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                            "footprint size must be positive",
                                                            "pathfinder.footprint", {}, "map"));
    }
    std::unordered_set<std::pair<int, int>, CellHash> blocks;
    blocks.reserve(static_cast<std::size_t>(footprintW) * static_cast<std::size_t>(footprintH));
    for (int y = 0; y < footprintH; ++y)
        for (int x = 0; x < footprintW; ++x) blocks.emplace(originX + x, originY + y);
    return keepsPathOpenWithBlocks(*this, impl_->grid, sx, sy, gx, gy, blocks);
}

Path *Pathfinder::findPath(int sx, int sy, int gx, int gy,
                           const std::function<float(int, int)> &entryPenalty) {
    impl_->restoredFrom.reset();
    auto *path = new Path();
    Grid &grid = impl_->grid;
    if (!ensureSynced(grid)) return path;
    if (!grid.isWalkable(sx, sy) || !grid.isWalkable(gx, gy)) return path;
    if (sx == gx && sy == gy) {
        path->add(sx, sy);
        path->setTotalCost(0.f);
        return path;
    }

    const int w = grid.width;
    const int n = w * grid.height;
    const auto idx = [w](int x, int y) { return y * w + x; };

    std::vector<float> gScore(size_t(n), kInf);
    std::vector<int> parent(size_t(n), -1);
    std::vector<uint8_t> closed(size_t(n), 0);

    std::priority_queue<HeapNode, std::vector<HeapNode>, std::greater<HeapNode>> open;
    const int start = idx(sx, sy);
    const int goal = idx(gx, gy);
    gScore[size_t(start)] = 0.f;
    open.push(HeapNode{heuristic(grid.topology, sx, sy, gx, gy), start});

    bool found = false;
    while (!open.empty()) {
        const HeapNode cur = open.top();
        open.pop();
        if (closed[size_t(cur.idx)]) continue;
        closed[size_t(cur.idx)] = 1;
        if (cur.idx == goal) {
            found = true;
            break;
        }
        const int cx = cur.idx % w;
        const int cy = cur.idx / w;
        grid.forEachWalkableNeighbor(cx, cy, [&](int nx, int ny, float edgeCost) {
            const int ni = idx(nx, ny);
            if (closed[size_t(ni)]) return;
            float penalty = entryPenalty ? entryPenalty(nx, ny) : 0.0f;
            if (!std::isfinite(penalty)) return;
            penalty = std::max(0.0f, penalty);
            const float tentative = gScore[size_t(cur.idx)] + edgeCost + penalty;
            if (tentative >= gScore[size_t(ni)]) return;
            gScore[size_t(ni)] = tentative;
            parent[size_t(ni)] = cur.idx;
            open.push(HeapNode{tentative + heuristic(grid.topology, nx, ny, gx, gy), ni});
        });
    }

    if (!found) return path;

    int cur = goal;
    while (cur != -1) {
        path->add(cur % w, cur / w);
        if (cur == start) break;
        cur = parent[size_t(cur)];
    }
    path->reverse();
    path->setTotalCost(gScore[size_t(goal)]);
    return path;
}

FlowField *Pathfinder::buildFlowField(int gx, int gy) {
    impl_->restoredFrom.reset();
    Grid &grid = impl_->grid;
    if (!ensureSynced(grid)) {
        auto *empty = new FlowField();
        empty->resize(0, 0);
        empty->setGoal(gx, gy);
        return empty;
    }

    if (impl_->hasCachedField && !grid.dirty && impl_->cachedGoalX == gx && impl_->cachedGoalY == gy &&
        impl_->cachedField.getWidth() == grid.width &&
        impl_->cachedField.getHeight() == grid.height) {
        auto *copy = new FlowField();
        *copy = impl_->cachedField;
        return copy;
    }

    FlowField *field = buildFlowFieldUncached(grid, gx, gy);
    impl_->cachedField = *field;
    impl_->cachedGoalX = gx;
    impl_->cachedGoalY = gy;
    impl_->hasCachedField = true;
    grid.dirty = false;
    return field;
}

Path *Pathfinder::followFlow(FlowField *field, int sx, int sy) {
    impl_->restoredFrom.reset();
    auto *path = new Path();
    Grid &grid = impl_->grid;
    if (!field || !ensureSynced(grid)) return path;
    if (!field->isReachable(sx, sy)) return path;

    const int gx = field->getGoalX();
    const int gy = field->getGoalY();
    const int maxSteps = grid.width * grid.height + 2;
    int x = sx;
    int y = sy;
    path->add(x, y);
    float total = 0.f;
    for (int step = 0; step < maxSteps; ++step) {
        if (x == gx && y == gy) break;
        const int nx = field->nextX(x, y);
        const int ny = field->nextY(x, y);
        if (nx == x && ny == y) break;
        const float c0 = field->costAt(x, y);
        const float c1 = field->costAt(nx, ny);
        if (c0 < kInf && c1 < kInf && c0 >= c1) total += (c0 - c1);
        x = nx;
        y = ny;
        path->add(x, y);
    }
    path->setTotalCost(total);
    if (path->getLength() > 0) {
        const int lx = path->getX(path->getLength() - 1);
        const int ly = path->getY(path->getLength() - 1);
        if (lx != gx || ly != gy) path->clear();
    }
    return path;
}

Path *Pathfinder::findGroupPath(int sx, int sy, int gx, int gy) {
    impl_->restoredFrom.reset();
    Grid &grid = impl_->grid;
    if (!ensureSynced(grid)) return new Path();
    if (!impl_->hasCachedField || grid.dirty || impl_->cachedGoalX != gx || impl_->cachedGoalY != gy ||
        impl_->cachedField.getWidth() != grid.width ||
        impl_->cachedField.getHeight() != grid.height) {
        FlowField *tmp = buildFlowFieldUncached(grid, gx, gy);
        impl_->cachedField = *tmp;
        delete tmp;
        impl_->cachedGoalX = gx;
        impl_->cachedGoalY = gy;
        impl_->hasCachedField = true;
        grid.dirty = false;
    }
    return followFlow(&impl_->cachedField, sx, sy);
}

// --- Path / FlowField (same TU; keep export surface small on Windows) ---

void Path::clear() {
    cells_.clear();
    totalCost_ = 0.f;
}

void Path::add(int x, int y) { cells_.push_back(Cell{x, y}); }

void Path::reverse() {
    for (size_t i = 0, j = cells_.size(); i + 1 < j; ++i, --j) std::swap(cells_[i], cells_[j - 1]);
}

int Path::getLength() const { return int(cells_.size()); }

int Path::getX(int index) const {
    if (index < 0 || index >= int(cells_.size())) return 0;
    return cells_[size_t(index)].x;
}

int Path::getY(int index) const {
    if (index < 0 || index >= int(cells_.size())) return 0;
    return cells_[size_t(index)].y;
}

float Path::getTotalCost() const { return totalCost_; }

void Path::setTotalCost(float cost) { totalCost_ = cost; }

void FlowField::clear() {
    width_ = height_ = 0;
    goalX_ = goalY_ = 0;
    cost_.clear();
    nextX_.clear();
    nextY_.clear();
}

void FlowField::resize(int width, int height) {
    width_ = width > 0 ? width : 0;
    height_ = height > 0 ? height : 0;
    const int n = width_ * height_;
    cost_.assign(size_t(n), kUnreachable);
    nextX_.assign(size_t(n), 0);
    nextY_.assign(size_t(n), 0);
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const int i = index(x, y);
            nextX_[size_t(i)] = x;
            nextY_[size_t(i)] = y;
        }
    }
}

void FlowField::setGoal(int x, int y) {
    goalX_ = x;
    goalY_ = y;
}

bool FlowField::inBounds(int x, int y) const {
    return x >= 0 && y >= 0 && x < width_ && y < height_;
}

float FlowField::costAt(int x, int y) const {
    if (!inBounds(x, y)) return kUnreachable;
    return cost_[size_t(index(x, y))];
}

void FlowField::setCost(int x, int y, float cost) {
    if (!inBounds(x, y)) return;
    cost_[size_t(index(x, y))] = cost;
}

int FlowField::nextX(int x, int y) const {
    if (!inBounds(x, y)) return x;
    return nextX_[size_t(index(x, y))];
}

int FlowField::nextY(int x, int y) const {
    if (!inBounds(x, y)) return y;
    return nextY_[size_t(index(x, y))];
}

void FlowField::setNext(int x, int y, int nx, int ny) {
    if (!inBounds(x, y)) return;
    const int i = index(x, y);
    nextX_[size_t(i)] = nx;
    nextY_[size_t(i)] = ny;
}

bool FlowField::isReachable(int x, int y) const {
    const float c = costAt(x, y);
    return c < kUnreachable && c >= 0.f;
}

}  // namespace eve::map
