#pragma once
#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include "common/Result.h"
#include "devtools/McpJson.hpp"

namespace eve::dev {
/** @brief Encode owning JSON data as a Squirrel literal without losing pointer containers or precision.
 * @param value Parsed JSON value. @param depth Internal recursion depth, limited to 16.
 * @return Owning literal or InvalidArgument for unsupported/nonfinite/deep input.
 * @thread Calling thread; no VM access, callbacks or retained references.
 */
[[nodiscard]] inline Result<std::string> sceneLiteral(const Poco::Dynamic::Var& value, int depth = 0) {
    const auto reject = [] {
        return Result<std::string>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Scene parameters require finite JSON values with depth at most 16",
            "devtools.scene.parameters"));
    };
    if (depth > 16) return reject();
    if (value.isEmpty()) return Result<std::string>::success("null");
    if (value.isBoolean()) return Result<std::string>::success(value.convert<bool>() ? "true" : "false");
    if (value.isInteger()) return Result<std::string>::success(std::to_string(value.convert<Poco::Int64>()));
    if (value.isNumeric()) {
        const double number = value.convert<double>();
        if (!std::isfinite(number)) return reject();
        std::ostringstream stream;
        stream.imbue(std::locale::classic());
        stream << std::setprecision(std::numeric_limits<double>::max_digits10) << number;
        auto text = stream.str();
        if (text.find_first_of(".eE") == std::string::npos) text += ".0";
        return Result<std::string>::success(std::move(text));
    }
    if (value.isString())
        return Result<std::string>::success("\"" + mcpJsonEscape(value.convert<std::string>()) + "\"");
    if (value.type() == typeid(Poco::JSON::Array::Ptr)) {
        const auto array = value.extract<Poco::JSON::Array::Ptr>();
        if (!array) return reject();
        std::string text = "[";
        for (std::size_t i = 0; i < array->size(); ++i) {
            auto item = sceneLiteral(array->get(static_cast<unsigned int>(i)), depth + 1);
            if (!item.ok()) return Result<std::string>::failure(item.status());
            if (i) text += ",";
            text += item.value();
        }
        return Result<std::string>::success(text + "]");
    }
    if (value.type() == typeid(Poco::JSON::Object::Ptr)) {
        const auto object = value.extract<Poco::JSON::Object::Ptr>();
        if (!object) return reject();
        std::string text  = "{";
        bool        first = true;
        for (const auto& [key, value] : *object) {
            auto item = sceneLiteral(value, depth + 1);
            if (!item.ok()) return Result<std::string>::failure(item.status());
            if (!first) text += ",";
            first = false;
            text += "[\"" + mcpJsonEscape(key) + "\"]=" + item.value();
        }
        return Result<std::string>::success(text + "}");
    }
    return reject();
}
}  // namespace eve::dev
