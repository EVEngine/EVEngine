#include "devtools/ConsolePanel.hpp"
#include "devtools/Immortal.hpp"
#include "devtools/StderrCapture.hpp"

#include "common/CrashLog.h"
#include "common/ScriptCompiler.h"
#include "common/ScriptError.h"

#include <simplesquirrel/simplesquirrel.hpp>
#include <squirrel.h>

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

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

// One console line has to survive a full debug dump: the previous fixed 1 KiB
// buffer silently cut long prints, and truncated output is exactly what an
// unattended agent cannot afford to miss. Over-long lines are now truncated
// explicitly, and the marker says how much was dropped.
constexpr std::size_t kMaxCapturedLine = 64 * 1024;

std::string formatCaptured(const SQChar* s, va_list args) {
    const char* format = s ? s : "";
    va_list     probe;
    va_copy(probe, args);
    const int needed = std::vsnprintf(nullptr, 0, format, probe);
    va_end(probe);
    if (needed < 0) return std::string(format);

    const std::size_t size = static_cast<std::size_t>(needed);
    const std::size_t keep = std::min(size, kMaxCapturedLine);
    std::vector<char> buffer(keep + 1, '\0');
    std::vsnprintf(buffer.data(), buffer.size(), format, args);
    std::string text(buffer.data(), keep);
    if (size > keep) {
        text += "\n[console: truncated " + std::to_string(size - keep) + " of " + std::to_string(size) + " bytes]";
    }
    return text;
}

// Squirrel print callback (varargs, printf-style). Captured into the console
// log, then forwarded to the previous handler so stdout/stderr behavior stays.
void capturePrint(HSQUIRRELVM v, const SQChar* s, ...) {
    va_list args;
    va_start(args, s);
    const std::string text = formatCaptured(s, args);
    va_end(args);
    ConsolePanel::instance().addLog("print", text);
    forwardPrint(v, text.c_str());
}

void captureError(HSQUIRRELVM v, const SQChar* s, ...) {
    va_list args;
    va_start(args, s);
    const std::string text = formatCaptured(s, args);
    va_end(args);
    ConsolePanel::instance().addLog("error", text);
    forwardError(v, text.c_str());
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
    appendLocked(std::move(level), std::move(text));
}

void ConsolePanel::appendLocked(std::string level, std::string text) {
    ConsoleLine line;
    line.seq       = nextSeq_++;
    line.timestamp = nowStamp();
    line.level     = level;
    line.text      = text;
    log_.push_back(std::move(line));
    while (log_.size() > maxEntries_) {
        droppedThrough_ = log_.front().seq;
        log_.pop_front();
    }
    // Errors also go to the persistent crash/error log: after a crash the MCP
    // session is gone, and eve.log is the only evidence of what went wrong.
    if (level == "error") eve::recordLogEvent("error", text);
}

void ConsolePanel::addEngineLine(const std::string& text) {
    if (text.empty()) return;
    std::lock_guard<std::mutex> lock(mu_);
    // capturePrint/captureError forward to stderr, so the descriptor-level
    // capture sees the same text a moment later. Drop only that echo: a genuine
    // repeat emitted by the engine itself keeps its own entry.
    if (!log_.empty()) {
        const ConsoleLine& previous = log_.back();
        if (previous.text == text && (previous.level == "print" || previous.level == "error")) return;
    }
    appendLocked("engine", text);
}

bool ConsolePanel::isCapturingStderr() const { return stderrCaptureActive(); }

void ConsolePanel::addInfo(std::string text) { addLog("info", std::move(text)); }

void ConsolePanel::addWarn(std::string text) { addLog("warn", std::move(text)); }

void ConsolePanel::addError(std::string text) { addLog("error", std::move(text)); }

void ConsolePanel::clear() {
    std::lock_guard<std::mutex> lock(mu_);
    // Dropping lines must advance the drop cursor, never the sequence cursor:
    // an existing reader cursor stays meaningful and cannot re-read or stall.
    droppedThrough_ = nextSeq_ == 0 ? 0 : nextSeq_ - 1;
    log_.clear();
}

void ConsolePanel::setMaxEntries(size_t n) {
    std::lock_guard<std::mutex> lock(mu_);
    maxEntries_ = n == 0 ? 1 : n;
    while (log_.size() > maxEntries_) {
        droppedThrough_ = log_.front().seq;
        log_.pop_front();
    }
}

std::uint64_t ConsolePanel::nextSeq() const {
    std::lock_guard<std::mutex> lock(mu_);
    return nextSeq_;
}

std::uint64_t ConsolePanel::droppedThrough() const {
    std::lock_guard<std::mutex> lock(mu_);
    return droppedThrough_;
}

ConsoleSlice ConsolePanel::read(std::uint64_t afterSeq, size_t max, const std::string& level) const {
    std::lock_guard<std::mutex> lock(mu_);

    ConsoleSlice slice;
    slice.nextSeq        = nextSeq_;
    slice.droppedThrough = droppedThrough_;
    slice.firstSeq       = log_.empty() ? nextSeq_ : log_.front().seq;

    const size_t want = max == 0 ? 64 : max;
    if (afterSeq == 0) {
        // Tail read: newest matching lines, returned oldest first.
        slice.truncated = droppedThrough_ > 0;
        for (auto it = log_.rbegin(); it != log_.rend() && slice.lines.size() < want; ++it) {
            if (!level.empty() && it->level != level) continue;
            slice.lines.push_back(*it);
        }
        std::reverse(slice.lines.begin(), slice.lines.end());
    } else {
        slice.truncated = afterSeq < droppedThrough_;
        for (const auto& line : log_) {
            if (line.seq <= afterSeq) continue;
            if (!level.empty() && line.level != level) continue;
            if (slice.lines.size() >= want) break;
            slice.lines.push_back(line);
        }
    }
    // The resumable cursor is the last line actually covered by this read. It is
    // deliberately not `nextSeq`: that is the seq the *next* appended line will
    // receive, and paging from it would skip exactly one line.
    slice.cursor = slice.lines.empty() ? afterSeq : slice.lines.back().seq;
    return slice;
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
    // Engine-side diagnostics (std::cerr and fprintf(stderr)) are the other half
    // of a scripted game's output; mirror them into the same ordered stream.
    const StderrCaptureStatus capture =
        startStderrCapture([](const std::string& line) { ConsolePanel::instance().addEngineLine(line); });
    // A handler-less capture must be observable, not silent: an agent that sees
    // no `engine` lines has to know the coverage is missing rather than assume
    // the engine printed nothing.
    if (capture != StderrCaptureStatus::Active) {
        addLog("warn", "engine stderr capture unavailable; only script print/error reaches this console");
    }
}

void ConsolePanel::detach() {
    stopStderrCapture();
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
