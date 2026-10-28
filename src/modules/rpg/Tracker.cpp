#include "rpg/Tracker.h"
#include "rpg/Quest.h"
#include "rpg/QuestSystem.h"

namespace eve::rpg {

Tracker::Tracker() { QuestSystem::syncAuto(*this); }
void        Tracker::syncAuto() { QuestSystem::syncAuto(*this); }
bool        Tracker::activate(const std::string& id) { return QuestSystem::activate(*this, id); }
bool        Tracker::canActivate(const std::string& id) const { return canActivateReason(id).empty(); }
std::string Tracker::canActivateReason(const std::string& id) const {
    return QuestSystem::canActivateReason(*this, id);
}
void Tracker::notify(const std::string& topic, const std::string& target, int amount) {
    QuestSystem::notify(*this, topic, target, amount);
}
bool Tracker::claim(const std::string& id) { return QuestSystem::claim(*this, id); }
bool Tracker::reset(const std::string& id) { return QuestSystem::reset(*this, id); }
bool Tracker::abandon(const std::string& id) { return QuestSystem::fail(*this, id, "abandoned"); }
bool Tracker::fail(const std::string& id, const std::string& reason) { return QuestSystem::fail(*this, id, reason); }
int  Tracker::getCount() const { return int(order_.size()); }
std::string Tracker::getId(int i) const {
    return i >= 0 && size_t(i) < order_.size() ? order_[size_t(i)] : std::string{};
}
std::string Tracker::getState(const std::string& id) const {
    auto i = entries_.find(id);
    return i == entries_.end() ? std::string{} : i->second.state;
}
bool Tracker::hasTag(const std::string& id, const std::string& tag) const {
    auto* d = QuestRegistry::find(id);
    return d && d->hasTag(tag);
}
std::string Tracker::getExtra(const std::string& id, const std::string& key) const {
    auto* d = QuestRegistry::find(id);
    if (!d) return {};
    auto i = d->extra.find(key);
    return i == d->extra.end() ? std::string{} : i->second;
}
int Tracker::getObjectiveCount(const std::string& id) const {
    auto i = entries_.find(id);
    return i == entries_.end() ? 0 : int(i->second.objectives.size());
}

static const ObjectiveRuntime* objectiveAt(const std::unordered_map<std::string, QuestRuntime>& entries,
                                           const std::string& id, int index) {
    auto i = entries.find(id);
    return i != entries.end() && index >= 0 && size_t(index) < i->second.objectives.size()
               ? &i->second.objectives[size_t(index)]
               : nullptr;
}
std::string Tracker::getObjectiveId(const std::string& id, int i) const {
    auto* v = objectiveAt(entries_, id, i);
    return v ? v->id : std::string{};
}
int Tracker::getObjectiveCurrent(const std::string& id, int i) const {
    auto* v = objectiveAt(entries_, id, i);
    return v ? v->current : 0;
}
int Tracker::getObjectiveCountRequired(const std::string& id, int i) const {
    auto* v = objectiveAt(entries_, id, i);
    return v ? v->count : 0;
}
bool Tracker::isObjectiveDone(const std::string& id, int i) const {
    auto* v = objectiveAt(entries_, id, i);
    return v && v->done;
}
int Tracker::getRewardCount(const std::string& id) const {
    auto* d = QuestRegistry::find(id);
    return d ? int(d->rewards.size()) : 0;
}
std::string Tracker::getRewardType(const std::string& id, int i) const {
    auto* d = QuestRegistry::find(id);
    return d && i >= 0 && size_t(i) < d->rewards.size() ? d->rewards[size_t(i)].type : std::string{};
}
std::string Tracker::getRewardId(const std::string& id, int i) const {
    auto* d = QuestRegistry::find(id);
    return d && i >= 0 && size_t(i) < d->rewards.size() ? d->rewards[size_t(i)].id : std::string{};
}
float Tracker::getRewardAmount(const std::string& id, int i) const {
    auto* d = QuestRegistry::find(id);
    return d && i >= 0 && size_t(i) < d->rewards.size() ? float(d->rewards[size_t(i)].amount) : 0.f;
}
void Tracker::pollEvents() {
    polled_ = std::move(pending_);
    pending_.clear();
}
int                      Tracker::getEventCount() const { return int(polled_.size()); }
static const QuestEvent* eventAt(const std::vector<QuestEvent>& e, int i) {
    return i >= 0 && size_t(i) < e.size() ? &e[size_t(i)] : nullptr;
}
#define QUEST_GETTER(Name, Field)            \
    std::string Tracker::Name(int i) const { \
        auto* e = eventAt(polled_, i);       \
        return e ? e->Field : std::string{}; \
    }
QUEST_GETTER(getEventEntryId, entryId)
QUEST_GETTER(getEventObjectiveId, objectiveId)
QUEST_GETTER(getEventAction, action)
QUEST_GETTER(getEventTopic, topic)
QUEST_GETTER(getEventTarget, target)
QUEST_GETTER(getEventReason, reason)
#undef QUEST_GETTER
int Tracker::getEventAmount(int i) const {
    auto* e = eventAt(polled_, i);
    return e ? e->amount : 0;
}

}  // namespace eve::rpg
