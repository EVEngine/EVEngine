#include "devtools/Debugger.hpp"

#include "devtools/RenderVision.hpp"

#include <simplesquirrel/simplesquirrel.hpp>
#include <squirrel.h>

#include <algorithm>
#include <chrono>
#include <thread>
#include <utility>

namespace eve::dev {
namespace {

std::string truthyString(bool v) { return v ? "true" : "false"; }

std::string describeSqValue(HSQUIRRELVM vm, SQInteger idx) {
    const SQObjectType t = sq_gettype(vm, idx);
    switch (t) {
        case OT_NULL:
            return "null";
        case OT_INTEGER: {
            SQInteger v = 0;
            sq_getinteger(vm, idx, &v);
            return std::to_string(static_cast<long long>(v));
        }
        case OT_FLOAT: {
            SQFloat v = 0;
            sq_getfloat(vm, idx, &v);
            return std::to_string(static_cast<double>(v));
        }
        case OT_BOOL: {
            SQBool v = SQFalse;
            sq_getbool(vm, idx, &v);
            return v ? "true" : "false";
        }
        case OT_STRING: {
            const SQChar* s = nullptr;
            sq_getstring(vm, idx, &s);
            return s ? std::string("\"") + s + "\"" : "\"\"";
        }
        case OT_TABLE:
            return "<table>";
        case OT_ARRAY:
            return "<array>";
        case OT_USERDATA:
            return "<userdata>";
        case OT_CLOSURE:
            return "<closure>";
        case OT_NATIVECLOSURE:
            return "<native>";
        case OT_INSTANCE:
            return "<instance>";
        case OT_CLASS:
            return "<class>";
        case OT_THREAD:
            return "<thread>";
        default:
            return "<other>";
    }
}

std::string typeName(HSQUIRRELVM vm, SQInteger idx) {
    switch (sq_gettype(vm, idx)) {
        case OT_NULL:
            return "null";
        case OT_INTEGER:
            return "integer";
        case OT_FLOAT:
            return "float";
        case OT_BOOL:
            return "bool";
        case OT_STRING:
            return "string";
        case OT_TABLE:
            return "table";
        case OT_ARRAY:
            return "array";
        case OT_USERDATA:
            return "userdata";
        case OT_CLOSURE:
            return "closure";
        case OT_NATIVECLOSURE:
            return "native";
        case OT_INSTANCE:
            return "instance";
        case OT_CLASS:
            return "class";
        case OT_THREAD:
            return "thread";
        default:
            return "other";
    }
}

std::string typeName(HSQUIRRELVM vm, SQInteger idx);

/** Extended describe used by the variable tree / evaluate results. */
VariableInfo describeAt(HSQUIRRELVM vm, SQInteger idx) {
    VariableInfo info;
    const SQObjectType t = sq_gettype(vm, idx);
    info.type = typeName(vm, idx);
    switch (t) {
        case OT_NULL:
            info.value = "null";
            break;
        case OT_INTEGER: {
            SQInteger v = 0;
            sq_getinteger(vm, idx, &v);
            info.value = std::to_string(static_cast<long long>(v));
            break;
        }
        case OT_FLOAT: {
            SQFloat v = 0;
            sq_getfloat(vm, idx, &v);
            info.value = std::to_string(static_cast<double>(v));
            break;
        }
        case OT_BOOL: {
            SQBool v = SQFalse;
            sq_getbool(vm, idx, &v);
            info.value = v ? "true" : "false";
            break;
        }
        case OT_STRING: {
            const SQChar* s = nullptr;
            sq_getstring(vm, idx, &s);
            info.value = std::string("\"") + (s ? s : "") + "\"";
            break;
        }
        case OT_TABLE:
        case OT_ARRAY: {
            const SQInteger n = sq_getsize(vm, idx);
            info.value = std::string(t == OT_TABLE ? "<table (" : "<array (") +
                         std::to_string(static_cast<long long>(n)) + ")>";
            info.expandable = true;
            info.childCount = static_cast<int>(n);
            break;
        }
        case OT_CLASS:
            info.value = "<class>";
            info.expandable = true;
            break;
        case OT_INSTANCE:
            info.value = "<instance>";
            info.expandable = true;
            break;
        case OT_USERDATA:
            info.value = "<userdata>";
            break;
        case OT_CLOSURE:
            info.value = "<closure>";
            info.expandable = true;  // expandable: free variables
            break;
        case OT_NATIVECLOSURE:
            info.value = "<native>";
            break;
        case OT_THREAD:
            info.value = "<thread>";
            break;
        case OT_GENERATOR:
            info.value = "<generator>";
            break;
        default:
            info.value = "<other>";
            break;
    }
    return info;
}

/** Enumerate children of the value at `idx` (table/array/class/instance). */
std::vector<VariableInfo> enumerateAt(HSQUIRRELVM vm, SQInteger idx) {
    std::vector<VariableInfo> out;
    const SQObjectType t = sq_gettype(vm, idx);
    const SQInteger top  = sq_gettop(vm);
    const int absIdx     = idx < 0 ? top + static_cast<int>(idx) + 1 : static_cast<int>(idx);

    if (t == OT_ARRAY) {
        const SQInteger size = sq_getsize(vm, idx);
        for (SQInteger i = 0; i < size; ++i) {
            const SQInteger before = sq_gettop(vm);
            sq_pushinteger(vm, i);
            if (SQ_FAILED(sq_get(vm, absIdx))) {
                sq_settop(vm, before);
                continue;
            }
            VariableInfo info = describeAt(vm, -1);
            info.name         = std::to_string(static_cast<long long>(i));
            sq_settop(vm, before);
            out.push_back(std::move(info));
        }
        return out;
    }

    if (t == OT_TABLE || t == OT_CLASS) {
        sq_pushinteger(vm, 0);  // iterator position
        while (SQ_SUCCEEDED(sq_next(vm, absIdx))) {
            VariableInfo info = describeAt(vm, -1);
            switch (sq_gettype(vm, -2)) {
                case OT_STRING: {
                    const SQChar* s = nullptr;
                    sq_getstring(vm, -2, &s);
                    info.name = s ? s : "";
                    break;
                }
                case OT_INTEGER: {
                    SQInteger k = 0;
                    sq_getinteger(vm, -2, &k);
                    info.name = std::to_string(static_cast<long long>(k));
                    break;
                }
                case OT_FLOAT: {
                    SQFloat k = 0;
                    sq_getfloat(vm, -2, &k);
                    info.name = std::to_string(static_cast<double>(k));
                    break;
                }
                case OT_BOOL: {
                    SQBool k = SQFalse;
                    sq_getbool(vm, -2, &k);
                    info.name = truthyString(k != SQFalse);
                    break;
                }
                default:
                    info.name = "<key>";
                    break;
            }
            sq_pop(vm, 2);
            out.push_back(std::move(info));
        }
        sq_settop(vm, top);
        return out;
    }

    if (t == OT_INSTANCE) {
        sq_getclass(vm, absIdx);  // class at top
        const int clsAbs = sq_gettop(vm);
        sq_pushinteger(vm, 0);
        while (SQ_SUCCEEDED(sq_next(vm, clsAbs))) {
            const SQChar* s = nullptr;
            if (sq_gettype(vm, -2) == OT_STRING) sq_getstring(vm, -2, &s);
            const std::string name = s ? s : "";
            const SQInteger before = sq_gettop(vm);
            sq_push(vm, absIdx);  // instance
            sq_pushstring(vm, name.c_str(), -1);
            VariableInfo info;
            if (SQ_SUCCEEDED(sq_get(vm, -2))) {
                info = describeAt(vm, -1);
            } else {
                info.type  = "error";
                info.value = "not found";
            }
            info.name = name;
            sq_settop(vm, before);
            sq_pop(vm, 2);  // drop key + value; keep iterator position
            out.push_back(std::move(info));
        }
        sq_settop(vm, top);
        return out;
    }

    if (t == OT_CLOSURE) {
        SQInteger nparams = 0, nfree = 0;
        if (SQ_SUCCEEDED(sq_getclosureinfo(vm, idx, &nparams, &nfree))) {
            for (SQUnsignedInteger i = 0; i < static_cast<SQUnsignedInteger>(nfree); ++i) {
                const SQChar* n = sq_getfreevariable(vm, absIdx, i);
                VariableInfo info = describeAt(vm, -1);
                info.name = n ? n : ("upvalue_" + std::to_string(static_cast<long long>(i)));
                sq_poptop(vm);
                out.push_back(std::move(info));
            }
        }
        return out;
    }
    return out;
}

bool valueTruthy(const VariableInfo& info) {
    if (info.type == "error") return false;
    if (info.type == "null") return false;
    if (info.type == "bool") return info.value == "true";
    if (info.type == "integer") {
        try {
            return std::stoll(info.value) != 0;
        } catch (...) {
            return true;
        }
    }
    if (info.type == "float") {
        try {
            return std::stod(info.value) != 0.0;
        } catch (...) {
            return true;
        }
    }
    return true;
}

}  // namespace

Debugger& Debugger::instance() {
    static Debugger inst;
    return inst;
}

std::string Debugger::normalizeSource(std::string source) {
    if (source.empty()) return source;
    // Strip common URI prefixes VS Code may send.
    if (source.rfind("file://", 0) == 0) {
        source = source.substr(7);
        // file://localhost/Users/... → /Users/...
        if (source.rfind("localhost/", 0) == 0) source = source.substr(9);
    }
    // Unify separators.
    for (char& c : source) {
        if (c == '\\') c = '/';
    }
    // Drop leading ./
    while (source.size() >= 2 && source[0] == '.' && source[1] == '/') source = source.substr(2);
    return source;
}

std::string Debugger::sourceBasename(const std::string& source) {
    const std::string norm = normalizeSource(source);
    const auto slash       = norm.find_last_of('/');
    return (slash == std::string::npos) ? norm : norm.substr(slash + 1);
}

bool Debugger::sourcesMatch(const std::string& a, const std::string& b) {
    const std::string na = normalizeSource(a);
    const std::string nb = normalizeSource(b);
    if (na.empty() || nb.empty()) return false;
    if (na == nb) return true;
    const std::string ba = sourceBasename(na);
    const std::string bb = sourceBasename(nb);
    if (!ba.empty() && ba == bb) return true;
    // Suffix match: ".../scripts/main.nut" vs "scripts/main.nut"
    const std::string& longer  = na.size() >= nb.size() ? na : nb;
    const std::string& shorter = na.size() >= nb.size() ? nb : na;
    if (longer.size() > shorter.size() &&
        longer.compare(longer.size() - shorter.size(), shorter.size(), shorter) == 0) {
        const auto idx = longer.size() - shorter.size();
        return idx == 0 || longer[idx - 1] == '/';
    }
    return false;
}

void Debugger::attach(HSQUIRRELVM vm) {
    std::lock_guard<std::mutex> lock(mu_);
    vm_   = vm;
    mode_ = RunMode::Running;
    reason_ = PauseReason::None;
    pauseLoc_ = {};
    stepFrameArmed_ = false;
}

void Debugger::detach() {
    std::lock_guard<std::mutex> lock(mu_);
    vm_ = nullptr;
    mode_ = RunMode::Running;
    reason_ = PauseReason::None;
    stepFrameArmed_ = false;
}

void Debugger::pause(PauseReason reason) {
    reason_.store(reason);
    mode_.store(RunMode::Paused);
    // Frame-level Pause has no script site; drop a stale hook location so
    // smart step() does not treat this as mid-script.
    if (reason == PauseReason::PauseKey) pauseLoc_ = {};
}

void Debugger::resume() {
    reason_.store(PauseReason::None);
    mode_.store(RunMode::Running);
    stepFrameArmed_  = false;
    stepStartDepth_  = 0;
    stepSkipLoc_     = {};
    pauseLoc_        = {};
}

void Debugger::stepFrame() {
    reason_.store(PauseReason::Step);
    mode_.store(RunMode::StepFrame);
    stepFrameArmed_ = true;
}

int Debugger::scriptStackDepth() const {
    HSQUIRRELVM vm = vm_;
    if (!vm) return 0;
    int depth = 0;
    for (int level = 0;; ++level) {
        SQStackInfos si;
        if (SQ_FAILED(sq_stackinfos(vm, level, &si))) break;
        ++depth;
    }
    return depth;
}

void Debugger::beginScriptStep(RunMode stepMode) {
    reason_.store(PauseReason::Step);
    stepStartDepth_ = scriptStackDepth();
    // Squirrel can emit several _OP_LINE for one source line; skip the line we
    // are currently paused on until the location changes.
    stepSkipLoc_ = pauseLoc_;
    // Not currently inside a script frame (frame-level pause): stop on the
    // first line we see — treat like stepInto with an open depth gate.
    if (stepStartDepth_ <= 0) {
        mode_.store(RunMode::StepInto);
        stepStartDepth_ = 0;
        return;
    }
    mode_.store(stepMode);
}

void Debugger::stepInto() { beginScriptStep(RunMode::StepInto); }

void Debugger::stepOver() { beginScriptStep(RunMode::StepOver); }

void Debugger::stepOut() {
    // No caller to return to → just step over the current line.
    if (scriptStackDepth() <= 1) {
        beginScriptStep(RunMode::StepOver);
        return;
    }
    beginScriptStep(RunMode::StepOut);
}

void Debugger::step() {
    // Prefer script step-over when we have a script pause site; else one frame.
    const PauseReason r = reason_.load();
    if (!pauseLoc_.empty() &&
        (r == PauseReason::Breakpoint || r == PauseReason::Step || r == PauseReason::Exception)) {
        stepOver();
        return;
    }
    stepFrame();
}

bool Debugger::shouldRunUpdate() {
    const RunMode m = mode_.load();
    if (m == RunMode::Running) return true;
    if (m == RunMode::StepFrame) return true;
    // Allow the game loop to enter eve_update so script steps can begin after
    // a frame-level pause.
    if (m == RunMode::StepInto || m == RunMode::StepOver || m == RunMode::StepOut) return true;
    return false;  // Paused
}

void Debugger::notifyFrameDone() {
    if (mode_.load() == RunMode::StepFrame || stepFrameArmed_) {
        stepFrameArmed_ = false;
        reason_.store(PauseReason::Step);
        mode_.store(RunMode::Paused);
        // Frame step finished outside the line hook — next smart step is frame.
        pauseLoc_ = {};
    }
}

bool Debugger::matchBreakpoint(const std::string& source, int line) const {
    if (!bpsEnabled_.load()) return false;
    for (const auto& bp : bps_) {
        if (!bp.enabled || bp.line != line) continue;
        if (sourcesMatch(source, bp.source)) return true;
    }
    return false;
}

bool Debugger::matchBreakpointAny(const std::string& source, int line) const {
    for (const auto& bp : bps_) {
        if (bp.line != line) continue;
        if (sourcesMatch(source, bp.source)) return true;
    }
    return false;
}

bool Debugger::conditionHolds(const Breakpoint& bp) const {
    if (bp.condition.empty()) return true;
    // Failed / unreadable conditions stop (safer than silently never stopping).
    const VariableInfo r = evaluate(bp.condition, 0);
    if (r.type == "error") return true;
    return valueTruthy(r);
}

bool Debugger::onScriptLine(const SourceLoc& loc) {
    const RunMode m = mode_.load();
    if (m == RunMode::Paused) {
        return true;
    }

    // Verification: any breakpoint whose exact line we just executed is real.
    // Report it (once) even when stepping filters or conditions suppress the stop.
    std::vector<int> verifiedNow;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!bps_.empty() && matchBreakpointAny(loc.source, loc.line)) {
            for (auto& bp : bps_) {
                if (bp.verified || bp.line != loc.line ||
                    !sourcesMatch(loc.source, bp.source))
                    continue;
                bp.verified    = true;
                verifiedNow.push_back(bp.id);
            }
        }
    }
    for (const int id : verifiedNow) {
        if (bpEventFn_) bpEventFn_(id, loc.source, loc.line, true);
    }

    const bool stepping =
        m == RunMode::StepInto || m == RunMode::StepOver || m == RunMode::StepOut;

    // While leaving the paused line, ignore further events for that exact
    // source+line (extra _OP_LINE and re-armed breakpoints on the same site).
    if (stepping && !stepSkipLoc_.empty() && stepSkipLoc_.line == loc.line &&
        sourcesMatch(stepSkipLoc_.source, loc.source)) {
        return false;
    }

    // Breakpoints win over step filters (hit inside a skipped call).
    Breakpoint matched;
    bool hit = false;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (bpsEnabled_.load()) {
            for (const auto& bp : bps_) {
                if (!bp.enabled || bp.line != loc.line ||
                    !sourcesMatch(loc.source, bp.source))
                    continue;
                matched = bp;
                hit     = true;
                break;
            }
        }
    }
    if (hit) {
        if (!conditionHolds(matched)) return false;
        stepSkipLoc_ = {};
        pauseLoc_    = loc;
        reason_.store(PauseReason::Breakpoint);
        mode_.store(RunMode::Paused);
        RenderVision::instance().notifyPending("breakpoint", loc.source, loc.line);
        return true;
    }

    if (stepping) {
        const int depth = scriptStackDepth();
        bool stop       = false;
        if (m == RunMode::StepInto) {
            stop = true;
        } else if (m == RunMode::StepOver) {
            // Same frame or outer: stop. Deeper (inside a call): keep going.
            stop = depth <= stepStartDepth_;
        } else {  // StepOut
            stop = depth < stepStartDepth_;
        }
        if (stop) {
            stepSkipLoc_ = {};
            pauseLoc_    = loc;
            reason_.store(PauseReason::Step);
            mode_.store(RunMode::Paused);
            return true;
        }
        return false;
    }
    return false;
}

