#pragma once

#include "rts/RTSSystems.h"

#include <algorithm>
#include <vector>

namespace eve::rts::combat_internal {

// Provider membership is fixed after the first suppression hit. Component values remain live.
class SuppressionAuraSources {
    struct Source {
        Unit::Motion*      motion;
        Unit::Faction*     faction;
        Unit::Morale*      morale;
        Unit::Containment* containment;
        Unit::Durability*  durability;
    };

    std::vector<Source> sources_;

public:
    SuppressionAuraSources() {
        auto view = ecs::View<Unit, Unit::Motion, Unit::Faction, Unit::Morale, Unit::Containment, Unit::Durability>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [motion, faction, morale, containment, durability] = *it;
            if (morale->auraRange > 0.0f) sources_.push_back({motion, faction, morale, containment, durability});
        }
    }

    [[nodiscard]] float factor(WorldPosition target, const FactionLink& targetFaction) const {
        float result = 1.0f;
        for (const auto& source : sources_) {
            if (!source.durability->alive || source.containment->container.isBound() ||
                source.morale->auraRange <= 0.0f ||
                !FactionRelationSystem::isAllied(source.faction->link, targetFaction))
                continue;
            const float dx = target.x - source.motion->x;
            const float dy = target.y - source.motion->y;
            if (dx * dx + dy * dy <= source.morale->auraRange * source.morale->auraRange)
                result = std::min(result, std::clamp(source.morale->auraSuppressionFactor, 0.0f, 1.0f));
        }
        return result;
    }
};

}  // namespace eve::rts::combat_internal
