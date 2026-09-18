#include "devtools/McpScriptTools.hpp"

#include "devtools/ConsolePanel.hpp"
#include "devtools/McpJson.hpp"
#include "devtools/McpSquirrelJson.hpp"

#include "common/ScriptError.h"

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace eve::dev {
namespace {

// Bounds: a project script is not a trusted schema source, so every field the
// agent receives is capped and every rejection is reported to the console.
constexpr std::size_t kMaxTools         = 128;
constexpr std::size_t kMaxNameLength    = 64;
constexpr std::size_t kMinNameLength    = 3;
constexpr std::size_t kMaxDescription   = 512;
constexpr std::size_t kMaxSchemaBytes   = 16 * 1024;
constexpr std::size_t kMaxResultBytes   = 256 * 1024;
constexpr int         kMaxArgumentDepth = 16;

/** Prefixes owned by the built-in tool tables (McpServer / agent / runtime). */
const char* const kReservedPrefixes[] = {"eve_", "inspect_", "set_", "capture_", "get_"};

struct ScriptTool {
    std::string name;
    std::string description;
    std::string inputSchemaJson;
    HSQUIRRELVM vm = nullptr;
    HSQOBJECT   handler{};
};

std::mutex              g_mutex;
std::vector<ScriptTool> g_tools;

bool hasReservedPrefix(const std::string& name) {
    for (const char* raw : kReservedPrefixes) {
        const std::string prefix(raw);
        if (name.size() > prefix.size() && name.compare(0, prefix.size(), prefix) == 0) return true;
    }
    return false;
}

/**
 * @brief Read a string field of a spec table.
 * @return The field value, or `fallback` when absent or not a string.
 */
std::string tableString(HSQUIRRELVM vm, HSQOBJECT table, const char* key, const std::string& fallback = {}) {
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, table);
    sq_pushstring(vm, key, -1);
    std::string value = fallback;
    if (SQ_SUCCEEDED(sq_get(vm, -2)) && sq_gettype(vm, -1) == OT_STRING) {
        const SQChar* text = nullptr;
        if (SQ_SUCCEEDED(sq_getstring(vm, -1, &text)) && text) value = text;
    }
    sq_settop(vm, top);
    return value;
}

/** Serialize a table field to compact JSON; empty when missing or not a table. */
std::string tableFieldJson(HSQUIRRELVM vm, HSQOBJECT table, const char* field) {
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, table);
    sq_pushstring(vm, field, -1);
    std::string json;
    if (SQ_SUCCEEDED(sq_get(vm, -2)) && sq_gettype(vm, -1) == OT_TABLE) json = sqValueToJson(vm, -1);
    sq_settop(vm, top);
    return json;
}

/** Push `value` as a Squirrel value (table/array/scalar) for a handler argument. */
void pushJsonValue(HSQUIRRELVM vm, const Poco::Dynamic::Var& value, int depth) {
    if (depth > kMaxArgumentDepth || value.isEmpty()) {
        sq_pushnull(vm);
        return;
    }
    if (value.isBoolean()) {
        sq_pushbool(vm, value.convert<bool>() ? SQTrue : SQFalse);
        return;
    }
    if (value.isInteger()) {
        sq_pushinteger(vm, static_cast<SQInteger>(value.convert<Poco::Int64>()));
        return;
    }
    if (value.isNumeric()) {
        sq_pushfloat(vm, static_cast<SQFloat>(value.convert<double>()));
        return;
    }
    if (value.isString()) {
        const std::string text = value.convert<std::string>();
        sq_pushstring(vm, text.c_str(), static_cast<SQInteger>(text.size()));
        return;
    }
    if (value.type() == typeid(Poco::JSON::Array::Ptr)) {
        auto array = value.extract<Poco::JSON::Array::Ptr>();
        sq_newarray(vm, 0);
        const SQInteger target = sq_gettop(vm);
        for (unsigned index = 0; index < array->size(); ++index) {
            pushJsonValue(vm, array->get(index), depth + 1);
            sq_arrayappend(vm, target);
        }
        return;
    }
    if (value.type() == typeid(Poco::JSON::Object::Ptr)) {
        auto object = value.extract<Poco::JSON::Object::Ptr>();
        sq_newtable(vm);
        const SQInteger target = sq_gettop(vm);
        for (const auto& entry : *object) {
            sq_pushstring(vm, entry.first.c_str(), static_cast<SQInteger>(entry.first.size()));
            pushJsonValue(vm, entry.second, depth + 1);
            sq_newslot(vm, target, SQFalse);
        }
        return;
    }
    sq_pushnull(vm);
}

