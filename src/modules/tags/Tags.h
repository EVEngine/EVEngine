#pragma once

#include "common/Module.h"
#include "tags/TagStore.h"
#include "tags/GameplayTag.h"

#include <string>

namespace eve::tags {

/** @brief Script-facing module for generic tags and capabilities. */
class Tags : public Module {
public:
    Module_REG(Tags);
    Tags()           = default;
    ~Tags() override = default;

    /** @brief Adds a tag to a subject. */
    bool add(const std::string& subject, const std::string& tag);
    /** @brief Removes a tag from a subject. */
    bool remove(const std::string& subject, const std::string& tag);
    /** @brief Tests whether a subject has a tag. */
    bool has(const std::string& subject, const std::string& tag) const;
    /** @brief Gets a subject's tag count. */
    int count(const std::string& subject) const;
    /** @brief Gets a subject's tag by deterministic index, or an empty string. */
    std::string at(const std::string& subject, int index) const;
    /** @brief Gets the number of subjects carrying a tag. */
    int subjectCount(const std::string& tag) const;
    /** @brief Gets a subject carrying a tag by deterministic index, or an empty string. */
    std::string subjectAt(const std::string& tag, int index) const;

    /** @brief Adds a capability to a subject. */
    bool addCapability(const std::string& subject, const std::string& capability);
    /** @brief Removes a capability from a subject. */
    bool removeCapability(const std::string& subject, const std::string& capability);
    /** @brief Tests whether a subject has a capability. */
    bool hasCapability(const std::string& subject, const std::string& capability) const;
    /** @brief Gets a subject's capability count. */
    int capabilityCount(const std::string& subject) const;
    /** @brief Gets a capability by deterministic index, or an empty string. */
    std::string capabilityAt(const std::string& subject, int index) const;

    /** @brief Removes every tag and capability belonging to a subject. */
    bool removeSubject(const std::string& subject);
    /** @brief Clears all state and resets event sequencing. */
    void clear();
    /** @brief Gets queued change event count. */
    int eventCount() const;
    /** @brief Gets event sequence by index, or zero. */
    int eventSequence(int index) const;
    /** @brief Gets event action by index, or an empty string. */
    std::string eventAction(int index) const;
    /** @brief Gets event subject by index, or an empty string. */
    std::string eventSubject(int index) const;
    /** @brief Gets event tag or capability by index, or an empty string. */
    std::string eventValue(int index) const;
    /** @brief Clears queued events without changing state. */
    void clearEvents();
    /** @brief Gives C++ callers direct access to the underlying store. */
    TagStore& store() { return store_; }
    /**
     * @brief Gives C++ callers owner-thread access to the canonical gameplay-tag registry.
     * @return Borrowed mutable reference owned by this module.
     * @lifetime Valid until this Tags module is destroyed; definitions returned by the registry are owning copies.
     * @thread Owner-thread-only; no synchronization is performed.
     * @reentrancy Does not invoke callbacks.
     */
    GameplayTagRegistry& registry() { return registry_; }
    /**
     * @brief Gives C++ callers immutable owner-thread access to the canonical gameplay-tag registry.
     * @return Borrowed immutable reference owned by this module.
     * @lifetime Valid until this Tags module is destroyed.
     * @thread Owner-thread-only; no synchronization is performed.
     * @reentrancy Does not invoke callbacks.
     */
    const GameplayTagRegistry& registry() const { return registry_; }

private:
    TagStore store_;
    GameplayTagRegistry registry_;
};

}  // namespace eve::tags
