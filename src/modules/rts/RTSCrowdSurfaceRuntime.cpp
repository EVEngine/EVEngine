#include "crowd/Crowd.h"
#include "map/PathfinderState.h"
#include "rts/RTSCrowdSurfaceInternal.h"
#include "rts/RTSNavigationCapture.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>

namespace eve::rts {
namespace {
Result<void> validateSurfaceSettings(const CrowdSurfaceTracking& settings) {
    if (settings.surface.isZero() || settings.movement.isZero() || settings.movementRevision == 0 ||
        settings.costModelRevision == 0 || !std::isfinite(settings.clearanceRadius) || settings.clearanceRadius < 0.f ||
        settings.maxInflationChecks < 0 || settings.maxInflationChecks > 10000000)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Invalid Crowd surface tracking configuration", "crowdSurfaceTracking"));
    return Result<void>::success();
}
Result<void> validateGeometry(const NavigationGrid& grid) {
    if (!std::isfinite(grid.cellSize) || grid.cellSize <= 0.f || !std::isfinite(grid.originX) ||
        !std::isfinite(grid.originY))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid saved navigation geometry", "rts.navigation"));
    return Result<void>::success();
}
}  // namespace
struct RTSNavigationCapture::Impl {
    struct Tracker {
        CrowdSurfaceTracking          settings;
        map::NavigationChangeStamp    change;
        NavigationGrid                geometry;
        crowd::NavigationSurfaceStamp publication;
        std::uint64_t                 captureRevision = 0;
        bool                          currentProvider = false;
    };
    PersistentId           epoch;
    map::PathfinderState   provider;
    NavigationGrid         desired;
    std::optional<Tracker> tracker;
    Impl(PersistentId value, map::PathfinderState state) : epoch(value), provider(std::move(state)) {}
    Result<void> validate() const {
        if (epoch.isNil())
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::Conflict, "Nil navigation epoch"));
        auto geometry = validateGeometry(desired);
        if (!geometry) return geometry;
        if (!tracker) return Result<void>::success();
        auto settings = validateSurfaceSettings(tracker->settings);
        if (!settings) return settings;
        auto previous = validateGeometry(tracker->geometry);
        if (!previous) return previous;
        const auto& p     = tracker->publication;
        const bool  empty = p.surface.isZero() && p.movement.isZero() && p.surfaceRevision == 0 &&
                           p.movementRevision == 0 && p.costModelRevision == 0 && p.clearanceRadius == 0.f;
        const bool complete = !p.surface.isZero() && !p.movement.isZero() && p.surfaceRevision != 0 &&
                              p.movementRevision != 0 && p.costModelRevision != 0 && std::isfinite(p.clearanceRadius) &&
                              p.clearanceRadius >= 0.f;
        if (!empty && !complete)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "Partial saved surface publication"));
        if (tracker->captureRevision == 0) {
            if (tracker->currentProvider || tracker->change.configuration != 0 || tracker->change.layer != 0 ||
                tracker->geometry.cellSize != 1.f || tracker->geometry.originX != 0.f ||
                tracker->geometry.originY != 0.f)
                return Result<void>::failure(
                    Diagnostic::error(DiagnosticCode::Conflict, "Unpublished tracker carries source progress"));
        } else if (!complete || tracker->change.configuration == 0 || p.surface != tracker->settings.surface ||
                   p.movement != tracker->settings.movement ||
                   p.movementRevision != tracker->settings.movementRevision ||
                   p.costModelRevision != tracker->settings.costModelRevision ||
                   p.clearanceRadius != tracker->settings.clearanceRadius)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "Published tracker identity differs from settings"));
        return Result<void>::success();
    }
};
RTSNavigationCapture::RTSNavigationCapture(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
RTSNavigationCapture::~RTSNavigationCapture()                                          = default;
RTSNavigationCapture::RTSNavigationCapture(RTSNavigationCapture&&) noexcept            = default;
RTSNavigationCapture& RTSNavigationCapture::operator=(RTSNavigationCapture&&) noexcept = default;
PersistentId          RTSNavigationCapture::saveEpoch() const noexcept { return impl_ ? impl_->epoch : PersistentId{}; }
OptionalRef<const map::PathfinderState> RTSNavigationCapture::provider() const noexcept {
    return impl_ ? OptionalRef<const map::PathfinderState>{std::cref(impl_->provider)} : std::nullopt;
}
namespace {
namespace navigation_archive {
struct Invalid {
    Status status;
};
[[noreturn]] void invalid(const char* message) {
    throw Invalid{Status::failure(Diagnostic::error(DiagnosticCode::ParseError, message, "rts.navigation"))};
}
template <class T>
T take(Result<T> value) {
    if (!value) throw Invalid{value.status()};
    return std::move(value).takeValue();
}
const Value::Array& tuple(const Value& value, std::size_t size) {
    auto* a = value.getIf<Value::Array>();
    if (!a || a->size() != size) invalid("Unknown or missing navigation tuple fields");
    return *a;
}
std::uint64_t counter(const Value& value) {
    auto* s = value.getIf<std::string>();
    if (!s) invalid("Expected navigation counter text");
    std::uint64_t n = 0;
    auto          p = std::from_chars(s->data(), s->data() + s->size(), n);
    if (p.ec != std::errc{} || p.ptr != s->data() + s->size() || std::to_string(n) != *s)
        invalid("Invalid canonical navigation counter");
    return n;
}
float number(const Value& value) {
    if (!value.isDouble() && !value.isInt64()) invalid("Expected navigation number");
    double n = value.isDouble() ? value.asDouble() : double(value.asInt());
    if (!std::isfinite(n) || std::abs(n) > std::numeric_limits<float>::max()) invalid("Invalid navigation float");
    return float(n);
}
bool boolean(const Value& value) {
    auto* b = value.getIf<bool>();
    if (!b) invalid("Expected navigation boolean");
    return *b;
}
Value          geometry(const NavigationGrid& grid) { return Value::Array{grid.cellSize, grid.originX, grid.originY}; }
NavigationGrid geometry(const Value& value) {
    const auto& a = tuple(value, 3);
    return {number(a[0]), number(a[1]), number(a[2])};
}
Value settings(const CrowdSurfaceTracking& value) {
    return Value::Array{std::to_string(value.surface.value()),
                        std::to_string(value.movement.value()),
                        std::to_string(value.movementRevision),
                        std::to_string(value.costModelRevision),
                        value.clearanceRadius,
                        value.maxInflationChecks};
}
CrowdSurfaceTracking settings(const Value& value) {
    const auto& a = tuple(value, 6);
    if (!a[5].isInt64()) invalid("Expected inflation budget integer");
    return {crowd::NavigationSurfaceId(counter(a[0])),
            crowd::MovementClassId(counter(a[1])),
            counter(a[2]),
            counter(a[3]),
            number(a[4]),
            a[5].asInt()};
}
Value publication(const crowd::NavigationSurfaceStamp& value) {
    return Value::Array{std::to_string(value.surface.value()),   std::to_string(value.movement.value()),
                        std::to_string(value.surfaceRevision),   std::to_string(value.movementRevision),
                        std::to_string(value.costModelRevision), value.clearanceRadius};
}
crowd::NavigationSurfaceStamp publication(const Value& value) {
    const auto& a = tuple(value, 6);
    return {crowd::NavigationSurfaceId(counter(a[0])),
            crowd::MovementClassId(counter(a[1])),
            counter(a[2]),
            counter(a[3]),
            counter(a[4]),
            number(a[5])};
}
}  // namespace navigation_archive
}  // namespace
Result<SnapshotEnvelope> RTSNavigationCapture::seal(Revision revision, SimulationTick tick,
                                                    const SnapshotHashProvider& hash) const {
    using R = Result<SnapshotEnvelope>;
    if (!impl_ || revision.isZero())
        return R::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "Navigation capture moved or save revision zero"));
    auto valid = impl_->validate();
    if (!valid) return R::failure(valid.status());
    auto provider = impl_->provider.encodeState();
    if (!provider) return R::failure(provider.status());
    Value tracker;
    if (impl_->tracker) {
        const auto& t = *impl_->tracker;
        tracker       = Value::Array{navigation_archive::settings(t.settings),
                               Value::Array{std::to_string(t.change.configuration), std::to_string(t.change.layer)},
                               navigation_archive::geometry(t.geometry),
                               navigation_archive::publication(t.publication),
                               std::to_string(t.captureRevision),
                               t.currentProvider};
    }
    return makeSnapshotEnvelope("eve.rts-navigation", *LogicalId::fromParts("rts", "navigation"), SchemaVersion(1),
                                impl_->epoch, revision, tick,
                                Value::object({{"provider", std::move(provider).takeValue()},
                                               {"desired", navigation_archive::geometry(impl_->desired)},
                                               {"tracker", std::move(tracker)}}),
                                hash);
}
Result<RTSNavigationCapture> RTSNavigationCapture::parse(SnapshotEnvelope snapshot, PersistentId epoch,
                                                         const SnapshotHashProvider& hash) {
    using R       = Result<RTSNavigationCapture>;
    auto verified = verifySnapshotEnvelope(snapshot, hash);
    if (!verified) return R::failure(verified.status());
    if (snapshot.schemaVersion != SchemaVersion(1))
        return R::failure(Diagnostic::error(DiagnosticCode::UnknownVersion, "Unknown RTS navigation version"));
    if (snapshot.type != "eve.rts-navigation" || snapshot.schema != *LogicalId::fromParts("rts", "navigation") ||
        epoch.isNil() || snapshot.instanceId != epoch || snapshot.revision.isZero())
        return R::failure(Diagnostic::error(DiagnosticCode::Conflict, "RTS navigation save boundary mismatch"));
    try {
        const auto* root = snapshot.payload.getIf<Value::Object>();
        if (!root || root->size() != 3 || !root->contains("provider") || !root->contains("desired") ||
            !root->contains("tracker"))
            navigation_archive::invalid("Unknown or missing RTS navigation fields");
        auto provider   = navigation_archive::take(map::PathfinderState::prepareState(root->at("provider")));
        auto result     = std::make_unique<Impl>(epoch, std::move(provider));
        result->desired = navigation_archive::geometry(root->at("desired"));
        if (!root->at("tracker").isNull()) {
            const auto& a      = navigation_archive::tuple(root->at("tracker"), 6);
            const auto& change = navigation_archive::tuple(a[1], 2);
            result->tracker =
                Impl::Tracker{navigation_archive::settings(a[0]),
                              {navigation_archive::counter(change[0]), navigation_archive::counter(change[1])},
                              navigation_archive::geometry(a[2]),
                              navigation_archive::publication(a[3]),
                              navigation_archive::counter(a[4]),
                              navigation_archive::boolean(a[5])};
        }
        auto valid = result->validate();
        if (!valid) return R::failure(valid.status());
        return R::success(RTSNavigationCapture(std::move(result)));
    } catch (const navigation_archive::Invalid& error) {
        return R::failure(error.status);
    }
}
Result<RTSNavigationCapture> RTS::captureNavigation(PersistentId epoch, map::Pathfinder& provider) const {
    using R = Result<RTSNavigationCapture>;
    if (epoch.isNil() || !ownedWorld_ || ownedWorld_->is_deferred() || pathfinder_ != &provider ||
        (crowdSurfaceRuntime_ && !hasLiveCrowdProvider()))
        return R::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation,
                              "Navigation capture needs epoch, idle owned world and actual live providers"));
    if (navigationEvent_)
        return R::failure(
            Diagnostic::error(DiagnosticCode::Unsupported,
                              "Native navigation capture requires explicit callback implementation admission"));
    auto state = provider.captureState();
    if (!state) return R::failure(state.status());
    auto captured     = std::make_unique<RTSNavigationCapture::Impl>(epoch, std::move(state).takeValue());
    captured->desired = navigationGrid_;
    if (crowdSurfaceRuntime_) {
        const auto& source = *crowdSurfaceRuntime_;
        captured->tracker =
            RTSNavigationCapture::Impl::Tracker{source.settings,    source.change,          source.geometry,
                                                source.publication, source.captureRevision, source.source == &provider};
    }
    auto valid = captured->validate();
    if (!valid) return R::failure(valid.status());
    return R::success(RTSNavigationCapture(std::move(captured)));
}
Result<void> RTS::restoreNavigation(const RTSNavigationCapture& captured, map::Pathfinder& provider) {
    if (!captured.impl_ || !ownedWorld_ || ownedWorld_->is_deferred() || pathfinder_ || crowdSurfaceRuntime_ ||
        navigationEvent_ || !provider.isRestoredFrom(captured.impl_->provider) ||
        (captured.impl_->tracker && !hasLiveCrowdProvider()))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation,
                              "Navigation restore needs pristine target and exact prepared provider"));
    auto valid = captured.impl_->validate();
    if (!valid) return valid;
    std::unique_ptr<CrowdSurfaceRuntime> staged;
    if (captured.impl_->tracker) {
        const auto& source      = *captured.impl_->tracker;
        staged                  = std::make_unique<CrowdSurfaceRuntime>();
        staged->settings        = source.settings;
        staged->change          = source.change;
        staged->geometry        = source.geometry;
        staged->publication     = source.publication;
        staged->captureRevision = source.captureRevision;
        staged->source          = source.currentProvider ? &provider : nullptr;
    }
    // Do not call setNavigationProvider: it clears saved Unit traffic-recovery facts and invalidates tracking.
    pathfinder_     = &provider;
    navigationGrid_ = captured.impl_->desired;
    crowdSurfaceRuntime_.swap(staged);
    return Result<void>::success();
}
Result<void> RTS::configureCrowdSurfaceTracking(CrowdSurfaceTracking settings) try {
    auto valid = validateSurfaceSettings(settings);
    if (!valid) return valid;
    auto staged      = std::make_unique<CrowdSurfaceRuntime>();
    staged->settings = settings;
    if (crowdSurfaceRuntime_ && crowdSurfaceRuntime_->publication.surface == settings.surface)
        staged->publication = crowdSurfaceRuntime_->publication;
    crowdSurfaceRuntime_ = std::move(staged);
    return Result<void>::success();
} catch (const std::exception& error) {
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, error.what(), "crowdSurfaceTracking"));
}

