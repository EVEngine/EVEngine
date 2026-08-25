#include "devtools/ReloadSession.h"

#include "devtools/Snapshot.hpp"

namespace eve::dev {

ReloadSession& ReloadSession::instance() {
    static ReloadSession inst;
    return inst;
}

bool ReloadSession::begin(HSQUIRRELVM vm, std::string* err) {
    if (active_) {
        if (err) *err = "reload session already active";
        return false;
    }
    persistentRoots_ = Snapshot::instance().roots();
    transientRoots_  = Snapshot::instance().transientRoots();
    if (!Snapshot::instance().captureState(vm, buffer_, err)) {
        buffer_ = StateValue::null();
        persistentRoots_.clear();
        transientRoots_.clear();
        return false;
    }
    // Best-effort manual rollback point (same file as the F6/F7 snapshot).
    std::string saveErr;
    Snapshot::instance().saveFile(vm, "eve_snapshot.json", &saveErr);
    active_ = true;
    return true;
}

bool ReloadSession::commit(HSQUIRRELVM vm, std::string* err) {
    if (!active_) {
        if (err) *err = "no active reload session";
        return false;
    }
    StateValue merged = buffer_;

    // Merge freshly-defined roots under the captured values: captured values
    // win, fields the reloaded script added are kept.
    StateValue  current;
    std::string captureErr;
    if (Snapshot::instance().captureState(vm, current, &captureErr)) {
        StateValue*       mergedRoots  = merged.find("roots");
        const StateValue* currentRoots = current.find("roots");
        if (mergedRoots && mergedRoots->isObject() && currentRoots && currentRoots->isObject()) {
            for (const auto& name : mergedRoots->keys()) {
                StateValue*       oldRoot = mergedRoots->find(name);
                const StateValue* newRoot = currentRoots->find(name);
                if (oldRoot && oldRoot->isObject() && newRoot && newRoot->isObject()) {
                    oldRoot->mergeDefaults(*newRoot);
                }
            }
        }
    }

    const bool ok = restore(vm, merged, err);
    // Keep the original buffer and the session open when restoration fails.
    // The script orchestrator can then restore old root bindings and call
    // abort(), instead of being stranded in a partially restored state.
    if (ok) {
        buffer_ = StateValue::null();
        persistentRoots_.clear();
        transientRoots_.clear();
        active_ = false;
    }
    return ok;
}

bool ReloadSession::abort(HSQUIRRELVM vm, std::string* err) {
    if (!active_) {
        if (err) *err = "no active reload session";
        return false;
    }
    Snapshot::instance().setRootPolicies(persistentRoots_, transientRoots_);
    const bool ok = restore(vm, buffer_, err);
    buffer_ = StateValue::null();
    persistentRoots_.clear();
    transientRoots_.clear();
    active_ = false;
    return ok;
}

bool ReloadSession::restore(HSQUIRRELVM vm, const StateValue& state, std::string* err) {
    return Snapshot::instance().restoreState(vm, state, err);
}

}  // namespace eve::dev
