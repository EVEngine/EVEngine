#pragma once

/** @brief 供 building 模块内多个 *.cpp 共用的小型 JSON 读取辅助。 */

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::building::json_helpers {

inline double asDouble(const Poco::Dynamic::Var &v, double fallback) {
    try {
        if (v.isEmpty()) return fallback;
        return v.convert<double>();
    } catch (...) {
    }
    return fallback;
}

inline int asInt(const Poco::Dynamic::Var &v, int fallback) {
    try {
        if (v.isEmpty()) return fallback;
        return v.convert<int>();
    } catch (...) {
    }
    return fallback;
}

inline float asFloat(const Poco::Dynamic::Var &v, float fallback) {
    return static_cast<float>(asDouble(v, double(fallback)));
}

inline std::string asString(const Poco::Dynamic::Var &v, const std::string &fallback = {}) {
    try {
        if (v.isEmpty()) return fallback;
        return v.convert<std::string>();
    } catch (...) {
    }
    return fallback;
}

inline std::vector<std::string> asStringArray(Poco::JSON::Object::Ptr o, const char *key) {
    std::vector<std::string> out;
    if (!o || !o->has(key)) return out;
    try {
        auto arr = o->getArray(key);
        if (!arr) return out;
        out.reserve(arr->size());
        for (size_t i = 0; i < arr->size(); ++i) out.push_back(asString(arr->get(i)));
    } catch (...) {
    }
    return out;
}

inline std::vector<int> asIntArray(Poco::JSON::Object::Ptr o, const char *key) {
    std::vector<int> out;
    if (!o || !o->has(key)) return out;
    try {
        auto arr = o->getArray(key);
        if (!arr) return out;
        out.reserve(arr->size());
        for (size_t i = 0; i < arr->size(); ++i) out.push_back(asInt(arr->get(i), 0));
    } catch (...) {
    }
    return out;
}

inline std::unordered_map<std::string, std::string> asStringMap(Poco::JSON::Object::Ptr o,
                                                               const char *key) {
    std::unordered_map<std::string, std::string> out;
    if (!o || !o->has(key)) return out;
    try {
        auto child = o->getObject(key);
        if (!child) return out;
        std::vector<std::string> names;
        child->getNames(names);
        for (const auto &n : names) out[n] = asString(child->get(n));
    } catch (...) {
    }
    return out;
}

inline std::unordered_map<std::string, int> asIntMap(Poco::JSON::Object::Ptr o, const char *key) {
    std::unordered_map<std::string, int> out;
    if (!o || !o->has(key)) return out;
    try {
        auto child = o->getObject(key);
        if (!child) return out;
        std::vector<std::string> names;
        child->getNames(names);
        for (const auto &n : names) out[n] = asInt(child->get(n), 0);
    } catch (...) {
    }
    return out;
}

}  // namespace eve::building::json_helpers
