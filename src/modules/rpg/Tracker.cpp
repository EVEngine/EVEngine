#include "rpg/Tracker.h"

#include "rpg/Quest.h"
#include "rpg/QuestSystem.h"

#include <utility>

namespace eve::rpg {

Tracker::Tracker() { QuestSystem::syncAuto(this); }

void Tracker::syncAuto() { QuestSystem::syncAuto(this); }

bool Tracker::activate(const std::string &id) { return QuestSystem::activate(this, id); }

bool Tracker::canActivate(const std::string &id) { return QuestSystem::canActivate(this, id); }

std::string Tracker::canActivateReason(const std::string &id) {
    return QuestSystem::canActivateReason(this, id);
}

void Tracker::notify(const std::string &topic, const std::string &target, int amount) {
    QuestSystem::notify(this, topic, target, amount);
}

bool Tracker::claim(const std::string &id) { return QuestSystem::claim(this, id); }

bool Tracker::reset(const std::string &id) { return QuestSystem::reset(this, id); }

bool Tracker::abandon(const std::string &id) { return QuestSystem::abandon(this, id); }

bool Tracker::fail(const std::string &id, const std::string &reason) {
    return QuestSystem::fail(this, id, reason);
}

void Tracker::pollEvents() {
    polled = std::move(pending);
    pending.clear();
}

int Tracker::getCount() const { return int(entries.size()); }

std::string Tracker::getId(int index) const {
    if (index < 0 || size_t(index) >= order.size()) return {};
    return order[size_t(index)];
}

std::string Tracker::getState(const std::string &id) const {
    auto it = entries.find(id);
    return it == entries.end() ? std::string{} : it->second.state;
}

bool Tracker::hasTag(const std::string &id, const std::string &tag) const {
    const QuestDefinition *def = QuestRegistry::find(id);
    return def && def->hasTag(tag);
}

std::string Tracker::getExtra(const std::string &id, const std::string &key) const {
    const QuestDefinition *def = QuestRegistry::find(id);
    return def ? def->getExtra(key, "") : std::string{};
}

int Tracker::getObjectiveCount(const std::string &id) const {
    auto it = entries.find(id);
    return it == entries.end() ? 0 : int(it->second.objectives.size());
}

std::string Tracker::getObjectiveId(const std::string &id, int index) const {
    auto it = entries.find(id);
    if (it == entries.end()) return {};
    if (index < 0 || size_t(index) >= it->second.objectives.size()) return {};
    return it->second.objectives[size_t(index)].id;
}

int Tracker::getObjectiveCurrent(const std::string &id, int index) const {
    auto it = entries.find(id);
    if (it == entries.end()) return 0;
    if (index < 0 || size_t(index) >= it->second.objectives.size()) return 0;
    return it->second.objectives[size_t(index)].current;
}

int Tracker::getObjectiveCountRequired(const std::string &id, int index) const {
    auto it = entries.find(id);
    if (it == entries.end()) return 0;
    if (index < 0 || size_t(index) >= it->second.objectives.size()) return 0;
    return it->second.objectives[size_t(index)].count;
}

bool Tracker::isObjectiveDone(const std::string &id, int index) const {
    auto it = entries.find(id);
    if (it == entries.end()) return false;
    if (index < 0 || size_t(index) >= it->second.objectives.size()) return false;
    return it->second.objectives[size_t(index)].done;
}

int Tracker::getRewardCount(const std::string &id) const {
    const QuestDefinition *def = QuestRegistry::find(id);
    return def ? int(def->rewards.size()) : 0;
}

std::string Tracker::getRewardType(const std::string &id, int i) const {
    const QuestDefinition *def = QuestRegistry::find(id);
    if (!def || i < 0 || size_t(i) >= def->rewards.size()) return {};
    return def->rewards[size_t(i)].type;
}

std::string Tracker::getRewardId(const std::string &id, int i) const {
    const QuestDefinition *def = QuestRegistry::find(id);
    if (!def || i < 0 || size_t(i) >= def->rewards.size()) return {};
    return def->rewards[size_t(i)].id;
}

double Tracker::getRewardAmount(const std::string &id, int i) const {
    const QuestDefinition *def = QuestRegistry::find(id);
    if (!def || i < 0 || size_t(i) >= def->rewards.size()) return 0.0;
    return def->rewards[size_t(i)].amount;
}

int Tracker::getEventCount() const { return int(polled.size()); }

std::string Tracker::getEventEntryId(int i) const {
    if (i < 0 || size_t(i) >= polled.size()) return {};
    return polled[size_t(i)].entryId;
}

std::string Tracker::getEventObjectiveId(int i) const {
    if (i < 0 || size_t(i) >= polled.size()) return {};
    return polled[size_t(i)].objectiveId;
}

std::string Tracker::getEventAction(int i) const {
    if (i < 0 || size_t(i) >= polled.size()) return {};
    return polled[size_t(i)].action;
}

std::string Tracker::getEventTopic(int i) const {
    if (i < 0 || size_t(i) >= polled.size()) return {};
    return polled[size_t(i)].topic;
}

std::string Tracker::getEventTarget(int i) const {
    if (i < 0 || size_t(i) >= polled.size()) return {};
    return polled[size_t(i)].target;
}

int Tracker::getEventAmount(int i) const {
    if (i < 0 || size_t(i) >= polled.size()) return 0;
    return polled[size_t(i)].amount;
}

std::string Tracker::getEventReason(int i) const {
    if (i < 0 || size_t(i) >= polled.size()) return {};
    return polled[size_t(i)].reason;
}

}  // namespace eve::rpg