#pragma once
#include "common/Export.h"


/**
 * @file TacticsTypes.h
 * @brief Deterministic board values and short-root ECS entities for tactics.
 */

#include "common/ECS.h"
#include "common/Identity.h"
#include "common/Result.h"
#include "common/Revision.h"
#include "common/SubjectRef.h"
#include "common/Time.h"

#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace eve::tactics {

/** @brief Logical board coordinate independent of rendering projection. */
struct Cell {
    int x     = 0;
    int y     = 0;
    int layer = 0;

    friend bool operator==(const Cell&, const Cell&) noexcept = default;
    friend auto operator<=>(const Cell& left, const Cell& right) noexcept {
        return std::tie(left.y, left.x, left.layer) <=> std::tie(right.y, right.x, right.layer);
    }
};

/** @brief Supported deterministic board-neighbour topologies. */
enum class BoardTopology : std::uint8_t { Square4, Square8, HexAxial };

/** @brief Tactical battle lifecycle. */
enum class BattleStatus : std::uint8_t { Setup, Running, Ended };

/** @brief Explicit, externally observable battle phase. */
enum class BattlePhase : std::uint8_t {
    Setup,
    BattleStart,
    RoundStart,
    TurnStart,
    Acting,
    Reaction,
    TurnEnd,
    RoundEnd,
    BattleEnd,
};

/** @brief Stable id of the built-in side-alternating turn policy. */
inline constexpr std::string_view kSideAlternatingPolicyId = "side_alternating";
/** @brief Stable id of the built-in per-unit initiative turn policy. */
inline constexpr std::string_view kInitiativePolicyId = "initiative";
/**
 * @brief Stable id of the built-in charge-time (CTB/ATB) turn policy.
 *
 * This policy schedules by accumulated charge instead of "one activation per unit
 * per round", so a unit with higher initiative really does act more often. See
 * `ChargeTimeBattlePolicy` for the exact gain/ready/consume rule.
 */
inline constexpr std::string_view kChargeTimeBattlePolicyId = "charge_time_battle";
/**
 * @brief Charge a unit must accumulate before it may activate under CTB scheduling.
 *
 * Part of the policy's behaviour rather than of persisted data, but it is a named
 * constant because the charge values stored in a snapshot are only meaningful
 * relative to it.
 */
inline constexpr int kChargeTimeBattleThreshold = 100;

/**
 * @brief Typed spelling of the built-in turn-policy ids.
 *
 * These are **ids, not an enum**. A battle stores the id string and resolves its
 * behaviour from `TurnPolicyRegistry`, so a project policy is added by registering
 * an `ITurnPolicy` and needs no change here. The spelling stays stable because
 * persisted payloads, replay commands and scripts carry these exact strings.
 */
struct TurnPolicyKind {
    static constexpr std::string_view SideAlternating = kSideAlternatingPolicyId;
    static constexpr std::string_view Initiative      = kInitiativePolicyId;
    static constexpr std::string_view ChargeTimeBattle = kChargeTimeBattlePolicyId;
};

/** @brief One authoritative board cell fact. */
struct CellState {
    int                      moveCost = 100;
    int                      height   = 0;
    bool                     passable = true;
    std::vector<std::string> tags;
};

/**
 * @brief One authoritative **directed** edge fact between two adjacent cells.
 *
 * Edges exist in addition to cells, not instead of them: a cell keeps the cost of
 * entering it from anywhere, while an edge refines one specific direction. That
 * separation is what lets a board express a one-way drop, a door that only opens
 * one way, or a diagonal that costs more than its orthogonal neighbours, without
 * duplicating cell facts or inventing a second board.
 */
struct EdgeState {
    /** @brief Whether travel is allowed in this exact direction. */
    bool                     passable  = true;
    /** @brief Additional cost charged only when crossing this direction. */
    int                      extraCost = 0;
    /** @brief Stable tags for game rules (for example `door`, `one_way`). */
    std::vector<std::string> tags;
};

