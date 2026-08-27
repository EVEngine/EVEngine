#pragma once

/**
 * @file RTSSystems.h
 * @brief Deterministic phase-one RTS systems and their ECS contracts.
 */

#include "rts/RTSAction.h"

#include <cstddef>
#include <span>
#include <vector>

namespace eve::rts {

/** @brief Deterministic formation layout strategies. */
enum class FormationKind : std::uint8_t {
    Line,
    Grid,
    Wedge,
};

/** @brief Input to the pure formation planner. */
struct FormationSpec {
    FormationKind kind = FormationKind::Line;
    float spacing = 32.0f;
    int columns = 0;

    /** @brief Validate spacing and grid column constraints. */
    [[nodiscard]] Result<void> validate() const;
};

/** @brief Static metadata for one ECS system's access and phase contract. */
struct SystemContract {
    const char* name = "";
    const char* view = "";
    const char* readSet = "";
    const char* writeSet = "";
    const char* structuralChanges = "";
    const char* events = "";
    const char* phase = "";
};

/** @brief Return the phase-one RTS ECS contracts for tooling and review. */
[[nodiscard]] std::span<const SystemContract> systemContracts() noexcept;

/**
 * @brief Pure deterministic formation planner.
 *
 * Positions are generated in input order. No wall clock or random stream is
 * read, so replay and lockstep callers receive the same layout.
 */
class FormationPlanner {
public:
    /**
     * @brief Plan positions around an anchor for a fixed unit count.
     * @param count Number of positions to produce.
     * @param anchor Formation anchor position.
     * @param spec Layout strategy and spacing.
     * @return Exactly `count` positions or a structured validation failure.
     */
    [[nodiscard]] static Result<std::vector<WorldPosition>> plan(
        std::size_t count, WorldPosition anchor, const FormationSpec& spec);
};

/** @brief Receipt containing all order ids accepted by command fan-out. */
struct FanOutReceipt {
    std::size_t requested = 0;
    std::size_t accepted = 0;
    std::vector<std::string> orderIds;
};

/**
 * @brief Validates a typed selection and fans one command out in formation order.
 *
 * This is a command boundary, not a new order implementation. It writes only
 * the selected units' generic OrderComponent and performs no structural ECS
 * mutation while its validation View is alive.
 */
class CommandFanOutSystem {
public:
    /**
     * @brief Submit one command to every selected live Unit.
     * @param unitHandles Generation-checked handles in player selection order.
     * @param command Common Move/Attack/Build/Gather command.
     * @param formation Deterministic target offset policy.
     * @return Accepted order ids in selection order, or a failure with no
     *         partially accepted command exposed to the caller.
     */
    [[nodiscard]] static Result<FanOutReceipt> fanOut(
        std::span<const ecs::EntityHandle> unitHandles, const CommandSpec& command,
        const FormationSpec& formation);
};

/** @brief Moves units toward their active generic order targets. */
class MotionSystem {
public:
    /**
     * @brief Advance all Units with Motion and Orders components.
     * @param step Injected deterministic simulation step.
     * @return Number of units inspected, or a structured failure.
     */
    [[nodiscard]] static Result<std::size_t> step(const SimulationStep& step);
};

/** @brief Sends active generic orders to one shared action executor. */
class OrderActionSystem {
public:
    /**
     * @brief Execute/advance active Unit orders and complete them when done.
     * @param step Injected deterministic simulation step.
     * @param executor Borrowed action adapter; it must outlive this call.
     * @return Number of action-bearing units inspected, or a structured failure.
     */
    [[nodiscard]] static Result<std::size_t> step(const SimulationStep& step,
                                                   IRTSActionExecutor& executor);
};

/** @brief Advances Building production queues without duplicating task state. */
class BuildingProductionSystem {
public:
    /** @brief Advance all Buildings with a Production component. */
    [[nodiscard]] static Result<std::size_t> step(const SimulationStep& step);
};

/** @brief Advances lifecycle-only effects on Units and Buildings. */
class EffectSystem {
public:
    /** @brief Advance all attached effects by the injected simulation delta. */
    [[nodiscard]] static Result<std::size_t> step(const SimulationStep& step);
};

}  // namespace eve::rts