std::string scriptErrorText(HSQUIRRELVM vm) {
    const eve::script::ScriptErrorContext context = eve::script::takeLastScriptError(vm);
    if (!context.empty()) return eve::script::formatScriptError(context);
    return "tool handler failed";
}

void reportRejection(const std::string& name, const std::string& reason) {
    ConsolePanel::instance().addLog("warn", "eve.mcp.tool rejected '" + name + "': " + reason);
}

/**
 * @brief Drop one entry's handler reference.
 *
 * Only ever releases against `owner`: an entry belonging to another VM is left
 * untouched because that VM may already be destroyed.
 */
void releaseEntry(ScriptTool& tool, HSQUIRRELVM owner) {
    if (tool.vm != owner || owner == nullptr || sq_isnull(tool.handler)) return;
    sq_release(owner, &tool.handler);
}

std::string resultEnvelope(const std::string& text, bool isError) { return textContentResult(text, isError); }

/** Register (or replace) one tool. Returns true when the registry changed. */
bool registerTool(HSQUIRRELVM vm, const std::string& name, HSQOBJECT spec) {
    if (!isValidScriptToolName(name)) {
        reportRejection(name,
                        "name must match [a-z][a-z0-9_]{2,63} and must not use a built-in prefix "
                        "(eve_ / inspect_ / set_ / capture_ / get_)");
        return false;
    }
    if (spec._type != OT_TABLE) {
        reportRejection(name, "spec must be a table with a handler closure");
        return false;
    }

    const std::string description = tableString(vm, spec, "description");
    if (description.size() > kMaxDescription) {
        reportRejection(name, "description is longer than " + std::to_string(kMaxDescription) + " characters");
        return false;
    }

    const std::string schema = tableFieldJson(vm, spec, "inputSchema");
    if (schema.size() > kMaxSchemaBytes) {
        reportRejection(name, "inputSchema serializes to more than " + std::to_string(kMaxSchemaBytes) + " bytes");
        return false;
    }
    if (!schema.empty() && schema.front() != '{') {
        reportRejection(name, "inputSchema must be a table describing a JSON object");
        return false;
    }

    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, spec);
    sq_pushstring(vm, "handler", -1);
    const bool hasHandler =
        SQ_SUCCEEDED(sq_get(vm, -2)) && (sq_gettype(vm, -1) == OT_CLOSURE || sq_gettype(vm, -1) == OT_NATIVECLOSURE);
    if (!hasHandler) {
        sq_settop(vm, top);
        reportRejection(name, "spec.handler must be a function");
        return false;
    }
    HSQOBJECT handler = {};
    sq_getstackobj(vm, -1, &handler);
    sq_addref(vm, &handler);
    sq_settop(vm, top);

    ScriptTool tool;
    tool.name            = name;
    tool.description     = description.empty() ? ("Project tool '" + name + "'") : description;
    tool.inputSchemaJson = schema;
    tool.vm              = vm;
    tool.handler         = handler;

    bool full = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const auto                  existing =
            std::find_if(g_tools.begin(), g_tools.end(), [&](const ScriptTool& entry) { return entry.name == name; });
        if (existing != g_tools.end()) {
            // Re-registration replaces: a hot-reloaded mcp.nut must not
            // accumulate duplicates or leak the previous closure.
            releaseEntry(*existing, vm);
            *existing = std::move(tool);
        } else if (g_tools.size() >= kMaxTools) {
            full = true;
        } else {
            g_tools.push_back(std::move(tool));
        }
    }
    if (full) {
        // push_back did not happen, so `tool` still owns the reference.
        releaseEntry(tool, vm);
        reportRejection(name, "registry is full (" + std::to_string(kMaxTools) + " tools)");
        return false;
    }
    ConsolePanel::instance().addLog("info", "eve.mcp.tool registered: " + name);
    return true;
}

}  // namespace

bool isValidScriptToolName(const std::string& name) {
    if (name.size() < kMinNameLength || name.size() > kMaxNameLength) return false;
    if (name.front() < 'a' || name.front() > 'z') return false;
    for (char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }
    return !hasReservedPrefix(name);
}

