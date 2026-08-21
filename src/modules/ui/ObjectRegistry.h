#pragma once

#include "common/Export.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace eve::ui {

/** @brief One registered live script instance. */
struct EVENGINE_API ObjectEntry {
    uint64_t id = 0;
    std::string className;
    std::string label;
    ssq::Object object;
};

/**
 * @brief Reflectable script-object registry backing the database panel.
 *
 * Holds rooted references to script class instances grouped by class name.
 * The database panel lists entries per class, creates instances through the
 * active Runtime and edits their reflected properties; scripts register their
 * own live objects through `ui.dbRegister(obj)`.
 */
class EVENGINE_API ObjectRegistry {
public:
    ObjectRegistry(const ObjectRegistry&) = delete;
    ObjectRegistry& operator=(const ObjectRegistry&) = delete;

    static ObjectRegistry& instance();

    /**
     * @brief Creates a class instance through the active Runtime and registers it.
     * @param className Reflected script class name.
     * @return The new entry id, or 0 when the class is unknown / construction failed.
     */
    uint64_t create(const std::string& className);
    /**
     * @brief Registers an existing live script instance.
     * @param className Class name used for grouping (auto-derived when empty).
     * @param object    Live script instance (rooted while registered).
     * @param label     Optional display label (defaults to "Class #n").
     * @return The entry id, or 0 on failure.
     */
    uint64_t registerObject(const std::string& className, const ssq::Object& object,
                            const std::string& label = {});
    /** @brief Removes an entry by id (the object itself stays alive). */
    bool unregister(uint64_t id);
    /** @brief Removes every entry of a class. */
    void clear(const std::string& className);
    /** @brief Removes every entry. */
    void clearAll();

    /** @brief Class names with at least one entry, sorted. */
    std::vector<std::string> classNames() const;
    /** @brief Entries of a class, in registration order. */
    std::vector<ObjectEntry> entries(const std::string& className) const;
    /** @brief Look up one entry by id; nullptr when unknown. */
    const ObjectEntry* entry(uint64_t id) const;
    /** @brief Number of entries of a class. */
    size_t count(const std::string& className) const;

private:
    ObjectRegistry() = default;
    uint64_t nextId_ = 1;
    std::map<std::string, std::vector<ObjectEntry>> byClass_;
};

}  // namespace eve::ui
