#pragma once

/**
 * @file ClimbingECS.h
 * @brief Composition-only ECS components and fixed-phase climbing systems.
 */

#include "climbing/Climbing.h"
#include "climbing/ClimbingInput.h"
#include "common/ECS.h"
#include "physics/Body3D.h"
#include "physics/PhysicsLink.h"
#include "physics/World3D.h"

#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace eve::climbing {

/** @brief Hot authoritative transient motor component; replicated by value for prediction. */
struct ClimbingBody {
    Vec3                  feet;
    Vec3                  forward{0.f, 0.f, 1.f};
    Vec3                  up{0.f, 1.f, 0.f};
    Vec3                  velocity;
    Vec3                  groundNormal{0.f, 1.f, 0.f};
    Vec3                  lastStableFeet;
    float                 capsuleRadius = 0.3f;
    float                 capsuleHeight = 1.8f;
    float                 maxSlopeRadians = 0.8f;
    float                 skin = 0.03f;
    float                 groundSnap = 0.2f;
    float                 stepHeight = 0.35f;
    bool                  grounded = true;
    ClimbingMovementMode  mode = ClimbingMovementMode::Grounded;
    eve::SimulationTick   lastGroundedTick = eve::SimulationTick::zero();
    eve::SimulationTick   lastOrdinaryJumpPressedTick = eve::SimulationTick::zero();
    bool                  hasOrdinaryJump = false;
};

/**
 * @brief Cold authoritative cross-domain links; handles are resolved on every system step.
 *
 * The owning gameplay entity creates and clears this component. Physics owns the linked body; target-first
 * destruction resolves as StaleHandle. `sceneEntity` is optional and must be rebuilt after restore. Runtime
 * snapshots do not persist process-local handles.
 */
struct ClimbingLinks {
    physics::PhysicsLink physicsBody;
    ecs::EntityHandle    sceneEntity{};
    std::string          animationBindingId;
    std::uint64_t        animationBindingGeneration = 0;
};

/** @brief Fixed-capacity owning candidate projection written once per PrePhysics probe phase. */
class ClimbingCandidateBuffer {
public:
    static constexpr std::size_t Capacity = ClimbingCandidateSet::Capacity;

    /** @brief Atomically replace the buffer; oversize input leaves the old contents unchanged. */
    [[nodiscard]] eve::Result<void> replace(std::span<const ClimbingCandidate> candidates,
                                             eve::SimulationTick tick);
    /** @brief Fill retained storage through the allocation-free production probe path. */
    [[nodiscard]] eve::Result<void> probe(ClimbingRuntime& runtime, physics::World3D& world,
                                           const ClimbingPose& pose, eve::SimulationTick tick);
    /** @brief Return an immutable synchronous view invalidated by the next replace/clear. */
    [[nodiscard]] std::span<const ClimbingCandidate> values() const noexcept {
        return values_.values();
    }
    /** @brief Clear derived candidates without affecting runtime state. */
    void clear() noexcept { values_.clear(); }
    /** @brief Tick that produced the current projection. */
    [[nodiscard]] eve::SimulationTick tick() const noexcept { return tick_; }

private:
    ClimbingCandidateSet values_;
    eve::SimulationTick tick_ = eve::SimulationTick::zero();
};

/**
 * @brief Hot authoritative execution component holding the module-owned runtime identity.
 *
 * The gameplay entity is the source-side owner and must call `releaseRuntime()` before destruction. Copies made
 * by ECS deferred publication only copy the generation handle; release invalidates every copy safely.
 */
struct ClimbingState {
    ClimbingRuntimeHandleRef runtime;
    ClimbingAdvance          lastAdvance;
    eve::SimulationTick      lastAdvanceTick = eve::SimulationTick::zero();

    /** @brief Create the canonical module-owned runtime referenced by this component. */
    [[nodiscard]] static eve::Result<ClimbingState> create();
    /** @brief Release the runtime once and clear this component's handle. */
    [[nodiscard]] eve::Result<void> releaseRuntime();
};

