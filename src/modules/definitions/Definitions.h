#pragma once
#include "common/Export.h"


#include "common/Module.h"
#include "common/Snapshot.h"
#include "common/Subscription.h"
#include "common/VersionedRegistry.h"
#include "common/definitions/DefinitionRuntime.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace eve::definitions {
class DefinitionRegistryCapture;

/**
 * @brief Compatibility names for the canonical common definition identity.
 *
 * New code uses `eve::DefinitionRef` and `eve::definition::DefinitionHandle`
 * directly. These aliases keep the Definitions module API source-compatible
 * while ensuring it cannot grow a second logical-reference representation.
 */
using DefinitionRef    = eve::DefinitionRef;
using DefinitionHandle = eve::definition::DefinitionHandle;

/** @brief One immutable view of a registered versioned JSON definition. */
struct Definition {
    std::string type;
    std::string id;
    eve::SchemaVersion version{1};
    // Compatibility projection maintained by VersionedRegistry's generation
    // projector. The registry slot is the authoritative generation owner.
    eve::Generation generation;
    std::string json       = "null";
};

/** @brief Registry mutation notification in deterministic sequence order. */
struct DefinitionEvent {
    eve::EventSequence sequence;
    std::string name;
    std::string type;
    std::string id;
    eve::SchemaVersion version;
    eve::Generation    generation;
};

/**
 * @brief Deterministic registry of topic-neutral, versioned JSON definitions.
 *
 * Returned Definition references remain valid until that key is replaced or
 * removed, or until restoreJson() replaces registry state. The reference is a
 * synchronous borrow; callers must not retain it across registry mutation.
 */
class EVENGINE_API_FOUNDATION DefinitionRegistry {
public:
    /**
     * @brief Creates a definition registry with an optional persistent identity.
     * @param instanceId Identity carried by new SnapshotEnvelope values; nil is a valid legacy identity.
     */
    explicit DefinitionRegistry(eve::PersistentId instanceId = {});

    using ChangeCallback = std::function<void(const DefinitionEvent&)>;

    /**
     * @brief Inserts a definition using the common generation-qualified registry.
     * @return The new handle, or a structured validation/conflict failure.
     */
    [[nodiscard]] eve::Result<DefinitionHandle> insert(const std::string& type, const std::string& id, int version,
                                                       const std::string& json);

    /**
     * @brief Replaces a live definition and invalidates its previous handle.
     * @return The replacement handle, or a structured failure.
     */
    [[nodiscard]] eve::Result<DefinitionHandle> replace(const std::string& type, const std::string& id, int version,
                                                        const std::string& json);

    /**
     * @brief Removes a definition and returns the retained tombstone handle.
     * @return A stale tombstone handle, or a structured failure.
     */
    [[nodiscard]] eve::Result<DefinitionHandle> remove(const std::string& type, const std::string& id);

    /**
     * @brief Resolves a definition through the common registry API.
     * @return A borrowed reference valid until the next registry mutation.
     */
    [[nodiscard]] eve::ResultRef<const Definition> resolve(const std::string& type, const std::string& id) const;

    /** @brief Resolves a generation-qualified handle and reports stale identities. */
    [[nodiscard]] eve::ResultRef<const Definition> resolveHandle(const DefinitionHandle& handle) const;

    /** @brief Returns the current generation-qualified handle through Result. */
    [[nodiscard]] eve::Result<DefinitionHandle> handle(const std::string& type, const std::string& id) const;

    /** @brief Returns the latest generation, including a retained tombstone. */
    [[nodiscard]] eve::Result<eve::Generation> generationOf(const std::string& type, const std::string& id) const;

    /** @brief Returns whether a key is retained as a tombstone. */
    [[nodiscard]] bool isTombstone(const std::string& type, const std::string& id) const noexcept;

    /** @brief Returns whether a generation-qualified handle is stale. */
    [[nodiscard]] bool isStale(const DefinitionHandle& handle) const noexcept;

    /** @brief Creates a reload-safe logical reference containing only type and id. */
    DefinitionRef reference(const std::string& type, const std::string& id) const;
    /** @brief Returns the total number of definitions. */
    int size() const;
    /** @brief Returns the number of definitions registered for a type. */
    int countType(const std::string& type) const;
    /** @brief Returns a definition by globally sorted type/id order, or nullptr. */
    const Definition* at(int index) const;
    /** @brief Returns a definition by id order within one type, or nullptr. */
    const Definition* atType(const std::string& type, int index) const;
    /** @brief Returns the number of retained mutation events. */
    int eventCount() const;
    /** @brief Returns a retained mutation event by sequence order, or nullptr. */
    const DefinitionEvent* eventAt(int index) const;
    /** @brief Clears retained events without resetting their sequence allocator. */
    void clearEvents();
    /** @brief Exports all registry state as deterministic canonical JSON. */
    std::string snapshotJson() const;
    /** @brief Transactionally restores registry state from snapshotJson() output. */
    [[nodiscard]] eve::Result<void> restoreJson(const std::string& json);

