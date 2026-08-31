#include "rpg/LevelSystem.h"

#include "rpg/RPGActor.h"

namespace eve::rpg {

namespace {
std::vector<LevelUpEvent> &levelUpEvents() {
    static std::vector<LevelUpEvent> events;
    return events;
}
}  // namespace

int LevelSystem::getLevel(RPGActor *actor) {
    return actor ? actor->progression()->level : 1;
}

double LevelSystem::getXp(RPGActor *actor) {
    return actor ? actor->progression()->xp : 0.0;
}

double LevelSystem::getXpToNext(RPGActor *actor) {
    return actor ? actor->progression()->xpToNext : 100.0;
}

void LevelSystem::setXpToNext(RPGActor *actor, double value) {
    if (!actor) return;
    actor->progression()->xpToNext = value > 0.0 ? value : 0.0;
}

void LevelSystem::setLevel(RPGActor *actor, int level) {
    if (!actor) return;
    actor->progression()->level = level > 0 ? level : 1;
}

bool LevelSystem::gainXp(RPGActor *actor, double amount, double xpGrowth) {
    if (!actor || amount <= 0.0) return false;
    if (xpGrowth <= 0.0) xpGrowth = 1.2;
    auto &p = *actor->progression();
    p.xp += amount;
    bool leveled = false;
    while (p.xp >= p.xpToNext && p.xpToNext > 0.0) {
        p.xp -= p.xpToNext;
        ++p.level;
        p.xpToNext *= xpGrowth;
        LevelUpEvent ev;
        ev.actor = actor;
        ev.previousLevel = p.level - 1;
        ev.newLevel = p.level;
        levelUpEvents().push_back(std::move(ev));
        leveled = true;
    }
    return leveled;
}

void LevelSystem::pollLevelUps(std::vector<LevelUpEvent> &out) {
    out.clear();
    out = std::move(levelUpEvents());
    levelUpEvents().clear();
}

}  // namespace eve::rpg