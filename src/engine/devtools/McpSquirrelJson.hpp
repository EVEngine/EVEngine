#pragma once

// Squirrel -> compact JSON conversion shared by the embedded MCP server and the
// script tool registry. Kept in one place so a value produced by a scene kit and
// a value returned by a project tool serialize identically.

#include "devtools/McpJson.hpp"

#include <squirrel.h>

#include <string>

namespace eve::dev {

/**
 * @brief Maximum container nesting serialized from a script value.
 *
 * Script tables can be cyclic (`a.b = a`), and an untrusted or generated table
 * would otherwise recurse until the stack dies. Beyond this depth containers are
 * emitted as null, which keeps the produced document valid JSON.
 */
constexpr int kSqJsonMaxDepth = 16;

/** @brief Forward declaration: the key serializer falls back to the value serializer. */
inline std::string sqValueToJson(HSQUIRRELVM vm, SQInteger idx, int depth = 0);

/**
 * @brief Serialize a Squirrel table key as a JSON string literal.
 *
 * JSON object keys are always strings; a numeric or boolean key would otherwise
 * produce a document no parser accepts, and an opaque key (closure, instance)
 * must not silently become `null` and lose the entry's identity.
 */
inline std::string sqKeyToJson(HSQUIRRELVM vm, SQInteger idx) {
    if (idx < 0) idx = sq_gettop(vm) + idx + 1;
    if (sq_gettype(vm, idx) == OT_STRING) {
        const SQChar* value = nullptr;
        sq_getstring(vm, idx, &value);
        return std::string("\"") + mcpJsonEscape(value ? value : "") + "\"";
    }
    const std::string text = sqValueToJson(vm, idx, 0);
    if (text.empty() || text.front() != '"') return "\"" + mcpJsonEscape(text) + "\"";
    return text;
}

/**
 * @brief Serialize the Squirrel value at stack index `idx` to compact JSON.
 * @param vm VM owning the value.
 * @param idx Stack index (negative indices are relative to the top).
 * @param depth Current nesting depth; callers pass 0.
 * @return Compact single-line JSON text.
 */
inline std::string sqValueToJson(HSQUIRRELVM vm, SQInteger idx, int depth) {
    if (idx < 0) idx = sq_gettop(vm) + idx + 1;  // normalize relative -> absolute
    if (depth > kSqJsonMaxDepth) return "null";
    switch (sq_gettype(vm, idx)) {
        case OT_NULL: return "null";
        case OT_BOOL: {
            SQBool value = SQFalse;
            sq_getbool(vm, idx, &value);
            return value ? "true" : "false";
        }
        case OT_INTEGER: {
            SQInteger value = 0;
            sq_getinteger(vm, idx, &value);
            return std::to_string(static_cast<long long>(value));
        }
        case OT_FLOAT: {
            SQFloat value = 0;
            sq_getfloat(vm, idx, &value);
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(value));
            return buffer;
        }
        case OT_STRING: {
            const SQChar* value = nullptr;
            sq_getstring(vm, idx, &value);
            return std::string("\"") + mcpJsonEscape(value ? value : "") + "\"";
        }
        case OT_ARRAY: {
            std::string out   = "[";
            bool        first = true;
            sq_pushnull(vm);
            while (SQ_SUCCEEDED(sq_next(vm, idx))) {
                if (!first) out += ",";
                first = false;
                out += sqValueToJson(vm, -1, depth + 1);
                sq_pop(vm, 2);
            }
            sq_pop(vm, 1);
            out += "]";
            return out;
        }
        case OT_TABLE: {
            std::string out   = "{";
            bool        first = true;
            sq_pushnull(vm);
            while (SQ_SUCCEEDED(sq_next(vm, idx))) {
                if (!first) out += ",";
                first = false;
                out += sqKeyToJson(vm, -2);
                out += ":";
                out += sqValueToJson(vm, -1, depth + 1);
                sq_pop(vm, 2);
            }
            sq_pop(vm, 1);
            out += "}";
            return out;
        }
        default: return "\"<unserializable>\"";
    }
}

}  // namespace eve::dev