    /** @brief Freeze canonical content and exact instance/revision/tick metadata without invoking callbacks.
     * @return Detached native facts or canonical payload failure, without changing registry or event projections.
     * @ownership Subscriptions and callbacks are excluded; the result survives registry destruction.
     * @thread Content owner thread outside mutation/dispatch. No locks or policy callbacks.
     * @cost Copies/encodes registry values, generations, tombstones and history once per explicit checkpoint.
     * @throws std::bad_alloc Registry remains unchanged.
     */
    [[nodiscard]] Result<DefinitionRegistryCapture> capture() const;
    /** @brief Construct an independent registry using the canonical restore kernel and captured envelope metadata.
     * @param captured Native facts borrowed only during this call; moved captures reject.
     * @return New registry with exact content/counters/identity, or failure without changing inputs/live registries.
     * @ownership Fresh observer scope; source subscriptions/callbacks are never copied or invoked.
     * @thread Content owner thread; no external callbacks or capability publication.
     * @cost Registry-sized canonical validation and owning storage rebuild per isolated restore attempt.
     * @throws std::bad_alloc Local candidate discarded; the same capture remains retryable.
     */
    [[nodiscard]] static Result<std::unique_ptr<DefinitionRegistry>> prepareRestored(
        const DefinitionRegistryCapture& captured);

    /**
     * @brief Captures this registry in the common versioned snapshot envelope.
     * @param hashProvider Explicit content-digest provider; it is never defaulted silently.
     * @return A sealed snapshot, or a structured serialization/hash failure.
     */
    [[nodiscard]] eve::Result<eve::SnapshotEnvelope> snapshot(const eve::SnapshotHashProvider& hashProvider) const;

    /**
     * @brief Restores a verified or migratable snapshot transactionally.
     * @param snapshot Source envelope. Its schema must be `definitions:registry`.
     * @param hashProvider Explicit provider used to verify and reseal the payload.
     * @return Success, or a failure leaving all registry state unchanged.
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
     * @return Success, or a failure leaving all registry state unchanged.
     */
    [[nodiscard]] eve::Result<void> restoreSnapshotJson(std::string_view                 json,
                                                        const eve::SnapshotHashProvider& hashProvider);

    /**
     * @brief Subscribes to successful definition register, replace and remove operations.
     * @param callback Synchronously invoked after the registry state and event
     *        sequence have been updated. The callback receives a borrowed
     *        immutable event valid only for the callback call.
     * @return Move-only token that cancels this registration on destruction.
     * @warning The registry and token are thread-affine. Subscribe, dispose,
     *          and registry mutations must run on the registry owner thread.
     * @warning Callback dispatch is reentrant: a callback may add or remove
     *          subscriptions and may perform another registry operation. No
     *          registry lock is held while the callback runs.
     * @remarks A successful restoreJson() is a bulk replacement and does not
     *          synthesize per-definition events; callers must re-query after
     *          restore because previously borrowed Definition references expire.
     */
    [[nodiscard("retain Subscription or explicitly dispose it")]] eve::Subscription subscribe(ChangeCallback callback);

private:
    using Key = std::pair<std::string, std::string>;
    struct EventData {
        eve::SchemaVersion version;
    };
    using Storage = eve::VersionedRegistry<Key, Definition, EventData>;

    friend class DefinitionRegistryCapture;
    /** @brief Canonical CPU snapshot reader shared by live restore and detached admission.
     * @cost Validates and copies all registry entries/history once per explicit load. */
    [[nodiscard]] static eve::Result<Storage::State> prepareSnapshotState(const std::string& json);
    /** @brief Canonical CPU snapshot writer shared by live inspection and detached sealing.
     * @cost Copies complete registry entries/history per explicit snapshot. */
    [[nodiscard]] static eve::Value encodeSnapshotState(const Storage::State& state);
    static DefinitionEvent  projectEvent(const Storage::Event& event);
    static DefinitionHandle projectHandle(const Storage::Handle& handle);
    static Storage::Handle  storageHandle(const DefinitionHandle& handle);
    void                    clearEventProjection() const;

    Storage                              storage_;
    eve::PersistentId                    instanceId_;
    eve::Revision                        revision_;
    eve::SimulationTick                  tick_;
    mutable std::vector<DefinitionEvent> eventProjection_;
};

/** @brief Script module factory for independent definition registries. */
class Definitions : public Module {
public:
    Module_REG(Definitions);
    /** @brief Constructs a Definitions. */
    Definitions()           = default;
    /** @brief Releases Definitions resources. */
    ~Definitions() override = default;

    /** @brief Allocates a module-owned definition registry. */
    DefinitionRegistry* newRegistry();

private:
    std::vector<std::unique_ptr<DefinitionRegistry>> registries_;
};

}  // namespace eve::definitions