bool isScriptTool(const std::string& name) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return std::any_of(g_tools.begin(), g_tools.end(), [&](const ScriptTool& tool) { return tool.name == name; });
}

std::vector<std::string> scriptToolNames() {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::vector<std::string>    names;
    names.reserve(g_tools.size());
    for (const auto& tool : g_tools) names.push_back(tool.name);
    return names;
}

int scriptToolCount() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return static_cast<int>(g_tools.size());
}

std::string scriptToolSchemas() {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::string                 out;
    for (const auto& tool : g_tools) {
        if (!out.empty()) out += ',';
        out += "{\"name\":\"" + mcpJsonEscape(tool.name) + "\",\"description\":\"" + mcpJsonEscape(tool.description) +
               "\",\"inputSchema\":";
        out += tool.inputSchemaJson.empty() ? std::string("{\"type\":\"object\"}") : tool.inputSchemaJson;
        out += "}";
    }
    return out;
}

std::string callScriptTool(const std::string& name, Poco::JSON::Object::Ptr args) {
    HSQUIRRELVM vm      = nullptr;
    HSQOBJECT   handler = {};
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const auto                  found =
            std::find_if(g_tools.begin(), g_tools.end(), [&](const ScriptTool& tool) { return tool.name == name; });
        if (found == g_tools.end()) return resultEnvelope("error: unknown tool " + name, true);
        vm      = found->vm;
        handler = found->handler;
        // Hold our own reference across the call: the handler may unregister or
        // clear the registry, and the closure must stay alive while it runs.
        if (vm && !sq_isnull(handler)) sq_addref(vm, &handler);
    }
    if (!vm || sq_isnull(handler)) {
        if (vm && !sq_isnull(handler)) sq_release(vm, &handler);
        return resultEnvelope("error: tool '" + name + "' has no handler in this VM", true);
    }

    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, handler);
    sq_pushroottable(vm);
    if (args) {
        pushJsonValue(vm, Poco::Dynamic::Var(args), 0);
    } else {
        sq_newtable(vm);
    }

    std::string text;
    bool        isError = false;
    if (SQ_FAILED(sq_call(vm, 2, SQTrue, SQTrue))) {
        text    = "error: " + scriptErrorText(vm);
        isError = true;
    } else {
        text = sqValueToJson(vm, -1);
        if (text.size() > kMaxResultBytes) {
            text = "error: tool '" + name + "' returned " + std::to_string(text.size()) + " bytes of JSON, above the " +
                   std::to_string(kMaxResultBytes) + " byte limit";
            isError = true;
        }
    }
    sq_settop(vm, top);
    sq_release(vm, &handler);
    return resultEnvelope(text, isError);
}

void clearScriptTools(HSQUIRRELVM owner) {
    std::vector<ScriptTool> dropped;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        dropped.swap(g_tools);
    }
    for (auto& tool : dropped) releaseEntry(tool, owner);
}

void exposeMcpScriptApi(ssq::VM& vm) {
    HSQUIRRELVM raw = vm.getHandle();
    if (!raw) return;

    try {
        ssq::Table eveTable = vm.find("eve").toTable();
        ssq::Table mcp      = eveTable.addTable("mcp");

        mcp.addFunc("tool", [raw](const std::string& name, ssq::Object spec) -> bool {
            return registerTool(raw, name, spec.getRaw());
        });

        mcp.addFunc("remove", [raw](const std::string& name) -> bool {
            bool removed = false;
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                const auto found = std::find_if(g_tools.begin(), g_tools.end(),
                                                [&](const ScriptTool& entry) { return entry.name == name; });
                if (found != g_tools.end()) {
                    releaseEntry(*found, raw);
                    g_tools.erase(found);
                    removed = true;
                }
            }
            if (removed) ConsolePanel::instance().addLog("info", "eve.mcp.tool removed: " + name);
            return removed;
        });

        mcp.addFunc("tools", []() { return scriptToolNames(); });

        mcp.addFunc("clear", [raw]() -> int {
            const int count = scriptToolCount();
            clearScriptTools(raw);
            ConsolePanel::instance().addLog("info", "eve.mcp.tool cleared " + std::to_string(count) + " tool(s)");
            return count;
        });
    } catch (...) {
        // `eve` table missing: leave the MCP export API unavailable rather than
        // failing the whole DevTools script surface.
    }
}

}  // namespace eve::dev