void Debugger::waitWhilePaused(const std::function<void()>& pump) {
    while (mode_.load() == RunMode::Paused) {
        if (pump) pump();
        else if (pump_) pump_();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (!vm_) break;
    }
}

int Debugger::setBreakpoint(std::string source, int line, bool enabled,
                            std::string condition) {
    source = normalizeSource(std::move(source));
    if (source.empty() || line <= 0) return 0;
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& bp : bps_) {
        if (bp.line == line && normalizeSource(bp.source) == source) {
            bp.enabled = enabled;
            if (!condition.empty()) bp.condition = std::move(condition);
            return bp.id;
        }
    }
    Breakpoint bp;
    bp.source    = std::move(source);
    bp.line      = line;
    bp.enabled   = enabled;
    bp.condition = std::move(condition);
    bp.id        = nextBpId_++;
    bps_.push_back(bp);
    return bp.id;
}

bool Debugger::clearBreakpoint(std::string source, int line) {
    std::lock_guard<std::mutex> lock(mu_);
    const auto before = bps_.size();
    bps_.erase(std::remove_if(bps_.begin(), bps_.end(),
                              [&](const Breakpoint& bp) {
                                  return bp.line == line && sourcesMatch(source, bp.source);
                              }),
               bps_.end());
    return bps_.size() != before;
}