/** @brief Owning persistent board record used by snapshots and replay inspection. */
struct BoardCellRecord {
    Cell                      cell;
    CellState                 state;
    std::optional<SubjectRef> occupant;
};

/** @brief Owning persistent edge record used by snapshots and replay inspection. */
struct BoardEdgeRecord {
    Cell      from;
    Cell      to;
    EdgeState state;
};

/** @brief Initial and per-round tactical resource values for one unit. */
struct TurnResourceSpec {
    int actionPoints   = 0;
    int movePoints     = 0;
    int reactionPoints = 0;
    int initiative     = 0;

    /** @brief Validate non-negative consumable resource values. */
    [[nodiscard]] Result<void> validate() const;
};

/**
 * @brief Authoritative board facts and occupancy indexes for one battle.
 *
 * This value is owner-thread-affine. It invokes no callbacks. Every mutation
 * preserves the cell-to-subject and subject-to-cell indexes atomically.
 */
class EVENGINE_API_DOMAINS BoardState {
public:
    /** @brief Construct an empty board with square-four topology. */
    BoardState() = default;

    /** @brief Return the board topology. */
    [[nodiscard]] BoardTopology topology() const noexcept { return topology_; }
    /** @brief Change topology while retaining existing cell facts. */
    void setTopology(BoardTopology topology) noexcept { topology_ = topology; }

    /**
     * @brief Add a cell with validated positive movement cost.
     * @return Applied, or Conflict when the cell exists, or InvalidArgument.
     */
    [[nodiscard]] Result<void> addCell(Cell cell, CellState state = {});
    /** @brief Return whether the logical cell exists. */
    [[nodiscard]] bool contains(Cell cell) const noexcept;
    /** @brief Return an owning copy of a cell fact, or NotFound. */
    [[nodiscard]] Result<CellState> cell(Cell cell) const;

    /**
     * @brief Place an unplaced subject on an existing passable empty cell.
     * @return Applied or a structured validation/conflict failure.
     */
    [[nodiscard]] Result<void> place(SubjectRef subject, Cell cell);
    /**
     * @brief Move a placed subject to an existing passable empty cell atomically.
     * @return Applied or a structured validation/conflict failure; failure leaves both indexes unchanged.
     */
    [[nodiscard]] Result<void> move(SubjectRef subject, Cell destination);
    /**
     * @brief Remove a placed subject from the board.
     * @return Applied, or NotFound when the subject is not placed.
     */
    [[nodiscard]] Result<void> remove(SubjectRef subject);

    /** @brief Return the occupying subject, or empty when the cell is unoccupied. */
    [[nodiscard]] std::optional<SubjectRef> occupant(Cell cell) const;
    /** @brief Return the subject cell, or empty when it is not placed. */
    [[nodiscard]] std::optional<Cell> position(SubjectRef subject) const;
    /** @brief Return cells in deterministic coordinate order. */
    [[nodiscard]] std::vector<Cell> cells() const;
    /** @brief Return all cell facts and occupants in deterministic coordinate order. */
    [[nodiscard]] std::vector<BoardCellRecord> records() const;
    /** @brief Return deterministic neighbouring cells that exist on this board. */
    [[nodiscard]] std::vector<Cell> neighbours(Cell origin) const;

