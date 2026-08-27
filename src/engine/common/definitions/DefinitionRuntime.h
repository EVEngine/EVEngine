#pragma once

/**
 * @file DefinitionRuntime.h
 * @brief Common identity and hot-reload contract for typed runtime instances.
 *
 * Definitions are immutable, replaceable authoring data.  Runtime instances
 * own mutable state and keep an explicit link to the definition incarnation
 * from which that state was built.  This header contains only the neutral
 * contract; a gameplay module supplies its own strongly typed state and
 * definition parser.
 */

#include "common/Generation.h"
#include "common/ResourceRef.h"
#include "common/Result.h"

#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace eve::definition {

/** @brief Policy used when a live definition is replaced. */
enum class ReloadPolicy : std::uint8_t {
    /** @brief Replace the definition generation and preserve all instance values. */
    KeepInstanceValues,
    /** @brief Replace the typed state with defaults produced from the new definition. */
    ReapplyDefaults,
    /** @brief Let the consumer atomically construct a new state from the new definition. */
    RebuildInstance,
    /** @brief Reject the reload while the instance is active; inactive instances keep values. */
    RejectWhileActive,
};

/**
 * @brief Stable spelling for a runtime definition reload policy.
 * @return A non-null borrowed pointer to immutable static text; the caller
 *         must not free or retain it as mutable storage.
 * @ownership Borrowed from the program's static storage.
 * @nullable No.
 * @lifetime Static for the process lifetime.
 * @thread Thread-safe and side-effect free.
 * @reentrancy Does not invoke callbacks or access runtime-instance state.
 */
inline const char* reloadPolicyName(ReloadPolicy policy) noexcept {
    switch (policy) {
        case ReloadPolicy::KeepInstanceValues: return "keep_instance_values";
        case ReloadPolicy::ReapplyDefaults: return "reapply_defaults";
        case ReloadPolicy::RebuildInstance: return "rebuild_instance";
        case ReloadPolicy::RejectWhileActive: return "reject_while_active";
    }
    return "unknown";
}

/**
 * @brief A generation-qualified reference to one definition incarnation.
 *
 * The logical reference identifies the definition across reloads.  The
 * generation identifies the exact registry incarnation and therefore becomes
 * stale after replacement or removal.  This is not an ECS handle and does not
 * keep the definition alive.
 */
struct DefinitionHandle {
    eve::DefinitionRef reference;
    eve::Generation    generation;

    /** @brief Whether both the logical reference and generation are valid. */
    [[nodiscard]] bool isValid() const noexcept { return reference.id().isValid() && !generation.isZero(); }

    friend bool operator==(const DefinitionHandle&, const DefinitionHandle&) noexcept = default;
};

/**
 * @brief Canonical identity carried by every typed runtime instance.
 *
 * `instanceId` remains stable through a hot reload or instance rebuild;
 * `definition` remains the logical name; `definitionGeneration` records the
 * exact definition used by the current state.  A runtime instance is invalid
 * when any of these fields is missing.
 */
struct InstanceIdentity {
    eve::PersistentId  instanceId;
    eve::DefinitionRef definition;
    eve::Generation    definitionGeneration;

    /** @brief Construct and validate one complete instance identity. */
    [[nodiscard]] static eve::Result<InstanceIdentity> create(eve::PersistentId  instanceId,
                                                              eve::DefinitionRef definition,
                                                              eve::Generation    definitionGeneration) {
        if (instanceId.isNil())
            return eve::Result<InstanceIdentity>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "runtime instance identity must not be nil", "instanceId", {},
                "common.definitions"));
        if (!definition.id().isValid())
            return eve::Result<InstanceIdentity>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "runtime definition reference is invalid",
                                       "definition", {}, "common.definitions"));
        if (definitionGeneration.isZero())
            return eve::Result<InstanceIdentity>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "runtime definition generation must be positive",
                "definitionGeneration", {}, "common.definitions"));
        return eve::Result<InstanceIdentity>::success(
            InstanceIdentity{instanceId, std::move(definition), definitionGeneration});
    }

    /** @brief Convert the logical reference and generation into a checked handle. */
    [[nodiscard]] DefinitionHandle definitionHandle() const noexcept {
        return DefinitionHandle{definition, definitionGeneration};
    }

    /** @brief Whether this identity has all required non-zero fields. */
    [[nodiscard]] bool isValid() const noexcept {
        return !instanceId.isNil() && definition.id().isValid() && !definitionGeneration.isZero();
    }

    friend bool operator==(const InstanceIdentity&, const InstanceIdentity&) noexcept = default;
};

/** @brief Observable result of a successful or no-op definition reload. */
enum class ReloadDisposition : std::uint8_t {
    Unchanged,
    Kept,
    DefaultsReapplied,
    Rebuilt,
};