/** @brief Derived PostPhysics pose projection; gameplay validity never depends on these weights. */
struct ClimbingPoseProjection {
    ClimbingExecutionId executionId = ClimbingExecutionId::zero();
    Vec3                 leftHandAnchor;
    Vec3                 rightHandAnchor;
    float                leftHandWeight = 0.f;
    float                rightHandWeight = 0.f;
    float                leftFootWeight = 0.f;
    float                rightFootWeight = 0.f;
    float                pelvisWeight = 0.f;
    bool                 compactCollision = false;
};

/** @brief Bounded owning post-simulation event batch safe to dispatch after the ECS View closes. */
class ClimbingEventBatch {
public:
    static constexpr std::size_t Capacity = ClimbingRuntime::PendingEventCapacity;

    /** @brief Atomically replace the event batch or reject an impossible oversize input. */
    [[nodiscard]] eve::Result<void> replace(std::span<const ClimbingEvent> events,
                                             eve::SimulationTick tick);
    /** @brief Immutable synchronous event view. */
    [[nodiscard]] std::span<const ClimbingEvent> values() const noexcept { return {values_.data(), size_}; }
    /** @brief Simulation tick at which the runtime queue was drained. */
    [[nodiscard]] eve::SimulationTick tick() const noexcept { return tick_; }

private:
    std::array<ClimbingEvent, Capacity> values_{};
    std::size_t                         size_ = 0;
    eve::SimulationTick                tick_ = eve::SimulationTick::zero();
};

/** @brief Static review/tooling description of one concrete climbing ECS phase. */
struct ClimbingSystemContract {
    std::string_view name;
    std::string_view entityScope;
    std::string_view view;
    std::string_view readSet;
    std::string_view writeSet;
    std::string_view structuralChanges;
    std::string_view events;
    std::string_view services;
    std::string_view phase;
    std::string_view determinism;
};

/** @brief Return immutable process-lifetime contracts for the four climbing ECS phases. */
[[nodiscard]] std::span<const ClimbingSystemContract> climbingSystemContracts() noexcept;

namespace detail {
template <class T>
eve::Result<T> climbingEcsFailure(eve::DiagnosticCode code, std::string message, std::string path) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "climbing.ecs"));
}

inline bool activeClimbingPhase(ClimbingPhase phase) noexcept {
    return phase != ClimbingPhase::Idle && phase != ClimbingPhase::Completed &&
           phase != ClimbingPhase::Cancelled && phase != ClimbingPhase::Failed;
}
}  // namespace detail

/** @brief PrePhysics candidate producer over a caller-selected existing domain root. */
class ClimbingProbeSystem {
public:
    /**
     * @brief Probe every entity in `View<EntityRoot, Body, Intent, State, Links, CandidateBuffer>`.
     * @tparam EntityRoot Existing project/domain short root; no climbing entity base is introduced.
     * @param world Synchronously borrowed Physics world; never retained.
     * @param tick Injected deterministic tick labeling every produced buffer.
     * @return Number of processed entities, or the first structured failure.
     * @thread Owning ECS and Physics simulation thread.
     * @reentrancy Does not invoke scripts or callbacks.
     */
    template <class EntityRoot>
    [[nodiscard]] static eve::Result<std::size_t> step(physics::World3D& world,
                                                        eve::SimulationTick tick) {
        std::size_t processed = 0;
        auto view = ecs::View<EntityRoot, ClimbingBody, ClimbingIntent, ClimbingState,
                              ClimbingLinks, ClimbingCandidateBuffer>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [body, intent, state, links, buffer] = *it;
            (void)intent;
            auto runtime = Climbing::resolve(state->runtime);
            if (!runtime.isBound())
                return detail::climbingEcsFailure<std::size_t>(eve::DiagnosticCode::StaleHandle,
                                                                "climbing runtime link is stale",
                                                                "state.runtime");
            auto linkedBody = links->physicsBody.resolve(world);
            if (!linkedBody)
                return eve::Result<std::size_t>::failure(linkedBody.status());
            const bool hasEligibleIntent = std::any_of(intent->commands.begin(), intent->commands.end(),
                                                       [&](const BufferedClimbingCommand& command) {
                                                           return command.consumedExecutionId.isZero() &&
                                                                  command.pressedTick <= tick && tick <= command.expiryTick;
                                                       });
            if (!hasEligibleIntent) {
                buffer->clear();
                ++processed;
                continue;
            }
            const ClimbingPose pose{body->feet, body->forward,
                                    std::sqrt(body->velocity.x * body->velocity.x +
                                              body->velocity.z * body->velocity.z),
                                    linkedBody.value()->getId(), body->velocity.y, body->grounded,
                                    intent->move, intent->look, intent->mode};
            auto probed = buffer->probe(*runtime, world, pose, tick);
            if (!probed) return eve::Result<std::size_t>::failure(probed.status());
            ++processed;
        }
        return eve::Result<std::size_t>::success(processed);
    }
};