    /**
     * @brief Declare a directed edge between two existing adjacent cells.
     * @param from Source cell; it must already exist on this board.
     * @param to Destination cell; it must already exist and be a neighbour of `from`.
     * @param state Directed traversal facts for this one direction.
     * @return Applied, InvalidArgument when either endpoint is missing or they are
     *         not adjacent, or Conflict when this direction is already declared.
     * @remarks The reverse direction is independent and stays whatever it was; a
     *          one-way edge is `addEdge(a,b,...)` without `addEdge(b,a,...)`.
     */
    [[nodiscard]] Result<void> addEdge(Cell from, Cell to, EdgeState state = {});
    /** @brief Return a declared directed edge, or NotFound when none is declared. */
    [[nodiscard]] Result<EdgeState> edge(Cell from, Cell to) const;
    /**
     * @brief Return a declared directed edge, or empty when none is declared.
     *
     * This is the query path-search uses: an undeclared direction is the common
     * case, so it must not build a diagnostic on every relaxed neighbour.
     */
    [[nodiscard]] std::optional<EdgeState> tryEdge(Cell from, Cell to) const;
    /** @brief Return every declared directed edge in deterministic order. */
    [[nodiscard]] std::vector<BoardEdgeRecord> edgeRecords() const;
    /** @brief Validate the two occupancy indexes and all referenced cells. */
    [[nodiscard]] Result<void> validateInvariants() const;

private:
    [[nodiscard]] static std::string subjectKey(SubjectRef subject);

    BoardTopology                    topology_ = BoardTopology::Square4;
    std::map<Cell, CellState>        cells_;
    std::map<Cell, SubjectRef>       occupantByCell_;
    std::map<std::string, Cell>      cellBySubject_;
    std::map<std::pair<Cell, Cell>, EdgeState> edges_;
};

/** @brief Tactical side short-root entity. */
class TacticalSide : public ecs::Entity {
public:
    ENTITY(TacticalSide, ecs::Entity)

    /** @brief Release through the ECS generation boundary. */
    void release() override { ecs::DestroyEntity(this); }

    /** @brief Cold, persistent and authoritative identity component. */
    struct Identity {
        ecs::EntityHandle self{};
        SubjectRef        subject;
        std::string       displayName;
    };

    COMPONENT(Identity, identity)
};

/** @brief Tactical unit short-root entity. */
class TacticalUnit : public ecs::Entity {
public:
    ENTITY(TacticalUnit, ecs::Entity)

    /** @brief Release through the ECS generation boundary. */
    void release() override { ecs::DestroyEntity(this); }

    /** @brief Cold, persistent and authoritative identity component. */
    struct Identity {
        ecs::EntityHandle self{};
        SubjectRef        subject;
        LogicalId         definition;
    };
    /** @brief Linked battle and side identities; both are generation checked on use. */
    struct Membership {
        ecs::EntityHandle battle{};
        ecs::EntityHandle side{};
    };
    /** @brief Hot authoritative tactical position and facing. */
    struct Position {
        Cell cell;
        int  facing = 0;
        bool placed = false;
    };
    /** @brief Hot authoritative per-turn resources. */
    struct TurnResources {
        int  actionPoints         = 0;
        int  movePoints           = 0;
        int  reactionPoints       = 0;
        int  roundActionPoints    = 0;
        int  roundMovePoints      = 0;
        int  roundReactionPoints  = 0;
        int  initiative           = 0;
        /**
         * @brief Accumulated scheduling charge for charge-time policies.
         *
         * Stays zero under every policy whose `ChargeModel` declares no charge, so
         * the field is inert unless the battle actually uses CTB scheduling. It is
         * persisted at schema version 5, because it is exactly the state that makes
         * "the fast unit acts more often" survive a save/restore.
         */
        int  charge               = 0;
        bool alive                = true;
        bool acted                = false;
    };

    COMPONENT(Identity, identity)
    COMPONENT(Membership, membership)
    COMPONENT(Position, position)
    COMPONENT(TurnResources, turn)
};

/** @brief One deterministic battle lifecycle event owned by Battle. */
struct BattleEvent {
    std::uint64_t sequence = 0;
    std::uint64_t causationCommand = 0;
    std::uint64_t correlationCommand = 0;
    BattlePhase  from     = BattlePhase::Setup;
    BattlePhase  to       = BattlePhase::Setup;
    SimulationTick tick   = SimulationTick::zero();
    std::string   type;
    SubjectRef    subject;
};

/** @brief One eligible deterministic reaction choice. */
struct ReactionCandidate {
    SubjectRef reactor;
    LogicalId  action;
    int        priority   = 0;
    int        initiative = 0;
};

