#pragma once

/**
 * @file RTSReplay.h
 * @brief Stable tick-addressed RTS command recording and replay.
 */

#include "rts/RTSSystems.h"

#include <map>

namespace eve::rts {

class RTS;
class IRTSActionExecutor;

/** @brief Stable operation kinds accepted by the shared deterministic RTS input stream. */
enum class RTSReplayOperation : std::uint8_t {
    UnitCommand,
    Construction,
    Production,
    Research,
    Ability,
    CancelProduction,
    CancelAbility,
    CancelConstruction,
    SellBuilding,
    ReinforcementProduction,
    RequestFireSupport,
    CancelFireSupport,
    SuppressArea,
    Escort,
};

/** @brief One process-independent command using stable subjects instead of ECS handles. */
struct RTSReplayCommand {
    SimulationTick tick{};
    RTSReplayOperation operation = RTSReplayOperation::UnitCommand;
    std::vector<SubjectRef> units;
    CommandSpec command;
    SubjectRef targetEntity;
    FormationSpec formation;
    SubjectRef producer;
    SubjectRef faction;
    SubjectRef resultSubject;
    LogicalId definition;
    std::string value;
    int priority = 0;
    std::size_t limit = 0;
    WorldPosition point;
};

/**
 * @brief Deterministic lockstep input buffer and portable command log.
 *
 * This class owns only input history. Simulation state and order execution
 * remain authoritative in RTS and the generic orders module.
 */
class RTSCommandLog {
public:
    /** @brief Validate, normalize, queue, and retain one future command. */
    [[nodiscard]] Result<void> queue(RTSReplayCommand command, SimulationTick currentTick = {});
    /** @brief Apply all commands for one tick through the existing RTS order boundary. */
    [[nodiscard]] Result<std::size_t> apply(SimulationTick tick, RTS& world);
    /** @brief Export retained commands in a locale-independent text format. */
    [[nodiscard]] std::string exportText() const;
    /** @brief Parse and atomically import a command log. */
    [[nodiscard]] Result<void> importText(std::string_view text, SimulationTick currentTick = {},
                                          bool clearExisting = true);
    /** @brief Remove queued and retained commands. */
    void clear() noexcept;
    /** @brief Number of retained commands. */
    [[nodiscard]] std::size_t size() const noexcept { return history_.size(); }

private:
    std::map<std::uint64_t, std::vector<RTSReplayCommand>> queued_;
    std::vector<RTSReplayCommand> history_;
};

/** @brief Fixed-step coordinator that applies lockstep inputs before the existing RTS system pipeline. */
class RTSLockstep {
public:
    /** @brief Construct with a 60 Hz fixed simulation interval. */
    RTSLockstep();
    /** @brief Set a finite, strictly positive fixed interval. */
    [[nodiscard]] Result<void> setFixedStep(Duration value);
    /**
     * @brief Advance an exact number of deterministic ticks.
     * @return Total accepted commands and processed RTS system records.
     */
    [[nodiscard]] Result<std::size_t> step(RTS& world, IRTSActionExecutor& executor,
                                           std::uint64_t count = 1);
    /** @brief Queue a command relative to the current lockstep timeline. */
    [[nodiscard]] Result<void> queue(RTSReplayCommand command) {
        return commands_.queue(std::move(command), tick_);
    }
    /** @brief Mutable command-log boundary for import/export tooling. */
    [[nodiscard]] RTSCommandLog& commands() noexcept { return commands_; }
    /** @brief Read-only command-log boundary. */
    [[nodiscard]] const RTSCommandLog& commands() const noexcept { return commands_; }
    /** @brief Current completed simulation tick. */
    [[nodiscard]] SimulationTick currentTick() const noexcept { return tick_; }
    /** @brief Configured fixed interval. */
    [[nodiscard]] Duration fixedStep() const noexcept { return fixedStep_; }
    /** @brief Reset the timeline and optionally retain the command history/queue. */
    void reset(SimulationTick tick = {}, bool clearCommands = true) noexcept;

private:
    SimulationTick tick_{};
    Duration fixedStep_{};
    RTSCommandLog commands_;
};

}  // namespace eve::rts
