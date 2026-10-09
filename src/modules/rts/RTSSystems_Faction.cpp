#include "rts/RTSSystems.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace eve::rts {
namespace {

bool finitePosition(WorldPosition position) {
    return std::isfinite(position.x) && std::isfinite(position.y);
}

}  // namespace

Result<int> VeterancySystem::award(Unit& unit, float experience) {
    auto veterancy = unit.veterancy();
    auto durability = unit.durability();
    auto combatPolicy = unit.combat();
    if (!durability->alive || !std::isfinite(experience) || experience <= 0.0f)
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "RTS veterancy award requires a live unit and positive finite experience", "experience"));
    if (veterancy->veteranThreshold <= 0.0f)
        return Result<int>::success(0, Status::success(StatusCode::NoOp));
    if (!std::isfinite(veterancy->experience) || veterancy->experience < 0.0f ||
        !std::isfinite(veterancy->veteranThreshold) || !std::isfinite(veterancy->eliteThreshold) ||
        veterancy->eliteThreshold <= veterancy->veteranThreshold ||
        !std::isfinite(veterancy->veteranDamageFactor) ||
        !std::isfinite(veterancy->eliteDamageFactor) ||
        !std::isfinite(veterancy->veteranHealthFactor) ||
        !std::isfinite(veterancy->eliteHealthFactor) ||
        veterancy->veteranDamageFactor < 1.0f ||
        veterancy->eliteDamageFactor < veterancy->veteranDamageFactor ||
        veterancy->veteranHealthFactor < 1.0f ||
        veterancy->eliteHealthFactor < veterancy->veteranHealthFactor ||
        veterancy->level < 0 || veterancy->level > 2)
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                      "RTS veterancy thresholds and factors are inconsistent",
                                                      "unit.veterancy"));
    veterancy->experience += experience;
    const int oldLevel = veterancy->level;
    if (veterancy->eliteThreshold > 0.0f && veterancy->experience >= veterancy->eliteThreshold)
        veterancy->level = 2;
    else if (veterancy->experience >= veterancy->veteranThreshold)
        veterancy->level = 1;
    if (veterancy->level == oldLevel)
        return Result<int>::success(0, Status::success(StatusCode::Applied));
    const auto damageAt = [&](int level) {
        return level >= 2 ? veterancy->eliteDamageFactor
                          : level >= 1 ? veterancy->veteranDamageFactor : 1.0f;
    };
    const auto healthAt = [&](int level) {
        return level >= 2 ? veterancy->eliteHealthFactor
                          : level >= 1 ? veterancy->veteranHealthFactor : 1.0f;
    };
    combatPolicy->upgradeDamageFactor *= damageAt(veterancy->level) / damageAt(oldLevel);
    const double healthRatio = static_cast<double>(healthAt(veterancy->level) / healthAt(oldLevel));
    durability->state.maxHealth *= healthRatio;
    durability->state.health = std::min(durability->state.maxHealth,
                                        durability->state.health * healthRatio);
    return Result<int>::success(veterancy->level - oldLevel, Status::success(StatusCode::Applied));
}

bool FactionRelationSystem::isAllied(Faction* left, Faction* right) noexcept {
    if (left == nullptr || right == nullptr) return false;
    if (left == right) return true;
    auto matches = ecs::View<Match, Match::Participants>();
    for (auto it = matches.begin(); it != matches.end(); ++it) {
        auto [participants] = *it;
        const Match::Participants::Entry* leftEntry = nullptr;
        const Match::Participants::Entry* rightEntry = nullptr;
        for (const auto& entry : participants->entries) {
            if (entry.eliminated) continue;
            if (entry.faction.resolve() == left) leftEntry = &entry;
            if (entry.faction.resolve() == right) rightEntry = &entry;
        }
        if (leftEntry != nullptr && rightEntry != nullptr)
            return leftEntry->team == rightEntry->team;
    }
    return false;
}

bool FactionRelationSystem::isAllied(const FactionLink& left, const FactionLink& right) noexcept {
    return isAllied(dynamic_cast<Faction*>(left.resolve()), dynamic_cast<Faction*>(right.resolve()));
}

bool FactionIntelSystem::isTargetable(Faction* viewer, SubjectRef subject) noexcept {
    if (viewer == nullptr || !subject.isValid()) return false;
    const auto intel = viewer->intel();
    if (!intel->enabled) return true;
    const auto found = std::find_if(intel->contacts.begin(), intel->contacts.end(),
                                    [&](const auto& contact) { return contact.subject == subject; });
    return found != intel->contacts.end() && found->subject == subject &&
           found->visible && found->detected;
}

