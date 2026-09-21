#include "tactics/TacticsTypes.h"

#include <algorithm>
#include <array>
#include <utility>

namespace eve::tactics {
namespace {

Result<void> failure(DiagnosticCode code, std::string message, std::string path) {
    return Result<void>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

}  // namespace

Result<void> BoardState::addCell(Cell cellValue, CellState state) {
    if (state.moveCost <= 0)
        return failure(DiagnosticCode::InvalidArgument, "tactics cell movement cost must be positive",
                       "cell.moveCost");
    std::sort(state.tags.begin(), state.tags.end());
    state.tags.erase(std::unique(state.tags.begin(), state.tags.end()), state.tags.end());
    if (cells_.contains(cellValue))
        return failure(DiagnosticCode::Conflict, "tactics board cell already exists", "cell");
    cells_.emplace(cellValue, std::move(state));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

bool BoardState::contains(Cell cellValue) const noexcept { return cells_.contains(cellValue); }

Result<CellState> BoardState::cell(Cell cellValue) const {
    const auto found = cells_.find(cellValue);
    if (found == cells_.end())
        return Result<CellState>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "tactics board cell does not exist", "cell"));
    return Result<CellState>::success(found->second);
}

Result<void> BoardState::place(SubjectRef subject, Cell cellValue) {
    if (!subject.isValid())
        return failure(DiagnosticCode::InvalidArgument, "tactics placement requires a valid subject", "subject");
    const auto target = cells_.find(cellValue);
    if (target == cells_.end())
        return failure(DiagnosticCode::NotFound, "tactics placement cell does not exist", "cell");
    if (!target->second.passable)
        return failure(DiagnosticCode::PreconditionViolation, "tactics placement cell is not passable", "cell");
    if (occupantByCell_.contains(cellValue))
        return failure(DiagnosticCode::Conflict, "tactics placement cell is occupied", "cell");
    const std::string key = subjectKey(subject);
    if (cellBySubject_.contains(key))
        return failure(DiagnosticCode::Conflict, "tactics subject is already placed", "subject");
    occupantByCell_.emplace(cellValue, subject);
    cellBySubject_.emplace(key, cellValue);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> BoardState::move(SubjectRef subject, Cell destination) {
    if (!subject.isValid())
        return failure(DiagnosticCode::InvalidArgument, "tactics movement requires a valid subject", "subject");
    const std::string key    = subjectKey(subject);
    const auto        source = cellBySubject_.find(key);
    if (source == cellBySubject_.end())
        return failure(DiagnosticCode::NotFound, "tactics movement subject is not placed", "subject");
    const auto target = cells_.find(destination);
    if (target == cells_.end())
        return failure(DiagnosticCode::NotFound, "tactics movement destination does not exist", "destination");
    if (!target->second.passable)
        return failure(DiagnosticCode::PreconditionViolation, "tactics movement destination is not passable",
                       "destination");
    if (occupantByCell_.contains(destination))
        return failure(DiagnosticCode::Conflict, "tactics movement destination is occupied", "destination");
    const Cell oldCell = source->second;
    occupantByCell_.erase(oldCell);
    occupantByCell_.emplace(destination, subject);
    source->second = destination;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> BoardState::remove(SubjectRef subject) {
    if (!subject.isValid())
        return failure(DiagnosticCode::InvalidArgument, "tactics removal requires a valid subject", "subject");
    const auto found = cellBySubject_.find(subjectKey(subject));
    if (found == cellBySubject_.end())
        return failure(DiagnosticCode::NotFound, "tactics subject is not placed", "subject");
    occupantByCell_.erase(found->second);
    cellBySubject_.erase(found);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

std::optional<SubjectRef> BoardState::occupant(Cell cellValue) const {
    const auto found = occupantByCell_.find(cellValue);
    return found == occupantByCell_.end() ? std::nullopt : std::optional<SubjectRef>(found->second);
}

std::optional<Cell> BoardState::position(SubjectRef subject) const {
    if (!subject.isValid()) return std::nullopt;
    const auto found = cellBySubject_.find(subjectKey(subject));
    return found == cellBySubject_.end() ? std::nullopt : std::optional<Cell>(found->second);
}

std::vector<Cell> BoardState::cells() const {
    std::vector<Cell> result;
    result.reserve(cells_.size());
    for (const auto& [coordinate, state] : cells_) {
        (void)state;
        result.push_back(coordinate);
    }
    return result;
}

std::vector<BoardCellRecord> BoardState::records() const {
    std::vector<BoardCellRecord> result;
    result.reserve(cells_.size());
    for (const auto& [cellValue, state] : cells_) result.push_back({cellValue, state, occupant(cellValue)});
    return result;
}

std::vector<Cell> BoardState::neighbours(Cell origin) const {
    static constexpr std::array<Cell, 4> square4{{{1, 0, 0}, {0, 1, 0}, {-1, 0, 0}, {0, -1, 0}}};
    static constexpr std::array<Cell, 8> square8{{{1, 0, 0},   {1, 1, 0},  {0, 1, 0},  {-1, 1, 0},
                                                  {-1, 0, 0}, {-1, -1, 0}, {0, -1, 0}, {1, -1, 0}}};
    static constexpr std::array<Cell, 6> hex{{{1, 0, 0}, {1, -1, 0}, {0, -1, 0}, {-1, 0, 0}, {-1, 1, 0}, {0, 1, 0}}};
    std::vector<Cell> result;
    const auto append = [&](const auto& offsets) {
        for (const Cell offset : offsets) {
            const Cell candidate{origin.x + offset.x, origin.y + offset.y, origin.layer};
            if (cells_.contains(candidate)) result.push_back(candidate);
        }
    };
    switch (topology_) {
        case BoardTopology::Square4: append(square4); break;
        case BoardTopology::Square8: append(square8); break;
        case BoardTopology::HexAxial: append(hex); break;
    }
    return result;
}

Result<void> BoardState::addEdge(Cell from, Cell to, EdgeState state) {
    if (!cells_.contains(from))
        return failure(DiagnosticCode::NotFound, "tactics edge source cell does not exist", "board.edge.from");
    if (!cells_.contains(to))
        return failure(DiagnosticCode::NotFound, "tactics edge destination cell does not exist", "board.edge.to");
    const std::vector<Cell> adjacent = neighbours(from);
    if (std::find(adjacent.begin(), adjacent.end(), to) == adjacent.end())
        return failure(DiagnosticCode::InvalidArgument, "tactics edge endpoints are not adjacent", "board.edge");
    if (state.extraCost < 0)
        return failure(DiagnosticCode::InvalidArgument, "tactics edge extra cost must be non-negative",
                       "board.edge.extraCost");
    const std::pair<Cell, Cell> key{from, to};
    if (edges_.contains(key))
        return failure(DiagnosticCode::Conflict, "tactics edge is already declared", "board.edge");
    edges_.emplace(key, std::move(state));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<EdgeState> BoardState::edge(Cell from, Cell to) const {
    const auto found = edges_.find(std::pair<Cell, Cell>{from, to});
    if (found == edges_.end())
        return Result<EdgeState>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "tactics edge is not declared", "board.edge"));
    return Result<EdgeState>::success(found->second);
}

std::optional<EdgeState> BoardState::tryEdge(Cell from, Cell to) const {
    const auto found = edges_.find(std::pair<Cell, Cell>{from, to});
    if (found == edges_.end()) return std::nullopt;
    return found->second;
}

std::vector<BoardEdgeRecord> BoardState::edgeRecords() const {
    std::vector<BoardEdgeRecord> result;
    result.reserve(edges_.size());
    for (const auto& [key, state] : edges_) result.push_back({key.first, key.second, state});
    return result;
}

Result<void> BoardState::validateInvariants() const {
    for (const auto& [key, state] : edges_) {
        if (!cells_.contains(key.first) || !cells_.contains(key.second))
            return failure(DiagnosticCode::InvariantViolation, "tactics edge references a missing cell",
                           "board.edge");
    }
    if (occupantByCell_.size() != cellBySubject_.size())
        return failure(DiagnosticCode::InvariantViolation, "tactics occupancy indexes differ in size", "board");
    for (const auto& [cellValue, subject] : occupantByCell_) {
        if (!cells_.contains(cellValue))
            return failure(DiagnosticCode::InvariantViolation, "tactics occupancy references a missing cell",
                           "board.occupancy");
        const auto reverse = cellBySubject_.find(subjectKey(subject));
        if (reverse == cellBySubject_.end() || reverse->second != cellValue)
            return failure(DiagnosticCode::InvariantViolation, "tactics occupancy reverse index is inconsistent",
                           "board.occupancy");
    }
    return Result<void>::success(Status::success(StatusCode::NoOp));
}

std::string BoardState::subjectKey(SubjectRef subject) { return subject.format(); }

Result<void> TurnResourceSpec::validate() const {
    if (actionPoints < 0 || movePoints < 0 || reactionPoints < 0)
        return failure(DiagnosticCode::InvalidArgument,
                       "tactics consumable turn resources must be non-negative", "turnResources");
    return Result<void>::success(Status::success(StatusCode::NoOp));
}

std::string_view phaseName(BattlePhase phase) noexcept {
    switch (phase) {
        case BattlePhase::Setup: return "setup";
        case BattlePhase::BattleStart: return "battle_start";
        case BattlePhase::RoundStart: return "round_start";
        case BattlePhase::TurnStart: return "turn_start";
        case BattlePhase::Acting: return "acting";
        case BattlePhase::Reaction: return "reaction";
        case BattlePhase::TurnEnd: return "turn_end";
        case BattlePhase::RoundEnd: return "round_end";
        case BattlePhase::BattleEnd: return "battle_end";
    }
    return "unknown";
}

std::string_view statusName(BattleStatus status) noexcept {
    switch (status) {
        case BattleStatus::Setup: return "setup";
        case BattleStatus::Running: return "running";
        case BattleStatus::Ended: return "ended";
    }
    return "unknown";
}

std::string_view commandKindName(BattleCommandKind kind) noexcept {
    switch (kind) {
        case BattleCommandKind::Start: return "start";
        case BattleCommandKind::Advance: return "advance";
        case BattleCommandKind::Move: return "move";
        case BattleCommandKind::Face: return "face";
        case BattleCommandKind::Wait: return "wait";
        case BattleCommandKind::EndTurn: return "end_turn";
        case BattleCommandKind::Finish: return "finish";
        case BattleCommandKind::OpenReaction: return "open_reaction";
        case BattleCommandKind::AcceptReaction: return "accept_reaction";
        case BattleCommandKind::DeclineReaction: return "decline_reaction";
        case BattleCommandKind::DefeatUnit: return "defeat_unit";
        case BattleCommandKind::RollRandom: return "roll_random";
        case BattleCommandKind::UseAbility: return "use_ability";
    }
    return "unknown";
}

}  // namespace eve::tactics
