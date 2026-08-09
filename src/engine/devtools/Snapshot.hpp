#pragma once

#include "common/Export.h"

#include <string>
#include <unordered_set>
#include <vector>

struct SQVM;
typedef struct SQVM* HSQUIRRELVM;

namespace eve::dev {

/**
 * Script-state snapshot (engine is treated as stateless).
 *
 * Captures serializable Squirrel values from the root table — either
 * explicitly marked roots (`markRoot`) or a heuristic that skips engine
 * module bindings. Closures / native userdata are skipped.
 *
 * Format: JSON object `{ "version":1, "roots": { name: value, ... } }`
 */
class EVENGINE_API Snapshot {
public:
    static Snapshot& instance();

    Snapshot(const Snapshot&)            = delete;
    Snapshot& operator=(const Snapshot&) = delete;

    void markRoot(std::string name);
    void unmarkRoot(const std::string& name);
    void clearRoots();
    std::vector<std::string> roots() const;

    /** Capture marked roots, or heuristic roots when none marked. */
    std::string capture(HSQUIRRELVM vm, std::string* error = nullptr) const;
    bool        restore(HSQUIRRELVM vm, const std::string& json, std::string* error = nullptr) const;

    bool saveFile(HSQUIRRELVM vm, const std::string& path, std::string* error = nullptr) const;
    bool loadFile(HSQUIRRELVM vm, const std::string& path, std::string* error = nullptr) const;

    /** Built-in names never auto-captured (modules / boot bindings). */
    static bool isEngineBinding(const std::string& name);

private:
    Snapshot() = default;

    std::vector<std::string> resolveRoots(HSQUIRRELVM vm) const;

    mutable std::vector<std::string> marked_;
};

}  // namespace eve::dev
