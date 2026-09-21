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

#include "common/Export.h"
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
 * @brief Type-erased bookkeeping shared by every `RuntimeInstance<State>`.
 *
 * Identity validation, the active flag, the reload policy machine and the error
 * construction are identical for every domain state, so they live in one
 * non-template core compiled once in `DefinitionRuntime.cpp` instead of being
 * instantiated once per state type.
 *
 * The payload stays strongly typed at the public API: this core only carries it
 * as an owning pointer plus the per-state clone/assign/destroy operations. It is
 * never turned into a dynamic field map.
 *
 * Thread affinity matches `RuntimeInstance`: the owning domain thread only.
 */
class EVENGINE_API RuntimeInstanceCore {
public:
    /** @brief Deep-copies one payload for the copy constructor and copy assignment. */
    using CloneFunction = void* (*)(const void*);
    /** @brief Assigns one payload from another payload of the same state type. */
    using AssignFunction = void (*)(void* destination, const void* source);
    /** @brief Destroys one payload; must not throw. */
    using DestroyFunction = void (*)(void*) noexcept;
    /**
     * @brief Type-erased rebuild policy callback.
     * @return A newly allocated payload whose ownership transfers to the core, or a failure
     *         that leaves the core unchanged.
     */
    using RebuildFunction =
        std::function<eve::Result<void*>(const void*, const InstanceIdentity&, const DefinitionHandle&)>;

    /**
     * @brief Adopt an already-allocated payload.
     * @param state Payload this core now owns and destroys with `destroy`.
     * @ownership Takes ownership of `state`; all four function pointers must stay valid
     *            for the lifetime of the core.
     */
    RuntimeInstanceCore(InstanceIdentity identity, void* state, CloneFunction clone, AssignFunction assign,
                        DestroyFunction destroy, bool active) noexcept;
    /** @brief Destroy the adopted payload. */
    ~RuntimeInstanceCore();

    /** @brief Deep-copy the payload and the bookkeeping. */
    RuntimeInstanceCore(const RuntimeInstanceCore& other);
    /** @brief Copy-assign; the copy is made before any mutation, so a throw leaves `*this` intact. */
    RuntimeInstanceCore& operator=(const RuntimeInstanceCore& other);
    /** @brief Move the payload and the bookkeeping. */
    RuntimeInstanceCore(RuntimeInstanceCore&& other) noexcept;
    /** @brief Move-assign, destroying the current payload. */
    RuntimeInstanceCore& operator=(RuntimeInstanceCore&& other) noexcept;

    /** @brief Borrow the complete immutable identity. */
    [[nodiscard]] const InstanceIdentity& identity() const noexcept { return identity_; }
    /** @brief Borrow the payload; null only after a move. */
    [[nodiscard]] void* state() noexcept { return state_; }
    /** @brief Borrow the payload; null only after a move. */
    [[nodiscard]] const void* state() const noexcept { return state_; }
    /** @brief Whether the instance participates in active simulation. */
    [[nodiscard]] bool isActive() const noexcept { return active_; }
    /** @brief Set active state; this does not reload or rebuild the instance. */
    void setActive(bool active) noexcept { active_ = active; }

    /**
     * @brief Verify that a caller's definition handle is the exact current incarnation.
     * @return Success for an exact match, or StaleHandle/invalid-argument otherwise.
     */
    [[nodiscard]] eve::Result<void> checkDefinition(const DefinitionHandle& handle) const;

    /**
     * @brief Apply a definition replacement with an atomic payload policy.
     * @param next Exact current handle returned by the DefinitionRegistry.
     * @param policy Policy to execute for this instance.
     * @param defaults Payload used only by ReapplyDefaults; required by that policy.
     * @param rebuild Callback used only by RebuildInstance.
     * @return Reload transition, or a failure leaving payload and identity intact.
     * @remarks `RejectWhileActive` rejects active instances. When inactive it
     *          intentionally keeps values and only advances the generation.
     */
    [[nodiscard]] eve::Result<ReloadOutcome> reload(const DefinitionHandle& next, ReloadPolicy policy,
                                                    const void* defaults, const RebuildFunction& rebuild);

