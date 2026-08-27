#include "authority/Authority.h"

#include "common/Json.h"
#include "common/SquirrelBinding.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <sstream>
#include <utility>

namespace eve::authority {
namespace {

/** @brief Script-owned handle proxy; the authority store remains module-owned. */
struct ScriptAuthorityStore {
    explicit ScriptAuthorityStore(AuthorityStoreHandleRef value) : reference(value) {}
    ~ScriptAuthorityStore() noexcept {
        Authority::release(reference).ignore("script authority store proxy destruction");
    }
    AuthorityStoreHandleRef reference;
};

template <class T>
eve::Result<T> authorityBindingFailure(eve::DiagnosticCode code, std::string message,
                                       std::string path = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(
        code, std::move(message), std::move(path), {}, "authority.squirrel"));
}

template <class Ref, class Proxy, class Release>
ssq::Table makeOwnedProxy(HSQUIRRELVM vm, eve::Result<Ref>&& reference, Release&& release) {
    if (!reference)
        return eve::script::projectStatusResult(vm, reference.status(), false, false);
    const Ref ref = std::move(reference).takeValue();
    auto object = eve::script::makeOwnedSquirrelInstance<Proxy>(
        vm, std::make_unique<Proxy>(ref));
    if (!object) {
        const eve::Status status = object.status();
        object.ignore("failed to create owned authority proxy");
        std::invoke(std::forward<Release>(release), ref).ignore(
            "rollback failed owned authority allocation");
        return eve::script::projectStatusResult(vm, status, false, false);
    }
    ssq::Object owned = std::move(object).takeValue();
    auto result = eve::script::projectStatusResult(
        vm, eve::Status::success(eve::StatusCode::Applied), true, false);
    result.set("value", owned);
    result.set("ownership", std::string("owned"));
    result.set("ownerEpoch", static_cast<std::int64_t>(ref.ownerEpoch));
    result.set("handle", static_cast<std::int64_t>(ref.packed()));
    return result;
}

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

void advanceRevision(eve::Revision& revision) {
    if (const auto next = revision.incremented()) revision = *next;
}

eve::LogicalId authoritySchema() {
    const auto schema = eve::LogicalId::parse("authority:store");
    if (!schema) std::terminate();
    return *schema;
}

const eve::SnapshotMigrationChain& authorityMigrations() {
    static const eve::SnapshotMigrationChain chain = [] {
        eve::SnapshotMigrationChain result;
        const auto registration = result.add(
            authoritySchema(), eve::SchemaVersion(0), eve::SchemaVersion(1),
            [](const eve::Value& payload) -> eve::Result<eve::Value> {
                const auto* object = payload.getIf<eve::Value::Object>();
                if (!object)
                    return eve::Result<eve::Value>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::ParseError, "authority snapshot payload must be an object"));
                eve::Value::Object migrated = *object;
                migrated["version"] = eve::Value(std::int64_t(1));
                return eve::Result<eve::Value>::success(eve::Value(std::move(migrated)));
            });
        if (!registration.ok()) std::terminate();
        return result;
    }();
    return chain;
}

template <class T>
eve::Result<T> snapshotFailure(eve::DiagnosticCode code, std::string message) {
    return eve::Result<T>::failure(eve::Diagnostic::error(code, std::move(message)));
}

}  // namespace

Store::Store(eve::PersistentId instanceId) : instanceId_(instanceId) {}

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
    advanceRevision(revision_);
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
    advanceRevision(revision_);
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
    if (removed > 0) advanceRevision(revision_);
    return removed;
}

