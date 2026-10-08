#include "map/Pathfinder.h"
#include "rts/RTS.h"
#include "rts/RTSSystemMovementInternal.h"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <set>

namespace eve::rts {
namespace {
using Cell = std::pair<int, int>;
constexpr std::array<Cell, 4> directions{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};

std::optional<Cell> cellAt(WorldPosition point, const NavigationGrid& grid) {
    const double     x     = (static_cast<double>(point.x) - grid.originX) / grid.cellSize;
    const double     y     = (static_cast<double>(point.y) - grid.originY) / grid.cellSize;
    constexpr double limit = static_cast<double>(std::numeric_limits<int>::max()) - 4096.0;
    if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > limit || std::abs(y) > limit) return std::nullopt;
    return Cell{static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y))};
}

bool isNarrow(const map::Pathfinder& pathfinder, Cell cell) {
    if (!pathfinder.isWalkable(cell.first, cell.second)) return false;
    int exits = 0;
    for (const auto& [dx, dy] : directions)
        if (pathfinder.isWalkable(cell.first + dx, cell.second + dy)) ++exits;
    return exits <= 2;
}

struct Corridor {
    std::set<Cell> cells;
    std::set<Cell> exits;
};

Result<Corridor> discoverCorridor(const map::Pathfinder& pathfinder, Cell seed) {
    Corridor result;
    result.cells.insert(seed);
    std::vector<Cell> pending{seed};
    for (std::size_t i = 0; i < pending.size(); ++i) {
        const auto cell            = pending[i];
        int        narrowNeighbors = 0;
        for (const auto& [dx, dy] : directions) {
            const Cell next{cell.first + dx, cell.second + dy};
            if (!pathfinder.isWalkable(next.first, next.second)) continue;
            if (!isNarrow(pathfinder, next)) {
                result.exits.insert(next);
                continue;
            }
            ++narrowNeighbors;
            if (result.cells.contains(next)) continue;
            if (result.cells.size() >= 1024)
                return Result<Corridor>::failure(
                    Diagnostic::error(DiagnosticCode::Unsupported,
                                      "RTS corridor exceeds the 1024-cell reservation budget", "traffic.corridor"));
            result.cells.insert(next);
            pending.push_back(next);
        }
        if (narrowNeighbors <= 1) result.exits.insert(cell);
    }
    return Result<Corridor>::success(std::move(result));
}

std::optional<WorldPosition> evacuationStep(const map::Pathfinder& pathfinder, const NavigationGrid& grid,
                                            const Corridor& corridor, Cell exit, Cell current, WorldPosition position,
                                            float radius, std::set<Cell>& parking) {
    if (corridor.cells.contains(exit)) return std::nullopt;
    const auto world = [&](Cell cell) {
        return WorldPosition{grid.originX + static_cast<float>(cell.first) * grid.cellSize,
                             grid.originY + static_cast<float>(cell.second) * grid.cellSize};
    };
    std::optional<Cell> bay;
    for (const auto& [dx, dy] : directions) {
        if (!corridor.cells.contains({exit.first - dx, exit.second - dy})) continue;
        for (int forward = 0; forward < 3 && !bay; ++forward) {
            for (int side = 1; side <= 3 && !bay; ++side) {
                for (int sign : {-1, 1}) {
                    const Cell candidate{exit.first + dx * forward - dy * side * sign,
                                         exit.second + dy * forward + dx * side * sign};
                    if (!parking.contains(candidate) && !corridor.cells.contains(candidate) &&
                        systems_internal::isFormationSegmentClear(pathfinder, grid, world(exit), world(candidate),
                                                                  radius)) {
                        bay = candidate;
                        break;
                    }
                }
            }
        }
    }
    if (!bay) return std::nullopt;
    parking.insert(*bay);
    WorldPosition target = world(*bay);
    if (corridor.cells.contains(current)) {
        std::map<Cell, Cell> towardExit;
        towardExit.emplace(exit, exit);
        std::vector<Cell> pending{exit};
        for (std::size_t i = 0; i < pending.size() && !towardExit.contains(current); ++i) {
            for (const auto& [dx, dy] : directions) {
                const Cell next{pending[i].first + dx, pending[i].second + dy};
                if (corridor.cells.contains(next) && towardExit.emplace(next, pending[i]).second)
                    pending.push_back(next);
            }
        }
        const auto next = towardExit.find(current);
        if (next == towardExit.end()) return std::nullopt;
        target = world(next->second);
    }
    if (!systems_internal::isFormationSegmentClear(pathfinder, grid, position, target, radius)) {
        target = world(current);
        if (!systems_internal::isFormationSegmentClear(pathfinder, grid, position, target, radius)) return std::nullopt;
    }
    if (std::hypot(static_cast<double>(target.x) - position.x, static_cast<double>(target.y) - position.y) < 1e-4)
        return std::nullopt;
    return target;
}
}  // namespace

