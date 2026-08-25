#include "authority/Authority.h"

#include "common/Json.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace eve::authority {
namespace {

std::string quote(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : value) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20)
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c) << std::dec;
                else
                    out << static_cast<char>(c);
        }
    }
    return out.str() + '"';
}

bool parseU64(const eve::json::Value& value, uint64_t& output) {
    if (!value.isString()) return false;
    try {
        size_t pos = 0;
        output     = std::stoull(value.asString(), &pos);
        return pos == value.asString().size();
    } catch (...) {
        return false;
    }
}

bool parseRuleId(const std::string& id, uint64_t& output) {
    constexpr const char* prefix = "authority-";
    if (!id.starts_with(prefix)) return false;
    const std::string suffix = id.substr(std::char_traits<char>::length(prefix));
    if (suffix.size() != 16) return false;
    try {
        size_t pos = 0;
        output     = std::stoull(suffix, &pos);
        return pos == suffix.size() && output != 0;
    } catch (...) {
        return false;
    }
}

bool wins(const Rule& candidate, const Rule& current) {
    if (candidate.priority != current.priority) return candidate.priority > current.priority;
    if (candidate.effect != current.effect) return candidate.effect == RuleEffect::Deny;
    return candidate.order < current.order;
}

}  // namespace

std::string effectName(RuleEffect effect) { return effect == RuleEffect::Grant ? "grant" : "deny"; }
std::string eventKindName(EventKind kind) {
    switch (kind) {
        case EventKind::Granted: return "granted";
        case EventKind::Denied: return "denied";
        case EventKind::Revoked: return "revoked";
        case EventKind::Expired: return "expired";
    }
    return "unknown";
}

std::string Store::add(RuleEffect effect, const std::string& actor, const std::string& scope,
                       const std::string& capability, const std::string& source, int priority, double duration) {
    if (actor.empty() || scope.empty() || capability.empty() || !std::isfinite(duration)) return {};
    std::ostringstream id;
    id << "authority-" << std::setw(16) << std::setfill('0') << nextId_++;
    Rule rule;
    rule.id         = id.str();
    rule.actor      = actor;
    rule.scope      = scope;
    rule.capability = capability;
    rule.source     = source;
    rule.effect     = effect;
    rule.priority   = priority;
    rule.duration   = std::max(0.0, duration);
    rule.remaining  = duration > 0.0 ? duration : -1.0;
    rule.order      = nextOrder_++;
    rules_.push_back(std::move(rule));
    emit(effect == RuleEffect::Grant ? EventKind::Granted : EventKind::Denied, rules_.back());
    return rules_.back().id;
}

std::string Store::grant(const std::string& actor, const std::string& scope, const std::string& capability,
                         const std::string& source, int priority, double duration) {
    return add(RuleEffect::Grant, actor, scope, capability, source, priority, duration);
}
std::string Store::deny(const std::string& actor, const std::string& scope, const std::string& capability,
                        const std::string& source, int priority, double duration) {
    return add(RuleEffect::Deny, actor, scope, capability, source, priority, duration);
}

void Store::emit(EventKind kind, const Rule& rule, const std::string& reason) {
    events_.push_back({nextSequence_++, kind, rule.id, rule.actor, rule.scope, rule.capability, rule.source, reason});
}

bool Store::revoke(const std::string& id, const std::string& reason) {
    const auto it = std::find_if(rules_.begin(), rules_.end(), [&id](const Rule& rule) { return rule.id == id; });
    if (it == rules_.end()) return false;
    emit(EventKind::Revoked, *it, reason);
    rules_.erase(it);
    query_.clear();
    return true;
}