void Debugger::clearBreakpoints(const std::string& source) {
    std::lock_guard<std::mutex> lock(mu_);
    if (source.empty()) {
        bps_.clear();
        return;
    }
    bps_.erase(std::remove_if(bps_.begin(), bps_.end(),
                              [&](const Breakpoint& bp) { return sourcesMatch(source, bp.source); }),
               bps_.end());
}

std::vector<Breakpoint> Debugger::breakpoints() const {
    std::lock_guard<std::mutex> lock(mu_);
    return bps_;
}

bool Debugger::hasBreakpoint(const std::string& source, int line) const {
    std::lock_guard<std::mutex> lock(mu_);
    return matchBreakpoint(source, line);
}

void Debugger::addWatch(std::string expression) {
    if (expression.empty()) return;
    std::lock_guard<std::mutex> lock(mu_);
    for (const auto& e : watchExprs_) {
        if (e == expression) return;
    }
    watchExprs_.push_back(std::move(expression));
}

bool Debugger::removeWatch(const std::string& expression) {
    std::lock_guard<std::mutex> lock(mu_);
    const auto before = watchExprs_.size();
    watchExprs_.erase(std::remove(watchExprs_.begin(), watchExprs_.end(), expression),
                      watchExprs_.end());
    return watchExprs_.size() != before;
}

