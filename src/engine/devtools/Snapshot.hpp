#pragma once

#include "common/Export.h"
#include "common/Result.h"
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
 * explicitly persistent roots (`markRoot`) or a compatibility heuristic that
 * skips engine module bindings. Roots marked transient are excluded. Native
 * providers choose Preserve or Reset through IStateProvider::reloadPolicy().
 * Closures / native userdata are **skipped, not fatal**: the reloaded script recreates
 * them, so a value the capture cannot represent is left out and named in the `skipped`
 * array inside the captured state. Failing the whole capture instead meant that moving an
 * entity from a global into a container — the entity list the script ECS encourages —
 * turned a working reload into a refused one.
 *
 * Which omissions are named: everything unserializable **inside** a captured root (that
 * root was accepted as state, so e.g. `gameState.tick = function(){}` or
 * `gameState.leader = Hero()` is a real loss), and live objects at the root
 * (`hero <- Hero()`). Function and class *definitions* at the root are ordinary script
 * bindings, so they stay silently skipped rather than flooding the list.
 *
 * Script tables (string keys) and arrays preserve identity, including aliases
 * across roots, self references and mutual cycles. Non-string table keys are rejected.
 * Capture rejects more than 65536 containers or first-visit nesting deeper than 32
 * edges; repeated references close immediately without further recursion.
 * Failed capture leaves the output and VM unchanged. Restore validates the entire
 * graph, allocates all containers, then fills their edges before committing roots.
 * Native providers are rolled back on failure. Providers must accept their own
 * captured state for rollback; rollback failures are reported explicitly.
 *
 * @thread Owner thread during a paused reload; borrowed VM handles remain valid
 *         for the synchronous call. Snapshot values independently own their data.
 *
 * Format (v3): JSON object
 * `{ "version":3, "roots": { name: scalar_or_ref, ... },
 *    "objects": [{ "type":"table", "value": { key: scalar_or_ref, ... } },
 *                { "type":"array", "value": [scalar_or_ref, ...] }, ...],
 *    "native": { "<stateKind>": <captured>, ... },
 *    "skipped": ["squad[0] (class instance)", ...] }`.
 * A ref is exactly `{ "$ref": index }`, with a zero-based index into objects.
 * User containers are always nodes, so user keys cannot collide with ref metadata.
 * `skipped` is only emitted when non-empty. Legacy v1/v2 tree files remain readable;
 * unknown versions and malformed/dangling references fail before live state changes.
 */
class EVENGINE_API_FOUNDATION Snapshot {
public:
    static Snapshot& instance();

    Snapshot(const Snapshot&)            = delete;
    Snapshot& operator=(const Snapshot&) = delete;

    /** @brief Mark a root as persistent, replacing a transient declaration. */
    void markRoot(std::string name);
    /** @brief Remove an explicit persistent-root declaration. */
    void unmarkRoot(const std::string& name);
    /** @brief Mark a root for reconstruction from new script definitions. */
    void markTransientRoot(std::string name);
    /** @brief Remove an explicit transient-root declaration. */
    void unmarkTransientRoot(const std::string& name);
    /** @brief Clear every explicit persistent and transient declaration. */
    void clearRoots();
    /** @brief Return the explicit persistent roots. */
    std::vector<std::string> roots() const;
    /** @brief Return the explicit transient roots. */
    std::vector<std::string> transientRoots() const;

    /** @brief Atomically replace explicit persistent/transient root policy. */
    void setRootPolicies(std::vector<std::string> persistent, std::vector<std::string> transient);

    /** @brief Capture marked roots, or heuristic roots when none marked. */
    std::string capture(HSQUIRRELVM vm, std::string* error = nullptr) const;
    bool        restore(HSQUIRRELVM vm, const std::string& json, std::string* error = nullptr) const;

    bool saveFile(HSQUIRRELVM vm, const std::string& path, std::string* error = nullptr) const;
    bool loadFile(HSQUIRRELVM vm, const std::string& path, std::string* error = nullptr) const;

    /** @brief Marked roots, or heuristic roots when none marked (script-facing). */
    std::vector<std::string> rootsFor(HSQUIRRELVM vm) const { return resolveRoots(vm); }

    /**
     * @brief Capture script roots plus every Preserve IStateProvider.
     * @param out Receives the owning v3 object graph described above, plus
     *            `"skipped"` (array of `path (type)` strings) when values had to be left
     *            out. Keeping the omissions inside the captured state means no extra
     *            out-parameter and one channel for whoever reads the snapshot.
     * @return false on a missing VM, unsupported keys, exceeded capture limits or
     *         provider failure. Unserializable live values are skipped as documented above.
     */
    bool captureState(HSQUIRRELVM vm, StateValue& out, std::string* error = nullptr) const;

    /**
     * @brief Merge v3 script defaults while retaining captured values and identities.
     * @param captured Borrowed owning snapshot; unchanged by this operation.
     * @param defaults Borrowed v3 snapshot of freshly defined state; unchanged.
     * @return An independent owning snapshot, or a diagnostic for invalid graphs/limits.
     * @note Missing table fields are imported with their aliases/cycles; array items
     *       and existing roots retain captured values. Native state stays captured.
     * @thread Owner thread during a paused reload; no VM access or script callbacks.
     */
    [[nodiscard]] Result<StateValue> mergeDefaultsState(const StateValue& captured, const StateValue& defaults) const;

    /**
     * @brief Restore a captured state; v1-shaped values restore roots only.
     * @return false on invalid input or provider failure. Script roots are unchanged
     *         on failure; native providers are restored from pre-operation backups.
     */
    bool restoreState(HSQUIRRELVM vm, const StateValue& state, std::string* error = nullptr) const;

    /** @brief Built-in names never auto-captured (modules / boot bindings). */
    static bool isEngineBinding(const std::string& name);

private:
    Snapshot() = default;

    std::vector<std::string> resolveRoots(HSQUIRRELVM vm) const;

    mutable std::vector<std::string> marked_;
    mutable std::vector<std::string> transient_;
};

}  // namespace eve::dev
