#pragma once

#include "common/Module.h"
#include "common/Snapshot.h"
#include "common/SquirrelOwnership.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace eve::authority {

/** @brief Handle domain for module-owned authority stores. */
struct AuthorityStoreHandleTag {};
/** @brief Generation- and module-epoch-qualified authority store reference. */
using AuthorityStoreHandleRef = eve::script::RuntimeHandleRef<AuthorityStoreHandleTag>;

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
    /**
     * @brief Creates an authority store with an optional persistent identity.
     * @param instanceId Identity carried by new SnapshotEnvelope values; nil is a valid legacy identity.
     */
    explicit Store(eve::PersistentId instanceId = {});

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
    /**
     * @brief Transactionally restores a snapshot produced by snapshotJson().
     * @param json Snapshot JSON to parse and validate.
     * @return Success, or a structured parse/invariant diagnostic; failure
     *         leaves every observable store field unchanged.
     * @thread Call on the store's owning simulation thread.
     * @reentrancy Does not invoke callbacks.
     */
    [[nodiscard]] eve::Result<void> restoreJson(const std::string& json);

    /**
     * @brief Captures this store in the common versioned snapshot envelope.
     * @param hashProvider Explicit content-digest provider; it is never defaulted silently.
     * @return A sealed snapshot, or a structured serialization/hash failure.
     */
    [[nodiscard]] eve::Result<eve::SnapshotEnvelope> snapshot(const eve::SnapshotHashProvider& hashProvider) const;

    /**
     * @brief Restores a verified or migratable snapshot transactionally.
     * @param snapshot Source envelope. Its schema must be `authority:store`.
     * @param hashProvider Explicit provider used to verify and reseal the payload.
     * @return Success, or a failure leaving all store state unchanged.
     */
    [[nodiscard]] eve::Result<void> restoreSnapshot(const eve::SnapshotEnvelope&     snapshot,
                                                    const eve::SnapshotHashProvider& hashProvider);

    /**
     * @brief Serializes the common snapshot envelope as canonical JSON.
     * @param hashProvider Explicit content-digest provider.
     * @return Canonical envelope JSON or a structured failure.
     */
    [[nodiscard]] eve::Result<std::string> snapshotEnvelopeJson(const eve::SnapshotHashProvider& hashProvider) const;

    /**
     * @brief Parses and transactionally restores a common snapshot envelope.
     * @param json Canonical snapshot envelope JSON.
     * @param hashProvider Explicit provider used to verify contentHash.
     * @return Success, or a failure leaving all store state unchanged.
     */
    [[nodiscard]] eve::Result<void> restoreSnapshotJson(std::string_view                 json,
                                                        const eve::SnapshotHashProvider& hashProvider);

private:
    std::string add(RuleEffect effect, const std::string& actor, const std::string& scope,
                    const std::string& capability, const std::string& source, int priority, double duration);
    void        emit(EventKind kind, const Rule& rule, const std::string& reason = {});

    uint64_t                 nextId_       = 1;
    uint64_t                 nextOrder_    = 1;
    uint64_t                 nextSequence_ = 1;
    eve::PersistentId        instanceId_;
    eve::Revision            revision_;
    eve::SimulationTick      tick_;
    std::deque<Rule>         rules_;
    std::deque<Event>        events_;
    std::vector<const Rule*> query_;
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

    /**
     * @brief Allocates an authority store and returns its ownership reference.
     * @return A generation-qualified reference; the current Authority module owns the store.
     * @remarks The reference becomes stale after release, module unload, or reload.
     */
    [[nodiscard]] static eve::Result<AuthorityStoreHandleRef> newStore();
    /** @brief Resolves a live store as a non-owning observation. */
    [[nodiscard]] static eve::script::Borrowed<Store> resolve(AuthorityStoreHandleRef reference) noexcept;
    /** @brief Releases a module-owned store. */
    [[nodiscard]] static eve::Result<void> release(AuthorityStoreHandleRef reference);
    /** @brief Reports whether a store reference is stale for the current module. */
    [[nodiscard]] static bool isStale(AuthorityStoreHandleRef reference) noexcept;

private:
    eve::script::RuntimeObjectRegistry<Store, AuthorityStoreHandleTag> stores_;
};

}  // namespace eve::authority