int Store::revokeBySource(const std::string& source, const std::string& reason) {
    int removed = 0;
    for (auto it = rules_.begin(); it != rules_.end();) {
        if (it->source == source) {
            emit(EventKind::Revoked, *it, reason);
            it = rules_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    query_.clear();
    return removed;
}

void Store::update(double dtSeconds) {
    if (dtSeconds <= 0.0 || !std::isfinite(dtSeconds)) return;
    for (auto it = rules_.begin(); it != rules_.end();) {
        if (it->remaining < 0.0) {
            ++it;
            continue;
        }
        it->remaining = std::max(0.0, it->remaining - dtSeconds);
        if (it->remaining == 0.0) {
            emit(EventKind::Expired, *it, "duration_elapsed");
            it = rules_.erase(it);
        } else {
            ++it;
        }
    }
    query_.clear();
}

Decision Store::explain(const std::string& actor, const std::string& scope, const std::string& capability) const {
    const Rule* winner = nullptr;
    for (const auto& rule : rules_)
        if (rule.actor == actor && rule.scope == scope && rule.capability == capability &&
            (!winner || wins(rule, *winner)))
            winner = &rule;
    if (!winner) return {false, "no_matching_rule", {}, {}, 0};
    return {winner->effect == RuleEffect::Grant, winner->effect == RuleEffect::Grant ? "granted" : "denied", winner->id,
            winner->source, winner->priority};
}
bool Store::can(const std::string& actor, const std::string& scope, const std::string& capability) const {
    return explain(actor, scope, capability).allowed;
}
const Rule* Store::find(const std::string& id) const {
    const auto it = std::find_if(rules_.begin(), rules_.end(), [&id](const Rule& rule) { return rule.id == id; });
    return it == rules_.end() ? nullptr : &*it;
}

int Store::query(const std::string& actor, const std::string& scope, const std::string& capability) {
    query_.clear();
    for (const auto& rule : rules_)
        if ((actor.empty() || rule.actor == actor) && (scope.empty() || rule.scope == scope) &&
            (capability.empty() || rule.capability == capability))
            query_.push_back(&rule);
    return static_cast<int>(query_.size());
}
int         Store::queryActor(const std::string& actor) { return query(actor, {}, {}); }
int         Store::queryScope(const std::string& scope) { return query({}, scope, {}); }
int         Store::queryCapability(const std::string& capability) { return query({}, {}, capability); }
const Rule* Store::queryAt(int index) const {
    return index >= 0 && static_cast<size_t>(index) < query_.size() ? query_[static_cast<size_t>(index)] : nullptr;
}
int          Store::eventCount() const { return static_cast<int>(events_.size()); }
const Event* Store::eventAt(int index) const {
    return index >= 0 && static_cast<size_t>(index) < events_.size() ? &events_[static_cast<size_t>(index)] : nullptr;
}
void Store::clearEvents() { events_.clear(); }

std::string Store::snapshotJson() const {
    std::ostringstream out;
    out << "{\"version\":1,\"nextId\":" << quote(std::to_string(nextId_))
        << ",\"nextOrder\":" << quote(std::to_string(nextOrder_))
        << ",\"nextSequence\":" << quote(std::to_string(nextSequence_)) << ",\"rules\":[";
    bool first = true;
    for (const auto& rule : rules_) {
        if (!first) out << ',';
        first = false;
        out << "{\"id\":" << quote(rule.id) << ",\"actor\":" << quote(rule.actor) << ",\"scope\":" << quote(rule.scope)
            << ",\"capability\":" << quote(rule.capability) << ",\"source\":" << quote(rule.source)
            << ",\"effect\":" << quote(effectName(rule.effect)) << ",\"priority\":" << rule.priority
            << ",\"duration\":" << std::setprecision(17) << rule.duration << ",\"remaining\":" << rule.remaining
            << ",\"order\":" << quote(std::to_string(rule.order)) << '}';
    }
    return out.str() + "]}";
}

bool Store::restoreJson(const std::string& json) {
    lastError_.clear();
    auto       document = eve::json::Document::parse(json, &lastError_);
    const auto root     = document.root();
    uint64_t   nextId = 0, nextOrder = 0, nextSequence = 0;
    if (!document.valid() || !root.isObject() || root.getInt("version") != 1 || !parseU64(root.get("nextId"), nextId) ||
        !parseU64(root.get("nextOrder"), nextOrder) || !parseU64(root.get("nextSequence"), nextSequence) ||
        nextId == 0 || nextOrder == 0 || nextSequence == 0 || !root.get("rules").isArray()) {
        if (lastError_.empty()) lastError_ = "invalid authority snapshot";
        return false;
    }
    std::deque<Rule> restored;
    const auto       values = root.get("rules");
    for (size_t i = 0; i < values.size(); ++i) {
        const auto item = values.at(i);
        Rule       rule;
        uint64_t   order = 0, numericId = 0;
        const auto effect = item.getString("effect");
        if (!item.isObject() || item.getString("id").empty() || item.getString("actor").empty() ||
            item.getString("scope").empty() || item.getString("capability").empty() ||
            (effect != "grant" && effect != "deny") || !parseU64(item.get("order"), order) || order == 0 ||
            order >= nextOrder || !parseRuleId(item.getString("id"), numericId) || numericId >= nextId ||
            !item.get("priority").isNumber() || !item.get("duration").isNumber() || !item.get("remaining").isNumber()) {
            lastError_ = "invalid rule at index " + std::to_string(i);
            return false;
        }
        rule.id         = item.getString("id");
        rule.actor      = item.getString("actor");
        rule.scope      = item.getString("scope");
        rule.capability = item.getString("capability");
        rule.source     = item.getString("source");
        rule.effect     = effect == "grant" ? RuleEffect::Grant : RuleEffect::Deny;
        rule.priority   = item.getInt("priority");
        rule.duration   = item.getDouble("duration");
        rule.remaining  = item.getDouble("remaining");
        rule.order      = order;
        if (!std::isfinite(rule.duration) || !std::isfinite(rule.remaining) || rule.duration < 0.0 ||
            rule.remaining == 0.0 || rule.remaining < -1.0 ||
            std::any_of(
                restored.begin(), restored.end(),
                [&](const Rule& old) { return old.id == rule.id || old.order == rule.order; })) {
            lastError_ = "invalid rule state at index " + std::to_string(i);
            return false;
        }
        restored.push_back(std::move(rule));
    }
    rules_        = std::move(restored);
    nextId_       = nextId;
    nextOrder_    = nextOrder;
    nextSequence_ = nextSequence;
    events_.clear();
    query_.clear();
    return true;
}
const std::string& Store::lastError() const { return lastError_; }

Store* Authority::newStore() {
    auto* module = Authority::create();
    module->stores_.push_back(std::make_unique<Store>());
    return module->stores_.back().get();
}

Module_IMPL(Authority, new Authority());

void Authority::expose(ssq::Table& table) {
    auto rule = table.addClass<Rule>("AuthorityRule", std::function<Rule*()>([] { return nullptr; }), false);
    rule.addFunc("getId", [](Rule* r) { return r ? r->id : std::string{}; });
    rule.addFunc("getActor", [](Rule* r) { return r ? r->actor : std::string{}; });
    rule.addFunc("getScope", [](Rule* r) { return r ? r->scope : std::string{}; });
    rule.addFunc("getCapability", [](Rule* r) { return r ? r->capability : std::string{}; });
    rule.addFunc("getSource", [](Rule* r) { return r ? r->source : std::string{}; });
    rule.addFunc("getEffect", [](Rule* r) { return r ? effectName(r->effect) : std::string{}; });
    rule.addFunc("getPriority", [](Rule* r) { return r ? r->priority : 0; });
    rule.addFunc("getRemaining", [](Rule* r) { return r ? static_cast<float>(r->remaining) : 0.0f; });

    auto decision =
        table.addClass<Decision>("AuthorityDecision", std::function<Decision*()>([] { return nullptr; }), false);
    decision.addFunc("isAllowed", [](Decision* d) { return d && d->allowed; });
    decision.addFunc("getReason", [](Decision* d) { return d ? d->reason : std::string{}; });
    decision.addFunc("getWinningRuleId", [](Decision* d) { return d ? d->winningRuleId : std::string{}; });
    decision.addFunc("getSource", [](Decision* d) { return d ? d->source : std::string{}; });
    decision.addFunc("getPriority", [](Decision* d) { return d ? d->priority : 0; });

    auto event = table.addClass<Event>("AuthorityEvent", std::function<Event*()>([] { return nullptr; }), false);
    event.addFunc("getSequence", [](Event* e) { return e ? static_cast<int64_t>(e->sequence) : int64_t{0}; });
    event.addFunc("getKind", [](Event* e) { return e ? eventKindName(e->kind) : std::string{}; });
    event.addFunc("getRuleId", [](Event* e) { return e ? e->ruleId : std::string{}; });
    event.addFunc("getReason", [](Event* e) { return e ? e->reason : std::string{}; });

    auto store = table.addClass<Store>("AuthorityStore", std::function<Store*()>([] { return nullptr; }), false);
    store.addFunc("grant", [](Store* s, const std::string& actor, const std::string& scope,
                              const std::string& capability, const std::string& source, int priority, float duration) {
        return s ? s->grant(actor, scope, capability, source, priority, duration) : std::string{};
    });
    store.addFunc("deny", [](Store* s, const std::string& actor, const std::string& scope,
                             const std::string& capability, const std::string& source, int priority, float duration) {
        return s ? s->deny(actor, scope, capability, source, priority, duration) : std::string{};
    });
    store.addFunc("revoke", &Store::revoke);
    store.addFunc("revokeBySource", &Store::revokeBySource);
    store.addFunc("update", [](Store* s, float dt) {
        if (s) s->update(dt);
    });
    store.addFunc("can", &Store::can);
    store.addFunc("explain", &Store::explain);
    store.addFunc(
        "find", [](Store* s, const std::string& id) -> Rule* { return s ? const_cast<Rule*>(s->find(id)) : nullptr; });
    store.addFunc("queryActor", &Store::queryActor);
    store.addFunc("queryScope", &Store::queryScope);
    store.addFunc("queryCapability", &Store::queryCapability);
    store.addFunc("query", &Store::query);
    store.addFunc("queryAt",
                  [](Store* s, int index) -> Rule* { return s ? const_cast<Rule*>(s->queryAt(index)) : nullptr; });
    store.addFunc("eventCount", &Store::eventCount);
    store.addFunc("eventAt",
                  [](Store* s, int index) -> Event* { return s ? const_cast<Event*>(s->eventAt(index)) : nullptr; });
    store.addFunc("clearEvents", &Store::clearEvents);
    store.addFunc("snapshotJson", &Store::snapshotJson);
    store.addFunc("restoreJson", &Store::restoreJson);
    store.addFunc("lastError", &Store::lastError);

    auto cls = table.addClass(name, Authority::create, false);
    expose(cls);
}
void Authority::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Authority::getName);
    cls.addFunc("newStore", [](Authority*) { return Authority::newStore(); });
}

}  // namespace eve::authority
