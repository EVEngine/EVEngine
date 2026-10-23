#include <algorithm>
#include <map>
#include <tuple>
#include <vector>
#include "common/Profile.h"
#include "map/Fov.h"
#include "rts/RTSSystemMovementInternal.h"
namespace eve::rts {
using systems_internal::distanceSquared;
using systems_internal::isFinitePosition;
const Faction::Intel::Contact* FogOfWarSystem::contact(const Faction& faction, SubjectRef subject) noexcept {
    const auto& contacts = const_cast<Faction&>(faction).intel()->contacts;
    const auto  it =
        std::find_if(contacts.begin(), contacts.end(), [&](const auto& value) { return value.subject == subject; });
    return it != contacts.end() && it->subject == subject ? &*it : nullptr;
}

Result<std::size_t> FogOfWarSystem::step(const SimulationStep& step, const NavigationGrid& grid, State& state,
                                         const FogProvider& provider) {
    EV_PROFILE_MODULE("rts", "FogOfWarSystem::step");
    if (!provider)
        return Result<std::size_t>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "RTS fog provider is required", "fog.provider"));
    if (step.delta.nanoseconds() < 0 || !std::isfinite(grid.cellSize) || grid.cellSize <= 0.0f)
        return Result<std::size_t>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "RTS fog step/grid is invalid", "fog.grid"));
    struct Source {
        ecs::EntityHandle handle{};
        ecs::EntityHandle faction{};
        map::Fov*         fov = nullptr;
        WorldPosition     position;
        float             sight             = 0.0f;
        float             detectionRange    = 0.0f;
        float             detectionStrength = 0.0f;
        float             radarRange        = 0.0f;
        float             radarResolution   = 0.0f;
        float             jammingRange      = 0.0f;
    };
    std::vector<Source> sources;
    auto addSource = [&](ecs::EntityHandle handle, const FactionLink& factionLink, WorldPosition position, float sight,
                         float detectionRange, float detectionStrength, float radarRange, float radarResolution,
                         float jammingRange, bool enabled) -> Result<void> {
        if (!enabled || (sight <= 0.0f && radarRange <= 0.0f && jammingRange <= 0.0f))
            return Result<void>::success(Status::success(StatusCode::NoOp));
        if (!isFinitePosition(position) || !std::isfinite(sight) || !std::isfinite(detectionRange) ||
            !std::isfinite(detectionStrength) || !std::isfinite(radarRange) || !std::isfinite(radarResolution) ||
            !std::isfinite(jammingRange) || sight < 0.0f || detectionRange < 0.0f || detectionStrength < 0.0f ||
            radarRange < 0.0f || radarResolution < 0.0f || jammingRange < 0.0f)
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "RTS vision values must be finite and non-negative", "vision"));
        auto* faction = dynamic_cast<Faction*>(factionLink.resolve());
        if (faction == nullptr) return Result<void>::success(Status::success(StatusCode::NoOp));
        map::Fov* fov = provider(*faction);
        if (fov == nullptr) return Result<void>::success(Status::success(StatusCode::NoOp));
        sources.push_back({handle, ecs::handle_of(faction), fov, position, sight, detectionRange, detectionStrength,
                           radarRange, radarResolution, jammingRange});
        return Result<void>::success(Status::success(StatusCode::Applied));
    };
    auto units = ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Faction, Unit::Vision, Unit::Containment>();
    for (auto it = units.begin(); it != units.end(); ++it) {
        auto [identity, motion, faction, vision, containment] = *it;
        if (containment->container.isBound()) continue;
        auto added = addSource(identity->self, faction->link, {motion->x, motion->y}, vision->sightRange,
                               vision->detectionRange, vision->detectionStrength, vision->radarRange,
                               vision->radarResolution, vision->jammingRange, vision->enabled);
        if (!added) return Result<std::size_t>::failure(added.status());
    }
    auto buildings = ecs::View<Building, Building::Identity, Building::Placement, Building::Faction, Building::Vision,
                               Building::Construction>();
    for (auto it = buildings.begin(); it != buildings.end(); ++it) {
        auto [identity, placement, faction, vision, construction] = *it;
        if (!placement->placed || construction->progress < 1.0f) continue;
        auto added = addSource(identity->self, faction->link, {placement->worldX, placement->worldY},
                               vision->sightRange, vision->detectionRange, vision->detectionStrength,
                               vision->radarRange, vision->radarResolution, vision->jammingRange, vision->enabled);
        if (!added) return Result<std::size_t>::failure(added.status());
    }
    using Key      = std::tuple<ecs::Table*, std::type_index, std::uint32_t, std::uint32_t, map::Fov*>;
    const auto key = [](ecs::EntityHandle handle, map::Fov* fov) {
        return Key{handle.table, handle.type, handle.id, handle.generation, fov};
    };
    std::map<Key, std::size_t> sourceIndex;
    std::vector<const Source*> radars, jammers;
    for (std::size_t i = 0; i < sources.size(); ++i) {
        const auto& source = sources[i];
        if (source.sight > 0.f) sourceIndex.emplace(key(source.handle, source.fov), i);
        if (source.radarRange > 0.f) radars.push_back(&source);
        if (source.jammingRange > 0.f) jammers.push_back(&source);
    }
    state.bindings.erase(std::remove_if(state.bindings.begin(), state.bindings.end(),
                                        [&](const auto& binding) {
                                            const bool retained =
                                                sourceIndex.contains(key(binding.source, binding.provider));
                                            if (!retained && binding.provider != nullptr && binding.revealer >= 0)
                                                binding.provider->removeRevealer(binding.revealer);
                                            return !retained;
                                        }),
                         state.bindings.end());
    const auto worldToCell = [&](float value, float origin) {
        return static_cast<int>(std::lround((value - origin) / grid.cellSize));
    };
    std::map<Key, std::size_t> bindingIndex;
    for (std::size_t i = 0; i < state.bindings.size(); ++i)
        bindingIndex.emplace(key(state.bindings[i].source, state.bindings[i].provider), i);
    std::vector<map::Fov*> providers;
    for (const auto& source : sources) {
        if (source.sight <= 0.0f) continue;
        auto      binding = bindingIndex.find(key(source.handle, source.fov));
        const int x       = worldToCell(source.position.x, grid.originX);
        const int y       = worldToCell(source.position.y, grid.originY);
        const int radius  = std::max(0, static_cast<int>(std::ceil(source.sight / grid.cellSize)));
        if (binding == bindingIndex.end()) {
            const int revealer = source.fov->addRevealer(x, y, radius);
            bindingIndex.emplace(key(source.handle, source.fov), state.bindings.size());
            state.bindings.push_back({source.handle, source.faction, source.fov, revealer});
        } else {
            source.fov->setRevealerPosition(state.bindings[binding->second].revealer, x, y);
            source.fov->setRevealerRadius(state.bindings[binding->second].revealer, radius);
            state.bindings[binding->second].faction = source.faction;
        }
        if (std::find(providers.begin(), providers.end(), source.fov) == providers.end())
            providers.push_back(source.fov);
    }
    for (auto* fov : providers) fov->compute();

    auto factions = ecs::View<Faction, Faction::Identity, Faction::Intel>();
    for (auto fit = factions.begin(); fit != factions.end(); ++fit) {
        auto [identity, intel] = *fit;
        auto*     viewer       = dynamic_cast<Faction*>(ecs::try_get(identity->self));
        map::Fov* fov          = viewer == nullptr ? nullptr : provider(*viewer);
        intel->enabled         = fov != nullptr;
        for (auto& value : intel->contacts) {
            value.ageSeconds += step.delta.seconds();
            value.visible  = false;
            value.detected = false;
        }
        if (fov == nullptr) continue;
        auto observe = [&](SubjectRef subject, std::string_view kind, ecs::EntityHandle targetFaction,
                           WorldPosition position, bool cloaked, float stealth) {
            auto* enemyFaction = dynamic_cast<Faction*>(ecs::try_get(targetFaction));
            if (enemyFaction == nullptr || enemyFaction == viewer || provider(*enemyFaction) == fov) return;
            const int x        = worldToCell(position.x, grid.originX);
            const int y        = worldToCell(position.y, grid.originY);
            bool      detected = !cloaked;
            if (cloaked && fov->isVisible(x, y)) {
                for (const auto& source : sources) {
                    if (source.fov != fov || source.detectionRange <= 0.0f || source.detectionStrength < stealth)
                        continue;
                    if (distanceSquared(source.position.x, source.position.y, position.x, position.y) <=
                        source.detectionRange * source.detectionRange) {
                        detected = true;
                        break;
                    }
                }
            }
            const bool visible = fov->isVisible(x, y) && detected;
            auto       found   = std::lower_bound(intel->contacts.begin(), intel->contacts.end(), subject,
                                                  [](const auto& value, SubjectRef key) {
                                              return value.subject.persistentId().bytes() < key.persistentId().bytes();
                                          });
            if (!visible) {
                const bool    jammed = std::any_of(jammers.begin(), jammers.end(), [&](const Source* jammer) {
                    if (jammer->fov == fov) return false;
                    return distanceSquared(jammer->position.x, jammer->position.y, position.x, position.y) <=
                           jammer->jammingRange * jammer->jammingRange;
                });
                const Source* radar  = nullptr;
                if (!jammed) {
                    for (const auto* candidate : radars) {
                        if (candidate->fov != fov) continue;
                        if (distanceSquared(candidate->position.x, candidate->position.y, position.x, position.y) <=
                            candidate->radarRange * candidate->radarRange) {
                            radar = candidate;
                            break;
                        }
                    }
                }
                if (radar == nullptr) return;
                const float         resolution = radar->radarResolution > 0.0f ? radar->radarResolution : grid.cellSize;
                const WorldPosition quantized{
                    grid.originX + (std::floor((position.x - grid.originX) / resolution) + 0.5f) * resolution,
                    grid.originY + (std::floor((position.y - grid.originY) / resolution) + 0.5f) * resolution};
                const std::string radarKind = kind == "unit" ? "radar_unit" : "radar_building";
                if (found == intel->contacts.end() || found->subject != subject)
                    intel->contacts.insert(found, {subject, radarKind, quantized, 0.0, false, false});
                else {
                    found->position   = quantized;
                    found->ageSeconds = 0.0;
                    found->visible    = false;
                    found->detected   = false;
                    found->kind       = radarKind;
                }
                return;
            }
            if (found == intel->contacts.end() || found->subject != subject)
                found = intel->contacts.insert(found, {subject, std::string(kind), position, 0.0, true, detected});
            else {
                found->position   = position;
                found->ageSeconds = 0.0;
                found->visible    = true;
                found->detected   = detected;
                found->kind       = kind;
            }
        };
        for (auto it = units.begin(); it != units.end(); ++it) {
            auto [targetIdentity, motion, faction, vision, containment] = *it;
            if (!containment->container.isBound())
                observe(targetIdentity->subject, "unit", faction->link.handle(), {motion->x, motion->y},
                        vision->cloaked, vision->stealth);
        }
        for (auto it = buildings.begin(); it != buildings.end(); ++it) {
            auto [targetIdentity, placement, faction, vision, construction] = *it;
            if (placement->placed)
                observe(targetIdentity->subject, "building", faction->link.handle(),
                        {placement->worldX, placement->worldY}, false, 0.0f);
        }
    }
    return Result<std::size_t>::success(sources.size(),
                                        Status::success(sources.empty() ? StatusCode::NoOp : StatusCode::Applied));
}


}  // namespace eve::rts