/** @brief Identity transition emitted by a typed runtime reload operation. */
struct ReloadOutcome {
    eve::PersistentId  instanceId;
    eve::DefinitionRef definition;
    eve::Generation    oldGeneration;
    eve::Generation    newGeneration;
    ReloadPolicy       policy      = ReloadPolicy::KeepInstanceValues;
    ReloadDisposition  disposition = ReloadDisposition::Unchanged;

    /** @brief Whether the reload changed the definition generation. */
    [[nodiscard]] bool changed() const noexcept { return oldGeneration != newGeneration; }
};

/**
 * @brief Owns one strongly typed runtime state and its definition identity.
 *
 * `State` is supplied by a domain adapter; this class never erases it into a
 * dynamic field map.  Reload first prepares a complete candidate state and
 * then swaps it at one commit boundary.  A parser, default factory or rebuild
 * callback failure therefore leaves both state and identity unchanged.
 *
 * The object is confined to its owning simulation/domain thread.  Callbacks
 * run synchronously without an internal lock and must not retain references to
 * the instance.  The instance itself owns `state()`; references returned by
 * that accessor are borrowed until the next successful reload or destruction.
 *
 * @tparam State Domain-specific, copyable runtime state. Candidate state is
 *         prepared behind an owning pointer so the commit itself is a
 *         non-throwing pointer swap.
 */
template <class State>
class RuntimeInstance {
    static_assert(std::is_copy_constructible_v<State>, "RuntimeInstance state must be copy constructible");

public:
    using RebuildFunction =
        std::function<eve::Result<State>(const State&, const InstanceIdentity&, const DefinitionHandle&)>;

private:
    /** @brief Construct a runtime instance from a validated identity and typed state. */
    RuntimeInstance(InstanceIdentity identity, State state, bool active = true)
        : identity_(std::move(identity)), state_(std::make_unique<State>(std::move(state))), active_(active) {}

public:
    /** @brief Copy a typed runtime instance. */
    RuntimeInstance(const RuntimeInstance& other)
        : identity_(other.identity_), state_(std::make_unique<State>(*other.state_)), active_(other.active_) {}
    /** @brief Copy-assign a typed runtime instance. */
    RuntimeInstance& operator=(const RuntimeInstance& other) {
        if (this == &other) return *this;
        auto candidate = std::make_unique<State>(*other.state_);
        identity_      = other.identity_;
        state_.swap(candidate);
        active_ = other.active_;
        return *this;
    }
    /** @brief Move a typed runtime instance. */
    RuntimeInstance(RuntimeInstance&&) noexcept = default;
    /** @brief Move-assign a typed runtime instance. */
    RuntimeInstance& operator=(RuntimeInstance&&) noexcept = default;
    /** @brief Destroy a typed runtime instance. */
    ~RuntimeInstance() = default;

    /**
     * @brief Construct a runtime instance after validating its identity.
     * @return An owning typed instance or a structured invalid-identity error.
     */
    [[nodiscard]] static eve::Result<RuntimeInstance> create(eve::PersistentId  instanceId,
                                                             eve::DefinitionRef definition,
                                                             eve::Generation definitionGeneration, State state,
                                                             bool active = true) {
        auto identity = InstanceIdentity::create(instanceId, std::move(definition), definitionGeneration);
        if (!identity) return eve::Result<RuntimeInstance>::failure(identity.status());
        return eve::Result<RuntimeInstance>::success(
            RuntimeInstance(std::move(identity).takeValue(), std::move(state), active));
    }

    /** @brief Borrow the complete immutable identity. */
    [[nodiscard]] const InstanceIdentity& identity() const noexcept { return identity_; }
    /** @brief Borrow the current typed state. */
    [[nodiscard]] const State& state() const noexcept { return *state_; }
    /** @brief Mutate typed state on the owning domain thread. */
    [[nodiscard]] State& state() noexcept { return *state_; }
    /** @brief Whether the instance participates in active simulation. */
    [[nodiscard]] bool isActive() const noexcept { return active_; }
    /** @brief Set active state; this does not reload or rebuild the instance. */
    void setActive(bool active) noexcept { active_ = active; }

