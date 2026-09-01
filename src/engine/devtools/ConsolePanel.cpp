#include "devtools/ConsolePanel.hpp"
#include "devtools/Immortal.hpp"

#include "common/ScriptError.h"
#include "common/ScriptCompiler.h"

#include <simplesquirrel/simplesquirrel.hpp>
#include <squirrel.h>

#include <cstdarg>
#include <cstdio>
#include <chrono>
#include <ctime>
#include <sstream>
#include <utility>

namespace eve::dev {
namespace {

ConsolePanel::ImGuiDrawer g_imguiDrawer = nullptr;

// Original Squirrel print/error callbacks (installed by the VM / std lib).
SQPRINTFUNCTION g_prevPrint = nullptr;
SQPRINTFUNCTION g_prevError = nullptr;

void forwardPrint(HSQUIRRELVM v, const SQChar* text) {
    if (g_prevPrint) {
        g_prevPrint(v, "%s", text ? text : "");
    } else {
        std::fputs(text ? text : "", stdout);
    }
}

void forwardError(HSQUIRRELVM v, const SQChar* text) {
    if (g_prevError) {
        g_prevError(v, "%s", text ? text : "");
    } else {
        std::fputs(text ? text : "", stderr);
    }
}

// Squirrel print callback (varargs, printf-style). Captured into the console
// log, then forwarded to the previous handler so stdout/stderr behavior stays.
void capturePrint(HSQUIRRELVM v, const SQChar* s, ...) {
    va_list args;
    va_start(args, s);
    char buf[1024];
    vsnprintf(buf, sizeof(buf), s ? s : "", args);
    va_end(args);
    ConsolePanel::instance().addLog("print", buf);
    forwardPrint(v, buf);
}

void captureError(HSQUIRRELVM v, const SQChar* s, ...) {
    va_list args;
    va_start(args, s);
    char buf[1024];
    vsnprintf(buf, sizeof(buf), s ? s : "", args);
    va_end(args);
    ConsolePanel::instance().addLog("error", buf);
    forwardError(v, buf);
}

std::string typeName(HSQUIRRELVM vm, SQInteger idx) {
    switch (sq_gettype(vm, idx)) {
        case OT_NULL: return "null";
        case OT_INTEGER: return "integer";
        case OT_FLOAT: return "float";
        case OT_BOOL: return "bool";
        case OT_STRING: return "string";
        case OT_TABLE: return "table";
        case OT_ARRAY: return "array";
        case OT_CLOSURE: return "closure";
        case OT_NATIVECLOSURE: return "native";
        case OT_USERDATA: return "userdata";
        case OT_CLASS: return "class";
        case OT_INSTANCE: return "instance";
        case OT_THREAD: return "thread";
        case OT_GENERATOR: return "generator";
        case OT_WEAKREF: return "weakref";
        default: return "other";
    }
}

std::string formatValue(HSQUIRRELVM vm, SQInteger idx) {
    switch (sq_gettype(vm, idx)) {
        case OT_NULL: return "null";
        case OT_BOOL: {
            SQBool b = SQFalse;
            sq_getbool(vm, idx, &b);
            return b ? "true" : "false";
        }
        case OT_INTEGER: {
            SQInteger v = 0;
            sq_getinteger(vm, idx, &v);
            return std::to_string(static_cast<long long>(v));
        }
        case OT_FLOAT: {
            SQFloat v = 0;
            sq_getfloat(vm, idx, &v);
            std::ostringstream oss;
            oss << static_cast<double>(v);
            return oss.str();
        }
        case OT_STRING: {
            const SQChar* s = nullptr;
            sq_getstring(vm, idx, &s);
            return std::string("\"") + (s ? s : "") + "\"";
        }
        case OT_TABLE: return "<table>";
        case OT_ARRAY: return "<array>";
        case OT_CLOSURE: return "<closure>";
        case OT_NATIVECLOSURE: return "<native>";
        case OT_USERDATA: return "<userdata>";
        case OT_INSTANCE: return "<instance>";
        case OT_CLASS: return "<class>";
        case OT_THREAD: return "<thread>";
        default: return "<" + typeName(vm, idx) + ">";
    }
}

}  // namespace

ConsolePanel& ConsolePanel::instance() {
    // Process-immortal singleton; see devtools/Immortal.hpp.
    return Immortal<ConsolePanel>::get();
}

std::string ConsolePanel::nowStamp() {
    using clock = std::chrono::system_clock;
    const auto t = clock::to_time_t(clock::now());
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
    return buf;
}

void ConsolePanel::setVisible(bool on) {
    std::lock_guard<std::mutex> lock(mu_);
    visible_ = on;
}

bool ConsolePanel::isVisible() const {
    std::lock_guard<std::mutex> lock(mu_);
    return visible_;
}

void ConsolePanel::toggleVisible() { setVisible(!isVisible()); }

void ConsolePanel::addLog(std::string level, std::string text) {
    std::lock_guard<std::mutex> lock(mu_);
    ConsoleLine line;
    line.timestamp = nowStamp();
    line.level     = std::move(level);
    line.text      = std::move(text);
    log_.push_back(std::move(line));
    while (log_.size() > maxEntries_) log_.pop_front();
}

void ConsolePanel::addInfo(std::string text) { addLog("info", std::move(text)); }

void ConsolePanel::addWarn(std::string text) { addLog("warn", std::move(text)); }

void ConsolePanel::addError(std::string text) { addLog("error", std::move(text)); }

void ConsolePanel::clear() {
    std::lock_guard<std::mutex> lock(mu_);
    log_.clear();
}

void ConsolePanel::setMaxEntries(size_t n) {
    std::lock_guard<std::mutex> lock(mu_);
    maxEntries_ = n == 0 ? 1 : n;
    while (log_.size() > maxEntries_) log_.pop_front();
}

std::vector<ConsoleLine> ConsolePanel::recent(size_t max) const {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<ConsoleLine> out;
    if (log_.empty() || max == 0) return out;
    const size_t start = log_.size() > max ? log_.size() - max : 0;
    out.assign(log_.begin() + static_cast<std::ptrdiff_t>(start), log_.end());
    return out;
}

std::string ConsolePanel::format(size_t max) const {
    auto lines = recent(max);
    std::ostringstream oss;
    for (const auto& l : lines) {
        oss << '[' << l.timestamp << "] " << l.level << " | " << l.text << '\n';
    }
    return oss.str();
}

void ConsolePanel::attach(HSQUIRRELVM vm) {
    if (!vm) return;
    detach();
    vm_ = vm;
    // Snapshot existing handlers once (they may be the stdlib defaults).
    if (!g_prevPrint) g_prevPrint = sq_getprintfunc(vm);
    if (!g_prevError) g_prevError = sq_geterrorfunc(vm);
    sq_setprintfunc(vm, capturePrint, captureError);
    addLog("info", "console attached to VM");
}

void ConsolePanel::detach() {
    if (!vm_) return;
    sq_setprintfunc(vm_, g_prevPrint, g_prevError);
    vm_ = nullptr;
    addLog("info", "console detached from VM");
}

std::string ConsolePanel::eval(const std::string& expression) {
    if (expression.empty()) return "error: empty expression";
    HSQUIRRELVM vm = vm_;
    if (!vm) return "error: no VM attached";
    addLog("cmd", expression);

    const SQInteger top = sq_gettop(vm);
    // Compile `return (expr);` so the evaluation result lands on the stack.
    const std::string source = "return (" + expression + ");";
    if (SQ_FAILED(eve::script::ScriptCompiler::compileBuffer(
            vm, source.c_str(), static_cast<SQInteger>(source.size()), _SC("console_repl.nut"), SQTrue))) {
        sq_settop(vm, top);
        const eve::script::ScriptErrorContext ctx = eve::script::captureCompileError(vm);
        const std::string err = "error: " +
                                (ctx.empty() ? std::string("compile failed")
                                             : eve::script::formatScriptError(ctx));
        addLog("error", err);
        return err;
    }
    sq_pushroottable(vm);
    if (SQ_FAILED(sq_call(vm, 1, SQTrue, SQTrue))) {
        sq_settop(vm, top);
        eve::script::ScriptErrorContext ctx = eve::script::takeLastScriptError(vm);
        const std::string err = "error: " +
                                (ctx.empty() ? std::string("runtime failed")
                                             : eve::script::formatScriptError(ctx));
        addLog("error", err);
        return err;
    }
    std::string result = formatValue(vm, -1);
    sq_settop(vm, top);
    addLog("result", result);
    return result;
}

void ConsolePanel::setImGuiDrawer(ImGuiDrawer fn) { g_imguiDrawer = fn; }

void ConsolePanel::drawImGui() {
    // ImGui UI lives in eve_imgui (ImGuiBackend) so EVDevTools does not
    // instantiate imgui.h inlines — that blew the MSVC 65535 export limit.
    if (g_imguiDrawer) g_imguiDrawer(*this);
}

}  // namespace eve::dev
