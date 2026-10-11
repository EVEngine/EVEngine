#include "physics/action/ActionCollisionIgnoreState.h"

#include "common/Capability.h"
#include "physics/Body3D.h"
#include "physics/World3D.h"

#include <utility>

namespace eve::physics::action_adapter {
namespace {

template <typename T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

}  // namespace

ActionCollisionIgnoreState::ActionCollisionIgnoreState(World3D& world, ActionCollisionPairResolver resolver)
    : world_(&world), worldLifetime_(world.queryLifetime_), worldHandle_(world.runtimeHandle()),
      resolver_(std::move(resolver)) {}

ActionCollisionIgnoreState::~ActionCollisionIgnoreState() {
    cap::removeListener<eve::action::IActionStateWindowSink>(this);
    releaseAll();
}

bool ActionCollisionIgnoreState::enabled() const {
    for (std::size_t index = 0; index < cap::listenerCount<eve::action::IActionStateWindowSink>(); ++index)
        if (cap::listenerAt<eve::action::IActionStateWindowSink>(index) == this) return true;
    return false;
}

void ActionCollisionIgnoreState::setEnabled(bool value) {
    const bool current = enabled();
    if (value && !current)
        cap::addListener<eve::action::IActionStateWindowSink>(this);
    else if (!value && current)
        cap::removeListener<eve::action::IActionStateWindowSink>(this);
}

bool ActionCollisionIgnoreState::supports(eve::action::ActionStateWindowKind kind) const noexcept {
    return kind == eve::action::ActionStateWindowKind::CollisionIgnore;
}

World3D* ActionCollisionIgnoreState::liveWorld() const noexcept {
    auto lifetime = worldLifetime_.lock();
    if (!lifetime || !world_ || !world_->isValid() || world_->runtimeHandle() != worldHandle_) return nullptr;
    return world_;
}

Result<ActionCollisionIgnoreState::PairKey> ActionCollisionIgnoreState::resolvePair(
    ecs::EntityHandle subject, std::string_view channel) const {
    World3D* world = liveWorld();
    if (!world) return failure<PairKey>(DiagnosticCode::StaleHandle, "collision world is no longer live", "world");
    if (!resolver_)
        return failure<PairKey>(DiagnosticCode::NotFound, "collision pair resolver is unavailable", "resolver");
    auto pair = resolver_(subject, channel);
    if (!pair) return Result<PairKey>::failure(pair.status());
    if (!pair.value().first.isValid() || !pair.value().second.isValid() ||
        pair.value().first.world != worldHandle_ || pair.value().second.world != worldHandle_)
        return failure<PairKey>(DiagnosticCode::InvalidArgument,
                                "collision pair must contain two links owned by this world", "pair");
    PairKey key{pair.value().first.body, pair.value().second.body};
    if (key.first == key.second)
        return failure<PairKey>(DiagnosticCode::InvalidArgument, "collision pair bodies must be distinct", "pair");
    if (key.second < key.first) std::swap(key.first, key.second);
    if (!world->findBody(key.first) || !world->findBody(key.second))
        return failure<PairKey>(DiagnosticCode::StaleHandle, "collision pair contains a stale body", "pair");
    return Result<PairKey>::success(key);
}

Result<void> ActionCollisionIgnoreState::enter(const eve::action::ActionStateWindowBinding& binding,
                                                const eve::action::ActionTimelineEvent& event,
                                                const eve::action::ActionNotifyContext& context) {
    if (!supports(binding.kind))
        return failure<void>(DiagnosticCode::Unsupported, "physics adapter does not own this window kind", "kind");
    std::optional<ecs::EntityHandle> subject;
    if (binding.targetIndex) {
        if (*binding.targetIndex >= context.targets.size())
            return failure<void>(DiagnosticCode::NotFound, "collision target index is unavailable", "targetIndex");
        subject = context.targets[*binding.targetIndex];
    } else {
        subject = context.source;
    }
    if (!subject) return failure<void>(DiagnosticCode::NotFound, "collision window has no source or target", "subject");
    auto pair = resolvePair(*subject, binding.resource);
    if (!pair) return Result<void>::failure(pair.status());

    const ActiveKey activeKey{context.executionId, event.itemId.format()};
    const auto activeFound = active_.find(activeKey);
    if (activeFound != active_.end()) {
        if (activeFound->second == pair.value())
            return Result<void>::success(Status::success(StatusCode::NoOp));
        return failure<void>(DiagnosticCode::Conflict,
                             "collision-window key resolves to a different body pair", "itemId");
    }

    World3D* world = liveWorld();
    Body3D* first = world ? world->findBody(pair.value().first) : nullptr;
    Body3D* second = world ? world->findBody(pair.value().second) : nullptr;
    if (!world || !first || !second)
        return failure<void>(DiagnosticCode::StaleHandle, "collision pair became stale during enter", "pair");
    auto pairFound = pairs_.find(pair.value());
    if (pairFound == pairs_.end()) {
        const bool restoreEnabled = world->isBodyPairCollisionEnabled(first, second);
        pairFound = pairs_.emplace(pair.value(), PairState{restoreEnabled, 0}).first;
        world->setBodyPairCollisionEnabled(first, second, false);
    }
    ++pairFound->second.owners;
    active_.emplace(activeKey, pair.value());
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> ActionCollisionIgnoreState::exit(const eve::action::ActionStateWindowBinding& binding,
                                               const eve::action::ActionTimelineEvent& event,
                                               const eve::action::ActionNotifyContext& context) {
    if (!supports(binding.kind))
        return failure<void>(DiagnosticCode::Unsupported, "physics adapter does not own this window kind", "kind");
    const ActiveKey activeKey{context.executionId, event.itemId.format()};
    const auto activeFound = active_.find(activeKey);
    if (activeFound == active_.end()) return Result<void>::success(Status::success(StatusCode::NoOp));
    const PairKey pairKey = activeFound->second;
    active_.erase(activeFound);
    const auto pairFound = pairs_.find(pairKey);
    if (pairFound == pairs_.end()) return Result<void>::success(Status::success(StatusCode::NoOp));
    if (pairFound->second.owners > 1) {
        --pairFound->second.owners;
    } else {
        restorePair(pairFound->first, pairFound->second);
        pairs_.erase(pairFound);
    }
    return Result<void>::success(Status::success(StatusCode::Applied));
}

void ActionCollisionIgnoreState::restorePair(const PairKey& key, const PairState& state) noexcept {
    World3D* world = liveWorld();
    Body3D* first = world ? world->findBody(key.first) : nullptr;
    Body3D* second = world ? world->findBody(key.second) : nullptr;
    if (world && first && second) world->setBodyPairCollisionEnabled(first, second, state.restoreEnabled);
}

void ActionCollisionIgnoreState::releaseAll() noexcept {
    for (const auto& [key, state] : pairs_) restorePair(key, state);
    active_.clear();
    pairs_.clear();
}

Result<void> ActionCollisionIgnoreState::shutdown() {
    setEnabled(false);
    const bool changed = !active_.empty() || !pairs_.empty();
    releaseAll();
    return Result<void>::success(Status::success(changed ? StatusCode::Applied : StatusCode::NoOp));
}

}  // namespace eve::physics::action_adapter
