#pragma once

#include "common/Export.h"
#include "common/StateValue.h"

#include <string>
#include <unordered_set>
#include <vector>

struct SQVM;
typedef struct SQVM* HSQUIRRELVM;

namespace eve::dev {

/**
 * @brief Script + native state snapshot for state hot reload.
 *
 * Captures serializable Squirrel values from the root table — either
 * explicitly marked roots (`markRoot`) or a heuristic that skips engine
 * module bindings — plus the state of every registered
 * eve::caps::IStateProvider. Closures / native userdata are skipped.
 *
 * Format (v2): JSON object
 * `{ "version":2, "roots": { name: value, ... },
 *    "native": { "<stateKind>": <captured>, ... } }`
 * v1 files (roots only) are still readable.
 */
class EVENGINE_API Snapshot {
public:
    static Snapshot& instance();

    Snapshot(const Snapshot&)            = delete;
    Snapshot& operator=(const Snapshot&) = delete;

    void                     markRoot(std::string name);
    void                     unmarkRoot(const std::string& name);
    void                     clearRoots();
    std::vector<std::string> roots() const;

    /** @brief Capture marked roots, or heuristic roots when none marked. */
    std::string capture(HSQUIRRELVM vm, std::string* error = nullptr) const;
    bool        restore(HSQUIRRELVM vm, const std::string& json, std::string* error = nullptr) const;

    bool saveFile(HSQUIRRELVM vm, const std::string& path, std::string* error = nullptr) const;
    bool loadFile(HSQUIRRELVM vm, const std::string& path, std::string* error = nullptr) const;

    /** @brief Marked roots, or heuristic roots when none marked (script-facing). */
    std::vector<std::string> rootsFor(HSQUIRRELVM vm) const { return resolveRoots(vm); }

    /**
     * @brief Capture script roots plus every registered IStateProvider.
     * @param out Receives `{ "version":2, "roots": {...}, "native": {...} }`.
     * @return false when the VM is null; provider failures are skipped.
     */
    bool captureState(HSQUIRRELVM vm, StateValue& out, std::string* error = nullptr) const;

    /**
     * @brief Restore a captured state; v1-shaped values restore roots only.
     * @return false on invalid input or when a provider's restore fails (its
     *         resetToDefaults() is attempted as a fallback).
     */
    bool restoreState(HSQUIRRELVM vm, const StateValue& state, std::string* error = nullptr) const;

    /** @brief Built-in names never auto-captured (modules / boot bindings). */
    static bool isEngineBinding(const std::string& name);

private:
    Snapshot() = default;

    std::vector<std::string> resolveRoots(HSQUIRRELVM vm) const;

    mutable std::vector<std::string> marked_;
};

}  // namespace eve::dev
