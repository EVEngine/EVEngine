#include "devtools/Debugger.hpp"

#include <simplesquirrel/simplesquirrel.hpp>
#include <squirrel.h>

#include <algorithm>
#include <chrono>
#include <thread>

namespace eve::dev {
namespace {

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

}  // namespace

Debugger& Debugger::instance() {
    static Debugger inst;
    return inst;
}

std::string Debugger::normalizeSource(std::string source) {
    if (source.empty()) return source;
    // Strip file:// prefix.
    const std::string pref = "file://";
    if (source.rfind(pref, 0) == 0) source = source.substr(pref.size());
    // Unify separators.
    for (char& c : source) {
        if (c == '\\') c = '/';
    }
    // Drop leading ./
    while (source.size() >= 2 && source[0] == '.' && source[1] == '/') source = source.substr(2);
    return source;
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
}

void Debugger::resume() {
    reason_.store(PauseReason::None);
    mode_.store(RunMode::Running);
    stepFrameArmed_ = false;
}

void Debugger::stepFrame() {
    reason_.store(PauseReason::Step);
    mode_.store(RunMode::StepFrame);
    stepFrameArmed_ = true;
}

void Debugger::stepLine() {
    reason_.store(PauseReason::Step);
    mode_.store(RunMode::StepLine);
}

bool Debugger::shouldRunUpdate() {
    const RunMode m = mode_.load();
    if (m == RunMode::Running) return true;
    if (m == RunMode::StepFrame) return true;
    return false;  // Paused / StepLine (script-level)
}

void Debugger::notifyFrameDone() {
    if (mode_.load() == RunMode::StepFrame || stepFrameArmed_) {
        stepFrameArmed_ = false;
        reason_.store(PauseReason::Step);
        mode_.store(RunMode::Paused);
    }
}

bool Debugger::matchBreakpoint(const std::string& source, int line) const {
    const std::string norm = normalizeSource(source);
    const auto slash = norm.find_last_of('/');
    const std::string base = (slash == std::string::npos) ? norm : norm.substr(slash + 1);

    for (const auto& bp : bps_) {
        if (!bp.enabled || bp.line != line) continue;
        const std::string bn = normalizeSource(bp.source);
        if (bn == norm) return true;
        const auto bs = bn.find_last_of('/');
        const std::string bbase = (bs == std::string::npos) ? bn : bn.substr(bs + 1);
        if (!base.empty() && base == bbase) return true;
        if (!bn.empty() && (norm.size() >= bn.size()) &&
            norm.compare(norm.size() - bn.size(), bn.size(), bn) == 0)
            return true;
    }
    return false;
}

bool Debugger::onScriptLine(const SourceLoc& loc) {
    const RunMode m = mode_.load();
    if (m == RunMode::StepLine) {
        pauseLoc_ = loc;
        reason_.store(PauseReason::Step);
        mode_.store(RunMode::Paused);
        return true;
    }
    if (m == RunMode::Paused) {
        // Already paused mid-script (nested?) — keep blocking.
        return true;
    }
    bool hit = false;
    {
        std::lock_guard<std::mutex> lock(mu_);
        hit = matchBreakpoint(loc.source, loc.line);
    }
    if (hit) {
        pauseLoc_ = loc;
        reason_.store(PauseReason::Breakpoint);
        mode_.store(RunMode::Paused);
        return true;
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

int Debugger::setBreakpoint(std::string source, int line, bool enabled) {
    source = normalizeSource(std::move(source));
    if (source.empty() || line <= 0) return 0;
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& bp : bps_) {
        if (bp.line == line && normalizeSource(bp.source) == source) {
            bp.enabled = enabled;
            return bp.id;
        }
    }
    Breakpoint bp;
    bp.source  = std::move(source);
    bp.line    = line;
    bp.enabled = enabled;
    bp.id      = nextBpId_++;
    bps_.push_back(bp);
    return bp.id;
}

bool Debugger::clearBreakpoint(std::string source, int line) {
    source = normalizeSource(std::move(source));
    std::lock_guard<std::mutex> lock(mu_);
    const auto before = bps_.size();
    bps_.erase(std::remove_if(bps_.begin(), bps_.end(),
                              [&](const Breakpoint& bp) {
                                  return bp.line == line && normalizeSource(bp.source) == source;
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
    const std::string norm = normalizeSource(source);
    bps_.erase(std::remove_if(bps_.begin(), bps_.end(),
                              [&](const Breakpoint& bp) {
                                  return normalizeSource(bp.source) == norm;
                              }),
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

VariableInfo Debugger::evaluate(const std::string& expression) const {
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
    auto local = readLocal(vm, 1, expression);
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
    const SQUnsignedInteger level = static_cast<SQUnsignedInteger>(stackLevel < 0 ? 1 : stackLevel);
    for (SQUnsignedInteger n = 0;; ++n) {
        const SQInteger top  = sq_gettop(vm);
        const SQChar*   name = sq_getlocal(vm, level, n);
        if (!name) {
            sq_settop(vm, top);
            break;
        }
        VariableInfo info;
        info.name  = name;
        info.value = describeSqValue(vm, -1);
        info.type  = typeName(vm, -1);
        sq_settop(vm, top);
        out.push_back(std::move(info));
    }
    return out;
}

std::vector<StackFrameInfo> Debugger::stackTrace(int maxFrames) const {
    std::vector<StackFrameInfo> out;
    HSQUIRRELVM vm = vm_;
    if (!vm) return out;
    if (maxFrames <= 0) maxFrames = 32;
    for (int level = 1; level <= maxFrames; ++level) {
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
        out.push_back(std::move(f));
    }
    return out;
}

}  // namespace eve::dev
