#include "devtools/DevTool.hpp"

#include "common/RenderTrace.h"

#include <simplesquirrel/simplesquirrel.hpp>
#include <squirrel.h>

#include <cstring>
#include <string>

namespace eve::dev {
namespace {

thread_local DevTool* g_active = nullptr;

std::string describeSqValue(HSQUIRRELVM vm, SQInteger idx) {
    const SQObjectType t = sq_gettype(vm, idx);
    switch (t) {
        case OT_NULL:
            return "null";
        case OT_INTEGER: {
            SQInteger v = 0;
            sq_getinteger(vm, idx, &v);
            return "i:" + std::to_string(static_cast<long long>(v));
        }
        case OT_FLOAT: {
            SQFloat v = 0;
            sq_getfloat(vm, idx, &v);
            return "f:" + std::to_string(static_cast<double>(v));
        }
        case OT_BOOL: {
            SQBool v = SQFalse;
            sq_getbool(vm, idx, &v);
            return v ? "b:1" : "b:0";
        }
        case OT_STRING: {
            const SQChar* s = nullptr;
            sq_getstring(vm, idx, &s);
            return std::string("s:") + (s ? s : "");
        }
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
        case OT_GENERATOR:
            return "generator";
        case OT_USERPOINTER:
            return "userpointer";
        case OT_THREAD:
            return "thread";
        case OT_CLASS:
            return "class";
        case OT_INSTANCE:
            return "instance";
        case OT_WEAKREF:
            return "weakref";
        default:
            return "other";
    }
}

void nativeDebugHook(HSQUIRRELVM v, SQInteger type, const SQChar* sourcename, SQInteger line,
                     const SQChar* funcname) {
    if (!g_active) return;
    g_active->handleDebugEvent(v, static_cast<int>(type), sourcename ? sourcename : "",
                               static_cast<int>(line), funcname ? funcname : "");
}

SourceLoc stackTopLoc(HSQUIRRELVM vm) {
    SourceLoc loc;
    SQStackInfos si;
    if (SQ_SUCCEEDED(sq_stackinfos(vm, 1, &si))) {
        if (si.source) loc.source = si.source;
        loc.line = static_cast<int>(si.line);
        if (si.funcname) loc.function = si.funcname;
    }
    return loc;
}

}  // namespace

DevTool& DevTool::instance() {
    static DevTool inst;
    return inst;
}

void DevTool::attach(ssq::VM& vm, bool sampleLocals) { attach(vm.getHandle(), sampleLocals); }

void DevTool::installRenderTracer() {
    renderFlow_.clear();
    eve::debug::setRenderTracer(&renderFlow_);
    renderTraceEnabled_ = true;
}

void DevTool::uninstallRenderTracer() {
    if (eve::debug::renderTracer() == &renderFlow_) eve::debug::setRenderTracer(nullptr);
    renderTraceEnabled_ = false;
}

void DevTool::enableRenderTrace(bool on) {
    if (on)
        installRenderTracer();
    else
        uninstallRenderTracer();
}

void DevTool::attach(HSQUIRRELVM vm, bool sampleLocals) {
    if (!vm) return;
    detach();
    vm_           = vm;
    sampleLocals_ = sampleLocals;
    graph_.clear();
    localSnap_.clear();
    lastReport_.clear();

    sq_enabledebuginfo(vm_, SQTrue);
    sq_setnativedebughook(vm_, nativeDebugHook);
    g_active = this;
    installRenderTracer();
}

void DevTool::detach() {
    if (vm_) {
        sq_setnativedebughook(vm_, nullptr);
        // Leave debuginfo enabled; harmless for subsequent runs on same VM.
    }
    if (g_active == this) g_active = nullptr;
    vm_ = nullptr;
    localSnap_.clear();
    uninstallRenderTracer();
}

void DevTool::handleDebugEvent(HSQUIRRELVM vm, int type, const char* source, int line,
                               const char* funcname) {
    SourceLoc loc;
    loc.source   = source ? source : "";
    loc.line     = line;
    loc.function = funcname ? funcname : "";

    // Squirrel passes 'l' / 'c' / 'r' as event type characters.
    switch (type) {
        case 'c':
            graph_.onCall(loc, loc.function);
            localSnap_.erase(static_cast<int>(graph_.currentStack().size()));
            break;
        case 'r':
            graph_.onReturn(loc, loc.function);
            // Drop snapshot for the frame that just returned.
            localSnap_.erase(static_cast<int>(graph_.currentStack().size()) + 1);
            break;
        case 'l':
            graph_.onLine(loc);
            if (sampleLocals_) sampleFrameLocals(vm, loc);
            break;
        default:
            break;
    }
}

void DevTool::sampleFrameLocals(HSQUIRRELVM vm, const SourceLoc& loc) {
    if (!vm) return;
    const int depth = static_cast<int>(graph_.currentStack().size());
    auto&     prev  = localSnap_[depth];
    std::unordered_map<std::string, std::string> cur;

    // Level 0 = current function (hook runs inside the debug callback frame?);
    // Squirrel docs: level is the call stack level — 0 is top (current).
    // When the native hook fires, level 1 is typically the script frame of interest.
    const SQUnsignedInteger level = 1;
    for (SQUnsignedInteger n = 0;; ++n) {
        const SQInteger top = sq_gettop(vm);
        const SQChar*   name = sq_getlocal(vm, level, n);
        if (!name) {
            sq_settop(vm, top);
            break;
        }
        const std::string key(name);
        // Skip temporaries / this binding noise if desired; keep "this" for OO flow.
        const std::string val = describeSqValue(vm, -1);
        sq_settop(vm, top);

        cur[key] = val;
        auto it  = prev.find(key);
        if (it == prev.end() || it->second != val) {
            // First sighting or value change ⇒ definition for the data-flow graph.
            graph_.onDef(loc, key);
        }
    }
    prev.swap(cur);
}

SliceResult DevTool::analyzeError(const std::string& errorMessage,
                                  const std::vector<std::string>& hintVars) const {
    SliceCriterion c;
    c.variables = hintVars;

    if (vm_) {
        SQStackInfos si;
        if (SQ_SUCCEEDED(sq_stackinfos(vm_, 1, &si))) {
            if (si.source) c.loc.source = si.source;
            c.loc.line = static_cast<int>(si.line);
            if (si.funcname) c.loc.function = si.funcname;
        } else {
            c.loc = stackTopLoc(vm_);
        }
    }

    // Prefer last recorded line if stackinfos unavailable.
    if (c.loc.empty() && !graph_.events().empty()) {
        for (auto it = graph_.events().rbegin(); it != graph_.events().rend(); ++it) {
            if (it->kind == TraceKind::Line || it->kind == TraceKind::Use ||
                it->kind == TraceKind::Def) {
                c.loc = it->loc;
                break;
            }
        }
    }

    (void)errorMessage;
    return graph_.sliceBackward(c);
}

std::string DevTool::formatError(const std::string& errorMessage,
                                 const std::vector<std::string>& hintVars) const {
    SliceCriterion c;
    c.variables = hintVars;
    if (vm_) {
        SQStackInfos si;
        if (SQ_SUCCEEDED(sq_stackinfos(vm_, 1, &si))) {
            if (si.source) c.loc.source = si.source;
            c.loc.line = static_cast<int>(si.line);
            if (si.funcname) c.loc.function = si.funcname;
        }
    }
    if (c.loc.empty() && !graph_.events().empty()) {
        for (auto it = graph_.events().rbegin(); it != graph_.events().rend(); ++it) {
            if (!it->loc.empty()) {
                c.loc = it->loc;
                break;
            }
        }
    }
    return graph_.formatErrorReport(errorMessage, c);
}

void DevTool::markErrorUses(const SourceLoc& loc,
                            const std::vector<std::string>& hintVars) {
    if (!vm_) return;
    SourceLoc site = loc;
    if (site.empty()) {
        SQStackInfos si;
        if (SQ_SUCCEEDED(sq_stackinfos(vm_, 1, &si))) {
            if (si.source) site.source = si.source;
            site.line = static_cast<int>(si.line);
            if (si.funcname) site.function = si.funcname;
        }
    }
    graph_.onLine(site);

    const SQUnsignedInteger level = 1;
    for (SQUnsignedInteger n = 0;; ++n) {
        const SQInteger top  = sq_gettop(vm_);
        const SQChar*   name = sq_getlocal(vm_, level, n);
        if (!name) {
            sq_settop(vm_, top);
            break;
        }
        const std::string key(name);
        sq_settop(vm_, top);
        if (!hintVars.empty()) {
            bool wanted = false;
            for (const auto& h : hintVars) {
                if (h == key) {
                    wanted = true;
                    break;
                }
            }
            if (!wanted) continue;
        }
        graph_.onUse(site, key);
    }
}

std::string DevTool::notifyError(const std::string& errorMessage,
                                 const std::vector<std::string>& hintVars) {
    SourceLoc site;
    if (vm_) {
        SQStackInfos si;
        if (SQ_SUCCEEDED(sq_stackinfos(vm_, 1, &si))) {
            if (si.source) site.source = si.source;
            site.line = static_cast<int>(si.line);
            if (si.funcname) site.function = si.funcname;
        }
    }
    markErrorUses(site, hintVars);
    // Ensure the error is marked on the render flow even if Exception ctor
    // already did (idempotent append of another Error node is fine).
    if (renderTraceEnabled_) renderFlow_.error(errorMessage.c_str());

    std::string report;
    if (vm_ || !graph_.events().empty()) report += formatError(errorMessage, hintVars);
    if (renderTraceEnabled_ && !renderFlow_.events().empty()) {
        if (!report.empty()) report += "\n";
        report += renderFlow_.formatErrorReport(errorMessage);
    }
    if (report.empty()) report = std::string("Error: ") + errorMessage + "\n";
    lastReport_ = report;
    return lastReport_;
}

}  // namespace eve::dev
