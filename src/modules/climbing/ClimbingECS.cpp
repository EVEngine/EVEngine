#include "climbing/ClimbingECS.h"

#include <algorithm>

namespace eve::climbing {
namespace {

template <class T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message, std::string path) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "climbing.ecs"));
}

}  // namespace

eve::Result<void> ClimbingCandidateBuffer::replace(std::span<const ClimbingCandidate> candidates,
                                                    eve::SimulationTick tick) {
    if (candidates.size() > Capacity)
        return failure<void>(eve::DiagnosticCode::PreconditionViolation,
                             "climbing candidate buffer capacity exceeded", "candidates");
    ClimbingCandidateSet replacement;
    for (const ClimbingCandidate& candidate : candidates) replacement.consider(candidate);
    replacement.sortAndLimit(Capacity);
    values_.swap(replacement);
    tick_ = tick;
    return eve::Result<void>::success();
}

eve::Result<void> ClimbingCandidateBuffer::probe(ClimbingRuntime& runtime, physics::World3D& world,
                                                  const ClimbingPose& pose, eve::SimulationTick tick) {
    auto probed = runtime.probeInto(world, pose, values_, tick);
    if (!probed) return eve::Result<void>::failure(probed.status());
    tick_ = tick;
    return eve::Result<void>::success();
}

eve::Result<ClimbingState> ClimbingState::create() {
    auto created = Climbing::newRuntime();
    if (!created) return eve::Result<ClimbingState>::failure(created.status());
    ClimbingState state;
    state.runtime = std::move(created).takeValue();
    return eve::Result<ClimbingState>::success(std::move(state));
}

eve::Result<void> ClimbingState::releaseRuntime() {
    if (!runtime.isValid())
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "climbing state has no runtime to release", "state.runtime");
    auto released = Climbing::release(runtime);
    if (!released) return eve::Result<void>::failure(released.status());
    runtime = {};
    lastAdvance = {};
    lastAdvanceTick = eve::SimulationTick::zero();
    return eve::Result<void>::success();
}

eve::Result<void> ClimbingEventBatch::replace(std::span<const ClimbingEvent> events,
                                               eve::SimulationTick tick) {
    if (events.size() > Capacity)
        return failure<void>(eve::DiagnosticCode::PreconditionViolation,
                             "climbing event batch capacity exceeded", "events");
    std::array<ClimbingEvent, Capacity> replacement{};
    std::copy(events.begin(), events.end(), replacement.begin());
    values_ = std::move(replacement);
    size_ = events.size();
    tick_ = tick;
    return eve::Result<void>::success();
}

std::span<const ClimbingSystemContract> climbingSystemContracts() noexcept {
    static constexpr ClimbingSystemContract contracts[] = {
        {"climbing.input", "caller-selected existing domain root",
         "View<EntityRoot, ClimbingIntent>", "owning command queue; SimulationTick", "ClimbingIntent",
         "none", "none", "SimulationStep", "input", "bit-exact for identical command streams"},
        {"climbing.probe", "caller-selected existing domain root",
         "View<EntityRoot, ClimbingBody, ClimbingIntent, ClimbingState, ClimbingLinks, ClimbingCandidateBuffer>",
         "Body; Intent; State runtime; Links; Physics world", "ClimbingCandidateBuffer", "none", "none",
         "Physics owning queries; SimulationTick", "pre_physics",
         "stable identity/order; geometry tolerance-bounded"},
        {"climbing.selection", "caller-selected existing domain root",
         "View<EntityRoot, ClimbingBody, ClimbingIntent, ClimbingState, ClimbingLinks>",
         "Body; Intent; definitions; Physics world", "Intent consumed edge; State runtime", "none",
         "ClimbingStarted queued only after commit", "Physics; definitions; optional resource adapters",
         "pre_physics", "quantized cost and stable identity tie-break"},
        {"climbing.motion", "caller-selected existing domain root",
         "View<EntityRoot, ClimbingBody, ClimbingIntent, ClimbingState, ClimbingLinks>",
         "State execution; Body; Intent; Links; animation desired root delta",
         "Body; State runtime; linked Physics body",
         "none", "contact/land/cancel to bounded runtime queue", "Physics mover; SimulationStep", "physics",
         "fixed-tick replay stable; Physics contacts tolerance-bounded"},
        {"climbing.pose", "caller-selected existing domain root",
         "View<EntityRoot, ClimbingState, ClimbingPoseProjection, ClimbingEventBatch>",
         "State actual motion and contact anchors", "derived pose projection; owning event batch", "none",
         "drain only; external dispatch occurs after View closes", "Animation; optional IK", "post_physics",
         "gameplay state independent of final skin pose"},
    };
    return contracts;
}

}  // namespace eve::climbing