void Store::update(double dtSeconds) {
    if (dtSeconds <= 0.0 || !std::isfinite(dtSeconds)) return;
    bool changed = false;
    for (auto it = rules_.begin(); it != rules_.end();) {
        if (it->remaining < 0.0) {
            ++it;
            continue;
        }
        it->remaining = std::max(0.0, it->remaining - dtSeconds);
        changed = true;
        if (it->remaining == 0.0) {
            emit(EventKind::Expired, *it, "duration_elapsed");
            it = rules_.erase(it);
        } else {
            ++it;
        }
    }
    query_.clear();
    if (changed) advanceRevision(revision_);
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
void Store::clearEvents() {
    if (!events_.empty()) {
        events_.clear();
        advanceRevision(revision_);
    }
}

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

eve::Result<void> Store::restoreJson(const std::string& json) {
    std::string parseError;
    auto        document = eve::json::Document::parse(json, &parseError);
    const auto root     = document.root();
    uint64_t   nextId = 0, nextOrder = 0, nextSequence = 0;
    if (!document.valid() || !root.isObject() || root.getInt("version") != 1 || !parseU64(root.get("nextId"), nextId) ||
        !parseU64(root.get("nextOrder"), nextOrder) || !parseU64(root.get("nextSequence"), nextSequence) ||
        nextId == 0 || nextOrder == 0 || nextSequence == 0 || !root.get("rules").isArray()) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError,
            parseError.empty() ? "invalid authority snapshot" : parseError,
            "authority.store"));
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
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::ParseError,
                "invalid rule at index " + std::to_string(i),
                "authority.store.rules[" + std::to_string(i) + "]"));
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
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvariantViolation,
                "invalid rule state at index " + std::to_string(i),
                "authority.store.rules[" + std::to_string(i) + "]"));
        }
        restored.push_back(std::move(rule));
    }
    rules_        = std::move(restored);
    nextId_       = nextId;
    nextOrder_    = nextOrder;
    nextSequence_ = nextSequence;
    events_.clear();
    query_.clear();
    revision_ = eve::Revision(nextSequence > 0 ? nextSequence - 1 : 0);
    tick_ = eve::SimulationTick{};
    return eve::Result<void>::success();
}

eve::Result<eve::SnapshotEnvelope> Store::snapshot(
    const eve::SnapshotHashProvider& hashProvider) const {
    auto payload = eve::Value::fromJson(snapshotJson());
    if (!payload.ok()) return eve::Result<eve::SnapshotEnvelope>::failure(payload.status());
    return eve::makeSnapshotEnvelope("authority.store", authoritySchema(), eve::SchemaVersion(1), instanceId_,
                                     revision_, tick_, std::move(payload).takeValue(), hashProvider);
}

eve::Result<void> Store::restoreSnapshot(
    const eve::SnapshotEnvelope& source, const eve::SnapshotHashProvider& hashProvider) {
    if (source.type != "authority.store" || source.schema != authoritySchema())
        return snapshotFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                     "snapshot does not belong to authority::Store");
    if (!instanceId_.isNil() && source.instanceId != instanceId_)
        return snapshotFailure<void>(eve::DiagnosticCode::Conflict,
                                     "snapshot instanceId does not match authority::Store");

    auto migrated = authorityMigrations().migrate(source, eve::SchemaVersion(1), hashProvider);
    if (!migrated.ok()) return eve::Result<void>::failure(migrated.status());
    const auto& candidate = migrated.value();
    auto metadata = eve::validateSnapshotPayloadMetadata(candidate.payload, candidate.revision, candidate.tick);
    if (!metadata.ok()) return eve::Result<void>::failure(metadata.status());
    auto         payload = candidate.payload.toJson();
    if (!payload.ok()) return eve::Result<void>::failure(payload.status());
    const std::string payloadJson = std::move(payload).takeValue();

    // restoreJson parses into local containers before replacing rules/counters;
    // it is therefore the domain candidate-builder and has no partial writes.
    auto restored = restoreJson(payloadJson);
    if (!restored.ok()) return eve::Result<void>::failure(restored.status());

    instanceId_ = candidate.instanceId;
    revision_   = candidate.revision;
    tick_       = candidate.tick;
    return eve::Result<void>::success();
}

eve::Result<std::string> Store::snapshotEnvelopeJson(
    const eve::SnapshotHashProvider& hashProvider) const {
    auto value = snapshot(hashProvider);
    if (!value.ok()) return eve::Result<std::string>::failure(value.status());
    return std::move(value).andThen(
        [](eve::SnapshotEnvelope&& envelope) { return eve::serializeSnapshotEnvelope(envelope); });
}

eve::Result<void> Store::restoreSnapshotJson(
    std::string_view json, const eve::SnapshotHashProvider& hashProvider) {
    auto source = eve::parseSnapshotEnvelope(json, hashProvider);
    if (!source.ok()) return eve::Result<void>::failure(source.status());
    return restoreSnapshot(std::move(source).takeValue(), hashProvider);
}

eve::Result<AuthorityStoreHandleRef> Authority::newStore() {
    Authority* module = Authority::create();
    return module->stores_.emplace(std::make_unique<Store>());
}

eve::script::Borrowed<Store> Authority::resolve(
    AuthorityStoreHandleRef reference) noexcept {
    Authority* module = ModuleManager::getInstance<Authority>("Authority");
    if (!module) return {};
    return module->stores_.resolve(reference);
}

eve::Result<void> Authority::release(AuthorityStoreHandleRef reference) {
    Authority* module = ModuleManager::getInstance<Authority>("Authority");
    if (!module)
        return authorityBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                             "Authority module is no longer loaded", "store");
    return module->stores_.erase(reference);
}

