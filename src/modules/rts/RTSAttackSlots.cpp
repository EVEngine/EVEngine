#include "common/Profile.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <new>
#include <numbers>
#include <tuple>
#include "rts/RTS.h"
#include "rts/RTSSystemMovementInternal.h"

namespace eve::rts {
Result<std::size_t> RTS::stepAttackSlots() {
    EV_PROFILE_MODULE("rts", "RTS::stepAttackSlots");
    try {
        // Scope: Unit infantry with current Attack/AttackMove and a live Unit target.
        // Read: Identity, Orders, Motion, Combat, Crowd, Durability, Containment; canonical map.
        // Write: Navigation combatSlotTarget/combatSlotWaiting, after closing the view.
        // No structural mutations/events/callbacks; simulation phase before both motion backends.
        struct Projection {
            ecs::EntityHandle            handle;
            std::optional<WorldPosition> target;
            bool                         waiting = false;
        };
        std::vector<Projection> plan;
        struct Member {
            ecs::EntityHandle handle;
            SubjectRef        subject;
            float             range, radius, distance;
            std::size_t       projection;
        };
        using Key = std::tuple<ecs::Table*, std::type_index, std::uint32_t, std::uint32_t>;
        struct Group {
            ecs::EntityHandle   target;
            std::vector<Member> members;
        };
        std::map<Key, Group> groups;
        {
            auto view = ecs::View<Unit, Unit::Identity, Unit::Navigation, Unit::Motion, Unit::Orders, Unit::Combat,
                                  Unit::Crowd, Unit::Durability, Unit::Containment>();
            for (auto it = view.begin(); it != view.end(); ++it) {
                auto [identity, nav, motion, orders, combat, crowd, durability, containment] = *it;
                (void)nav;
                const auto projection = plan.size();
                plan.push_back({identity->self, {}, false});
                if (!durability->alive || containment->container.isBound()) continue;
                auto current = systems_internal::readCurrent(orders->values);
                if (!current) return Result<std::size_t>::failure(current.status());
                if (!current.value() ||
                    (current.value()->kind != OrderKind::Attack && current.value()->kind != OrderKind::AttackMove))
                    continue;
                const auto handle =
                    current.value()->kind == OrderKind::Attack ? current.value()->targetEntity : combat->target;
                auto* target = dynamic_cast<Unit*>(ecs::try_get(handle));
                if (!target || !target->durability()->alive || target->containment()->container.isBound()) continue;
                const float range = combat->engagementRange;
                const float touch = crowd->radius + target->crowd()->radius;
                if (!std::isfinite(touch) || crowd->radius < 0.f || target->crowd()->radius < 0.f ||
                    !std::isfinite(motion->x) || !std::isfinite(motion->y) || !std::isfinite(target->motion()->x) ||
                    !std::isfinite(target->motion()->y))
                    return Result<std::size_t>::failure(
                        Diagnostic::error(DiagnosticCode::InvalidArgument,
                                          "Infantry slot geometry must be finite and non-negative", "combat.slots"));
                // Rifle/artillery standoff remains owned by the weapon policy; infantry melee alone needs front
                // admission.
                if (!std::isfinite(range) || range <= 0.f || touch <= 0.f || range > touch * 2.f) continue;
                const float distance = std::hypot(motion->x - target->motion()->x, motion->y - target->motion()->y);
                if (distance > range + touch * 2.f) continue;
                const Key key{handle.table, handle.type, handle.id, handle.generation};
                auto&     group = groups[key];
                group.target    = handle;
                group.members.push_back(
                    {identity->self, identity->subject, range, crowd->radius, distance, projection});
            }
        }
        std::size_t processed = 0;
        for (auto& [key, group] : groups) {
            (void)key;
            auto* target = dynamic_cast<Unit*>(ecs::try_get(group.target));
            if (!target) continue;
            std::sort(group.members.begin(), group.members.end(), [](const Member& a, const Member& b) {
                if (a.distance != b.distance) return a.distance < b.distance;
                return a.subject.persistentId() < b.subject.persistentId();
            });
            float radius = 0.f, maximumFrontRadius = 0.f, maximumRange = 0.f;
            for (const auto& member : group.members) {
                radius             = std::max(radius, member.radius);
                maximumRange       = std::max(maximumRange, member.range);
                maximumFrontRadius = std::max(maximumFrontRadius,
                                              std::max(target->crowd()->radius + member.radius, member.range * 0.8f));
            }
            const float spacing  = 2.f * radius + 0.1f;
            const int   capacity = std::clamp(
                int(std::clamp(std::floor(2.0 * std::numbers::pi * double(maximumFrontRadius) / spacing), 1.0, 64.0)),
                1, 64);
            std::vector<bool>                            reserved(size_t(capacity), false);
            std::vector<std::pair<WorldPosition, float>> footprints;
            footprints.reserve(size_t(capacity));
            std::size_t waiting = 0;
            for (const auto& member : group.members) {
                auto* unit = dynamic_cast<Unit*>(ecs::try_get(member.handle));
                if (!unit) continue;
                auto&               projected = plan[member.projection];
                const WorldPosition from{unit->motion()->x, unit->motion()->y};
                int                 best         = -1;
                float               bestDistance = 0.f;
                WorldPosition       destination{};
                const float frontRadius = std::max(target->crowd()->radius + member.radius, member.range * 0.8f);
                for (int slot = 0; slot < capacity && frontRadius <= member.range; ++slot) {
                    if (reserved[size_t(slot)]) continue;
                    const float         angle = 2.f * std::numbers::pi_v<float> * float(slot) / float(capacity);
                    const WorldPosition candidate{target->motion()->x + std::cos(angle) * frontRadius,
                                                  target->motion()->y + std::sin(angle) * frontRadius};
                    const bool occupied = std::any_of(footprints.begin(), footprints.end(), [&](const auto& footprint) {
                        return std::hypot(candidate.x - footprint.first.x, candidate.y - footprint.first.y) <
                               member.radius + footprint.second + 0.1f;
                    });
                    if (occupied) continue;
                    if (pathfinder_ && !systems_internal::isFormationSegmentClear(*pathfinder_, navigationGrid_, from,
                                                                                  candidate, member.radius))
                        continue;
                    const float distance = std::hypot(candidate.x - from.x, candidate.y - from.y);
                    if (best < 0 || distance < bestDistance) {
                        best         = slot;
                        bestDistance = distance;
                        destination  = candidate;
                    }
                }
                if (best >= 0) {
                    reserved[size_t(best)] = true;
                    footprints.emplace_back(destination, member.radius);
                } else {
                    projected.waiting     = true;
                    const std::size_t row = waiting / size_t(capacity) + 1, slot = waiting++ % size_t(capacity);
                    const float       angle = 2.f * std::numbers::pi_v<float> * float(slot) / float(capacity);
                    const float       rearRadius =
                        std::max(maximumFrontRadius + float(row) * spacing, maximumRange + spacing);
                    destination = {target->motion()->x + std::cos(angle) * rearRadius,
                                   target->motion()->y + std::sin(angle) * rearRadius};
                    if (pathfinder_ && !systems_internal::isFormationSegmentClear(*pathfinder_, navigationGrid_, from,
                                                                                  destination, member.radius))
                        destination = from;
                }
                projected.target = destination;
                ++processed;
            }
        }
        for (const auto& projected : plan) {
            auto* unit = dynamic_cast<Unit*>(ecs::try_get(projected.handle));
            if (!unit) continue;
            unit->navigation()->combatSlotTarget  = projected.target;
            unit->navigation()->combatSlotWaiting = projected.waiting;
        }
        return Result<std::size_t>::success(processed);
    } catch (const std::bad_alloc&) {
        return Result<std::size_t>::failure(Diagnostic::error(
            DiagnosticCode::Failed, "Infantry attack-slot preparation allocation failed", "combat.slots"));
    }
}
}  // namespace eve::rts
