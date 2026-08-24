#pragma once

#include "common/Module.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace eve::policyregistry {

/** @brief Implementation category advertised by a policy descriptor. */
enum class ImplementationKind { Builtin, Script, Batch };

/** @brief Generation-qualified policy handle used to detect replacement or removal. */
struct PolicyHandle {
    std::string domain;
    std::string name;
    uint64_t    generation = 0;
};

/** @brief Discoverable policy description without an executable callback. */
struct PolicyDescriptor {
    std::string        domain;
    std::string        name;
    int                version  = 1;
    int                priority = 0;
    bool               enabled  = true;
    ImplementationKind kind     = ImplementationKind::Script;
    std::string        schemaId;
    std::string        metadataJson = "{}";
    uint64_t           generation   = 0;
};

/** @brief Deterministically sequenced policy registry mutation event. */
struct PolicyEvent {
    uint64_t    sequence = 0;
    std::string name;
    std::string domain;
    std::string policyName;
    uint64_t    generation = 0;
    bool        enabled    = false;
};

/** @brief Deterministic registry for discoverable, non-executable policy descriptions. */
class PolicyRegistry {
public:
    /** @brief Registers a new descriptor under its stable domain/name key. */
    bool registerPolicy(const std::string& domain, const std::string& name, int version, int priority, bool enabled,
                        const std::string& kind, const std::string& schemaId, const std::string& metadataJson);
    /** @brief Replaces an existing descriptor and advances its generation. */
    bool replacePolicy(const std::string& domain, const std::string& name, int version, int priority, bool enabled,
                       const std::string& kind, const std::string& schemaId, const std::string& metadataJson);
    /** @brief Removes a descriptor while retaining a generation tombstone. */
    bool remove(const std::string& domain, const std::string& name);
    /** @brief Changes enabled state and advances generation when the value changes. */
    bool enable(const std::string& domain, const std::string& name, bool enabled);
    /** @brief Resolves the current descriptor for a stable domain/name key. */
    const PolicyDescriptor* resolve(const std::string& domain, const std::string& name) const;
    /** @brief Resolves a handle only while its generation remains current. */
    const PolicyDescriptor* resolveHandle(const PolicyHandle& handle) const;
    /** @brief Resolves a descriptor only when its generation matches. */
    const PolicyDescriptor* resolveGeneration(const std::string& domain, const std::string& name,
                                              uint64_t generation) const;
    /** @brief Creates a generation-qualified handle, with generation zero when absent. */
    PolicyHandle handle(const std::string& domain, const std::string& name) const;
    /** @brief Selects the enabled highest-priority policy, breaking ties by lexical name. */
    const PolicyDescriptor* select(const std::string& domain) const;
    /** @brief Returns total descriptor count. */
    int size() const;
    /** @brief Returns descriptor count in a domain. */
    int countDomain(const std::string& domain) const;
    /** @brief Returns a descriptor in globally sorted domain/name order. */
    const PolicyDescriptor* at(int index) const;
    /** @brief Returns a descriptor in lexical name order within a domain. */
    const PolicyDescriptor* atDomain(const std::string& domain, int index) const;
    /** @brief Returns retained event count. */
    int eventCount() const;
    /** @brief Returns a retained event by sequence order. */
    const PolicyEvent* eventAt(int index) const;
    /** @brief Clears events without resetting the sequence allocator. */
    void clearEvents();
    /** @brief Exports registry state as deterministic JSON. */
    std::string snapshotJson() const;
    /** @brief Transactionally restores state from a snapshot. */
    bool restoreJson(const std::string& json);
    /** @brief Returns the latest validation or restore error. */
    const std::string& lastError() const;

private:
    using Key = std::pair<std::string, std::string>;
    bool mutate(bool replace, const std::string& domain, const std::string& name, int version, int priority,
                bool enabled, const std::string& kind, const std::string& schemaId, const std::string& metadataJson);
    void emit(const std::string& eventName, const PolicyDescriptor& descriptor);

    std::map<Key, PolicyDescriptor> descriptors_;
    std::map<Key, uint64_t>         generations_;
    std::vector<PolicyEvent>        events_;
    uint64_t                        nextEventSequence_ = 1;
    std::string                     lastError_;
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