void Debugger::clearWatches() {
    std::lock_guard<std::mutex> lock(mu_);
    watchExprs_.clear();
    watchCache_.clear();
}

std::vector<WatchEntry> Debugger::watches() const {
    std::lock_guard<std::mutex> lock(mu_);
    return watchCache_;
}

void Debugger::refreshWatches() {
    std::vector<std::string> exprs;
    {
        std::lock_guard<std::mutex> lock(mu_);
        exprs = watchExprs_;
    }
    std::vector<WatchEntry> cache;
    cache.reserve(exprs.size());
    for (const auto& e : exprs) {
        WatchEntry w;
        w.expression = e;
        auto info    = evaluate(e);
        w.ok         = !info.type.empty() && info.type != "error";
        w.value      = info.value;
        cache.push_back(std::move(w));
    }
    std::lock_guard<std::mutex> lock(mu_);
    watchCache_ = std::move(cache);
}

VariableInfo Debugger::readLocal(HSQUIRRELVM vm, unsigned level, const std::string& name) const {
    VariableInfo info;
    info.name = name;
    if (!vm) {
        info.type  = "error";
        info.value = "no vm";
        return info;
    }
    for (SQUnsignedInteger n = 0;; ++n) {
        const SQInteger top  = sq_gettop(vm);
        const SQChar*   lname = sq_getlocal(vm, level, n);
        if (!lname) {
            sq_settop(vm, top);
            break;
        }
        if (name == lname) {
            info.value = describeSqValue(vm, -1);
            info.type  = typeName(vm, -1);
            sq_settop(vm, top);
            return info;
        }
        sq_settop(vm, top);
    }
    info.type  = "error";
    info.value = "not found";
    return info;
}