void RTS::clearCrowdSurfaceTracking() noexcept { crowdSurfaceRuntime_.reset(); }
void RTS::invalidateCrowdSurfaceSource() noexcept {
    if (crowdSurfaceRuntime_) crowdSurfaceRuntime_->source = nullptr;
}

Result<void> RTS::synchronizeCrowdSurface() try {
    if (!crowdSurfaceRuntime_) return Result<void>::success(Status::success(StatusCode::NoOp));
    if (!pathfinder_ || !crowd_)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "Surface tracking requires map and Crowd providers",
                                                       "crowdSurfaceTracking"));
    auto&      runtime      = *crowdSurfaceRuntime_;
    const auto change       = pathfinder_->getNavigationChangeStamp();
    const auto field        = crowd_->getFieldStatus();
    const bool sameGeometry = runtime.geometry.cellSize == navigationGrid_.cellSize &&
                              runtime.geometry.originX == navigationGrid_.originX &&
                              runtime.geometry.originY == navigationGrid_.originY;
    const bool sameProvider    = runtime.source == pathfinder_;
    const bool samePublication = runtime.captureRevision != 0 && runtime.publication == field.surface;
    if (sameProvider && sameGeometry && samePublication && runtime.change == change)
        return Result<void>::success(Status::success(StatusCode::NoOp));
    auto captured = pathfinder_->captureNavigationGrid();
    if (!captured) return Result<void>::failure(captured.status());
    if (sameProvider && sameGeometry && samePublication && runtime.captureRevision == captured.value().revision()) {
        runtime.change = change;
        return Result<void>::success(Status::success(StatusCode::NoOp));
    }
    // Rebinding and geometry changes create a new physical surface revision even when cell content is equal.
    const auto previous =
        std::max(field.surface.surface == runtime.settings.surface ? field.surface.surfaceRevision : 0,
                 runtime.publication.surface == runtime.settings.surface ? runtime.publication.surfaceRevision : 0);
    if (previous == std::numeric_limits<std::uint64_t>::max())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Tracked surface revision exhausted", "crowdSurfaceTracking"));
    const crowd::NavigationSurfaceStamp stamp{
        runtime.settings.surface,          runtime.settings.movement,          previous + 1,
        runtime.settings.movementRevision, runtime.settings.costModelRevision, runtime.settings.clearanceRadius};
    auto projection =
        projectCrowdSurface(captured.value(), stamp, navigationGrid_, runtime.settings.maxInflationChecks);
    if (!projection) return Result<void>::failure(projection.status());
    auto prepared = crowd_->prepareNavigationSurface(projection.value());
    if (!prepared) return Result<void>::failure(prepared.status());
    auto committed = crowd_->commitNavigationSurface(std::move(prepared).takeValue());
    if (!committed) return committed;
    runtime.source          = pathfinder_;
    runtime.geometry        = navigationGrid_;
    runtime.change          = change;
    runtime.publication     = stamp;
    runtime.captureRevision = captured.value().revision();
    return Result<void>::success();
} catch (const std::exception& error) {
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, error.what(), "crowdSurfaceTracking"));
}
}  // namespace eve::rts
