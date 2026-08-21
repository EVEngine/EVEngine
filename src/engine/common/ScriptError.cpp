#include "common/ScriptError.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cctype>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace eve::script {
namespace {

thread_local std::unordered_map<HSQUIRRELVM, ScriptErrorContext> g_last_errors;

const char* typeName(SQObjectType type) {
    switch (type) {
        case OT_NULL: return "null";
        case OT_INTEGER: return "integer";
        case OT_FLOAT: return "float";
        case OT_BOOL: return "bool";
        case OT_STRING: return "string";
        case OT_TABLE: return "table";
        case OT_ARRAY: return "array";
        case OT_USERDATA: return "userdata";
        case OT_CLOSURE: return "closure";
        case OT_NATIVECLOSURE: return "native";
        case OT_GENERATOR: return "generator";
        case OT_USERPOINTER: return "userpointer";
        case OT_THREAD: return "thread";
        case OT_CLASS: return "class";
        case OT_INSTANCE: return "instance";
        case OT_WEAKREF: return "weakref";
        default: return "other";
    }
}

// Formats the error value at stack slot 2 (the argument Squirrel passes to the
// runtime error handler). Containers are named by type instead of being
// stringified: sq_tostring can invoke _tostring metamethods, which could raise
// again and recurse into the error handler.
std::string errorValueString(HSQUIRRELVM vm) {
    if (!vm) return "unknown error";
    switch (sq_gettype(vm, 2)) {
        case OT_NULL:
            return "null";
        case OT_BOOL: {
            SQBool value = SQFalse;
            if (SQ_SUCCEEDED(sq_getbool(vm, 2, &value)))
                return value ? "true" : "false";
            break;
        }
        case OT_INTEGER: {
            SQInteger value = 0;
            if (SQ_SUCCEEDED(sq_getinteger(vm, 2, &value)))
                return std::to_string(static_cast<long long>(value));
            break;
        }
        case OT_FLOAT: {
            SQFloat value = 0;
            if (SQ_SUCCEEDED(sq_getfloat(vm, 2, &value))) {
                std::ostringstream out;
                out << value;
                return out.str();
            }
            break;
        }
        case OT_STRING: {
            const SQChar* value = nullptr;
            if (SQ_SUCCEEDED(sq_getstring(vm, 2, &value)) && value) return value;
            break;
        }
        default:
            break;
    }
    return std::string("error value of type ") + typeName(sq_gettype(vm, 2));
}

}  // namespace

ScriptErrorContext captureScriptError(HSQUIRRELVM vm) {
    ScriptErrorContext ctx;
    if (!vm) return ctx;
    ctx.message = errorValueString(vm);
    // Level 0 is the error-handler closure itself; level 1 is the throwing
    // script frame. Walk outward until Squirrel runs out of frames.
    for (int level = 1; level <= 64; ++level) {
        SQStackInfos si;
        if (SQ_FAILED(sq_stackinfos(vm, level, &si))) break;
        ScriptFrame frame;
        frame.source = si.source ? si.source : "";
        frame.function = si.funcname ? si.funcname : "";
        frame.line = static_cast<int>(si.line);
        ctx.stack.push_back(std::move(frame));
    }
    if (!ctx.stack.empty()) {
        ctx.source = ctx.stack.front().source;
        ctx.function = ctx.stack.front().function;
        ctx.line = ctx.stack.front().line;
    }
    return ctx;
}

ScriptErrorContext captureCompileError(HSQUIRRELVM vm) {
    ScriptErrorContext ctx;
    if (!vm) return ctx;
    // ssq::VM stores the last compile error on the VM (foreign pointer) and
    // exposes it through getLastCompileException(). Only call this after a
    // failed compile, otherwise the stored exception may not exist yet.
    auto* machine = reinterpret_cast<ssq::VM*>(sq_getforeignptr(vm));
    if (!machine) return ctx;
    try {
        ctx.message = machine->getLastCompileException().what();
    } catch (...) {
        return ctx;
    }
    parseCompileError(ctx.message, &ctx.source, &ctx.line, &ctx.column, &ctx.message);
    return ctx;
}