VariableInfo Debugger::readRoot(HSQUIRRELVM vm, const std::string& name) const {
    VariableInfo info;
    info.name = name;
    if (!vm) {
        info.type  = "error";
        info.value = "no vm";
        return info;
    }
    const SQInteger top = sq_gettop(vm);
    sq_pushroottable(vm);
    sq_pushstring(vm, name.c_str(), -1);
    if (SQ_SUCCEEDED(sq_get(vm, -2))) {
        info.value = describeSqValue(vm, -1);
        info.type  = typeName(vm, -1);
    } else {
        info.type  = "error";
        info.value = "not found";
    }
    sq_settop(vm, top);
    return info;
}

VariableInfo Debugger::evaluateLegacy(const std::string& expression) const {
    HSQUIRRELVM vm = vm_;
    if (!vm || expression.empty()) {
        VariableInfo info;
        info.name  = expression;
        info.type  = "error";
        info.value = "unavailable";
        return info;
    }
    // Prefer local, then roottable slot. Full expression eval is intentionally
    // limited (safe for watches of variable names / dotted root paths).
    auto local = readLocal(vm, 0, expression);
    if (local.type != "error") return local;

    // Support a.b root path (tables only).
    if (expression.find('.') != std::string::npos) {
        VariableInfo info;
        info.name             = expression;
        const SQInteger top   = sq_gettop(vm);
        sq_pushroottable(vm);
        bool ok = true;
        size_t start = 0;
        while (start < expression.size()) {
            size_t dot = expression.find('.', start);
            if (dot == std::string::npos) dot = expression.size();
            const std::string part = expression.substr(start, dot - start);
            sq_pushstring(vm, part.c_str(), -1);
            if (SQ_FAILED(sq_get(vm, -2))) {
                ok = false;
                break;
            }
            // Replace parent with child: [root, parent, child] → keep child under root.
            sq_remove(vm, -2);
            start = dot + 1;
        }
        if (ok) {
            info.value = describeSqValue(vm, -1);
            info.type  = typeName(vm, -1);
        } else {
            info.type  = "error";
            info.value = "not found";
        }
        sq_settop(vm, top);
        return info;
    }
    return readRoot(vm, expression);
}

