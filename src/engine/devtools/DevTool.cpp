#include "devtools/DevTool.hpp"

#include "devtools/AiPanel.hpp"
#include "devtools/McpDevBridge.hpp"
#include "devtools/McpServer.hpp"

#include "common/Module.h"
#include "common/RenderTrace.h"
#include "event/Event.h"

#include <simplesquirrel/simplesquirrel.hpp>
#include <squirrel.h>

#include <cstring>
#include <string>
#include <vector>

// AiPanel implementation lives in AiPanel.inc (included at file end) so it does
// not create an extra MSVC export-heavy object file.

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

    Debugger::instance().attach(vm);
    Debugger::instance().setPump([this]() { pumpWhilePaused(); });

    sq_enabledebuginfo(vm_, SQTrue);
    sq_setnativedebughook(vm_, nativeDebugHook);
    g_active = this;
    installRenderTracer();
}

void DevTool::detach() {
    stopDap();
    stopMcp();
    Debugger::instance().detach();
    if (vm_) {
        sq_setnativedebughook(vm_, nullptr);
        // Leave debuginfo enabled; harmless for subsequent runs on same VM.
    }
    if (g_active == this) g_active = nullptr;
    vm_ = nullptr;
    localSnap_.clear();
    uninstallRenderTracer();
}

int DevTool::startDap(uint16_t port) { return DebugAdapter::instance().listen(port); }

void DevTool::stopDap() { DebugAdapter::instance().stop(); }

int DevTool::startMcp(uint16_t port) { return McpServer::instance().listen(port); }

void DevTool::stopMcp() { McpServer::instance().stop(); }

McpServer& DevTool::mcp() { return McpServer::instance(); }

AiPanel& DevTool::ai() { return AiPanel::instance(); }

void DevTool::poll() {
    DebugAdapter::instance().poll();
    McpServer::instance().poll();
}

void DevTool::drawAiPanel() { AiPanel::instance().drawImGui(); }

void DevTool::exposeScriptApi(ssq::VM& vm) {
    try {
        ssq::Table eveTbl = vm.find("eve").toTable();
        ssq::Table dev    = eveTbl.addTable("dev");

        dev.addFunc("pause", [this]() {
            debugger().pause(PauseReason::PauseKey);
            dap().notifyStopped(PauseReason::PauseKey, debugger().pauseLocation());
        });
        // Note: Squirrel reserves `resume` (generators) and `continue` (loops),
        // so the script binding cannot be named either of those.
        dev.addFunc("continueRun", [this]() {
            debugger().resume();
            dap().notifyContinued();
        });
        dev.addFunc("togglePause", [this]() {
            if (debugger().isPaused()) {
                debugger().resume();
                dap().notifyContinued();
            } else {
                debugger().pause(PauseReason::PauseKey);
                dap().notifyStopped(PauseReason::PauseKey, debugger().pauseLocation());
            }
        });
        dev.addFunc("isPaused", [this]() { return debugger().isPaused(); });
        dev.addFunc("stepFrame", [this]() {
            debugger().stepFrame();
            dap().notifyContinued();
        });
        // stepInto / stepOver / stepOut — primary script stepping (DAP F11 / F10 / Shift+F11).
        dev.addFunc("stepInto", [this]() {
            debugger().stepInto();
            dap().notifyContinued();
        });
        dev.addFunc("stepOver", [this]() {
            debugger().stepOver();
            dap().notifyContinued();
        });
        dev.addFunc("stepOut", [this]() {
            debugger().stepOut();
            dap().notifyContinued();
        });
        // Historical alias for stepInto.
        dev.addFunc("stepLine", [this]() {
            debugger().stepLine();
            dap().notifyContinued();
        });
        // Convenience: stepOver when mid-script, else one frame.
        dev.addFunc("step", [this]() {
            debugger().step();
            dap().notifyContinued();
        });
        dev.addFunc("shouldRunUpdate", [this]() { return debugger().shouldRunUpdate(); });
        dev.addFunc("notifyFrameDone", [this]() {
            const bool wasStep = debugger().mode() == RunMode::StepFrame;
            debugger().notifyFrameDone();
            if (wasStep || debugger().isPaused())
                dap().notifyStopped(debugger().lastPauseReason(), debugger().pauseLocation());
        });
        dev.addFunc("poll", [this]() { poll(); });

        dev.addFunc("setBreakpoint", [this](std::string source, int line) {
            return debugger().setBreakpoint(std::move(source), line, true);
        });
        dev.addFunc("clearBreakpoint", [this](std::string source, int line) {
            return debugger().clearBreakpoint(std::move(source), line);
        });
        dev.addFunc("clearBreakpoints", [this]() { debugger().clearBreakpoints(); });

        dev.addFunc("addWatch", [this](std::string expr) { debugger().addWatch(std::move(expr)); });
        dev.addFunc("removeWatch", [this](std::string expr) { return debugger().removeWatch(expr); });
        dev.addFunc("clearWatches", [this]() { debugger().clearWatches(); });
        dev.addFunc("eval", [this](std::string expr) {
            auto info = debugger().evaluate(expr);
            return info.value;
        });

        dev.addFunc("markStateRoot",
                    [](std::string name) { Snapshot::instance().markRoot(std::move(name)); });
        dev.addFunc("unmarkStateRoot",
                    [](std::string name) { Snapshot::instance().unmarkRoot(name); });
        dev.addFunc("saveSnapshot", [this](std::string path) {
            std::string err;
            const bool  ok = snapshot().saveFile(vm_, path, &err);
            if (!ok) return std::string("error:") + err;
            return std::string("ok");
        });
        dev.addFunc("loadSnapshot", [this](std::string path) {
            std::string err;
            const bool  ok = snapshot().loadFile(vm_, path, &err);
            if (!ok) return std::string("error:") + err;
            return std::string("ok");
        });
        dev.addFunc("captureSnapshot", [this]() {
            std::string err;
            return snapshot().capture(vm_, &err);
        });
        dev.addFunc("restoreSnapshot", [this](std::string json) {
            std::string err;
            const bool  ok = snapshot().restore(vm_, json, &err);
            if (!ok) return std::string("error:") + err;
            return std::string("ok");
        });

        // AI / MCP surface (DevTools panel + agent session log).
        ssq::Table ai = dev.addTable("ai");
        ai.addFunc("status", []() { return AiPanel::instance().statusLine(); });
        ai.addFunc("isVisible", []() { return AiPanel::instance().isVisible(); });
        ai.addFunc("setVisible", [](bool on) { AiPanel::instance().setVisible(on); });
        ai.addFunc("toggleVisible", []() { AiPanel::instance().toggleVisible(); });
        ai.addFunc("note", [](std::string text) {
            AiPanel::instance().addNote(std::move(text));
            return std::string("ok");
        });
        ai.addFunc("log", []() { return AiPanel::instance().formatLog(64); });
        ai.addFunc("clearLog", []() { AiPanel::instance().clearLog(); });
        ai.addFunc("mcpPort", []() { return AiPanel::instance().mcpPort(); });
        ai.addFunc("mcpConnected", []() { return AiPanel::instance().mcpConnected(); });
        ai.addFunc("draw", [this]() { drawAiPanel(); });
    } catch (...) {
        // If eve table missing, skip — attach still useful for C++/DAP/MCP.
    }
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
            if (Debugger::instance().onScriptLine(loc)) {
                dap().notifyStopped(Debugger::instance().lastPauseReason(), loc);
                Debugger::instance().refreshWatches();
                Debugger::instance().waitWhilePaused([this]() { pumpWhilePaused(); });
            }
            break;
        default:
            break;
    }
}