    /**
     * @brief Verify that a caller's definition handle is the exact current incarnation.
     * @return Success for an exact match, or StaleHandle/invalid-argument otherwise.
     */
    [[nodiscard]] eve::Result<void> checkDefinition(const DefinitionHandle& handle) const {
        if (!handle.isValid())
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                     "definition handle is invalid", "handle", {},
                                                                     "common.definitions"));
        if (handle != identity_.definitionHandle())
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::StaleHandle, "runtime instance refers to a different definition generation",
                "handle", {}, "common.definitions"));
        return eve::Result<void>::success();
    }

    /**
     * @brief Apply a definition replacement with an atomic typed-state policy.
     * @param next Exact current handle returned by the DefinitionRegistry.
     * @param policy Policy to execute for this instance.
     * @param defaults Typed defaults parsed from `next`; used only by
     *        ReapplyDefaults.
     * @param rebuild Callback used only by RebuildInstance. It returns a fully
     *        prepared State and may inspect the old state and new handle.
     * @return Reload transition, or a failure leaving state and identity intact.
     * @remarks `RejectWhileActive` rejects active instances. When inactive it
     *          intentionally keeps values and only advances the generation.
     */
    [[nodiscard]] eve::Result<ReloadOutcome> reload(const DefinitionHandle& next, ReloadPolicy policy,
                                                    const State& defaults, RebuildFunction rebuild = {}) {
        if (!next.isValid())
            return eve::Result<ReloadOutcome>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                              "next definition handle is invalid",
                                                                              "next", {}, "common.definitions"));
        if (next.reference != identity_.definition)
            return eve::Result<ReloadOutcome>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "definition reload targets a different logical definition",
                "next.reference", {}, "common.definitions"));
        if (next.generation < identity_.definitionGeneration)
            return eve::Result<ReloadOutcome>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::StaleHandle, "definition reload handle is older than the instance generation",
                "next.generation", {}, "common.definitions"));

        ReloadOutcome outcome{
            identity_.instanceId,        identity_.definition, identity_.definitionGeneration, next.generation, policy,
            ReloadDisposition::Unchanged};
        if (next.generation == identity_.definitionGeneration)
            return eve::Result<ReloadOutcome>::success(std::move(outcome), eve::Status::success(eve::StatusCode::NoOp));

        if (policy == ReloadPolicy::RejectWhileActive && active_)
            return eve::Result<ReloadOutcome>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "active runtime instance rejects definition reload", "policy", {},
                "common.definitions"));

        try {
            State             candidate(*state_);
            ReloadDisposition disposition = ReloadDisposition::Kept;
            if (policy == ReloadPolicy::ReapplyDefaults) {
                candidate   = defaults;
                disposition = ReloadDisposition::DefaultsReapplied;
            } else if (policy == ReloadPolicy::RebuildInstance) {
                if (!rebuild)
                    return eve::Result<ReloadOutcome>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "rebuild policy requires a rebuild callback", "rebuild",
                        {}, "common.definitions"));
                auto rebuilt = rebuild(*state_, identity_, next);
                if (!rebuilt) return eve::Result<ReloadOutcome>::failure(rebuilt.status());
                candidate   = std::move(rebuilt).takeValue();
                disposition = ReloadDisposition::Rebuilt;
            } else if (policy == ReloadPolicy::RejectWhileActive) {
                // The active guard was handled above. Inactive instances have
                // the documented keep-values behavior.
                disposition = ReloadDisposition::Kept;
            }

            auto committed = std::make_unique<State>(std::move(candidate));
            state_.swap(committed);
            identity_.definitionGeneration = next.generation;
            outcome.disposition            = disposition;
            return eve::Result<ReloadOutcome>::success(std::move(outcome),
                                                       eve::Status::success(eve::StatusCode::Applied));
        } catch (const std::exception&) {
            return eve::Result<ReloadOutcome>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "definition reload candidate preparation failed",
                                       {}, {}, "common.definitions"));
        } catch (...) {
            return eve::Result<ReloadOutcome>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "definition reload candidate preparation failed",
                                       {}, {}, "common.definitions"));
        }
    }

    /**
     * @brief Restore an already validated snapshot of this exact instance.
     * @param identity Snapshot identity; it must exactly match the current
     *        instance identity, including definition generation.
     * @param state Fully decoded candidate state. It is swapped only after all
     *        caller-side validation and decoding has succeeded.
     * @param active Active flag restored with the state.
     * @return Success, or Conflict/StaleHandle when the snapshot belongs to a
     *         different instance or definition incarnation.
     * @remarks This deliberately does not permit changing identity. A caller
     *          that wants to apply a newer definition must first perform the
     *          normal reload protocol and then capture a new snapshot.
     */
    [[nodiscard]] eve::Result<void> restoreExact(const InstanceIdentity& identity, State state, bool active) {
        if (!identity.isValid())
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                     "runtime snapshot identity is invalid", "identity",
                                                                     {}, "common.definitions"));
        if (identity.instanceId != identity_.instanceId || identity.definition != identity_.definition)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "runtime snapshot belongs to a different instance or definition",
                "identity", {}, "common.definitions"));
        if (identity.definitionGeneration != identity_.definitionGeneration)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::StaleHandle, "runtime snapshot definition generation is not current",
                "identity.definitionGeneration", {}, "common.definitions"));

        auto committed = std::make_unique<State>(std::move(state));
        state_.swap(committed);
        active_ = active;
        return eve::Result<void>::success();
    }

private:
    InstanceIdentity       identity_;
    std::unique_ptr<State> state_;
    bool                   active_ = true;
};

}  // namespace eve::definition
