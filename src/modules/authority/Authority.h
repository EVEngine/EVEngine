#pragma once
#include "common/Export.h"


#include "common/Module.h"
#include "common/Result.h"
#include "common/Snapshot.h"
#include "common/SquirrelOwnership.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace eve::authority {

/** @brief Handle domain for module-owned authority stores. */
struct AuthorityStoreHandleTag {};
/** @brief Generation- and module-epoch-qualified authority store reference. */
using AuthorityStoreHandleRef = eve::script::RuntimeHandleRef<AuthorityStoreHandleTag>;

/** @brief Whether an authority rule grants or denies a capability. */
enum class RuleEffect { Grant, Deny };

/**
 * @brief Opaque field token that matches any actor, scope, or capability.
 * @remarks A rule field equal to this value is a wildcard for that dimension.
 *          Query arguments never treat this token specially: a query for actor
 *          `"*"` only matches rules whose actor is `"*"` or another wildcard.
 */
inline constexpr std::string_view kWildcard = "*";

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
class EVENGINE_API_FOUNDATION Store {
public:
    /**
     * @brief Creates an authority store with an optional persistent identity.
     * @param instanceId Identity carried by new SnapshotEnvelope values; nil is a valid legacy identity.
     */
    explicit Store(eve::PersistentId instanceId = {});

    /**
     * @brief Adds a grant rule and returns its stable ID.
     * @param actor Opaque actor identity, or `kWildcard` to match any actor.
     * @param scope Opaque scope identity, or `kWildcard` to match any scope.
     * @param capability Opaque capability name, or `kWildcard` to match any capability.
     * @param source Opaque provenance used by revokeBySource and explain.
     * @param priority Higher values win; ties prefer deny, then earlier creation order.
     * @param duration Seconds until expiry; `0` means the rule does not auto-expire.
     * @return The stable rule id, or InvalidArgument when any required field is empty
     *         or duration is non-finite.
     */
    [[nodiscard]] eve::Result<std::string> grant(const std::string& actor, const std::string& scope,
                                                 const std::string& capability, const std::string& source,
                                                 int priority = 0, double duration = 0.0);
    /**
     * @brief Adds a deny rule and returns its stable ID.
     * @param actor Opaque actor identity, or `kWildcard` to match any actor.
     * @param scope Opaque scope identity, or `kWildcard` to match any scope.
     * @param capability Opaque capability name, or `kWildcard` to match any capability.
     * @param source Opaque provenance used by revokeBySource and explain.
     * @param priority Higher values win; ties prefer deny, then earlier creation order.
     * @param duration Seconds until expiry; `0` means the rule does not auto-expire.
     * @return The stable rule id, or InvalidArgument when any required field is empty
     *         or duration is non-finite.
     */
    [[nodiscard]] eve::Result<std::string> deny(const std::string& actor, const std::string& scope,
                                                const std::string& capability, const std::string& source,
                                                int priority = 0, double duration = 0.0);
    /**
     * @brief Revokes a rule by stable ID.
     * @return Applied on success, or NotFound when the id is absent.
     */
    [[nodiscard]] eve::Result<void> revoke(const std::string& id, const std::string& reason = "revoked");
    /**
     * @brief Revokes every rule from a source and returns the number removed.
     * @remarks `0` is a valid no-op outcome, not a failure.
     */
    [[nodiscard]] int revokeBySource(const std::string& source, const std::string& reason = "source_revoked");
    /**
     * @brief Revokes every rule whose actor field equals @p actor.
     * @remarks Use when an actor entity is destroyed; does not match wildcard actors.
     */
    [[nodiscard]] int revokeByActor(const std::string& actor, const std::string& reason = "actor_revoked");
    /**
     * @brief Revokes every rule whose scope field equals @p scope.
     * @remarks Use when a scope entity is destroyed; does not match wildcard scopes.
     */
    [[nodiscard]] int revokeByScope(const std::string& scope, const std::string& reason = "scope_revoked");
    /**
     * @brief Revokes every rule whose capability field equals @p capability.
     * @remarks Does not match wildcard capabilities.
     */
    [[nodiscard]] int revokeByCapability(const std::string& capability,
                                         const std::string& reason = "capability_revoked");
    /**
     * @brief Advances finite rule durations and expires rules reaching zero.
     * @param dtSeconds Injected simulation delta; non-positive or non-finite values are ignored.
     */
    void update(double dtSeconds);

