#pragma once
#include "common/Export.h"

/**
 * @file RuleEngine.h
 * @brief High-performance indexed rule engine for emergent gameplay triggers.
 */

#include "common/Result.h"
#include "common/SquirrelOwnership.h"
#include "emergence/FactStore.h"
#include "emergence/RuleTypes.h"
#include "emergence/WatchIndex.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace eve::emergence {

/** @brief Handle domain for module-owned rule engines. */
struct RuleEngineHandleTag {};
/** @brief Generation-qualified reference to a module-owned RuleEngine. */
using RuleEngineHandleRef = eve::script::RuntimeHandleRef<RuleEngineHandleTag>;

/**
 * @brief Indexed, deferred rule evaluator for tens of thousands of conditions.
 *
 * Fact mutations only wake rules that watch the changed key. Activations are
 * queued and resolved by `drain`, never inline during mutation. Built-in
 * fact.set plus economy credit/debit actions execute during drain; other kinds
 * go to `IEmergenceActionHandler` listeners and remain visible in the
 * activation log for scripts.
 *
 * @ownership Caller-owned when constructed directly; module-owned when created
 *            through `Emergence::newEngine()`.
 * @thread Owning simulation thread only.
 * @reentrancy `drain` must not be called reentrantly on the same engine.
 */
class EVENGINE_API_FOUNDATION RuleEngine {
public:
    RuleEngine()  = default;
    ~RuleEngine() = default;

    RuleEngine(const RuleEngine&)            = delete;
    RuleEngine& operator=(const RuleEngine&) = delete;
    RuleEngine(RuleEngine&&)                 = default;
    RuleEngine& operator=(RuleEngine&&)      = default;

    /**
     * @brief Validate and atomically replace every rule definition.
     * @param rules Owning definitions; watch keys are derived when omitted.
     * @return Committed rule count, or structured failure preserving the prior catalogue.
     */
    [[nodiscard]] eve::Result<int> replaceCatalogue(std::vector<RuleDefinition> rules);

    /**
     * @brief Replace the catalogue from a versioned JSON document.
     * @param json UTF-8 `eve.emergence.rules` version 1 document.
     * @return Committed rule count, or structured failure preserving the prior catalogue.
     */
    [[nodiscard]] eve::Result<int> replaceCatalogueJson(std::string_view json);

    /** @brief Remove every rule, fact, and queued activation. */
    void clear();

    /** @brief Committed rule count. */
    [[nodiscard]] int ruleCount() const noexcept { return static_cast<int>(rules_.size()); }
    /** @brief Whether a stable rule id exists. */
    [[nodiscard]] bool contains(std::string_view ruleId) const;
    /**
     * @brief Borrow one committed definition.
     * @ownership Borrowed from this engine; callers must not delete it.
     * @lifetime Invalidated by clear() or a successful catalogue replacement.
     * @nullable Null when the id is absent.
     * @thread Owning simulation thread only.
     */
    [[nodiscard]] const RuleDefinition* find(std::string_view ruleId) const;

    /**
     * @brief Borrow the authoritative fact store.
     * @ownership Borrowed; this engine remains the owner.
     * @lifetime Valid until the engine is destroyed.
     * @thread Owning simulation thread only.
     */
    [[nodiscard]] FactStore& facts() noexcept { return facts_; }
    /**
     * @brief Borrow the authoritative fact store.
     * @ownership Borrowed; this engine remains the owner.
     * @lifetime Valid until the engine is destroyed.
     * @thread Owning simulation thread only.
     */
    [[nodiscard]] const FactStore& facts() const noexcept { return facts_; }

