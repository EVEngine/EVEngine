#pragma once

// 供 rpg 模块内多个 *.cpp 共用的小型 JSON 读取辅助（风格与 map/TileConfig.cpp 一致）。

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>

#include <string>
#include <vector>

namespace eve::rpg::json_helpers {

inline double asDouble(const Poco::Dynamic::Var &v, double fallback) {
    try {
        if (v.isEmpty()) return fallback;
        return v.convert<double>();
    } catch (...) {
        return fallback;
    }
}

inline int asInt(const Poco::Dynamic::Var &v, int fallback) {
    try {
        if (v.isEmpty()) return fallback;
        return v.convert<int>();
    } catch (...) {
        return fallback;
    }
}

inline float asFloat(const Poco::Dynamic::Var &v, float fallback) {
    return static_cast<float>(asDouble(v, double(fallback)));
}

inline std::string asString(const Poco::Dynamic::Var &v, const std::string &fallback = {}) {
    try {
        if (v.isEmpty()) return fallback;
        return v.convert<std::string>();
    } catch (...) {
        return fallback;
    }
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

}  // namespace eve::rpg::json_helpers