std::vector<VariableInfo> Debugger::locals(int stackLevel) const {
    std::vector<VariableInfo> out;
    HSQUIRRELVM vm = vm_;
    if (!vm) return out;
    const SQUnsignedInteger level = static_cast<SQUnsignedInteger>(stackLevel < 0 ? 0 : stackLevel);
    for (SQUnsignedInteger n = 0;; ++n) {
        const SQInteger top  = sq_gettop(vm);
        const SQChar*   name = sq_getlocal(vm, level, n);
        if (!name) {
            sq_settop(vm, top);
            break;
        }
        VariableInfo info = describeAt(vm, -1);
        info.name         = name;
        sq_settop(vm, top);
        out.push_back(std::move(info));
    }
    return out;
}

std::vector<VariableInfo> Debugger::globals() const {
    std::vector<VariableInfo> out;
    HSQUIRRELVM vm = vm_;
    if (!vm) return out;
    const SQInteger top = sq_gettop(vm);
    sq_pushroottable(vm);
    out = enumerateAt(vm, -1);
    sq_settop(vm, top);
    return out;
}

bool Debugger::pushLocalValue(HSQUIRRELVM vm, unsigned level, const std::string& name) const {
    for (SQUnsignedInteger n = 0;; ++n) {
        const SQInteger top  = sq_gettop(vm);
        const SQChar*   lname = sq_getlocal(vm, level, n);
        if (!lname) {
            sq_settop(vm, top);
            return false;
        }
        if (name == lname) return true;  // local value is on the stack
        sq_settop(vm, top);
    }
}