void DevTool::handleDebugHotkey(const std::string& key) {
    if (key.empty()) return;
    Debugger& dbg = debugger();
    if (key == "F5") {
        if (!dbg.isPaused()) return;
        dbg.resume();
        dap().notifyContinued();
        return;
    }
    if (key == "F10") {
        if (!dbg.isPaused()) return;
        dbg.stepOver();
        dap().notifyContinued();
        return;
    }
    if (key == "F11") {
        if (!dbg.isPaused()) return;
        dbg.stepInto();
        dap().notifyContinued();
        return;
    }
    if (key == "F8") {
        if (!dbg.isPaused()) return;
        dbg.stepFrame();
        dap().notifyContinued();
        return;
    }
    if (key == "Pause") {
        // Already inside a script pause — Pause resumes (toggle).
        if (!dbg.isPaused()) return;
        dbg.resume();
        dap().notifyContinued();
    }
}

void DevTool::pumpWhilePaused() {
    poll();  // DAP continue / next / pause

    auto* ev = eve::ModuleManager::getInstance<eve::event::Event>("Event");
    if (!ev) return;
    ev->pump();

    // Consume debug hotkeys so F5/F8/F10/F11 work while blocked in the line hook;
    // re-queue everything else for the main loop after resume.
    std::vector<eve::event::Message*> keep;
    while (eve::event::Message* msg = ev->poll()) {
        bool handled = false;
        if (msg->name == "keypressed" && !msg->args.empty() &&
            msg->args[0].type == eve::event::Variant::Type::String) {
            const std::string& key = msg->args[0].s;
            if (key == "F5" || key == "F8" || key == "F10" || key == "F11" || key == "Pause") {
                handleDebugHotkey(key);
                handled = true;
            }
        }
        if (handled)
            delete msg;
        else
            keep.push_back(msg);
    }
    for (eve::event::Message* msg : keep) ev->push(msg);
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
        const auto ev = graph_.events();
        for (size_t i = ev.size(); i-- > 0;) {
            if (ev[i].kind == TraceKind::Line || ev[i].kind == TraceKind::Use ||
                ev[i].kind == TraceKind::Def) {
                c.loc = ev[i].loc;
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
        const auto ev = graph_.events();
        for (size_t i = ev.size(); i-- > 0;) {
            if (!ev[i].loc.empty()) {
                c.loc = ev[i].loc;
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

    debugger().pause(PauseReason::Exception);
    dap().notifyStopped(PauseReason::Exception, site);

    return lastReport_;
}

bool mcpDevAttached() { return DevTool::instance().isAttached(); }

std::size_t mcpCallgraphEvents() { return DevTool::instance().graph().events().size(); }

std::size_t mcpCallgraphStackDepth() {
    return DevTool::instance().graph().currentStack().size();
}

const std::string& mcpLastReport() { return DevTool::instance().lastReport(); }

std::string mcpFormatError(const std::string& message) {
    return DevTool::instance().formatError(message);
}

}  // namespace eve::dev

#include "devtools/AiPanel.inc"