Result<std::size_t> TrafficReservationSystem::step(const map::Pathfinder& pathfinder, const NavigationGrid& grid) {
    if (!std::isfinite(grid.cellSize) || grid.cellSize <= 0.f || !std::isfinite(grid.originX) ||
        !std::isfinite(grid.originY))
        return Result<std::size_t>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "RTS traffic grid requires finite origin and positive cell size", "traffic.grid"));
    struct Candidate {
        Unit::Navigation* navigation;
        SubjectRef        subject;
        int               priority;
        Cell              current;
        Cell              exit;
        Cell              next;
        bool              occupant;
        WorldPosition     position;
        float             radius;
    };
    std::map<Cell, Corridor>               corridors;
    std::map<Cell, Cell>                   owners;
    std::map<Cell, std::vector<Candidate>> contenders;
    std::vector<Unit::Navigation*>         recovering;
    std::size_t                            processed = 0;
    auto                                   view =
        ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Navigation, Unit::Orders, Unit::Containment, Unit::Crowd>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [identity, motion, navigation, orders, containment, crowd] = *it;
        if (!identity)
            return Result<std::size_t>::failure(Diagnostic::error(
                DiagnosticCode::InvariantViolation, "RTS traffic candidate has no identity", "unit.identity"));
        navigation->trafficWaiting = false;
        if (navigation->trafficRecoveryTarget) recovering.push_back(navigation);
        navigation->trafficRecoveryTarget.reset();
        if (containment->container.isBound()) continue;
        auto currentOrder = systems_internal::readCurrent(orders->values);
        if (!currentOrder) return Result<std::size_t>::failure(currentOrder.status());
        const auto& order = currentOrder.value();
        if (!order || !systems_internal::isMovementOrder(order->kind) || navigation->unreachable) continue;
        const bool hasPath = navigation->plannedOrderId == order->id;
        const auto current = cellAt({motion->x, motion->y}, grid);
        const auto goal    = cellAt(hasPath ? navigation->plannedGoal : order->target, grid);
        if (!current || !goal)
            return Result<std::size_t>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                  "RTS traffic coordinates exceed the grid index range",
                                                                  "traffic.position"));
        ++processed;
        std::optional<Cell> seed;
        if (isNarrow(pathfinder, *current)) seed = current;
        const std::size_t begin = hasPath ? navigation->waypointIndex : navigation->waypoints.size();
        for (std::size_t index = begin; !seed && index < navigation->waypoints.size() && index - begin < 3; ++index) {
            const auto cell = cellAt(navigation->waypoints[index], grid);
            if (cell && isNarrow(pathfinder, *cell)) seed = cell;
        }
        if (!seed) continue;
        auto owner = owners.find(*seed);
        if (owner == owners.end()) {
            auto discovered = discoverCorridor(pathfinder, *seed);
            if (!discovered) return Result<std::size_t>::failure(discovered.status());
            auto       corridor = std::move(discovered).takeValue();
            const Cell key      = *corridor.cells.begin();
            for (auto cell : corridor.cells) owners.emplace(cell, key);
            corridors.emplace(key, std::move(corridor));
            owner = owners.find(*seed);
        }
        const Cell  key      = owner->second;
        const auto& corridor = corridors.at(key);
        Cell        exit     = *goal;
        if (!corridor.exits.empty()) {
            exit = *std::min_element(corridor.exits.begin(), corridor.exits.end(), [&](Cell left, Cell right) {
                const auto distance = [&](Cell cell) {
                    return std::hypot(static_cast<double>(cell.first) - goal->first,
                                      static_cast<double>(cell.second) - goal->second);
                };
                const double a = distance(left), b = distance(right);
                return a != b ? a < b : left < right;
            });
        }
        Cell next = *goal;
        if (begin < navigation->waypoints.size()) {
            const auto point = cellAt(navigation->waypoints[begin], grid);
            if (point) next = *point;
        }
        contenders[key].push_back({navigation,
                                   identity->subject,
                                   navigation->movementPriority,
                                   *current,
                                   exit,
                                   next,
                                   corridor.cells.contains(*current),
                                   {motion->x, motion->y},
                                   crowd->radius});
    }
    for (auto& [key, values] : contenders) {
        std::sort(values.begin(), values.end(), [](const Candidate& left, const Candidate& right) {
            if (left.occupant != right.occupant) return left.occupant;
            if (left.priority != right.priority) return left.priority > right.priority;
            return left.subject.format() < right.subject.format();
        });
        const Cell     direction = values.front().exit;
        std::set<Cell> reserved;
        std::set<Cell> parking;
        for (auto& value : values) {
            const bool sameDirection         = value.exit == direction;
            const bool narrowNext            = isNarrow(pathfinder, value.next);
            value.navigation->trafficWaiting = !sameDirection || (narrowNext && !reserved.insert(value.next).second);
            if (!sameDirection)
                value.navigation->trafficRecoveryTarget =
                    evacuationStep(pathfinder, grid, corridors.at(key), direction, value.current, value.position,
                                   value.radius, parking);
        }
    }
    for (auto* navigation : recovering) {
        if (navigation->trafficRecoveryTarget) continue;
        navigation->plannedOrderId.clear();
        navigation->trafficWaiting = true;
    }
    return Result<std::size_t>::success(processed,
                                        Status::success(processed == 0 ? StatusCode::NoOp : StatusCode::Applied));
}
void RTS::setNavigationProvider(map::Pathfinder* pathfinder, NavigationGrid grid,
                                NavigationEvent unreachable) noexcept {
    auto* currentTable = ecs::current();
    for (const auto& handle : units_) {
        if (handle.table != currentTable) continue;
        auto* unit = dynamic_cast<Unit*>(ecs::try_get(handle));
        if (!unit || !unit->navigation()->trafficRecoveryTarget) continue;
        unit->navigation()->trafficRecoveryTarget.reset();
        unit->navigation()->trafficWaiting = false;
        unit->navigation()->plannedOrderId.clear();
    }
    pathfinder_      = pathfinder;
    navigationGrid_  = grid;
    navigationEvent_ = std::move(unreachable);
}

}  // namespace eve::rts