/** @brief PrePhysics selection/commit consumer over caller-selected domain entities. */
class ClimbingEcsSelectionSystem {
public:
    /**
     * @brief Consume at most one buffered command and commit one execution per visible entity.
     * @remarks Runtime and intent remain unchanged when selection fails for that entity. No callbacks are invoked.
     */
    template <class EntityRoot>
    [[nodiscard]] static eve::Result<std::size_t> step(physics::World3D& world,
                                                        ClimbingCommand command,
                                                        eve::SimulationTick tick) {
        std::size_t started = 0;
        auto view = ecs::View<EntityRoot, ClimbingBody, ClimbingIntent, ClimbingState, ClimbingLinks>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [body, intent, state, links] = *it;
            if (!ClimbingInputSystem::peek(*intent, command, tick)) continue;
            auto runtime = Climbing::resolve(state->runtime);
            if (!runtime.isBound())
                return detail::climbingEcsFailure<std::size_t>(eve::DiagnosticCode::StaleHandle,
                                                                "climbing runtime link is stale",
                                                                "state.runtime");
            auto linkedBody = links->physicsBody.resolve(world);
            if (!linkedBody) return eve::Result<std::size_t>::failure(linkedBody.status());
            const ClimbingPose pose{body->feet, body->forward,
                                    std::sqrt(body->velocity.x * body->velocity.x +
                                              body->velocity.z * body->velocity.z),
                                    linkedBody.value()->getId(), body->velocity.y, body->grounded,
                                    intent->move, intent->look, intent->mode};
            auto selected = ClimbingSelectionSystem::tryStart(*runtime, world, pose, *intent, command,
                                                               tick, body->lastGroundedTick);
            if (!selected) return eve::Result<std::size_t>::failure(selected.status());
            ++started;
        }
        return eve::Result<std::size_t>::success(started);
    }
};