    /**
     * @brief Returns whether the winning rule allows a capability.
     * @cost Linear in candidate rules for the query triple (exact key plus wildcard rules).
     */
    [[nodiscard]] bool can(const std::string& actor, const std::string& scope, const std::string& capability) const;
    /**
     * @brief Returns an explainable decision and its winning rule.
     * @cost Linear in candidate rules for the query triple (exact key plus wildcard rules).
     * @remarks Rule fields equal to `kWildcard` match any query value on that dimension.
     */
    [[nodiscard]] Decision explain(const std::string& actor, const std::string& scope,
                                   const std::string& capability) const;
    /**
     * @brief Returns a rule by stable ID, or nullptr.
     * @ownership Borrowed from this store; callers must not delete or retain it.
     * @nullable Null when the id is absent.
     * @lifetime Until the rule is revoked/expired or the store is restored/destroyed.
     */
    [[nodiscard]] const Rule* find(const std::string& id) const;

    /** @brief Queries rules for an actor in deterministic creation order. */
    int queryActor(const std::string& actor);
    /** @brief Queries rules for a scope in deterministic creation order. */
    int queryScope(const std::string& scope);
    /** @brief Queries rules for a capability in deterministic creation order. */
    int queryCapability(const std::string& capability);
    /**
     * @brief Queries rules matching actor, scope, and capability.
     * @remarks Empty query fields act as wildcards over stored rules. Stored
     *          `kWildcard` fields also match the corresponding non-empty query field.
     */
    int query(const std::string& actor, const std::string& scope, const std::string& capability);
    /**
     * @brief Returns a rule from the latest query, or nullptr.
     * @ownership Borrowed from this store; callers must not delete or retain it.
     * @nullable Null when the index is outside the latest query range.
     * @lifetime Until the next query/mutation/restore or store destruction.
     */
    const Rule* queryAt(int index) const;

    /**
     * @brief Sets the maximum number of retained lifecycle events.
     * @param capacity `0` keeps unlimited retention (default). Positive values discard
     *        the oldest events once the cap is exceeded. Negative values are rejected.
     * @return Applied, or InvalidArgument when capacity is negative.
     */
    [[nodiscard]] eve::Result<void> setEventCapacity(int capacity);
    /** @brief Returns the event retention capacity; `0` means unlimited. */
    [[nodiscard]] int eventCapacity() const noexcept { return eventCapacity_; }
    /** @brief Returns retained authority event count. */
    int eventCount() const;
    /**
     * @brief Returns a retained event, or nullptr.
     * @ownership Borrowed from this store's retained event deque.
     * @nullable Null when the index is outside the retained range.
     * @lifetime Until `clearEvents`, capacity trim, restore, or destruction; do not
     *           retain it across those calls.
     */
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
     * @cost Allocates and hashes the full rule set; amortize across save / sync boundaries.
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
    [[nodiscard]] eve::Result<std::string> add(RuleEffect effect, const std::string& actor, const std::string& scope,
                                               const std::string& capability, const std::string& source, int priority,
                                               double duration);
    void                                   emit(EventKind kind, const Rule& rule, const std::string& reason = {});
    void                                   trimEvents();
    void                                   indexInsert(const Rule& rule);
    void                                   indexErase(const Rule& rule);
    void                                   rebuildIndex();
    [[nodiscard]] static std::string       exactKey(const std::string& actor, const std::string& scope,
                                                    const std::string& capability);
    [[nodiscard]] static bool ruleMatches(const Rule& rule, const std::string& actor, const std::string& scope,
                                          const std::string& capability);
    [[nodiscard]] static bool hasWildcard(const Rule& rule);
    [[nodiscard]] int revokeMatching(const std::string& reason, const std::function<bool(const Rule&)>& predicate);

    uint64_t                                                  nextId_        = 1;
    uint64_t                                                  nextOrder_     = 1;
    uint64_t                                                  nextSequence_  = 1;
    int                                                       eventCapacity_ = 0;
    eve::PersistentId                                         instanceId_;
    eve::Revision                                             revision_;
    eve::SimulationTick                                       tick_;
    std::deque<Rule>                                          rules_;
    std::deque<Event>                                         events_;
    std::vector<const Rule*>                                  query_;
    std::unordered_map<std::string, std::vector<std::string>> exactIndex_;
    std::vector<std::string>                                  wildcardIds_;
};

/** @brief Returns the stable lowercase name of a rule effect. */
std::string effectName(RuleEffect effect);
/** @brief Returns the stable lowercase name of an event kind. */
EVENGINE_API_FOUNDATION std::string eventKindName(EventKind kind);

/** @brief Script module factory for generic authority stores. */
class Authority : public Module {
public:
    Module_REG(Authority);
    /** @brief Constructs a Authority. */
    Authority()           = default;
    /** @brief Releases Authority resources. */
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
