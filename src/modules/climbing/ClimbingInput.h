#pragma once

/**
 * @file ClimbingInput.h
 * @brief Tick-addressed command buffering for deterministic climbing input.
 */

#include "climbing/Climbing.h"
#include "common/Result.h"
#include "common/Time.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace eve::climbing {

/** @brief Edge and held commands understood by the climbing input phase. */
enum class ClimbingCommand : std::uint8_t { Jump, Climb, Drop, Sprint, Crouch };

/** @brief Named result of evaluating the deterministic grounded grace window. */
enum class ClimbingCoyoteState : std::uint8_t { Outside, Eligible };

/** @brief One owning edge command with deterministic lifetime and consumption evidence. */
struct BufferedClimbingCommand {
    ClimbingCommand     command             = ClimbingCommand::Climb;
    eve::SimulationTick pressedTick         = eve::SimulationTick::zero();
    eve::SimulationTick expiryTick          = eve::SimulationTick::zero();
    ClimbingExecutionId consumedExecutionId = ClimbingExecutionId::zero();

    friend bool operator==(const BufferedClimbingCommand&, const BufferedClimbingCommand&) = default;
};

/** @brief Hot authoritative intent component; contains no device or callback pointers. */
struct ClimbingIntent {
    Vec3                                 move;
    Vec3                                 look{0.f, 0.f, 1.f};
    ClimbingInputMode                    mode = ClimbingInputMode::Flow;
    std::vector<BufferedClimbingCommand> commands;
};

/**
 * @brief Input-phase operations over a ClimbingIntent component.
 *
 * Structural mutation: none. Read service: injected SimulationTick only. The system invokes no callbacks and
 * produces bit-exact results for the same command stream.
 */
class ClimbingInputSystem {
public:
    /** @brief Hard safety bound preventing an untrusted producer from growing intent indefinitely. */
    static constexpr std::size_t MaxBufferedCommands = 16;

    /** @brief Queue one edge command with expiry derived from an exact tick budget. */
    [[nodiscard]] static eve::Result<void> submit(ClimbingIntent& intent, ClimbingCommand command,
                                                  eve::SimulationTick pressedTick, std::uint64_t bufferTicks);

    /** @brief Queue one edge command using the authoritative profile input window. */
    [[nodiscard]] static eve::Result<void> submit(ClimbingIntent& intent, ClimbingCommand command,
                                                  eve::SimulationTick              pressedTick,
                                                  const ClimbingProfileDefinition& profile);

    /** @brief Remove expired and already-consumed records; returns the number removed. */
    [[nodiscard]] static std::size_t prune(ClimbingIntent& intent, eve::SimulationTick currentTick) noexcept;

    /**
     * @brief Atomically mark the oldest eligible command consumed by one committed execution.
     * @return Owning consumed record, or a successful empty optional when no eligible command exists.
     */
    [[nodiscard]] static eve::Result<std::optional<BufferedClimbingCommand>> consume(ClimbingIntent&     intent,
                                                                                     ClimbingCommand     command,
                                                                                     eve::SimulationTick currentTick,
                                                                                     ClimbingExecutionId executionId);

    /** @brief Observe the oldest eligible command without consuming or pruning intent. */
    [[nodiscard]] static std::optional<BufferedClimbingCommand> peek(const ClimbingIntent& intent,
                                                                     ClimbingCommand       command,
                                                                     eve::SimulationTick   currentTick) noexcept;

    /** @brief Evaluate a grounded grace window without reading wall clock. */
    [[nodiscard]] static ClimbingCoyoteState coyoteWindowState(eve::SimulationTick currentTick,
                                                               eve::SimulationTick lastGroundedTick,
                                                               std::uint64_t        coyoteTicks) noexcept;
};

/**
 * @brief PrePhysics selection transaction joining buffered input to one authoritative execution.
 *
 * Reads intent/body/definitions and synchronously borrowed Physics queries; writes intent and runtime only at commit.
 * It performs no structural mutation and invokes no callbacks or scripts.
 */
class ClimbingSelectionSystem {
public:
    /**
     * @brief Start the best candidate and consume its input edge exactly once.
     * @param lastGroundedTick Last authoritative grounded tick used for profile-configured coyote time.
     * @return Committed execution evidence, or a failure with both runtime and intent unchanged.
     */
    [[nodiscard]] static eve::Result<ClimbingStart> tryStart(ClimbingRuntime& runtime, physics::World3D& world,
                                                             const ClimbingPose& pose, ClimbingIntent& intent,
                                                             ClimbingCommand command, eve::SimulationTick tick,
                                                             eve::SimulationTick lastGroundedTick);
};

}  // namespace eve::climbing
