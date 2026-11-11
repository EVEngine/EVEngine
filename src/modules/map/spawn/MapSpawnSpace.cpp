// eve-hot-path
// map navigation planning and final publication checks.
#include "map/spawn/MapSpawnSpace.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include "map/Pathfinder.h"
#include "map/PreparedMapLoad.h"
#include "spawn/SpawnRandom.h"
#include "spawn/SpawnRegion.h"
#include "spawn/SpawnRestoreState.h"

namespace eve::map_spawn {
namespace {
template <class T>
Result<T> fail(DiagnosticCode code, std::string message) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), "map_spawn.space"));
}
bool   finite(spawn::Point p) { return std::isfinite(p.x) && std::isfinite(p.y); }
double clearance(const spawn::PlacementSpec& spec) {
    double radius = 0;
    for (const auto& c : spec.constraints)
        if (c.kind == spawn::ConstraintKind::Clearance) radius = std::max(radius, c.distance);
    return radius;
}
bool samePoint(spawn::Point a, spawn::Point b) { return a.x == b.x && a.y == b.y; }
bool sameRegion(const spawn::Region& a, const spawn::Region& b) {
    if (a.index() != b.index()) return false;
    if (const auto* circle = std::get_if<spawn::CircleRegion>(&a)) {
        const auto& other = std::get<spawn::CircleRegion>(b);
        return samePoint(circle->center, other.center) && circle->radius == other.radius;
    }
    const auto& points  = std::holds_alternative<spawn::PointRegion>(a) ? std::get<spawn::PointRegion>(a).points
                                                                        : std::get<spawn::PolygonRegion>(a).vertices;
    const auto& targets = std::holds_alternative<spawn::PointRegion>(b) ? std::get<spawn::PointRegion>(b).points
                                                                        : std::get<spawn::PolygonRegion>(b).vertices;
    return points.size() == targets.size() && std::equal(points.begin(), points.end(), targets.begin(), samePoint);
}
bool sameSpec(const spawn::PlacementSpec& a, const spawn::PlacementSpec& b) {
    return a.attempts == b.attempts && a.constraints.size() == b.constraints.size() &&
           std::equal(a.constraints.begin(), a.constraints.end(), b.constraints.begin(),
                      [](const auto& x, const auto& y) { return x.kind == y.kind && x.distance == y.distance; });
}
}  // namespace
struct MapSpawnSpace::Impl {
    struct Binding {
        std::shared_ptr<const spawn::CompiledMap> map;
        std::size_t                               index = 0;
        std::optional<spawn::SavedSpawnerKey>     saved;
    };
    std::vector<Binding>                    bindings;
    PersistentId                            owner;
    SpawnGridTransform                      transform;
    std::shared_ptr<spawn::ISpawnOccupancy> occupancy;
    map::NavigationGridSnapshot             navigation;
    std::optional<map::MapLease>            mapOwner;
    std::vector<spawn::Point>               players;
    bool                                    playersPresent = false;
    PlacementTraceMode                      traceMode      = PlacementTraceMode::Disabled;
    std::array<PlacementAttempt, 256>       trace;
    std::size_t                             traceHead = 0, traceSize = 0;
    std::uint64_t                           traceDropped = 0;
    void record(const spawn::BatchRequest& request, std::size_t unit, std::optional<spawn::Point> point,
                std::uint32_t attempt, PlacementStage stage, PlacementDecision decision) noexcept {
        if (traceMode != PlacementTraceMode::RecentAttempts) return;
        const auto row = PlacementAttempt{
            request.source, request.operation, request.units[unit].subject, point, attempt, stage, decision};
        trace[(traceHead + traceSize) % trace.size()] = row;
        if (traceSize < trace.size())
            ++traceSize;
        else {
            traceHead = (traceHead + 1) % trace.size();
            if (traceDropped != std::numeric_limits<std::uint64_t>::max()) ++traceDropped;
        }
    }
    Impl(PersistentId id, SpawnGridTransform conversion, std::shared_ptr<spawn::ISpawnOccupancy> source)
        : owner(id), transform(conversion), occupancy(std::move(source)) {}
    Result<std::size_t> instance(const spawn::BatchRequest& request) const {
        if (mapOwner && !mapOwner->isCurrent())
            return fail<std::size_t>(DiagnosticCode::StaleHandle, "Map owner destroyed");
        if (!request.lifetime.isCurrent() || request.lifetime.source() != request.source)
            return fail<std::size_t>(DiagnosticCode::StaleHandle, "Spatial source lifetime revoked");
        if (request.source.owner != owner || request.source.slot.index() >= bindings.size() ||
            !bindings[request.source.slot.index()].map)
            return fail<std::size_t>(DiagnosticCode::Conflict, "Spatial source does not belong to this map");
        const auto  index   = request.source.slot.index();
        const auto& binding = bindings[index];
        if (binding.saved && spawn::savedSpawnerKey(request.source) != *binding.saved)
            return fail<std::size_t>(DiagnosticCode::Conflict, "Saved spatial source address mismatch");
        if (request.lifetime.instanceId() != binding.map->instances()[binding.index].id)
            return fail<std::size_t>(DiagnosticCode::Conflict, "Spatial instance binding mismatch");
        return Result<std::size_t>::success(index);
    }
    bool terrain(spawn::Point p, double radius) const {
        const auto   o = transform.origin, c = transform.cellSize;
        const double right = o.x + c.x * navigation.width(), bottom = o.y + c.y * navigation.height();
        if (p.x - radius < o.x || p.y - radius < o.y || p.x + radius >= right || p.y + radius >= bottom) return false;
        const auto xmin =
            std::clamp(static_cast<int>(std::floor((p.x - radius - o.x) / c.x)), 0, navigation.width() - 1);
        const auto xmax =
            std::clamp(static_cast<int>(std::floor((p.x + radius - o.x) / c.x)), 0, navigation.width() - 1);
        const auto ymin =
            std::clamp(static_cast<int>(std::floor((p.y - radius - o.y) / c.y)), 0, navigation.height() - 1);
        const auto ymax =
            std::clamp(static_cast<int>(std::floor((p.y + radius - o.y) / c.y)), 0, navigation.height() - 1);
        for (int y = ymin; y <= ymax; ++y)
            for (int x = xmin; x <= xmax; ++x) {
                if (navigation.costs()[static_cast<std::size_t>(y) * navigation.width() + x] > 0) continue;
                const double left = o.x + x * c.x, top = o.y + y * c.y;
                const double dx = std::max({left - p.x, 0.0, p.x - left - c.x});
                const double dy = std::max({top - p.y, 0.0, p.y - top - c.y});
                if (std::hypot(dx, dy) <= radius) return false;
            }
        return true;
    }
    PlacementDecision decision(spawn::Point p, const spawn::PlacementSpec& spec,
                               std::span<const spawn::OccupiedDisk> occupied,
                               std::span<const spawn::Point>        batch) const {
        for (const auto& constraint : spec.constraints) {
            if (constraint.kind == spawn::ConstraintKind::Walkable && !terrain(p, 0))
                return PlacementDecision::Walkability;
            if (constraint.kind == spawn::ConstraintKind::DistanceFromPlayers)
                for (auto player : players)
                    if (std::hypot(p.x - player.x, p.y - player.y) < constraint.distance)
                        return PlacementDecision::PlayerDistance;
            if (constraint.kind == spawn::ConstraintKind::Clearance) {
                if (!terrain(p, constraint.distance)) return PlacementDecision::TerrainClearance;
                for (const auto& disk : occupied)
                    if (std::hypot(p.x - disk.center.x, p.y - disk.center.y) < constraint.distance + disk.radius)
                        return PlacementDecision::Occupied;
                for (auto other : batch)
                    if (std::hypot(p.x - other.x, p.y - other.y) < 2 * constraint.distance)
                        return PlacementDecision::BatchClearance;
            }
        }
        return PlacementDecision::Accepted;
    }
    Result<std::vector<spawn::OccupiedDisk>> capture(const spawn::PlacementSpec& spec) const {
        if (clearance(spec) == 0) return Result<std::vector<spawn::OccupiedDisk>>::success({});
        if (!occupancy)
            return fail<std::vector<spawn::OccupiedDisk>>(DiagnosticCode::NotFound, "Missing occupancy provider");
        auto result = [&]() -> Result<std::vector<spawn::OccupiedDisk>> {
            try {
                return occupancy->capture();
            } catch (const std::exception& error) {
                return fail<std::vector<spawn::OccupiedDisk>>(DiagnosticCode::CallbackFailure, error.what());
            } catch (...) {
                return fail<std::vector<spawn::OccupiedDisk>>(DiagnosticCode::CallbackFailure,
                                                              "Occupancy source threw");
            }
        }();
        if (!result) return result;
        for (const auto& disk : result.value())
            if (!finite(disk.center) || !std::isfinite(disk.radius) || disk.radius < 0 || !disk.subject.isValid())
                return fail<std::vector<spawn::OccupiedDisk>>(DiagnosticCode::InvariantViolation,
                                                              "Invalid domain footprint");
        return result;
    }
};
MapSpawnSpace::MapSpawnSpace(spawn::CompiledMap map, PersistentId owner, SpawnGridTransform transform,
                             std::shared_ptr<spawn::ISpawnOccupancy> occupancy)
    : impl_(std::make_unique<Impl>(owner, transform, std::move(occupancy))) {
    auto                     content = std::make_shared<const spawn::CompiledMap>(std::move(map));
    std::vector<std::size_t> slots(content->instances().size());
    std::iota(slots.begin(), slots.end(), 0);
    std::sort(slots.begin(), slots.end(),
              [&](auto a, auto b) { return content->instances()[a].id < content->instances()[b].id; });
    for (auto index : slots) impl_->bindings.push_back({content, index, {}});
}
MapSpawnSpace::MapSpawnSpace(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Result<std::shared_ptr<MapSpawnSpace>> MapSpawnSpace::prepareRestored(
    const spawn::SpawnRestoreState& state, SpawnGridTransform transform,
    std::shared_ptr<spawn::ISpawnOccupancy> occupancy) {
    if (state.owner().isNil())
        return fail<std::shared_ptr<MapSpawnSpace>>(DiagnosticCode::PreconditionViolation,
                                                    "No admitted placement state");
    const auto bindings = state.placementBindings();
    auto       impl     = std::make_unique<Impl>(state.owner(), transform, std::move(occupancy));
    impl->bindings.resize(state.slotCount());
    for (const auto& binding : bindings)
        impl->bindings[binding.source.slot] = {binding.map, binding.instance, binding.source};
    return Result<std::shared_ptr<MapSpawnSpace>>::success(
        std::shared_ptr<MapSpawnSpace>(new MapSpawnSpace(std::move(impl))));
}
MapSpawnSpace::~MapSpawnSpace() = default;
Result<void> MapSpawnSpace::bindMapLifetime(map::MapLease owner) {
    if (!owner.isCurrent()) return fail<void>(DiagnosticCode::StaleHandle, "Map owner destroyed");
    if (impl_->mapOwner) return fail<void>(DiagnosticCode::PreconditionViolation, "Map owner already bound");
    impl_->mapOwner = std::move(owner);
    return Result<void>::success();
}

Result<void> MapSpawnSpace::setNavigation(map::NavigationGridSnapshot snapshot) {
    if (!snapshot.valid()) return fail<void>(DiagnosticCode::InvalidArgument, "Missing navigation capture");
    if (snapshot.topology() == map::NavigationTopology::Hex)
        return fail<void>(DiagnosticCode::Unsupported, "Hex spawn projection not implemented");
    impl_->navigation = std::move(snapshot);
    return Result<void>::success();
}
Result<std::optional<std::vector<spawn::Point>>> MapSpawnSpace::replacePlayers(std::span<const spawn::Point> players) {
    if (players.size() > 4096 || std::any_of(players.begin(), players.end(), [](auto p) { return !finite(p); }))
        return fail<std::optional<std::vector<spawn::Point>>>(DiagnosticCode::InvalidArgument, "Invalid player frame");
    std::vector<spawn::Point>                staged(players.begin(), players.end());
    std::optional<std::vector<spawn::Point>> previous;
    if (impl_->playersPresent) previous = std::move(impl_->players);
    impl_->players        = std::move(staged);
    impl_->playersPresent = true;
    return Result<std::optional<std::vector<spawn::Point>>>::success(std::move(previous));
}
void MapSpawnSpace::restorePlayers(std::optional<std::vector<spawn::Point>> players) noexcept {
    impl_->playersPresent = players.has_value();
    if (players)
        impl_->players = std::move(*players);
    else
        impl_->players.clear();
}
Result<void> MapSpawnSpace::setPlayers(std::span<const spawn::Point> players) {
    auto replaced = replacePlayers(players);
    if (!replaced) return Result<void>::failure(replaced.status());
    return Result<void>::success();
}
Result<void> MapSpawnSpace::validate(const spawn::PlacementSpec& spec, const spawn::Region&) const {
    if (impl_->mapOwner && !impl_->mapOwner->isCurrent())
        return fail<void>(DiagnosticCode::StaleHandle, "Map owner destroyed");
    const auto& t = impl_->transform;
    if (impl_->owner.isNil() || !finite(t.origin) || !finite(t.cellSize) || t.cellSize.x <= 0 || t.cellSize.y <= 0 ||
        spec.attempts == 0)
        return fail<void>(DiagnosticCode::InvalidArgument, "Invalid spatial configuration");
    for (const auto& c : spec.constraints) {
        if (c.kind != spawn::ConstraintKind::Walkable && c.kind != spawn::ConstraintKind::Clearance &&
            c.kind != spawn::ConstraintKind::DistanceFromPlayers)
            return fail<void>(DiagnosticCode::Unsupported, "Unknown spatial constraint");
        if (!std::isfinite(c.distance) || c.distance < 0)
            return fail<void>(DiagnosticCode::InvalidArgument, "Invalid spatial constraint");
        if (c.kind == spawn::ConstraintKind::DistanceFromPlayers && !impl_->playersPresent)
            return fail<void>(DiagnosticCode::NotFound, "Missing player position frame");
        if ((c.kind == spawn::ConstraintKind::Walkable || c.kind == spawn::ConstraintKind::Clearance) &&
            !impl_->navigation.valid())
            return fail<void>(DiagnosticCode::NotFound, "Missing navigation provider capture");
        if (c.kind == spawn::ConstraintKind::Clearance) {
            if (c.distance <= 0)
                return fail<void>(DiagnosticCode::InvalidArgument, "Clearance radius must be positive");
            if (!impl_->occupancy) return fail<void>(DiagnosticCode::NotFound, "Missing occupancy provider");
            if ((std::ceil(2 * c.distance / t.cellSize.x) + 2) * (std::ceil(2 * c.distance / t.cellSize.y) + 2) > 4096)
                return fail<void>(DiagnosticCode::Unsupported, "Clearance exceeds bounded cell query budget");
        }
    }
    if (impl_->navigation.valid() && (!std::isfinite(t.origin.x + t.cellSize.x * impl_->navigation.width()) ||
                                      !std::isfinite(t.origin.y + t.cellSize.y * impl_->navigation.height())))
        return fail<void>(DiagnosticCode::InvalidArgument, "Grid world bounds overflow");
    return Result<void>::success();
}
void MapSpawnSpace::resetTrace(PlacementTraceMode mode) noexcept {
    impl_->traceMode = mode;
    impl_->traceHead = impl_->traceSize = 0;
    impl_->traceDropped                 = 0;
}
PlacementTrace MapSpawnSpace::captureTrace() const {
    PlacementTrace result;
    result.mode    = impl_->traceMode;
    result.dropped = impl_->traceDropped;
    result.attempts.reserve(impl_->traceSize);
    for (std::size_t i = 0; i < impl_->traceSize; ++i)
        result.attempts.push_back(impl_->trace[(impl_->traceHead + i) % impl_->trace.size()]);
    return result;
}
Result<spawn::PlacementOutcome> MapSpawnSpace::plan(const spawn::BatchRequest&  request,
                                                    const spawn::PlacementSpec& requestedSpec,
                                                    const spawn::Region& requestedRegion, std::uint64_t& random) {
    auto index = impl_->instance(request);
    if (!index) return Result<spawn::PlacementOutcome>::failure(index.status());
    const auto& binding  = impl_->bindings[index.value()];
    const auto& instance = binding.map->instances()[binding.index];
    const auto& spec     = instance.revision->definition().placement;
    if (!sameSpec(requestedSpec, spec) || !sameRegion(requestedRegion, instance.region))
        return fail<spawn::PlacementOutcome>(DiagnosticCode::Conflict, "Space and scheduler content revisions differ");
    auto valid = validate(spec, instance.region);
    if (!valid) return Result<spawn::PlacementOutcome>::failure(valid.status());
    auto occupied = impl_->capture(spec);
    if (!occupied) return Result<spawn::PlacementOutcome>::failure(occupied.status());
    std::vector<spawn::Point> positions;
    positions.reserve(request.units.size());
    for (std::size_t unit = 0; unit < request.units.size(); ++unit) {
        bool accepted = false;
        for (std::uint32_t attempt = 0; attempt < spec.attempts; ++attempt) {
            const auto u = spawn::nextRandomDraw(random), v = spawn::nextRandomDraw(random);
            auto       point = spawn::sampleRegion(*binding.map, binding.index, u, v);
            if (!point) {
                impl_->record(request, unit, std::nullopt, attempt + 1, PlacementStage::Sampling,
                              PlacementDecision::SamplingError);
                return Result<spawn::PlacementOutcome>::failure(point.status());
            }
            const auto decision = point.value() ? impl_->decision(*point.value(), spec, occupied.value(), positions)
                                                : PlacementDecision::OutsideRegion;
            impl_->record(request, unit, point.value(), attempt + 1, PlacementStage::Sampling, decision);
            if (decision == PlacementDecision::Accepted) {
                positions.push_back(*point.value());
                accepted = true;
                break;
            }
        }
        if (!accepted) return Result<spawn::PlacementOutcome>::success(spawn::PlacementBlocked{});
    }
    return Result<spawn::PlacementOutcome>::success(std::move(positions));
}
Result<spawn::CommitOutcome> MapSpawnSpace::check(const spawn::BatchRequest&    request,
                                                  std::span<const spawn::Point> positions) const {
    auto index = impl_->instance(request);
    if (!index) return Result<spawn::CommitOutcome>::failure(index.status());
    if (positions.size() != request.units.size())
        return fail<spawn::CommitOutcome>(DiagnosticCode::InvalidArgument, "Position count mismatch");
    const auto& binding  = impl_->bindings[index.value()];
    const auto& instance = binding.map->instances()[binding.index];
    const auto& spec     = instance.revision->definition().placement;
    auto        valid    = validate(spec, instance.region);
    if (!valid) return Result<spawn::CommitOutcome>::failure(valid.status());
    auto occupied = impl_->capture(spec);
    if (!occupied) return Result<spawn::CommitOutcome>::failure(occupied.status());
    for (std::size_t i = 0; i < positions.size(); ++i) {
        auto distance = spawn::distanceToRegion(*binding.map, binding.index, positions[i]);
        if (!distance) return Result<spawn::CommitOutcome>::failure(distance.status());
        const auto decision = distance.value() > 0
                                  ? PlacementDecision::OutsideRegion
                                  : impl_->decision(positions[i], spec, occupied.value(), positions.first(i));
        impl_->record(request, i, positions[i], 0, PlacementStage::FinalCheck, decision);
        if (decision != PlacementDecision::Accepted)
            return Result<spawn::CommitOutcome>::success(spawn::CommitOutcome::PlacementBlocked);
    }
    return Result<spawn::CommitOutcome>::success(spawn::CommitOutcome::Committed);
}
struct SpatialSpawnFactory::Impl {
    std::shared_ptr<spawn::ISpawnFactory> domain;
    std::shared_ptr<MapSpawnSpace>        space;
    struct Prepared                       final : spawn::IPreparedSpawn {
        std::weak_ptr<Impl>                    owner;
        spawn::BatchRequest                    request;
        std::vector<spawn::Point>              positions;
        std::unique_ptr<spawn::IPreparedSpawn> domain;
        bool                                   committed = false;
        bool                                   isPublished() const noexcept override { return domain->isPublished(); }
        Result<spawn::CommitOutcome>           commit() override {
            auto state = owner.lock();
            if (!state) return fail<spawn::CommitOutcome>(DiagnosticCode::StaleHandle, "Spatial factory unloaded");
            if (!request.lifetime.isCurrent())
                return fail<spawn::CommitOutcome>(DiagnosticCode::StaleHandle, "Source revoked");
            if (committed) return Result<spawn::CommitOutcome>::success(spawn::CommitOutcome::Committed);
            if (!domain->isPublished()) {
                auto ready = state->space->check(request, positions);
                if (!ready || ready.value() == spawn::CommitOutcome::PlacementBlocked) return ready;
            }
            auto result = domain->commit();
            if (result && result.value() == spawn::CommitOutcome::Committed) committed = true;
            return result;
        }
    };
};
SpatialSpawnFactory::SpatialSpawnFactory(std::shared_ptr<spawn::ISpawnFactory> domain,
                                         std::shared_ptr<MapSpawnSpace>        space)
    : impl_(std::make_shared<Impl>()) {
    impl_->domain = std::move(domain);
    impl_->space  = std::move(space);
}
SpatialSpawnFactory::~SpatialSpawnFactory() = default;
const spawn::ISpawnFactory& SpatialSpawnFactory::domainFactory() const noexcept {
    return impl_->domain ? impl_->domain->domainFactory() : *this;
}

Result<spawn::PrepareOutcome> SpatialSpawnFactory::prepare(const spawn::BatchRequest&    request,
                                                           std::span<const spawn::Point> positions) {
    if (!impl_->domain || !impl_->space)
        return fail<spawn::PrepareOutcome>(DiagnosticCode::NotFound, "Missing domain/space service");
    auto prepared = impl_->domain->prepare(request, positions);
    if (!prepared) return prepared;
    if (std::holds_alternative<spawn::PlacementBlocked>(prepared.value())) return prepared;
    const auto& transaction = std::get<std::unique_ptr<spawn::IPreparedSpawn>>(prepared.value());
    if (!transaction) return fail<spawn::PrepareOutcome>(DiagnosticCode::InvariantViolation, "Null domain transaction");
    if (!transaction->isPublished()) {
        auto ready = impl_->space->check(request, positions);
        if (!ready) return Result<spawn::PrepareOutcome>::failure(ready.status());
        if (ready.value() == spawn::CommitOutcome::PlacementBlocked)
            return Result<spawn::PrepareOutcome>::success(spawn::PlacementBlocked{});
    }
    auto token     = std::make_unique<Impl::Prepared>();
    token->owner   = impl_;
    token->request = request;
    token->positions.assign(positions.begin(), positions.end());
    token->domain = std::move(std::get<std::unique_ptr<spawn::IPreparedSpawn>>(prepared.value()));
    if (!token->domain)
        return fail<spawn::PrepareOutcome>(DiagnosticCode::InvariantViolation, "Null domain transaction");
    std::unique_ptr<spawn::IPreparedSpawn> result = std::move(token);
    return Result<spawn::PrepareOutcome>::success(std::move(result));
}
Result<spawn::RemovalReceipt> SpatialSpawnFactory::remove(const spawn::RemovalRequest& request) {
    if (!impl_->domain) return fail<spawn::RemovalReceipt>(DiagnosticCode::NotFound, "Missing domain service");
    return impl_->domain->remove(request);
}
}  // namespace eve::map_spawn
