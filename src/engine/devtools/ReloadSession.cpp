#include "devtools/ReloadSession.h"
#include "devtools/McpFrameCapture.hpp"

#include "devtools/Snapshot.hpp"

#include <cstdio>

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
    // Values the capture could not represent (entity instances, closures, ...) are left
    // out, not fatal: refusing the session here used to make "keep a list of entities in a
    // global" block hot reload entirely. Say so once; the same list stays in the captured
    // state and in eve_snapshot.json for a tool to read.
    if (const StateValue* skipped = buffer_.find("skipped");
        skipped && skipped->isArray() && skipped->arraySize() > 0) {
        std::string names;
        for (std::size_t i = 0; i < skipped->arraySize() && i < 3; ++i) {
            if (!names.empty()) names += ", ";
            names += skipped->at(i).asString();
        }
        if (skipped->arraySize() > 3) names += ", ...";
        std::fprintf(stderr, "EVEngine: reload state skips %zu unserializable value(s): %s\n", skipped->arraySize(),
                     names.c_str());
    }
    // Best-effort manual rollback point (same file as the F6/F7 snapshot).
    std::string saveErr;
    Snapshot::instance().saveFile(vm, "eve_snapshot.json", &saveErr);
    resetMcpFrameCapture();
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
        auto result = Snapshot::instance().mergeDefaultsState(buffer_, current);
        if (!result.ok()) {
            if (err) *err = result.diagnostics().front().message();
            return false;
        }
        merged = std::move(result).takeValue();
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
