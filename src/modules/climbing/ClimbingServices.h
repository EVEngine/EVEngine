#pragma once

/**
 * @file ClimbingServices.h
 * @brief Consumer-owned contracts for optional stamina, event, and pose integrations.
 */

#include "climbing/ClimbingInput.h"
#include "common/StrongUint64.h"

#include <span>
#include <string>
#include <string_view>

namespace eve::climbing {

namespace detail {
struct ClimbingServiceSubjectTag {};
struct ClimbingStaminaReservationTag {};
}  // namespace detail

/** @brief Stable gameplay-owned subject identity passed to optional adapters. */
using ClimbingServiceSubject = eve::detail::StrongUint64<detail::ClimbingServiceSubjectTag>;
/** @brief Opaque reservation owned by the stamina authority between prepare and commit/cancel. */
using ClimbingStaminaReservation = eve::detail::StrongUint64<detail::ClimbingStaminaReservationTag>;

/** @brief Observable outcome of one optional-service interaction. */
enum class ClimbingOptionalServiceState : std::uint8_t { Disabled, ProviderAbsent, Applied };

/**
 * @brief Consumer-owned stamina reservation contract.
 *
 * The provider remains the only stamina authority. A successful prepare guarantees that commitPrepared is
 * infallible. All methods run synchronously on the simulation owner thread and must not re-enter climbing.
 */
class IClimbingStaminaAuthority {
public:
    static constexpr const char* capabilityName = "eve.climbing.stamina-authority.v1";
    virtual ~IClimbingStaminaAuthority() = default;

    /** @brief Stable adapter identity matched against ClimbingProfileDefinition::staminaAdapter. */
    [[nodiscard]] virtual std::string_view adapterId() const noexcept = 0;

    /** @brief Reserve an exact non-negative cost without publishing the debit. */
    [[nodiscard]] virtual eve::Result<ClimbingStaminaReservation> prepare(
        ClimbingServiceSubject subject, float cost, eve::SimulationTick tick) = 0;
    /** @brief Publish a successfully prepared debit; guaranteed infallible by the provider contract. */
    virtual void commitPrepared(ClimbingStaminaReservation reservation,
                                ClimbingExecutionId executionId) noexcept = 0;
    /** @brief Release an uncommitted reservation; guaranteed infallible and idempotent. */
    virtual void cancelPrepared(ClimbingStaminaReservation reservation) noexcept = 0;
};

/** @brief Strong result of evaluating all data-driven action condition tags. */
enum class ClimbingConditionDecision : std::uint8_t { Allowed, Denied };

/**
 * @brief Consumer-owned authority for project-specific condition tags.
 *
 * The provider is required when an action declares condition tags. It receives an immutable owning-definition view
 * synchronously and must not retain it or re-enter climbing.
 */
class IClimbingConditionAuthority {
public:
    static constexpr const char* capabilityName = "eve.climbing.condition-authority.v1";
    virtual ~IClimbingConditionAuthority() = default;
    /** @brief Evaluate every required tag for one subject at the injected simulation tick. */
    [[nodiscard]] virtual eve::Result<ClimbingConditionDecision> evaluate(
        ClimbingServiceSubject subject, std::span<const std::string> requiredTags,
        eve::SimulationTick tick) = 0;
};

/** @brief Optional post-simulation event adapter; it does not own the runtime event queue. */
class IClimbingEventSink {
public:
    static constexpr const char* capabilityName = "eve.climbing.event-sink.v1";
    virtual ~IClimbingEventSink() = default;
    /** @brief Consume an owning event view after all ECS simulation views have closed. */
    [[nodiscard]] virtual eve::Result<void> publish(ClimbingServiceSubject subject,
                                                     std::span<const ClimbingEvent> events) = 0;
};

/** @brief Optional post-physics Animation/IK adapter consuming derived pose constraints only. */
class IClimbingPoseAdapter {
public:
    static constexpr const char* capabilityName = "eve.climbing.pose-adapter.v1";
    virtual ~IClimbingPoseAdapter() = default;
    /** @brief Apply one derived pose projection; failure never invalidates authoritative climbing motion. */
    [[nodiscard]] virtual eve::Result<void> apply(ClimbingServiceSubject subject,
                                                   const ClimbingAdvance& advance) = 0;
};

/** @brief Result of a service-aware selection transaction. */
struct ClimbingServiceStart {
    ClimbingStart                 start;
    ClimbingOptionalServiceState stamina = ClimbingOptionalServiceState::Disabled;
};

/** @brief Selection transaction that atomically joins external stamina reservation to runtime publication. */
class ClimbingServiceSelectionSystem {
public:
    /**
     * @brief Start one action and publish any configured stamina debit exactly once.
     * @remarks Input, runtime, and stamina reservation are unchanged/released on every failure path.
     */
    [[nodiscard]] static eve::Result<ClimbingServiceStart> tryStart(
        ClimbingRuntime& runtime, physics::World3D& world, const ClimbingPose& pose, ClimbingIntent& intent,
        ClimbingCommand command, eve::SimulationTick tick, eve::SimulationTick lastGroundedTick,
        ClimbingServiceSubject subject);
};

/** @brief Dispatch to the optional event sink, returning ProviderAbsent instead of silently dropping. */
[[nodiscard]] eve::Result<ClimbingOptionalServiceState> dispatchClimbingEvents(
    ClimbingServiceSubject subject, std::span<const ClimbingEvent> events);

/** @brief Dispatch to the optional pose/IK adapter, returning ProviderAbsent when trimmed out. */
[[nodiscard]] eve::Result<ClimbingOptionalServiceState> applyClimbingPose(
    ClimbingServiceSubject subject, const ClimbingAdvance& advance);

}  // namespace eve::climbing