bool Debugger::pushPathValue(HSQUIRRELVM vm, VarKind kind, int frame,
                             const std::vector<std::string>& path) const {
    if (!vm) return false;
    const SQInteger top = sq_gettop(vm);
    if (kind == VarKind::Globals) {
        sq_pushroottable(vm);
    } else if (kind == VarKind::Locals) {
        if (path.empty()) return false;
        if (!pushLocalValue(vm, static_cast<unsigned>(frame < 0 ? 0 : frame), path[0]))
            return false;
    } else {
        return false;
    }

    const size_t start = (kind == VarKind::Globals) ? 0 : 1;
    for (size_t i = start; i < path.size(); ++i) {
        const SQInteger before = sq_gettop(vm);
        const int curAbs       = before;
        sq_pushstring(vm, path[i].c_str(), -1);
        if (SQ_FAILED(sq_get(vm, curAbs))) {
            sq_settop(vm, top);
            return false;
        }
        sq_remove(vm, -2);  // drop parent, keep child
    }
    return true;
}

std::vector<VariableInfo> Debugger::containerChildren(VarKind kind, int frame,
                                                      const std::vector<std::string>& path) const {
    HSQUIRRELVM vm = vm_;
    if (!vm) return {};
    if (path.empty()) {
        if (kind == VarKind::Locals) return locals(frame);
        if (kind == VarKind::Globals) return globals();
        return {};
    }
    const SQInteger top = sq_gettop(vm);
    if (!pushPathValue(vm, kind, frame, path)) {
        sq_settop(vm, top);
        return {};
    }
    std::vector<VariableInfo> out = enumerateAt(vm, -1);
    sq_settop(vm, top);
    return out;
}

