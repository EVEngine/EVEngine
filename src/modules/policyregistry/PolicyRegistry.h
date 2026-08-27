#pragma once

#include "common/Module.h"
#include "common/Snapshot.h"
#include "common/Subscription.h"
#include "common/VersionedRegistry.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace eve::policyregistry {

/** @brief Implementation category advertised by a policy descriptor. */
enum class ImplementationKind { Builtin, Script, Batch };

/** @brief Generation-qualified policy handle used to detect replacement or removal. */
struct PolicyHandle {
    std::string    domain;
    std::string    name;
    eve::Generation generation;
};

/** @brief Discoverable policy description without an executable callback. */
struct PolicyDescriptor {
    std::string        domain;
    std::string        name;
    eve::SchemaVersion version{1};
    int                priority = 0;
    bool               enabled  = true;
    ImplementationKind kind     = ImplementationKind::Script;
    std::string        schemaId;
    std::string        metadataJson = "{}";
    // Compatibility projection; the VersionedRegistry slot owns generation.
    eve::Generation    generation;
};

/** @brief Deterministically sequenced policy registry mutation event. */
struct PolicyEvent {
    eve::EventSequence sequence;
    std::string         name;
    std::string         domain;
    std::string         policyName;
    eve::SchemaVersion  version;
    eve::Generation     generation;
    bool                enabled = false;
};

/** @brief Deterministic registry for discoverable, non-executable policy descriptions. */
class PolicyRegistry {
public:
    /**
     * @brief Creates an independent policy registry.
     * @param instanceId Identity carried by common snapshot envelopes.
     */
    explicit PolicyRegistry(eve::PersistentId instanceId = {});

    /** @brief Inserts a descriptor through the common checked registry. */
    [[nodiscard]] eve::Result<PolicyHandle> insert(
        const std::string& domain, const std::string& name, int version, int priority, bool enabled,
        const std::string& kind, const std::string& schemaId, const std::string& metadataJson);
    /** @brief Replaces a descriptor and invalidates the prior generation handle. */
    [[nodiscard]] eve::Result<PolicyHandle> replace(
        const std::string& domain, const std::string& name, int version, int priority, bool enabled,
        const std::string& kind, const std::string& schemaId, const std::string& metadataJson);
    /** @brief Removes a descriptor and returns its retained tombstone handle. */
    [[nodiscard]] eve::Result<PolicyHandle> remove(
        const std::string& domain, const std::string& name);
    /** @brief Changes enabled state through the same replacement generation path. */
    [[nodiscard]] eve::Result<PolicyHandle> enable(
        const std::string& domain, const std::string& name, bool enabled);
    /** @brief Resolves a descriptor through the common checked-registry API. */
    [[nodiscard]] eve::ResultRef<const PolicyDescriptor> resolve(
        const std::string& domain, const std::string& name) const;
    /** @brief Resolves a generation-qualified policy handle through Result. */
    [[nodiscard]] eve::ResultRef<const PolicyDescriptor> resolveHandle(
        const PolicyHandle& handle) const;
    /** @brief Returns the current generation-qualified handle through Result. */
    [[nodiscard]] eve::Result<PolicyHandle> handle(
        const std::string& domain, const std::string& name) const;
    /** @brief Returns the latest generation, including a retained tombstone. */
    [[nodiscard]] eve::Result<eve::Generation> generationOf(
        const std::string& domain, const std::string& name) const;
    /** @brief Returns whether a key is retained as a removal tombstone. */
    [[nodiscard]] bool isTombstone(const std::string& domain, const std::string& name) const noexcept;
    /** @brief Returns whether a generation-qualified policy handle is stale. */
    [[nodiscard]] bool isStale(const PolicyHandle& handle) const noexcept;

    /** @brief Selects the enabled highest-priority policy, breaking ties by lexical name. */
    const PolicyDescriptor* select(const std::string& domain) const;
    /** @brief Returns total live descriptor count. */
    int size() const;
    /** @brief Returns live descriptor count in a domain. */
    int countDomain(const std::string& domain) const;
    /** @brief Returns a live descriptor in globally sorted domain/name order. */
    const PolicyDescriptor* at(int index) const;
    /** @brief Returns a live descriptor in lexical name order within a domain. */
    const PolicyDescriptor* atDomain(const std::string& domain, int index) const;
    /** @brief Returns retained event count. */
    int eventCount() const;
    /** @brief Returns a retained event by sequence order. */
    const PolicyEvent* eventAt(int index) const;
    /** @brief Clears events without resetting the sequence allocator. */
    void clearEvents();
    /** @brief Exports registry state as deterministic JSON. */
    std::string snapshotJson() const;
    /** @brief Transactionally restores state from the registry snapshot. */
    [[nodiscard]] eve::Result<void> restoreJson(const std::string& json);

    /** @brief Captures this registry in the common snapshot envelope. */
    [[nodiscard]] eve::Result<eve::SnapshotEnvelope> snapshot(
        const eve::SnapshotHashProvider& hashProvider) const;
    /** @brief Restores a verified snapshot transactionally without per-item events. */
    [[nodiscard]] eve::Result<void> restoreSnapshot(
        const eve::SnapshotEnvelope& snapshot, const eve::SnapshotHashProvider& hashProvider);
    /** @brief Serializes the common snapshot envelope as canonical JSON. */
    [[nodiscard]] eve::Result<std::string> snapshotEnvelopeJson(
        const eve::SnapshotHashProvider& hashProvider) const;
    /** @brief Parses and transactionally restores a common snapshot envelope. */
    [[nodiscard]] eve::Result<void> restoreSnapshotJson(
        std::string_view json, const eve::SnapshotHashProvider& hashProvider);

    /**
     * @brief Subscribes to successful registry mutations.
     * @return Owner-thread-affine RAII cancellation token.
     */
    [[nodiscard("retain Subscription or explicitly dispose it")]] eve::Subscription subscribe(
        std::function<void(const PolicyEvent&)> callback);

private:
    using Key = std::pair<std::string, std::string>;
    struct EventData {
        eve::SchemaVersion version;
        bool               enabled = false;
    };
    using Storage = eve::VersionedRegistry<Key, PolicyDescriptor, EventData>;

    static PolicyEvent projectEvent(const Storage::Event& event);
    static PolicyHandle projectHandle(const Storage::Handle& handle);
    static Storage::Handle storageHandle(const PolicyHandle& handle);
    void clearEventProjection() const;

    Storage                          storage_;
    eve::PersistentId                instanceId_;
    eve::Revision                    revision_;
    eve::SimulationTick              tick_;
    mutable std::vector<PolicyEvent> eventProjection_;
};

/** @brief Returns the stable lowercase implementation kind name. */
std::string implementationKindName(ImplementationKind kind);
/** @brief Parses a stable lowercase implementation kind name. */
bool parseImplementationKind(const std::string& name, ImplementationKind& kind);

/** @brief Script module factory for independent policy registries. */
class PolicyRegistryModule : public Module {
public:
    Module_REG(PolicyRegistryModule);
    PolicyRegistryModule()           = default;
    ~PolicyRegistryModule() override = default;

    /** @brief Allocates a module-owned policy registry. */
    PolicyRegistry* newRegistry();

private:
    std::vector<std::unique_ptr<PolicyRegistry>> registries_;
};

}  // namespace eve::policyregistry