bool Authority::isStale(AuthorityStoreHandleRef reference) noexcept {
    if (!reference.isValid()) return false;
    Authority* module = ModuleManager::getInstance<Authority>("Authority");
    return !module || module->stores_.isStale(reference);
}

Module_IMPL(Authority, new Authority());

void Authority::expose(ssq::Table& table) {
    const HSQUIRRELVM vm = table.getHandle();
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
    store.addFunc("restoreJson", [vm](Store* value, const std::string& json) {
        if (!value)
            return eve::script::projectResult(
                vm, authorityBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                   "authority store must not be null", "store"));
        return eve::script::projectResult(vm, value->restoreJson(json));
    });

    auto ownedStore = table.addClass<ScriptAuthorityStore>(
        "AuthorityStoreProxy",
        std::function<ScriptAuthorityStore*()>([] { return nullptr; }), true);
    ownedStore.addFunc("ownership", [](ScriptAuthorityStore*) { return std::string("owned"); });
    ownedStore.addFunc("ownerEpoch", [](ScriptAuthorityStore* value) {
        return value ? static_cast<int64_t>(value->reference.ownerEpoch) : int64_t{0};
    });
    ownedStore.addFunc("handle", [](ScriptAuthorityStore* value) {
        return value ? static_cast<int64_t>(value->reference.packed()) : int64_t{0};
    });
    ownedStore.addFunc("isStale", [](ScriptAuthorityStore* value) {
        return !value || Authority::isStale(value->reference);
    });
    ownedStore.addFunc("release", [vm](ScriptAuthorityStore* value) {
        if (!value)
            return eve::script::projectResult(
                vm, authorityBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                   "owned authority store proxy must not be null", "store"));
        return eve::script::projectResult(vm, Authority::release(value->reference));
    });
    ownedStore.addFunc("grant", [vm](ScriptAuthorityStore* value, const std::string& actor,
                                      const std::string& scope, const std::string& capability,
                                      const std::string& source, int priority, float duration) {
        if (!value)
            return eve::script::projectStatusResult(
                vm, authorityBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                   "owned authority store proxy must not be null", "store")
                        .status(), false, false);
        auto view = Authority::resolve(value->reference);
        if (!view.isBound())
            return eve::script::projectStatusResult(
                vm, authorityBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                                   "owned authority store handle is stale", "store")
                        .status(), false, false);
        const std::string id = view->grant(actor, scope, capability, source, priority, duration);
        const auto status = id.empty()
                                ? eve::Status::failure(eve::Diagnostic::error(
                                      eve::DiagnosticCode::InvalidArgument,
                                      "authority rule was rejected", "store", {}, "authority.squirrel"))
                                : eve::Status::success(eve::StatusCode::Applied);
        return eve::script::projectStatusResult(vm, status, !id.empty(), !id.empty(),
                                                eve::Value(id));
    });
    ownedStore.addFunc("deny", [vm](ScriptAuthorityStore* value, const std::string& actor,
                                     const std::string& scope, const std::string& capability,
                                     const std::string& source, int priority, float duration) {
        if (!value)
            return eve::script::projectStatusResult(
                vm, authorityBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                   "authority store proxy must not be null", "store")
                        .status(), false, false);
        auto view = Authority::resolve(value->reference);
        if (!view.isBound())
            return eve::script::projectStatusResult(
                vm, authorityBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                                   "authority store handle is stale", "store")
                        .status(), false, false);
        const std::string id = view->deny(actor, scope, capability, source, priority, duration);
        const auto status = id.empty()
                                ? eve::Status::failure(eve::Diagnostic::error(
                                      eve::DiagnosticCode::InvalidArgument,
                                      "authority rule was rejected", "store", {}, "authority.squirrel"))
                                : eve::Status::success(eve::StatusCode::Applied);
        return eve::script::projectStatusResult(vm, status, !id.empty(), !id.empty(), eve::Value(id));
    });
    ownedStore.addFunc("revoke", [](ScriptAuthorityStore* value, const std::string& id,
                                     const std::string& reason) {
        auto view = value ? Authority::resolve(value->reference) : eve::script::Borrowed<Store>();
        return view.isBound() && view->revoke(id, reason);
    });
    ownedStore.addFunc("revokeBySource", [](ScriptAuthorityStore* value, const std::string& source,
                                             const std::string& reason) {
        auto view = value ? Authority::resolve(value->reference) : eve::script::Borrowed<Store>();
        return view.isBound() ? view->revokeBySource(source, reason) : 0;
    });
    ownedStore.addFunc("update", [](ScriptAuthorityStore* value, float dt) {
        auto view = value ? Authority::resolve(value->reference) : eve::script::Borrowed<Store>();
        if (view.isBound()) view->update(dt);
    });
    ownedStore.addFunc("can", [](ScriptAuthorityStore* value, const std::string& actor,
                                  const std::string& scope, const std::string& capability) {
        auto view = value ? Authority::resolve(value->reference) : eve::script::Borrowed<Store>();
        return view.isBound() && view->can(actor, scope, capability);
    });
    ownedStore.addFunc("explain", [](ScriptAuthorityStore* value, const std::string& actor,
                                      const std::string& scope, const std::string& capability) {
        auto view = value ? Authority::resolve(value->reference) : eve::script::Borrowed<Store>();
        return view.isBound() ? view->explain(actor, scope, capability) : Decision{};
    });
    ownedStore.addFunc("find", [](ScriptAuthorityStore* value, const std::string& id) -> Rule* {
        auto view = value ? Authority::resolve(value->reference) : eve::script::Borrowed<Store>();
        return view.isBound() ? const_cast<Rule*>(view->find(id)) : nullptr;
    });
    ownedStore.addFunc("queryActor", [](ScriptAuthorityStore* value, const std::string& actor) {
        auto view = value ? Authority::resolve(value->reference) : eve::script::Borrowed<Store>();
        return view.isBound() ? view->queryActor(actor) : 0;
    });
    ownedStore.addFunc("queryScope", [](ScriptAuthorityStore* value, const std::string& scope) {
        auto view = value ? Authority::resolve(value->reference) : eve::script::Borrowed<Store>();
        return view.isBound() ? view->queryScope(scope) : 0;
    });
    ownedStore.addFunc("queryCapability", [](ScriptAuthorityStore* value, const std::string& capability) {
        auto view = value ? Authority::resolve(value->reference) : eve::script::Borrowed<Store>();
        return view.isBound() ? view->queryCapability(capability) : 0;
    });
    ownedStore.addFunc("query", [](ScriptAuthorityStore* value, const std::string& actor,
                                    const std::string& scope, const std::string& capability) {
        auto view = value ? Authority::resolve(value->reference) : eve::script::Borrowed<Store>();
        return view.isBound() ? view->query(actor, scope, capability) : 0;
    });
    ownedStore.addFunc("queryAt", [](ScriptAuthorityStore* value, int index) -> Rule* {
        auto view = value ? Authority::resolve(value->reference) : eve::script::Borrowed<Store>();
        return view.isBound() ? const_cast<Rule*>(view->queryAt(index)) : nullptr;
    });
    ownedStore.addFunc("eventCount", [](ScriptAuthorityStore* value) {
        auto view = value ? Authority::resolve(value->reference) : eve::script::Borrowed<Store>();
        return view.isBound() ? view->eventCount() : 0;
    });
    ownedStore.addFunc("eventAt", [](ScriptAuthorityStore* value, int index) -> Event* {
        auto view = value ? Authority::resolve(value->reference) : eve::script::Borrowed<Store>();
        return view.isBound() ? const_cast<Event*>(view->eventAt(index)) : nullptr;
    });
    ownedStore.addFunc("clearEvents", [](ScriptAuthorityStore* value) {
        auto view = value ? Authority::resolve(value->reference) : eve::script::Borrowed<Store>();
        if (view.isBound()) view->clearEvents();
    });
    ownedStore.addFunc("snapshotJson", [](ScriptAuthorityStore* value) {
        auto view = value ? Authority::resolve(value->reference) : eve::script::Borrowed<Store>();
        return view.isBound() ? view->snapshotJson() : std::string{};
    });
    ownedStore.addFunc("restoreJson", [vm](ScriptAuthorityStore* value, const std::string& json) {
        auto view = value ? Authority::resolve(value->reference) : eve::script::Borrowed<Store>();
        if (!view.isBound())
            return eve::script::projectResult(
                vm, authorityBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                                   "owned authority store handle is stale", "store"));
        return eve::script::projectResult(vm, view->restoreJson(json));
    });

    auto cls = table.addClass(name, Authority::create, false);
    expose(cls);
}
void Authority::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Authority::getName);
    cls.addFunc("newStore", [vm = cls.getHandle()](Authority*) -> ssq::Table {
        return makeOwnedProxy<AuthorityStoreHandleRef, ScriptAuthorityStore>(
            vm, Authority::newStore(),
            [](AuthorityStoreHandleRef ref) { return Authority::release(ref); });
    });
}

}  // namespace eve::authority