    /**
     * @brief Swap in an already validated payload of this exact instance.
     * @param state Newly allocated payload; the core adopts it and destroys it when
     *              validation fails.
     * @return Success, or Conflict/StaleHandle when the snapshot belongs to a
     *         different instance or definition incarnation.
     */
    [[nodiscard]] eve::Result<void> restoreExact(const InstanceIdentity& identity, void* state, bool active);

private:
    InstanceIdentity identity_;
    void*            state_   = nullptr;
    CloneFunction    clone_   = nullptr;
    AssignFunction   assign_  = nullptr;
    DestroyFunction  destroy_ = nullptr;
    bool             active_  = true;
};

/** @brief Payload operations for one state type; used by `RuntimeInstance<State>`. */
template <class State>
[[nodiscard]] void* cloneRuntimeState(const void* state) {
    return new State(*static_cast<const State*>(state));
}

/** @brief Payload assignment for one state type. */
template <class State>
void assignRuntimeState(void* destination, const void* source) {
    *static_cast<State*>(destination) = *static_cast<const State*>(source);
}

/** @brief Payload destruction for one state type. */
template <class State>
void destroyRuntimeState(void* state) noexcept {
    delete static_cast<State*>(state);
}

/**
 * @brief Owns one strongly typed runtime state and its definition identity.
 *
 * `State` is supplied by a domain adapter; the typed accessors below keep it
 * strongly typed, and only the shared bookkeeping is type-erased into
 * `RuntimeInstanceCore`. Reload first prepares a complete candidate state and
 * then swaps it at one commit boundary. A parser, default factory or rebuild
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
        : core_(std::move(identity), new State(std::move(state)), &cloneRuntimeState<State>, &assignRuntimeState<State>,
                &destroyRuntimeState<State>, active) {}

public:
    /** @brief Copy a typed runtime instance. */
    RuntimeInstance(const RuntimeInstance&) = default;
    /** @brief Copy-assign a typed runtime instance. */
    RuntimeInstance& operator=(const RuntimeInstance&) = default;
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
    [[nodiscard]] const InstanceIdentity& identity() const noexcept { return core_.identity(); }
    /** @brief Borrow the current typed state. */
    [[nodiscard]] const State& state() const noexcept { return *static_cast<const State*>(core_.state()); }
    /** @brief Mutate typed state on the owning domain thread. */
    [[nodiscard]] State& state() noexcept { return *static_cast<State*>(core_.state()); }
    /** @brief Whether the instance participates in active simulation. */
    [[nodiscard]] bool isActive() const noexcept { return core_.isActive(); }
    /** @brief Set active state; this does not reload or rebuild the instance. */
    void setActive(bool active) noexcept { core_.setActive(active); }

    /**
     * @brief Verify that a caller's definition handle is the exact current incarnation.
     * @return Success for an exact match, or StaleHandle/invalid-argument otherwise.
     */
    [[nodiscard]] eve::Result<void> checkDefinition(const DefinitionHandle& handle) const {
        return core_.checkDefinition(handle);
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
     */
    [[nodiscard]] eve::Result<ReloadOutcome> reload(const DefinitionHandle& next, ReloadPolicy policy,
                                                    const State& defaults, RebuildFunction rebuild = {}) {
        RuntimeInstanceCore::RebuildFunction erased;
        if (rebuild) {
            erased = [callback = std::move(rebuild)](const void* state, const InstanceIdentity& identity,
                                                     const DefinitionHandle& handle) -> eve::Result<void*> {
                auto rebuilt = callback(*static_cast<const State*>(state), identity, handle);
                if (!rebuilt) return eve::Result<void*>::failure(rebuilt.status());
                return eve::Result<void*>::success(new State(std::move(rebuilt).takeValue()));
            };
        }
        return core_.reload(next, policy, &defaults, erased);
    }

    /**
     * @brief Restore an already validated snapshot of this exact instance.
     * @param identity Snapshot identity; it must exactly match the current
     *        instance identity, including definition generation.
     * @param state Fully decoded candidate state. The core adopts it and destroys
     *        it when the identity does not match.
     * @param active Active flag restored with the state.
     * @return Success, or Conflict/StaleHandle when the snapshot belongs to a
     *         different instance or definition incarnation.
     * @remarks This deliberately does not permit changing identity. A caller
     *          that wants to apply a newer definition must first perform the
     *          normal reload protocol and then capture a new snapshot.
     */
    [[nodiscard]] eve::Result<void> restoreExact(const InstanceIdentity& identity, State state, bool active) {
        return core_.restoreExact(identity, new State(std::move(state)), active);
    }

private:
    RuntimeInstanceCore core_;
};

}  // namespace eve::definition