/** @brief Physics-phase authoritative capsule motion consumer. */
class ClimbingMotionSystem {
public:
    /**
     * @brief Advance active executions and publish the corrected feet transform to Body and linked Physics body.
     * @param motion Owning per-step animation delta shared by this homogeneous batch; never retained.
     */
    template <class EntityRoot>
    [[nodiscard]] static eve::Result<std::size_t> step(physics::World3D& world,
                                                        const eve::SimulationStep& step,
                                                        const ClimbingMotionInput& motion = {}) {
        if (step.delta.nanoseconds() <= 0)
            return detail::climbingEcsFailure<std::size_t>(eve::DiagnosticCode::InvalidArgument,
                                                            "climbing motion delta must be positive",
                                                            "step.delta");
        std::size_t processed = 0;
        auto view = ecs::View<EntityRoot, ClimbingBody, ClimbingIntent, ClimbingState, ClimbingLinks>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [body, intent, state, links] = *it;
            auto runtime = Climbing::resolve(state->runtime);
            if (!runtime.isBound())
                return detail::climbingEcsFailure<std::size_t>(eve::DiagnosticCode::StaleHandle,
                                                                "climbing runtime link is stale",
                                                                "state.runtime");
            auto linkedBody = links->physicsBody.resolve(world);
            if (!linkedBody) return eve::Result<std::size_t>::failure(linkedBody.status());
            if (!detail::activeClimbingPhase(runtime->phase())) {
                const auto ordinaryStart = std::chrono::steady_clock::now();
                if (!(body->capsuleRadius > 0.f) ||
                    !(body->capsuleHeight > body->capsuleRadius * 2.f) || body->skin < 0.f ||
                    body->groundSnap < 0.f || body->stepHeight < 0.f ||
                    !std::isfinite(body->maxSlopeRadians) || body->maxSlopeRadians < 0.f ||
                    body->maxSlopeRadians >= 1.57079633f)
                    return detail::climbingEcsFailure<std::size_t>(eve::DiagnosticCode::InvalidArgument,
                                                                    "ordinary locomotion capsule is invalid",
                                                                    "body.capsule");
                const ClimbingLocomotionPolicy policy = runtime->locomotionPolicy();
                const float deltaSeconds = static_cast<float>(step.delta.seconds());
                const float moveLength = std::sqrt(intent->move.x * intent->move.x +
                                                   intent->move.z * intent->move.z);
                const Vec3 desiredVelocity{intent->move.x, 0.f, intent->move.z};
                const float acceleration = body->grounded
                                               ? (moveLength > 1e-6f ? policy.groundAcceleration
                                                                     : policy.groundBraking)
                                               : policy.groundAcceleration * policy.airControl;
                const float maxVelocityChange = acceleration * deltaSeconds;
                const Vec3 horizontalError{desiredVelocity.x - body->velocity.x, 0.f,
                                           desiredVelocity.z - body->velocity.z};
                const float errorLength = std::sqrt(horizontalError.x * horizontalError.x +
                                                    horizontalError.z * horizontalError.z);
                if (errorLength > maxVelocityChange && errorLength > 1e-6f) {
                    const float scale = maxVelocityChange / errorLength;
                    body->velocity.x += horizontalError.x * scale;
                    body->velocity.z += horizontalError.z * scale;
                } else {
                    body->velocity.x = desiredVelocity.x;
                    body->velocity.z = desiredVelocity.z;
                }

                const auto jump = ClimbingInputSystem::peek(*intent, ClimbingCommand::Jump, step.tick);
                const bool freshJump = jump && (!body->hasOrdinaryJump ||
                                                jump->pressedTick != body->lastOrdinaryJumpPressedTick);
                if (freshJump &&
                    (body->grounded ||
                     ClimbingInputSystem::coyoteWindowState(step.tick, body->lastGroundedTick,
                                                            policy.coyoteTicks) == ClimbingCoyoteState::Eligible)) {
                    body->velocity.y = policy.jumpSpeed;
                    body->grounded = false;
                    body->lastOrdinaryJumpPressedTick = jump->pressedTick;
                    body->hasOrdinaryJump = true;
                } else if (!body->grounded) {
                    body->velocity.y -= policy.gravity * deltaSeconds;
                } else {
                    body->velocity.y = 0.f;
                }

                physics::QueryFilter3D filter = policy.queryFilter;
                filter.ignoredBodyId = linkedBody.value()->getId();
                const physics::CapsuleMovePolicy3D moverPolicy{
                    body->up.x, body->up.y, body->up.z, body->maxSlopeRadians};
                const float lowerY = body->feet.y + body->capsuleRadius + body->skin;
                const float upperY = body->feet.y + body->capsuleHeight - body->capsuleRadius + body->skin;
                const float snap = body->grounded && !freshJump ? body->groundSnap : 0.f;
                auto moved = world.moveCapsuleOwned(
                    body->feet.x, lowerY, body->feet.z, body->feet.x, upperY, body->feet.z,
                    body->capsuleRadius, body->velocity.x * deltaSeconds,
                    body->velocity.y * deltaSeconds - snap, body->velocity.z * deltaSeconds,
                    filter, moverPolicy);
                if (!moved) return eve::Result<std::size_t>::failure(moved.status());
                physics::CapsuleMove3D movement = std::move(moved).takeValue();
                std::uint32_t queryCount = 1;
                std::uint32_t moverIterations = static_cast<std::uint32_t>(movement.iterations);
                const float desiredX = body->velocity.x * deltaSeconds;
                const float desiredZ = body->velocity.z * deltaSeconds;
                const float desiredHorizontal = std::sqrt(desiredX * desiredX + desiredZ * desiredZ);
                const float directHorizontal = std::sqrt(movement.deltaX * movement.deltaX +
                                                         movement.deltaZ * movement.deltaZ);
                if (body->grounded && !freshJump && body->stepHeight > 0.f &&
                    desiredHorizontal > 1e-6f && directHorizontal + body->skin < desiredHorizontal) {
                    auto raised = world.moveCapsuleOwned(
                        body->feet.x, lowerY, body->feet.z, body->feet.x, upperY, body->feet.z,
                        body->capsuleRadius, 0.f, body->stepHeight, 0.f, filter, moverPolicy);
                    if (!raised) return eve::Result<std::size_t>::failure(raised.status());
                    ++queryCount;
                    const physics::CapsuleMove3D rise = std::move(raised).takeValue();
                    moverIterations += static_cast<std::uint32_t>(rise.iterations);
                    if (rise.deltaY + body->skin >= body->stepHeight) {
                        auto crossed = world.moveCapsuleOwned(
                            body->feet.x, lowerY + rise.deltaY, body->feet.z,
                            body->feet.x, upperY + rise.deltaY, body->feet.z,
                            body->capsuleRadius, desiredX, 0.f, desiredZ, filter, moverPolicy);
                        if (!crossed) return eve::Result<std::size_t>::failure(crossed.status());
                        ++queryCount;
                        const physics::CapsuleMove3D across = std::move(crossed).takeValue();
                        moverIterations += static_cast<std::uint32_t>(across.iterations);
                        const float steppedHorizontal = std::sqrt(across.deltaX * across.deltaX +
                                                                  across.deltaZ * across.deltaZ);
                        auto lowered = world.moveCapsuleOwned(
                            body->feet.x + across.deltaX, lowerY + rise.deltaY,
                            body->feet.z + across.deltaZ, body->feet.x + across.deltaX,
                            upperY + rise.deltaY, body->feet.z + across.deltaZ,
                            body->capsuleRadius, 0.f,
                            -(rise.deltaY + body->groundSnap + body->skin), 0.f, filter, moverPolicy);
                        if (!lowered) return eve::Result<std::size_t>::failure(lowered.status());
                        ++queryCount;
                        const physics::CapsuleMove3D down = std::move(lowered).takeValue();
                        moverIterations += static_cast<std::uint32_t>(down.iterations);
                        if (down.grounded && steppedHorizontal > directHorizontal + body->skin) {
                            movement.constrained = rise.constrained || across.constrained || down.constrained;
                            movement.grounded = true;
                            movement.deltaX = across.deltaX;
                            movement.deltaY = rise.deltaY + down.deltaY;
                            movement.deltaZ = across.deltaZ;
                            movement.normalX = down.normalX;
                            movement.normalY = down.normalY;
                            movement.normalZ = down.normalZ;
                            movement.planeCount = rise.planeCount + across.planeCount + down.planeCount;
                            movement.iterations = rise.iterations + across.iterations + down.iterations;
                        }
                    }
                }
                body->feet = {body->feet.x + movement.deltaX, body->feet.y + movement.deltaY,
                              body->feet.z + movement.deltaZ};
                body->grounded = movement.grounded && body->velocity.y <= 0.f;
                body->mode = body->grounded ? ClimbingMovementMode::Grounded
                                            : ClimbingMovementMode::Airborne;
                body->velocity.x = movement.deltaX / deltaSeconds;
                body->velocity.z = movement.deltaZ / deltaSeconds;
                body->velocity.y = body->grounded ? 0.f : movement.deltaY / deltaSeconds;
                if (body->grounded) {
                    body->groundNormal = {movement.normalX, movement.normalY, movement.normalZ};
                    body->lastStableFeet = body->feet;
                    body->lastGroundedTick = step.tick;
                }
                if (moveLength > 1e-6f)
                    body->forward = {intent->move.x / moveLength, 0.f, intent->move.z / moveLength};
                linkedBody.value()->setPosition(body->feet.x,
                                                 body->feet.y + body->capsuleHeight * 0.5f,
                                                 body->feet.z);
                const auto ordinaryElapsed = std::chrono::steady_clock::now() - ordinaryStart;
                runtime->recordOrdinaryTick(
                    step.tick, queryCount, moverIterations,
                    static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                   ordinaryElapsed)
                                                   .count()));
                ++processed;
                continue;
            }
            const Vec3 previous = body->feet;
            auto advanced = runtime->advance(world, step, motion);
            if (!advanced) return eve::Result<std::size_t>::failure(advanced.status());
            state->lastAdvance = std::move(advanced).takeValue();
            state->lastAdvanceTick = step.tick;
            body->feet = state->lastAdvance.feet;
            const float inverseDelta = static_cast<float>(1.0 / step.delta.seconds());
            body->velocity = {(body->feet.x - previous.x) * inverseDelta,
                              (body->feet.y - previous.y) * inverseDelta,
                              (body->feet.z - previous.z) * inverseDelta};
            if (state->lastAdvance.hasTerminalVelocity)
                body->velocity = state->lastAdvance.terminalVelocity;
            body->grounded = state->lastAdvance.grounded;
            body->mode = detail::activeClimbingPhase(state->lastAdvance.phase)
                             ? ClimbingMovementMode::Climbing
                             : (body->grounded ? ClimbingMovementMode::Grounded
                                               : ClimbingMovementMode::Airborne);
            if (body->grounded) {
                body->lastStableFeet = body->feet;
                body->lastGroundedTick = step.tick;
            }
            linkedBody.value()->setPosition(body->feet.x, body->feet.y + body->capsuleHeight * 0.5f,
                                             body->feet.z);
            ++processed;
        }
        return eve::Result<std::size_t>::success(processed);
    }
};