Result<void> FormationSpec::validate() const {
    if (!std::isfinite(spacing) || spacing <= 0.0f)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "formation spacing must be finite and positive", "spacing"));
    if (columns < 0)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "formation columns must be non-negative", "columns"));
    switch (kind) {
        case FormationKind::Line:
        case FormationKind::Grid:
        case FormationKind::Wedge: return Result<void>::success(Status::success(StatusCode::Applied));
    }
    return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "formation kind is invalid", "kind"));
}

Result<std::vector<WorldPosition>> FormationPlanner::plan(std::size_t count, WorldPosition anchor,
                                                          const FormationSpec& spec) {
    auto valid = spec.validate();
    if (!valid) return Result<std::vector<WorldPosition>>::failure(valid.status());
    if (!finitePosition(anchor))
        return Result<std::vector<WorldPosition>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "formation anchor must be finite", "anchor"));

    std::vector<WorldPosition> result;
    result.reserve(count);
    if (count == 0)
        return Result<std::vector<WorldPosition>>::success(std::move(result), Status::success(StatusCode::NoOp));

    const float spacing = spec.spacing;
    switch (spec.kind) {
        case FormationKind::Line: {
            const float center = static_cast<float>(count - 1) * 0.5f;
            for (std::size_t index = 0; index < count; ++index) {
                result.push_back({anchor.x + (static_cast<float>(index) - center) * spacing, anchor.y});
            }
            break;
        }
        case FormationKind::Grid: {
            const std::size_t columns =
                spec.columns > 0 ? static_cast<std::size_t>(spec.columns)
                                 : static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(count))));
            if (columns == 0)
                return Result<std::vector<WorldPosition>>::failure(Diagnostic::error(
                    DiagnosticCode::InvariantViolation, "grid formation computed zero columns", "columns"));
            const std::size_t rows         = (count + columns - 1) / columns;
            const float       columnCenter = static_cast<float>(columns - 1) * 0.5f;
            const float       rowCenter    = static_cast<float>(rows - 1) * 0.5f;
            for (std::size_t index = 0; index < count; ++index) {
                const std::size_t row    = index / columns;
                const std::size_t column = index % columns;
                result.push_back({anchor.x + (static_cast<float>(column) - columnCenter) * spacing,
                                  anchor.y + (static_cast<float>(row) - rowCenter) * spacing});
            }
            break;
        }
        case FormationKind::Wedge: {
            std::size_t row      = 0;
            std::size_t rowStart = 0;
            for (std::size_t index = 0; index < count; ++index) {
                while (index >= rowStart + row + 1) {
                    rowStart += row + 1;
                    ++row;
                }
                const std::size_t slot      = index - rowStart;
                const float       rowCenter = static_cast<float>(row) * 0.5f;
                result.push_back({anchor.x + (static_cast<float>(slot) - rowCenter) * spacing,
                                  anchor.y + static_cast<float>(row) * spacing});
            }
            break;
        }
    }
    return Result<std::vector<WorldPosition>>::success(std::move(result), Status::success(StatusCode::Applied));
}

Result<void> BuildInfluenceSystem::validate(Faction& faction, WorldPosition position,
                                            LogicalId definition, const PlacementValidation& placement,
                                            bool requireInfluence) {
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !definition.isValid() || !placement)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "RTS building placement requires finite position, definition and provider", "placement"));
    if (requireInfluence) {
        bool covered = false;
        auto buildings = ecs::View<Building, Building::Placement, Building::Faction,
                                   Building::Construction, Building::Integrity,
                                   Building::Infrastructure>();
        for (auto it = buildings.begin(); it != buildings.end(); ++it) {
            auto [source, owner, construction, integrity, infrastructure] = *it;
            if (!source->placed || construction->progress < 1.0f || !integrity->alive || !infrastructure->powered ||
                infrastructure->buildInfluenceRadius <= 0.0f ||
                !FactionRelationSystem::isAllied(dynamic_cast<Faction*>(owner->link.resolve()), &faction))
                continue;
            const float dx = source->worldX - position.x;
            const float dy = source->worldY - position.y;
            const float radius = infrastructure->buildInfluenceRadius;
            if (dx * dx + dy * dy <= radius * radius) {
                covered = true;
                break;
            }
        }
        if (!covered)
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::Conflict, "RTS building position is outside powered allied build influence",
                "placement.influence"));
    }
    return placement(position, std::move(definition));
}

}  // namespace eve::rts