/** @brief One open reaction window on the battle-owned LIFO stack. */
struct ReactionWindow {
    std::uint64_t                  triggerSequence = 0;
    std::size_t                    depth           = 0;
    std::vector<ReactionCandidate> candidates;
};

/** @brief Accepted reaction identity and remaining resource snapshot. */
struct ReactionReceipt {
    std::uint64_t triggerSequence        = 0;
    SubjectRef   reactor;
    LogicalId    action;
    int          remainingReactionPoints = 0;
};

/**
 * @brief Upper bound on the opaque ability payload a declaration may carry.
 *
 * The payload is caller-owned effect data that tactics stores and replays verbatim, so
 * the only rule tactics can enforce without reading it is a bound: a persisted snapshot
 * must not grow without limit.
 */
inline constexpr std::size_t kMaxAbilityPayloadBytes = 4096;

/** @brief Accepted deterministic command kinds recorded for replay. */
enum class BattleCommandKind : std::uint8_t {
    Start,
    Advance,
    Move,
    Face,
    Wait,
    EndTurn,
    Finish,
    OpenReaction,
    AcceptReaction,
    DeclineReaction,
    DefeatUnit,
    RollRandom,
    /**
     * @brief Declare a unit ability activation against a target cell.
     *
     * Appended last on purpose: the numeric value is persisted in the command log,
     * so inserting a kind anywhere else would renumber every later kind and
     * silently reinterpret existing payloads. Its legal range is version-gated
     * (schema v4), not widened unconditionally.
     */
    UseAbility,
};

/**
 * @brief One accepted replay command using only persistent identities and values.
 *
 * Setup construction is represented by the starting snapshot. Runtime ECS
 * handles and pointers are intentionally excluded.
 */
struct BattleCommand {
    std::uint64_t                 sequence = 0;
    BattleCommandKind             kind = BattleCommandKind::Advance;
    Revision                      expectedRevision;
    Revision                      resultingRevision;
    SimulationStep                step;
    SubjectRef                    actor;
    Cell                          cell;
    /**
     * @brief The unit this command targeted, when the command targets one.
     *
     * Independent of @ref cell on purpose: an ability may name a primary unit *and* a
     * centre cell (a single-target effect inside an area), and the framework must not
     * silently pick one of the two for the effect owner.
     */
    SubjectRef                    targetUnit;
    /**
     * @brief Opaque caller-owned effect parameters, as declared by the caller.
     *
     * Tactics stores and replays this verbatim and never interprets it: the ability's
     * parameters belong to whoever owns the effect (`rpg`/`combat` or the game), and a
     * second interpretation here would be a second source of truth. The schema and
     * version of this blob are the caller's contract; tactics only enforces a size
     * bound so a persisted payload stays bounded.
     */
    std::string                   payload;
    int                           facing = 0;
    std::string                   policyId{kSideAlternatingPolicyId};
    std::uint64_t                 triggerSequence = 0;
    LogicalId                     action;
    std::vector<ReactionCandidate> candidates;
};

/** @brief Built-in deterministic objective rules. */
enum class ObjectiveKind : std::uint8_t { EliminateSide, SurviveRounds, OccupyCells };

/** @brief Objective lifecycle owned by its battle. */
enum class ObjectiveStatus : std::uint8_t { Pending, Completed };

/** @brief Persistent objective definition using stable side identities. */
struct ObjectiveSpec {
    LogicalId         id;
    ObjectiveKind     kind = ObjectiveKind::EliminateSide;
    SubjectRef        beneficiarySide;
    SubjectRef        targetSide;
    std::uint64_t     requiredRound = 0;
    std::vector<Cell> requiredCells;
    bool              endsBattle = true;
};

/** @brief Mutable objective projection and completion revision. */
struct ObjectiveState {
    ObjectiveSpec   spec;
    ObjectiveStatus status = ObjectiveStatus::Pending;
    Revision        completedRevision;
};

