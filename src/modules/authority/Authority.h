#pragma once

#include "common/Module.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace eve::authority {

/** @brief Whether an authority rule grants or denies a capability. */
enum class RuleEffect { Grant, Deny };

/** @brief One stable authority rule over an opaque actor and scope. */
struct Rule {
    std::string id;
    std::string actor;
    std::string scope;
    std::string capability;
    std::string source;
    RuleEffect  effect    = RuleEffect::Grant;
    int         priority  = 0;
    double      duration  = 0.0;
    double      remaining = -1.0;
    uint64_t    order     = 0;
};

/** @brief Explainable result of an authority decision. */
struct Decision {
    bool        allowed = false;
    std::string reason;
    std::string winningRuleId;
    std::string source;
    int         priority = 0;
};

/** @brief Kind of authority lifecycle event. */
enum class EventKind { Granted, Denied, Revoked, Expired };

/** @brief Deterministically sequenced authority lifecycle event. */
struct Event {
    uint64_t    sequence = 0;
    EventKind   kind     = EventKind::Granted;
    std::string ruleId;
    std::string actor;
    std::string scope;
    std::string capability;
    std::string source;
    std::string reason;
};

/** @brief Generic, deterministic capability authority store. */
class Store {
public:
    /** @brief Adds a grant rule and returns its stable ID, or an empty string for invalid input. */
    std::string grant(const std::string& actor, const std::string& scope, const std::string& capability,
                      const std::string& source, int priority = 0, double duration = 0.0);
    /** @brief Adds a deny rule and returns its stable ID, or an empty string for invalid input. */
    std::string deny(const std::string& actor, const std::string& scope, const std::string& capability,
                     const std::string& source, int priority = 0, double duration = 0.0);
    /** @brief Revokes a rule by stable ID. */
    bool revoke(const std::string& id, const std::string& reason = "revoked");
    /** @brief Revokes every rule from a source and returns the number removed. */
    int revokeBySource(const std::string& source, const std::string& reason = "source_revoked");
    /** @brief Advances finite rule durations and expires rules reaching zero. */
    void update(double dtSeconds);

    /** @brief Returns whether the winning rule allows a capability. */
    bool can(const std::string& actor, const std::string& scope, const std::string& capability) const;
    /** @brief Returns an explainable decision and its winning rule. */
    Decision explain(const std::string& actor, const std::string& scope, const std::string& capability) const;
    /** @brief Returns a rule by stable ID, or nullptr. */
    const Rule* find(const std::string& id) const;

    /** @brief Queries rules for an actor in deterministic creation order. */
    int queryActor(const std::string& actor);
    /** @brief Queries rules for a scope in deterministic creation order. */
    int queryScope(const std::string& scope);
    /** @brief Queries rules for a capability in deterministic creation order. */
    int queryCapability(const std::string& capability);
    /** @brief Queries rules matching actor, scope, and capability. Empty fields act as wildcards. */
    int query(const std::string& actor, const std::string& scope, const std::string& capability);
    /** @brief Returns a rule from the latest query, or nullptr. */
    const Rule* queryAt(int index) const;

    /** @brief Returns retained authority event count. */
    int eventCount() const;
    /** @brief Returns a retained event, or nullptr. */
    const Event* eventAt(int index) const;
    /** @brief Clears retained events without resetting sequence allocation. */
    void clearEvents();
    /** @brief Exports persistent state as deterministic compact JSON. */
    std::string snapshotJson() const;
    /** @brief Transactionally restores a snapshot produced by snapshotJson(). */
    bool restoreJson(const std::string& json);
    /** @brief Returns the latest restore error. */
    const std::string& lastError() const;

private:
    std::string add(RuleEffect effect, const std::string& actor, const std::string& scope,
                    const std::string& capability, const std::string& source, int priority, double duration);
    void        emit(EventKind kind, const Rule& rule, const std::string& reason = {});

    uint64_t                 nextId_       = 1;
    uint64_t                 nextOrder_    = 1;
    uint64_t                 nextSequence_ = 1;
    std::deque<Rule>         rules_;
    std::deque<Event>        events_;
    std::vector<const Rule*> query_;
    std::string              lastError_;
};

/** @brief Returns the stable lowercase name of a rule effect. */
std::string effectName(RuleEffect effect);
/** @brief Returns the stable lowercase name of an event kind. */
std::string eventKindName(EventKind kind);

/** @brief Script module factory for generic authority stores. */
class Authority : public Module {
public:
    Module_REG(Authority);
    Authority()           = default;
    ~Authority() override = default;

    /** @brief Allocates a module-owned authority store. */
    static Store* newStore();

private:
    std::vector<std::unique_ptr<Store>> stores_;
};

}  // namespace eve::authority
