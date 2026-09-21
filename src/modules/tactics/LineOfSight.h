#pragma once

/**
 * @file LineOfSight.h
 * @brief Injectable line-of-sight and cover policies over the tactical board.
 *
 * The gap this closes: a turn-based tactics game needs "can this unit see that cell" and
 * "how much cover does that cell give", and the reference framework answers both inside its
 * per-ability targeting code. That makes the answer untestable, unreusable and impossible to
 * replace with a different board shape.
 *
 * Here both questions are interfaces with **named capabilities**, plus one built-in grid
 * implementation that understands the three board topologies tactics already supports. The
 * interfaces are the seam a different board (a hex map with its own projection, a project's
 * own occlusion rules) plugs into: it registers its own provider instead of the tactics core
 * growing a second geometry.
 *
 * Blocking is read from the persisted cell **tags** rather than from a new field: `passable`
 * already means "may be entered", and a low wall or a smoke cloud blocks sight without
 * blocking movement. Reading a tag keeps sight a separate fact that costs no schema version,
 * and the tag name is a named constant so it is not a magic string.
 *
 * This header has no rendering or ECS dependency: policies read `BoardState` values only.
 */

#include "common/Result.h"
#include "tactics/TacticsPath.h"
#include "tactics/TacticsTypes.h"

#include <memory>
#include <string_view>
#include <vector>

namespace eve::tactics {

/** @brief Cell tag that makes a cell block line of sight. */
inline constexpr std::string_view kSightBlockerTag = "sight_blocker";

/** @brief How much protection a cell gives against an attacker. */
enum class CoverLevel : std::uint8_t {
    /** @brief No protection. */
    None,
    /** @brief Partial protection: an adjacent blocker, but not on the incoming line. */
    Half,
    /** @brief Full protection: a blocker sits between the attacker and the target. */
    Full,
};

/** @brief Stable protocol spelling of a cover level. */
[[nodiscard]] EVENGINE_API_DOMAINS std::string_view coverLevelName(CoverLevel level) noexcept;

/**
 * @brief Answers "can @p from see @p to" for one board.
 *
 * @remarks Implementations must be pure and deterministic: the answer feeds replay, so it
 *          may not depend on a clock, a random draw or mutable state. Symmetry is a
 *          property of an implementation, not something the caller may assume.
 */
class ILineOfSightPolicy {
public:
    /** @brief Capability name a provider registers under. */
    static constexpr const char* capabilityName = "eve.tactics.ILineOfSightPolicy";

    virtual ~ILineOfSightPolicy() = default;

    /** @brief Stable id of this implementation, for diagnostics and configuration. */
    [[nodiscard]] virtual std::string_view id() const noexcept = 0;

    /**
     * @brief Report whether @p from can see @p to.
     * @return Whether sight is unobstructed, or a structured refusal for inputs the policy
     *         cannot answer (cells on different layers, or cells the board does not contain).
     * @remarks Endpoints never block: a unit standing in smoke can still see out of its own
     *          cell, and a blocker on the target cell protects the target without hiding it.
     */
    [[nodiscard]] virtual Result<bool> visible(const BoardState& board, Cell from, Cell to) const = 0;
};

/**
 * @brief Answers "how much cover does @p target have against @p attacker".
 *
 * @remarks Kept separate from line of sight on purpose: "cannot see" and "sees but the shot
 *          is absorbed" are different rules with different gameplay, and one interface that
 *          returned both would force every caller to interpret a combined result.
 */
class ICoverPolicy {
public:
    /** @brief Capability name a provider registers under. */
    static constexpr const char* capabilityName = "eve.tactics.ICoverPolicy";

    virtual ~ICoverPolicy() = default;

    /** @brief Stable id of this implementation. */
    [[nodiscard]] virtual std::string_view id() const noexcept = 0;

    /**
     * @brief Report the cover @p target has against @p attacker.
     * @return The cover level, or a structured refusal for cells the policy cannot use.
     */
    [[nodiscard]] virtual Result<CoverLevel> cover(const BoardState& board, Cell attacker, Cell target) const = 0;
};

/**
 * @brief The built-in grid policy: a straight-line trace over the board's own topology.
 *
 * Rules, all deterministic and topology-aware:
 *  - the trace runs between the endpoints only; both endpoints are ignored, so standing in or
 *    behind a blocker never hides the cells themselves;
 *  - any traced cell that is tagged @ref kSightBlockerTag blocks the trace;
 *  - a traced cell the board does not contain blocks as well, so an unused part of the grid is
 *    not a free firing lane;
 *  - different layers are refused rather than projected, because "seeing between floors" is a
 *    rule this policy does not own.
 *
 * The trace is computed from the canonical cell order and then oriented to the query, which
 * makes `visible(a, b)` and `visible(b, a)` agree **by construction** rather than by two
 * implementations happening to match.
 */
class GridLineOfSightPolicy final : public ILineOfSightPolicy {
public:
    [[nodiscard]] std::string_view id() const noexcept override { return "grid_line_of_sight"; }

    [[nodiscard]] Result<bool> visible(const BoardState& board, Cell from, Cell to) const override;
};

/**
 * @brief The built-in cover policy: blockers adjacent to the target, prioritising the line.
 *
 * `Full` when the straight trace from attacker to target is obstructed (something is in the
 * way), `Half` when the target merely has an adjacent blocker that is not on that trace, and
 * `None` otherwise. The distinction matters because the two outcomes are different gameplay:
 * "the shot is absorbed" versus "the target is harder to hit".
 */
class GridCoverPolicy final : public ICoverPolicy {
public:
    [[nodiscard]] std::string_view id() const noexcept override { return "grid_cover"; }

    [[nodiscard]] Result<CoverLevel> cover(const BoardState& board, Cell attacker, Cell target) const override;
};

/**
 * @brief The process-wide built-in line-of-sight policy.
 *
 * @return A shared handle to an immutable object. The pointer type is deliberately
 *         non-const because that is what `eve::cap::provide` registers, and every method on
 *         the interface is `const`, so the handle grants no way to change behaviour.
 */
[[nodiscard]] EVENGINE_API_DOMAINS std::shared_ptr<ILineOfSightPolicy> gridLineOfSightPolicy();
/** @brief The process-wide built-in cover policy (same ownership remarks as above). */
[[nodiscard]] EVENGINE_API_DOMAINS std::shared_ptr<ICoverPolicy> gridCoverPolicy();

/**
 * @brief Cells within a metric range of @p origin that @p policy can actually see.
 *
 * @param board Authoritative board facts.
 * @param policy Sight rule to apply; a caller may pass a project implementation.
 * @param origin Observer cell.
 * @param minimum Inclusive lower bound of the range, in @p metric units.
 * @param maximum Inclusive upper bound of the range.
 * @param metric Logical distance the caller wants the range measured in.
 * @return The visible cells in the board's deterministic cell order, or a structured refusal
 *         when the origin is unknown or the range bounds are invalid.
 * @remarks This is the query a UI needs to fill `InteractionContext::targetableCells`, and it
 *          deliberately composes the existing range query with the sight policy instead of
 *          re-deriving either: range and sight stay separate rules with one owner each.
 * @cost Linear in the number of cells the range query returns, times the trace length of each;
 *       the result is a projection of the current board and must be recomputed after any
 *       change to it.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<std::vector<Cell>> visibleCellsInRange(const BoardState&         board,
                                                                                 const ILineOfSightPolicy& policy,
                                                                                 Cell origin, int minimum, int maximum,
                                                                                 CellRangeMetric metric);

}  // namespace eve::tactics
