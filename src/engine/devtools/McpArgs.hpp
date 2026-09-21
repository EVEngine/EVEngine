#pragma once

// Shared `arguments` accessors for the embedded MCP server and its satellite
// tool modules. One copy keeps "missing field" and "wrong type" behaviour
// identical across every tool family: both fall back to the caller's default
// instead of throwing a protocol error at the agent.

#include <Poco/JSON/Object.h>

#include <string>

namespace eve::dev {

/** @brief Raw JSON value of `key`, or an empty Var when absent. */
inline Poco::Dynamic::Var getArgVar(Poco::JSON::Object::Ptr args, const char* key) {
    if (!args || !args->has(key)) return Poco::Dynamic::Var();
    try {
        return args->get(key);
    } catch (...) {
        return Poco::Dynamic::Var();
    }
}

/** @brief String argument, or `def` when absent or not convertible. */
inline std::string getArgString(Poco::JSON::Object::Ptr args, const char* key, const std::string& def = {}) {
    if (!args || !args->has(key)) return def;
    try {
        return args->get(key).convert<std::string>();
    } catch (...) {
        return def;
    }
}

/** @brief Integer argument, or `def` when absent or not convertible. */
inline int getArgInt(Poco::JSON::Object::Ptr args, const char* key, int def = 0) {
    if (!args || !args->has(key)) return def;
    try {
        return args->get(key).convert<int>();
    } catch (...) {
        return def;
    }
}

/** @brief 64-bit integer argument (sequence numbers, handles), or `def`. */
inline long long getArgInt64(Poco::JSON::Object::Ptr args, const char* key, long long def = 0) {
    if (!args || !args->has(key)) return def;
    try {
        return static_cast<long long>(args->get(key).convert<Poco::Int64>());
    } catch (...) {
        return def;
    }
}

/** @brief Floating-point argument, or `def` when absent or not convertible. */
inline float getArgFloat(Poco::JSON::Object::Ptr args, const char* key, float def = 0.f) {
    if (!args || !args->has(key)) return def;
    try {
        return static_cast<float>(args->get(key).convert<double>());
    } catch (...) {
        return def;
    }
}

/** @brief Boolean argument, or `def` when absent or not convertible. */
inline bool getArgBool(Poco::JSON::Object::Ptr args, const char* key, bool def = false) {
    if (!args || !args->has(key)) return def;
    try {
        return args->get(key).convert<bool>();
    } catch (...) {
        return def;
    }
}

}  // namespace eve::dev
