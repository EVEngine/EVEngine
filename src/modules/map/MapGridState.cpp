#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <tuple>
#include "map/Map.h"
#include "map/MapGridStateInternal.inl"

namespace eve::map {
namespace {
constexpr std::int64_t maxCells = 4 * 1024 * 1024;
template <class T>
const T* existing(TileLayer& layer) noexcept {
    auto* buffer = layer.getComponentManager().template getComponentBuffer<T>();
    return buffer && buffer->has(layer.id) ? buffer->ptr(layer.id) : nullptr;
}
struct Invalid : std::runtime_error {
    using runtime_error::runtime_error;
};
const Value::Array& array(const Value& v, std::size_t limit = 1000000) {
    auto* a = v.getIf<Value::Array>();
    if (!a || a->size() > limit) throw Invalid("Invalid or oversized grid array");
    return *a;
}
const Value::Array& tuple(const Value& v, std::size_t size) {
    const auto& a = array(v);
    if (a.size() != size) throw Invalid("Grid tuple shape mismatch");
    return a;
}
std::int64_t integer(const Value& v, std::int64_t lo, std::int64_t hi) {
    if (!v.isInt64() || v.asInt() < lo || v.asInt() > hi) throw Invalid("Grid integer out of range");
    return v.asInt();
}
float number(const Value& v) {
    if (!v.isInt64() && !v.isDouble()) throw Invalid("Expected grid number");
    double n = v.isInt64() ? static_cast<double>(v.asInt()) : v.asDouble();
    if (!std::isfinite(n) || std::abs(n) > std::numeric_limits<float>::max()) throw Invalid("Invalid grid number");
    return static_cast<float>(n);
}
std::string text(const Value& v) {
    auto* s = v.getIf<std::string>();
    if (!s) throw Invalid("Expected grid text");
    return *s;
}
bool boolean(const Value& v) {
    if (!v.isBool()) throw Invalid("Expected grid boolean");
    return *v.getIf<bool>();
}
std::uint64_t counter(const Value& v) {
    auto          s = text(v);
    std::uint64_t n = 0;
    auto          r = std::from_chars(s.data(), s.data() + s.size(), n);
    if (r.ec != std::errc{} || r.ptr != s.data() + s.size() || !n || std::to_string(n) != s)
        throw Invalid("Invalid grid revision");
    return n;
}
LogicalId schema() { return *LogicalId::fromParts("map", "grid-runtime"); }
template <class T>
Result<T> invalid(const Invalid& e) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::ParseError, e.what(), "map.grid-state"));
}
// One codec defines the accepted facts for both captured and externally parsed states.
template <class State>
Value encode(const State& state) {
    Value::Array layers;
    for (const auto& l : state.layers) {
        const auto&  c = l.config;
        Value::Array gids, terrain, profiles, rules, families, custom;
        for (auto g : l.gids) gids.emplace_back(static_cast<std::int64_t>(g));
        for (auto t : l.terrain) terrain.emplace_back(t);
        for (const auto& p : l.profiles)
            profiles.push_back(Value::Array{p.gid, p.footprintW, p.footprintH, p.walkable, p.opaque, p.cost,
                                            int(p.enter), int(p.exit), static_cast<std::int64_t>(p.flags)});
        for (const auto& r : l.rules) rules.push_back(Value::Array{r.gid, r.terrain, r.neighborMask, r.weight});
        for (const auto& f : l.families)
            families.push_back(Value::Array{f.terrain, f.kind, static_cast<std::int64_t>(f.seed)});
        for (const auto& d : l.custom) custom.push_back(Value::Array{d.gid, d.name, d.type, d.value});
        layers.push_back(Value::Array{
            l.path,
            Value::Array{c.mapW, c.mapH, c.tileW, c.tileH, c.cellGapX, c.cellGapY, c.originX, c.originY,
                         int(c.orientation), int(c.staggerAxis), int(c.staggerIndex), c.hexSideLength},
            std::to_string(l.revision), Value(std::move(gids)), Value(std::move(terrain)), Value(std::move(profiles)),
            Value(std::move(rules)), Value(std::move(families)), Value(std::move(custom))});
    }
    return Value::object({{"layers", Value(std::move(layers))}});
}
template <class State>
void decode(const Value& payload, State& state) {
    const auto* obj = payload.getIf<Value::Object>();
    if (!obj || obj->size() != 1 || !obj->contains("layers")) throw Invalid("Unknown grid payload fields");
    const auto& layers = array(obj->at("layers"), 4096);
    if (layers.empty()) throw Invalid("Empty map grid state");
    for (const auto& row : layers) {
        const auto&           a = tuple(row, 9);
        typename State::Layer l;
        l.path           = text(a[0]);
        l.revision       = counter(a[2]);
        auto&       c    = l.config;
        const auto& g    = tuple(a[1], 12);
        c.mapW           = static_cast<int>(integer(g[0], 0, maxCells));
        c.mapH           = static_cast<int>(integer(g[1], 0, maxCells));
        const auto count = std::int64_t(c.mapW) * c.mapH;
        if (count > maxCells) throw Invalid("Grid cell limit exceeded");
        c.tileW         = number(g[2]);
        c.tileH         = number(g[3]);
        c.cellGapX      = number(g[4]);
        c.cellGapY      = number(g[5]);
        c.originX       = number(g[6]);
        c.originY       = number(g[7]);
        c.orientation   = static_cast<MapOrientation>(integer(g[8], 0, 3));
        c.staggerAxis   = static_cast<StaggerAxis>(integer(g[9], 0, 1));
        c.staggerIndex  = static_cast<StaggerIndex>(integer(g[10], 0, 1));
        c.hexSideLength = number(g[11]);
        if (c.tileW <= 0 || c.tileH <= 0 || c.hexSideLength < 0) throw Invalid("Invalid tile geometry");
        const auto& cells   = array(a[3], maxCells);
        const auto& terrain = array(a[4], maxCells);
        if (cells.size() != static_cast<std::size_t>(count) || terrain.size() != cells.size())
            throw Invalid("Grid cell shape mismatch");
        for (const auto& v : cells) l.gids.push_back(static_cast<std::uint32_t>(integer(v, 0, UINT32_MAX)));
        for (const auto& v : terrain) l.terrain.push_back(static_cast<int>(integer(v, INT32_MIN, INT32_MAX)));
        std::set<int> profileKeys, familyKeys;
        for (const auto& v : array(a[5])) {
            const auto&             p = tuple(v, 9);
            typename State::Profile r;
            r.gid        = static_cast<int>(integer(p[0], 1, INT32_MAX));
            r.footprintW = static_cast<int>(integer(p[1], 1, INT32_MAX));
            r.footprintH = static_cast<int>(integer(p[2], 1, INT32_MAX));
            r.walkable   = boolean(p[3]);
            r.opaque     = boolean(p[4]);
            r.cost       = number(p[5]);
            r.enter      = static_cast<std::uint8_t>(integer(p[6], 0, 255));
            r.exit       = static_cast<std::uint8_t>(integer(p[7], 0, 255));
            r.flags      = static_cast<std::uint32_t>(integer(p[8], 0, UINT32_MAX));
            if (r.cost <= 0 || !profileKeys.insert(r.gid).second)
                throw Invalid("Invalid or duplicate navigation profile");
            l.profiles.push_back(r);
        }
        std::set<std::tuple<int, int, int>> ruleKeys;
        for (const auto& v : array(a[6])) {
            const auto&                     r = tuple(v, 4);
            TileLayer::Tileset::TerrainRule t{int(integer(r[0], 1, INT32_MAX)), int(integer(r[1], 0, INT32_MAX)),
                                              int(integer(r[2], 0, 255)), int(integer(r[3], 1, INT32_MAX))};
            if (!ruleKeys.emplace(t.gid, t.terrain, t.neighborMask).second) throw Invalid("Duplicate terrain rule");
            l.rules.push_back(t);
        }
        for (const auto& v : array(a[7])) {
            const auto&                       f = tuple(v, 3);
            TileLayer::Tileset::TerrainFamily t{int(integer(f[0], 0, INT32_MAX)), text(f[1]),
                                                static_cast<std::uint32_t>(integer(f[2], 0, UINT32_MAX))};
            if ((t.kind != "terrain" && t.kind != "shore" && t.kind != "wall" && t.kind != "waterfall") ||
                !familyKeys.insert(t.terrain).second)
                throw Invalid("Invalid terrain family");
            l.families.push_back(std::move(t));
        }
        std::set<std::pair<int, std::string>> customKeys;
        for (const auto& v : array(a[8])) {
            const auto&                    d = tuple(v, 4);
            TileLayer::Tileset::CustomData t{int(integer(d[0], 1, INT32_MAX)), text(d[1]), text(d[2]), text(d[3])};
            if (t.name.empty() || !customKeys.emplace(t.gid, t.name).second) throw Invalid("Invalid custom tile data");
            l.custom.push_back(std::move(t));
        }
        state.layers.push_back(std::move(l));
    }
}
}  // namespace
MapGridState::MapGridState(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
MapGridState::~MapGridState() = default;
std::size_t                                 MapGridState::layerCount() const noexcept { return impl_->layers.size(); }
Result<std::shared_ptr<const MapGridState>> MapGridState::capture(Map& source) {
    using R = Result<std::shared_ptr<const MapGridState>>;
    try {
        Impl captured;
        if (source.loadedLayers_.empty() || source.loadedLayers_.size() > 4096)
            throw Invalid("Invalid map layer count");
        for (auto* layer : source.loadedLayers_) {
            if (!layer) throw Invalid("Missing map layer");
            const auto* resource = existing<TileLayer::Resource>(*layer);
            const auto* config   = existing<TileLayer::Config>(*layer);
            const auto* tiles    = existing<TileLayer::Tiles>(*layer);
            const auto* tileset  = existing<TileLayer::Tileset>(*layer);
            if (!resource || !config || !tiles || !tileset) throw Invalid("Map grid components are missing");
            Impl::Layer l;
            l.path          = resource->path;
            l.config        = *config;
            l.config.entity = nullptr;
            l.gids          = tiles->gids;
            l.terrain       = tiles->terrainIds;
            l.revision      = tiles->revision;
            for (const auto& v : tileset->visuals)
                l.profiles.push_back({v.gid, v.footprintW, v.footprintH, v.walkable, v.opaque, v.cost, v.enterMask,
                                      v.exitMask, v.semanticFlags});
            std::sort(l.profiles.begin(), l.profiles.end(), [](const auto& a, const auto& b) { return a.gid < b.gid; });
            l.rules    = tileset->terrainRules;
            l.families = tileset->terrainFamilies;
            l.custom   = tileset->customData;
            captured.layers.push_back(std::move(l));
        }
        auto admitted = std::make_unique<Impl>();
        decode(encode(captured), *admitted);
        return R::success(std::shared_ptr<const MapGridState>(new const MapGridState(std::move(admitted))));
    } catch (const Invalid& e) {
        return invalid<std::shared_ptr<const MapGridState>>(e);
    }
}
Result<SnapshotEnvelope> MapGridState::seal(PersistentId epoch, Revision revision, SimulationTick tick,
                                            const SnapshotHashProvider& hash) const {
    if (epoch.isNil() || revision.isZero())
        return Result<SnapshotEnvelope>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Grid state requires epoch/revision"));
    return makeSnapshotEnvelope("eve.map-grid-runtime", schema(), SchemaVersion(1), epoch, revision, tick,
                                encode(*impl_), hash);
}
Result<std::shared_ptr<const MapGridState>> MapGridState::parse(const SnapshotEnvelope& input, PersistentId epoch,
                                                                const SnapshotHashProvider& hash) {
    using R       = Result<std::shared_ptr<const MapGridState>>;
    auto snapshot = input;
    auto verified = verifySnapshotEnvelope(snapshot, hash);
    if (!verified) return R::failure(verified.status());
    if (snapshot.schemaVersion != SchemaVersion(1))
        return R::failure(Diagnostic::error(DiagnosticCode::UnknownVersion, "Unknown grid version"));
    if (snapshot.type != "eve.map-grid-runtime" || snapshot.schema != schema() || epoch.isNil() ||
        snapshot.instanceId != epoch || snapshot.revision.isZero())
        return R::failure(Diagnostic::error(DiagnosticCode::Conflict, "Grid participant boundary mismatch"));
    try {
        auto impl = std::make_unique<Impl>();
        decode(snapshot.payload, *impl);
        return R::success(std::shared_ptr<const MapGridState>(new const MapGridState(std::move(impl))));
    } catch (const Invalid& e) {
        return invalid<std::shared_ptr<const MapGridState>>(e);
    }
}
}  // namespace eve::map