    /** @brief Set a value fact and wake watchers when it changes. */
    [[nodiscard]] eve::Result<bool> setValue(std::string key, eve::Value value);
    /** @brief Set a tag fact and wake watchers when it changes. */
    [[nodiscard]] eve::Result<bool> setTag(std::string tag, bool present);
    /** @brief Set an attribute fact and wake watchers when it changes. */
    [[nodiscard]] eve::Result<bool> setAttribute(std::string key, eve::Value value);
    /** @brief Set a resource fact and wake watchers when it changes. */
    [[nodiscard]] eve::Result<bool> setResource(std::string key, eve::Value value);
    /** @brief Set a state fact and wake watchers when it changes. */
    [[nodiscard]] eve::Result<bool> setState(std::string key, eve::Value value);
    /** @brief Set an authority fact and wake watchers when it changes. */
    [[nodiscard]] eve::Result<bool> setAuthority(std::string scope, bool granted);

    /**
     * @brief Evaluate dirty rules and resolve activations for one simulation tick.
     * @param tick Injected simulation tick used for cooldown and activation metadata.
     * @return Number of activations produced this call (also retained until clearActivations).
     * @remarks Nested drain on the same engine returns PreconditionViolation.
     */
    [[nodiscard]] eve::Result<int> drain(std::uint64_t tick);

    /** @brief Activations retained from the most recent drain batches since clearActivations. */
    [[nodiscard]] int activationCount() const noexcept { return static_cast<int>(activations_.size()); }
    /**
     * @brief Borrow one retained activation.
     * @ownership Borrowed from this engine; callers must not delete it.
     * @lifetime Invalidated by clear, clearActivations, or catalogue replacement.
     * @nullable Null when index is out of range.
     * @thread Owning simulation thread only.
     */
    [[nodiscard]] const Activation* activationAt(int index) const;
    /** @brief Drop retained activations without touching facts or rules. */
    void clearActivations();

    /**
     * @brief Re-enable a once-fired rule and clear its edge latch.
     * @return Applied when the rule exists, NotFound otherwise.
     */
    [[nodiscard]] eve::Result<void> resetRule(std::string_view ruleId);

    /** @brief Diagnostic: rules marked dirty since the last successful drain start. */
    [[nodiscard]] int dirtyCount() const noexcept { return static_cast<int>(dirty_.size()); }
    /** @brief Diagnostic: condition evaluations performed during the last drain. */
    [[nodiscard]] std::uint64_t lastDrainEvaluations() const noexcept { return lastDrainEvaluations_; }
    /** @brief Diagnostic: distinct keys that woke at least one rule since construction/clear. */
    [[nodiscard]] std::uint64_t wakeKeyEvents() const noexcept { return wakeKeyEvents_; }

    /** @brief Export runtime state (facts + rule latches) as deterministic JSON. */
    [[nodiscard]] std::string snapshotJson() const;
    /**
     * @brief Transactionally restore runtime state; catalogue must already match rule ids.
     * @return Success, or structured failure leaving the engine unchanged.
     */
    [[nodiscard]] eve::Result<void> restoreJson(std::string_view json);

private:
    struct RuleRuntime {
        bool          passed        = false;
        bool          fired         = false;
        bool          disabled      = false;
        std::uint64_t cooldownUntil = 0;
    };

    void                            wakeKey(std::string_view key);
    [[nodiscard]] eve::Result<void> executeActions(const std::vector<EmergenceAction>& actions, std::uint64_t tick);
    [[nodiscard]] eve::Result<void> executeOne(const EmergenceAction& action, std::uint64_t tick);
    [[nodiscard]] static eve::Result<RuleDefinition> validateAndNormalize(RuleDefinition rule);

    FactStore                                      facts_;
    WatchIndex                                     watchIndex_;
    std::vector<RuleDefinition>                    rules_;
    std::vector<RuleRuntime>                       runtime_;
    std::unordered_map<std::string, std::uint32_t> idToIndex_;
    std::unordered_set<std::uint32_t>              dirty_;
    std::vector<Activation>                        activations_;
    std::uint64_t                                  nextSequence_         = 1;
    std::uint64_t                                  lastDrainEvaluations_ = 0;
    std::uint64_t                                  wakeKeyEvents_        = 0;
    bool                                           draining_             = false;
};

}  // namespace eve::emergence
