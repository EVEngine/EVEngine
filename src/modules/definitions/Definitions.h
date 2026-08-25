#pragma once

#include "common/Module.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace eve::definitions {

/** @brief Stable logical reference to a definition, resolved against its current generation. */
struct DefinitionRef {
    std::string type;
    std::string id;
};

/** @brief Generation-qualified handle used to detect replacement or removal of a definition. */
struct DefinitionHandle {
    std::string type;
    std::string id;
    uint64_t    generation = 0;
};

/** @brief One immutable view of a registered versioned JSON definition. */
struct Definition {
    std::string type;
    std::string id;
    int         version    = 1;
    uint64_t    generation = 0;
    std::string json       = "null";
};

/** @brief Registry mutation notification in deterministic sequence order. */
struct DefinitionEvent {
    uint64_t    sequence = 0;
    std::string name;
    std::string type;
    std::string id;
    int         version    = 0;
    uint64_t    generation = 0;
};

/**
 * @brief Deterministic registry of topic-neutral, versioned JSON definitions.
 *
 * Returned Definition pointers remain valid until that key is replaced or
 * removed, or until restoreJson() replaces registry state.
 */
class DefinitionRegistry {
public:
    /** @brief Registers a new type/id pair after canonicalizing its JSON value. */
    bool registerDefinition(const std::string& type, const std::string& id, int version, const std::string& json);
    /** @brief Replaces an existing type/id pair and advances its generation. */
    bool replaceDefinition(const std::string& type, const std::string& id, int version, const std::string& json);
    /** @brief Removes an existing definition, invalidating generation-qualified handles. */
    bool remove(const std::string& type, const std::string& id);
    /** @brief Resolves the current definition for a logical type/id reference. */
    const Definition* resolve(const std::string& type, const std::string& id) const;
    /** @brief Resolves a generation-qualified handle, or nullptr if it became stale. */
    const Definition* resolveHandle(const DefinitionHandle& handle) const;
    /** @brief Resolves type/id only when its current generation matches the supplied value. */
    const Definition* resolveGeneration(const std::string& type, const std::string& id, uint64_t generation) const;
    /** @brief Resolves a logical reference against the current registry generation. */
    const Definition* resolveRef(const DefinitionRef& reference) const;
    /** @brief Creates a generation-qualified handle for the current definition. */
    DefinitionHandle handle(const std::string& type, const std::string& id) const;
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
    bool restoreJson(const std::string& json);
    /** @brief Returns the most recent validation or restore error. */
    const std::string& lastError() const;

private:
    using Key = std::pair<std::string, std::string>;

    bool mutate(bool replace, const std::string& type, const std::string& id, int version, const std::string& json);
    void rebuildView() const;

    std::map<Key, Definition>              definitions_;
    std::map<Key, uint64_t>                generations_;
    std::vector<DefinitionEvent>           events_;
    uint64_t                               nextEventSequence_ = 1;
    mutable std::vector<const Definition*> view_;
    mutable std::string                    viewType_;
    std::string                            lastError_;
};

/** @brief Script module factory for independent definition registries. */
class Definitions : public Module {
public:
    Module_REG(Definitions);
    Definitions()           = default;
    ~Definitions() override = default;

    /** @brief Allocates a module-owned definition registry. */
    DefinitionRegistry* newRegistry();

private:
    std::vector<std::unique_ptr<DefinitionRegistry>> registries_;
};

}  // namespace eve::definitions