bool parseCompileError(const std::string& text, std::string* source, int* line,
                       int* column, std::string* message) {
    constexpr const char* kPrefix = "Compile error at ";
    if (text.rfind(kPrefix, 0) != 0) return false;
    const std::string rest = text.substr(sizeof(kPrefix) - 1);

    // Layout: <source>:<line>:<column> <description>. The source may itself
    // contain ':' (Windows drive letters), so scan for the first ':' after
    // which "<digits>:<digits> " follows — that marks the line/column pair.
    for (size_t i = 0; i < rest.size(); ++i) {
        if (rest[i] != ':') continue;
        size_t lineEnd = i + 1;
        while (lineEnd < rest.size() &&
               std::isdigit(static_cast<unsigned char>(rest[lineEnd])))
            ++lineEnd;
        if (lineEnd == i + 1 || lineEnd >= rest.size() || rest[lineEnd] != ':') continue;
        size_t columnEnd = lineEnd + 1;
        while (columnEnd < rest.size() &&
               std::isdigit(static_cast<unsigned char>(rest[columnEnd])))
            ++columnEnd;
        if (columnEnd == lineEnd + 1 || columnEnd >= rest.size() ||
            rest[columnEnd] != ' ')
            continue;

        try {
            if (source) *source = rest.substr(0, i);
            if (line)
                *line = std::stoi(rest.substr(i + 1, lineEnd - i - 1));
            if (column)
                *column = std::stoi(rest.substr(lineEnd + 1, columnEnd - lineEnd - 1));
            if (message) *message = rest.substr(columnEnd + 1);
        } catch (...) {
            return false;
        }
        return true;
    }
    return false;
}

std::string sourceLineText(const std::string& sourceText, int line) {
    if (line <= 0 || sourceText.empty()) return {};
    size_t start = 0;
    for (int current = 1; current < line; ++current) {
        const size_t newline = sourceText.find('\n', start);
        if (newline == std::string::npos) return {};
        start = newline + 1;
    }
    size_t end = sourceText.find('\n', start);
    std::string text =
        sourceText.substr(start, end == std::string::npos ? std::string::npos
                                                          : end - start);
    if (!text.empty() && text.back() == '\r') text.pop_back();
    return text;
}

std::string formatStackTrace(const std::vector<ScriptFrame>& frames) {
    std::ostringstream out;
    for (const ScriptFrame& frame : frames) {
        out << (frame.source.empty() ? "<unknown>" : frame.source) << ':';
        if (frame.line > 0)
            out << frame.line;
        else
            out << '?';
        if (!frame.function.empty()) out << " in " << frame.function;
        out << '\n';
    }
    return out.str();
}

std::string formatScriptError(const ScriptErrorContext& ctx) {
    std::ostringstream out;
    if (!ctx.source.empty()) {
        out << ctx.source;
        if (ctx.line > 0) out << ':' << ctx.line;
        out << " (" << (ctx.function.empty() ? "<anonymous>" : ctx.function)
            << "): " << ctx.message;
    } else {
        out << ctx.message;
    }
    if (!ctx.hint.empty()) out << "\n  " << ctx.hint;
    if (!ctx.stack.empty()) out << "\nStack:\n" << formatStackTrace(ctx.stack);
    std::string result = out.str();
    if (!result.empty() && result.back() == '\n') result.pop_back();
    return result;
}

void setLastScriptError(HSQUIRRELVM vm, ScriptErrorContext ctx) {
    if (!vm) return;
    g_last_errors[vm] = std::move(ctx);
}

ScriptErrorContext takeLastScriptError(HSQUIRRELVM vm) {
    if (!vm) return {};
    auto found = g_last_errors.find(vm);
    if (found == g_last_errors.end()) return {};
    ScriptErrorContext ctx = std::move(found->second);
    g_last_errors.erase(found);
    return ctx;
}

const ScriptErrorContext* peekLastScriptError(HSQUIRRELVM vm) {
    if (!vm) return nullptr;
    auto found = g_last_errors.find(vm);
    return found == g_last_errors.end() ? nullptr : &found->second;
}

void clearLastScriptError(HSQUIRRELVM vm) {
    if (!vm) return;
    g_last_errors.erase(vm);
}

}  // namespace eve::script