/** @brief PostPhysics derived pose and owning event-drain producer. */
class ClimbingPoseSystem {
public:
    /**
     * @brief Project pose constraints and drain runtime events without invoking external consumers inside the View.
     * @return Number of entities whose derived projection was refreshed.
     */
    template <class EntityRoot>
    [[nodiscard]] static eve::Result<std::size_t> step(eve::SimulationTick tick) {
        std::size_t processed = 0;
        auto view = ecs::View<EntityRoot, ClimbingState, ClimbingPoseProjection, ClimbingEventBatch>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [state, pose, events] = *it;
            auto runtime = Climbing::resolve(state->runtime);
            if (!runtime.isBound())
                return detail::climbingEcsFailure<std::size_t>(eve::DiagnosticCode::StaleHandle,
                                                                "climbing runtime link is stale",
                                                                "state.runtime");
            pose->executionId = state->lastAdvance.executionId;
            pose->leftHandAnchor = state->lastAdvance.leftHandAnchor;
            pose->rightHandAnchor = state->lastAdvance.rightHandAnchor;
            pose->leftHandWeight = state->lastAdvance.leftHandWeight;
            pose->rightHandWeight = state->lastAdvance.rightHandWeight;
            pose->leftFootWeight = state->lastAdvance.leftFootWeight;
            pose->rightFootWeight = state->lastAdvance.rightFootWeight;
            pose->pelvisWeight = state->lastAdvance.pelvisWeight;
            pose->compactCollision = state->lastAdvance.compactCollisionActive;
            auto drained = runtime->drainEvents();
            if (!drained) return eve::Result<std::size_t>::failure(drained.status());
            auto replaced = events->replace(drained.value(), tick);
            if (!replaced) return eve::Result<std::size_t>::failure(replaced.status());
            ++processed;
        }
        return eve::Result<std::size_t>::success(processed);
    }
};

}  // namespace eve::climbing
