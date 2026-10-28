#include "rpg/Progression.h"
#include "rpg/RPGActor.h"

#include <algorithm>
#include <cmath>

namespace eve::rpg {

namespace {
std::vector<LevelUpEvent>& events() {
    static std::vector<LevelUpEvent> value;
    return value;
}
}  // namespace

void ProgressionSystem::configure(RPGActor* actor, int level, int maxLevel, double baseXp, double growth,
                                  int pointsPerLevel) {
    if (!actor) return;
    auto p            = actor->progression();
    p->level          = std::max(1, level);
    p->maxLevel       = std::max(p->level, maxLevel);
    p->baseXp         = std::max(1.0, baseXp);
    p->growth         = std::max(1.0, growth);
    p->pointsPerLevel = std::max(0, pointsPerLevel);
    p->experience     = 0.0;
    p->unspentPoints  = 0;
}

double ProgressionSystem::experienceForNextLevel(RPGActor* actor) {
    if (!actor) return 0.0;
    const auto p = actor->progression();
    return p->level >= p->maxLevel ? 0.0 : std::ceil(p->baseXp * std::pow(p->growth, p->level - 1));
}

int ProgressionSystem::gainExperience(RPGActor* actor, double amount) {
    if (!actor || amount <= 0.0) return 0;
    auto p = actor->progression();
    p->experience += amount;
    int gained = 0;
    while (p->level < p->maxLevel) {
        double needed = experienceForNextLevel(actor);
        if (p->experience < needed) break;
        p->experience -= needed;
        int old = p->level++;
        p->unspentPoints += p->pointsPerLevel;
        events().push_back({actor, old, p->level, p->pointsPerLevel});
        ++gained;
    }
    if (p->level >= p->maxLevel) p->experience = 0.0;
    return gained;
}

bool ProgressionSystem::spendPoints(RPGActor* actor, int amount) {
    if (!actor || amount <= 0 || actor->progression()->unspentPoints < amount) return false;
    actor->progression()->unspentPoints -= amount;
    return true;
}

void ProgressionSystem::pollEvents(std::vector<LevelUpEvent>& out) {
    out = std::move(events());
    events().clear();
}

}  // namespace eve::rpg
