#pragma once

#include "common/Export.h"
#include "common/StateValue.h"

#include <string>
#include <vector>

struct SQVM;
typedef struct SQVM* HSQUIRRELVM;

namespace eve::dev {

/**
 * @brief One state hot-reload transaction: capture → reload → restore.
 *
 * A script reload (`soft_reload_scripts`) drives this via the `eve.dev` API:
 *
 * 1. begin(): snapshot script state roots + every registered IStateProvider
 *    into an in-memory buffer, and persist `eve_snapshot.json` as a manual
 *    rollback point.
 * 2. (scripts are re-dofile'd in between)
 * 3. commit(): restore the captured state on top of the new definitions —
 *    captured root values win, fields the new script added are kept, native
 *    providers are restored (falling back to resetToDefaults() on failure).
 * 4. abort(): restore the captured state unchanged and clear the session.
 */
class EVENGINE_API ReloadSession {
public:
    static ReloadSession& instance();

    ReloadSession(const ReloadSession&)            = delete;
    ReloadSession& operator=(const ReloadSession&) = delete;

    /**
     * @brief Start a session: capture current script + native state.
     * @return false when capture fails (session stays inactive).
     */
    bool begin(HSQUIRRELVM vm, std::string* err = nullptr);

    /**
     * @brief Restore captured state over the new definitions.
     *
     * On success the session ends. On failure the original capture remains
     * active so the caller can repair definition bindings and call abort().
     */
    bool commit(HSQUIRRELVM vm, std::string* err = nullptr);

    /** @brief End the session, rolling back to the captured state. */
    bool abort(HSQUIRRELVM vm, std::string* err = nullptr);

    /** @brief Whether a session is currently open. */
    bool active() const { return active_; }

private:
    ReloadSession() = default;

    bool restore(HSQUIRRELVM vm, const StateValue& state, std::string* err);

    StateValue buffer_;
    std::vector<std::string> persistentRoots_;
    std::vector<std::string> transientRoots_;
    bool                     active_ = false;
};

}  // namespace eve::dev
