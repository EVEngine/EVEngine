#pragma once

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

/** @brief Built-in deterministic turn scheduling policy. */
enum class TurnPolicyKind : std::uint8_t { SideAlternating, Initiative };

/** @brief One authoritative board cell fact. */
struct CellState {
    int                      moveCost = 100;
    int                      height   = 0;
    bool                     passable = true;
    std::vector<std::string> tags;
};

/** @brief Owning persistent board record used by snapshots and replay inspection. */
struct BoardCellRecord {
    Cell                      cell;
    CellState                 state;
    std::optional<SubjectRef> occupant;
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
class BoardState {
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
    /** @brief Validate the two occupancy indexes and all referenced cells. */
    [[nodiscard]] Result<void> validateInvariants() const;

private:
    [[nodiscard]] static std::string subjectKey(SubjectRef subject);

    BoardTopology                    topology_ = BoardTopology::Square4;
    std::map<Cell, CellState>        cells_;
    std::map<Cell, SubjectRef>       occupantByCell_;
    std::map<std::string, Cell>      cellBySubject_;
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
    int                           facing = 0;
    TurnPolicyKind                policy = TurnPolicyKind::SideAlternating;
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
        TurnPolicyKind               policy = TurnPolicyKind::SideAlternating;
        SimulationTick               tick   = SimulationTick::zero();
        std::uint64_t                round  = 0;
        std::size_t                  cursor = 0;
        std::optional<ecs::EntityHandle> activeSide;
        std::optional<ecs::EntityHandle> activeUnit;
        std::vector<ecs::EntityHandle>   sides;
        std::vector<ecs::EntityHandle>   units;
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

}  // namespace eve::tactics