VariableInfo Debugger::evaluate(const std::string& expression, int frameLevel) const {
    HSQUIRRELVM vm = vm_;
    if (!vm || expression.empty()) {
        VariableInfo info;
        info.name  = expression;
        info.type  = "error";
        info.value = "unavailable";
        return info;
    }
    const SQInteger top = sq_gettop(vm);
    const int level     = frameLevel < 0 ? 0 : frameLevel;

    // Locals env: fresh table seeded with the frame's locals; delegate = roottable
    // so global names also resolve (locals shadow globals, like in the frame).
    sq_newtable(vm);
    for (SQUnsignedInteger n = 0;; ++n) {
        const SQInteger before = sq_gettop(vm);
        const SQChar*   lname  = sq_getlocal(vm, static_cast<SQUnsignedInteger>(level), n);
        if (!lname) {
            sq_settop(vm, before);
            break;
        }
        // Stack after sq_getlocal: [env, value]; build [env, key, value].
        sq_pushstring(vm, lname, -1);  // key
        sq_push(vm, -2);               // duplicate value
        sq_remove(vm, -3);             // drop original value
        sq_newslot(vm, -3, SQFalse);   // env[key] = value (pops key+value)
    }
    sq_pushroottable(vm);
    if (SQ_FAILED(sq_setdelegate(vm, -2))) {
        sq_settop(vm, top);
        return evaluateLegacy(expression);
    }

    const std::string src = "return (" + expression + ");";
    if (SQ_FAILED(sq_compilebuffer(vm, src.c_str(), static_cast<SQInteger>(src.size()),
                                   _SC("eval"), SQTrue))) {
        sq_settop(vm, top);
        return evaluateLegacy(expression);
    }
    // stack: [env, closure]
    sq_push(vm, -2);                 // [env, closure, env]
    sq_setclosureroot(vm, -2);       // closure env = locals table (pops copy)
    sq_pushroottable(vm);            // this/arg for the call
    VariableInfo info;
    info.name = expression;
    if (SQ_SUCCEEDED(sq_call(vm, 1, SQTrue, SQFalse))) {
        info = describeAt(vm, -1);
    } else {
        info.type  = "error";
        info.value = "eval error";
    }
    sq_settop(vm, top);
    return info;
}

std::vector<StackFrameInfo> Debugger::stackTrace(int maxFrames) const {
    std::vector<StackFrameInfo> out;
    HSQUIRRELVM vm = vm_;
    if (!vm) return out;
    if (maxFrames <= 0) maxFrames = 32;
    for (int level = 0; level <= maxFrames; ++level) {
        SQStackInfos si;
        if (SQ_FAILED(sq_stackinfos(vm, level, &si))) break;
        StackFrameInfo f;
        f.id = level;
        if (si.source) f.loc.source = si.source;
        f.loc.line = static_cast<int>(si.line);
        if (si.funcname) {
            f.loc.function = si.funcname;
            f.name         = si.funcname;
        } else {
            f.name = "?";
        }
        // Native-only frames (Squirrel reports source "NATIVE" / line -1, e.g.
        // the uncaught-error hook parked above the throwing script frame) carry
        // no script location. Skip them so frame 0 in the IDE is the throw
        // site; ids stay at stack levels so scopes(frameId) still resolves to
        // the right frame's locals.
        if (f.loc.source == "NATIVE" || (f.loc.source.empty() && f.loc.line <= 0)) continue;
        out.push_back(std::move(f));
    }
    return out;
}

}  // namespace eve::dev