/** @brief Battle short-root and authoritative owner of board and turn state. */
class Battle : public ecs::Entity {
public:
    ENTITY(Battle, ecs::Entity)

    /** @brief Release through the ECS generation boundary. */
    void release() override { ecs::DestroyEntity(this); }

    /** @brief Cold, persistent and authoritative battle identity. */
    struct Identity {
        ecs::EntityHandle self{};
        SubjectRef        subject;
        std::uint64_t     seed = 0;
    };
    /** @brief Authoritative board component. */
    struct Board {
        BoardState value;
    };
    /** @brief Authoritative deterministic lifecycle and scheduling state. */
    struct TurnState {
        Revision                     revision;
        BattleStatus                 status = BattleStatus::Setup;
        BattlePhase                  phase  = BattlePhase::Setup;
        std::string                  policyId{kSideAlternatingPolicyId};
        SimulationTick               tick   = SimulationTick::zero();
        std::uint64_t                round  = 0;
        std::size_t                  cursor = 0;
        std::optional<ecs::EntityHandle> activeSide;
        std::optional<ecs::EntityHandle> activeUnit;
        std::vector<ecs::EntityHandle>   sides;
        /**
         * @brief Every unit of this battle, as declared by `newUnit`.
         *
         * This is the membership roster and the authority for "which units exist".
         * It is not an activation queue: see @ref schedule.
         */
        std::vector<ecs::EntityHandle>   units;
        /**
         * @brief The current round's activation queue, in activation order.
         *
         * @ref cursor indexes this list. It is a subset of @ref units: a policy with
         * a charge model leaves units out of the queue until they have accumulated
         * enough charge, so the queue can be shorter than the roster and it changes
         * from round to round. Persisted (snapshot v5) because a round may be
         * suspended by a save at any activation.
         */
        std::vector<ecs::EntityHandle>   schedule;
    };
    /** @brief Transient deterministic event projection; not a second state authority. */
    struct Events {
        std::uint64_t            nextSequence = 1;
        std::vector<BattleEvent> values;
    };
    /** @brief Authoritative transient reaction stack and root-chain cycle guard. */
    struct Reactions {
        std::size_t                 maxDepth = 8;
        std::vector<ReactionWindow> stack;
        std::vector<std::string>    seen;
    };
    /** @brief Accepted external commands in deterministic sequence order. */
    struct Commands {
        std::uint64_t              nextSequence = 1;
        std::vector<BattleCommand> values;
    };
    /** @brief Authoritative objective definitions and completion state. */
    struct Objectives {
        std::vector<ObjectiveState> values;
    };
    /** @brief One named deterministic random stream's serializable state. */
    struct RandomStreamState {
        std::uint64_t state = 0;
        std::uint64_t rollIndex = 0;
    };
    /** @brief Battle-owned named random streams; previews never mutate this component. */
    struct Random {
        std::map<std::string, RandomStreamState> streams;
    };

    COMPONENT(Identity, identity)
    COMPONENT(Board, board)
    COMPONENT(TurnState, turn)
    COMPONENT(Events, events)
    COMPONENT(Reactions, reactions)
    COMPONENT(Commands, commands)
    COMPONENT(Objectives, objectives)
    COMPONENT(Random, random)
};

/** @brief Return the stable protocol spelling of a battle phase. */
[[nodiscard]] std::string_view phaseName(BattlePhase phase) noexcept;
/** @brief Return the stable protocol spelling of a battle status. */
[[nodiscard]] std::string_view statusName(BattleStatus status) noexcept;
/**
 * @brief Return the stable protocol spelling of an accepted command kind.
 *
 * The command log is persisted and replayed, so its kind spelling is protocol:
 * scripts, diagnostics and tests branch on these strings, never on the numeric
 * enum value, which is free to change with the schema version.
 */
[[nodiscard]] std::string_view commandKindName(BattleCommandKind kind) noexcept;

}  // namespace eve::tactics
