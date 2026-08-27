#include "procgen/Params.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <locale>
#include <optional>
#include <sstream>
#include <string_view>

namespace eve::procgen {
namespace {

constexpr std::string_view kSeedKey   = "seed";
constexpr std::string_view kWidthKey  = "width";
constexpr std::string_view kHeightKey = "height";

void appendLength(std::string& output, std::string_view value) {
    output += std::to_string(value.size());
    output.push_back(':');
    output.append(value.data(), value.size());
}

std::string doubleToken(double value) {
    if (std::isnan(value)) return "nan";
    if (std::isinf(value)) return std::signbit(value) ? "-inf" : "+inf";

    char buffer[64];
    const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), value,
                                         std::chars_format::general,
                                         std::numeric_limits<double>::max_digits10);
    if (converted.ec == std::errc{}) return std::string(buffer, converted.ptr);

    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream.precision(std::numeric_limits<double>::max_digits10);
    stream << value;
    return stream.str();
}

std::string canonicalValue(const eve::Value& value) {
    switch (value.type()) {
    case eve::Value::Type::Null:
        return "null";
    case eve::Value::Type::Bool:
        return std::string("bool:") + (value.asBool() ? "1" : "0");
    case eve::Value::Type::Int64:
        return "int:" + std::to_string(value.asInt());
    case eve::Value::Type::Double:
        return "double:" + doubleToken(value.asDouble());
    case eve::Value::Type::String: {
        const std::string& text = value.asString();
        std::string result      = "string:";
        appendLength(result, text);
        return result;
    }
    case eve::Value::Type::Array: {
        std::string result = "array:";
        const auto* array  = value.getIf<eve::Value::Array>();
        result += std::to_string(array ? array->size() : 0);
        result.push_back(':');
        if (array) {
            for (const eve::Value& element : *array) {
                const std::string encoded = canonicalValue(element);
                appendLength(result, encoded);
            }
        }
        return result;
    }
    case eve::Value::Type::Object: {
        std::string result = "object:";
        const auto* object = value.getIf<eve::Value::Object>();
        result += std::to_string(object ? object->size() : 0);
        result.push_back(':');
        if (object) {
            for (const auto& [key, member] : *object) {
                appendLength(result, key);
                const std::string encoded = canonicalValue(member);
                appendLength(result, encoded);
            }
        }
        return result;
    }
    }
    return "null";
}

const eve::Value* findValue(const eve::Value::Object& values, const std::string& key) noexcept {
    const auto it = values.find(key);
    return it == values.end() ? nullptr : &it->second;
}

std::optional<int> asInt(const eve::Value& value) {
    if (const auto* integer = value.getIf<std::int64_t>()) {
        if (*integer < std::numeric_limits<int>::lowest() ||
            *integer > std::numeric_limits<int>::max())
            return std::nullopt;
        return static_cast<int>(*integer);
    }
    if (const auto* boolean = value.getIf<bool>()) return *boolean ? 1 : 0;
    if (const auto* number = value.getIf<double>()) {
        if (!std::isfinite(*number) || std::trunc(*number) != *number ||
            *number < static_cast<double>(std::numeric_limits<int>::lowest()) ||
            *number > static_cast<double>(std::numeric_limits<int>::max()))
            return std::nullopt;
        return static_cast<int>(*number);
    }
    return std::nullopt;
}

std::optional<float> asFloat(const eve::Value& value) {
    if (const auto* boolean = value.getIf<bool>()) return *boolean ? 1.f : 0.f;
    if (const auto* integer = value.getIf<std::int64_t>()) {
        const float converted = static_cast<float>(*integer);
        return std::isfinite(converted) ? std::optional<float>(converted) : std::nullopt;
    }
    if (const auto* number = value.getIf<double>()) {
        if (!std::isfinite(*number)) return std::nullopt;
        const float converted = static_cast<float>(*number);
        return std::isfinite(converted) ? std::optional<float>(converted) : std::nullopt;
    }
    return std::nullopt;
}

std::optional<bool> asBool(const eve::Value& value) {
    if (const auto* boolean = value.getIf<bool>()) return *boolean;
    if (const auto* integer = value.getIf<std::int64_t>()) {
        if (*integer == 0) return false;
        if (*integer == 1) return true;
        return std::nullopt;
    }
    if (const auto* number = value.getIf<double>()) {
        if (!std::isfinite(*number)) return std::nullopt;
        if (*number == 0.0) return false;
        if (*number == 1.0) return true;
    }
    return std::nullopt;
}

void appendField(std::string& output, std::string_view domain, std::string_view key,
                 const eve::Value& value) {
    appendLength(output, domain);
    appendLength(output, key);
    const std::string encoded = canonicalValue(value);
    appendLength(output, encoded);
    output.push_back(';');
}

}  // namespace

void Params::setSeed(uint32_t seed) { seed_ = seed; }
uint32_t Params::getSeed() const { return seed_; }

void Params::setSize(int width, int height) {
    width_  = width > 0 ? width : 1;
    height_ = height > 0 ? height : 1;
}
int Params::getWidth() const { return width_; }
int Params::getHeight() const { return height_; }

void Params::setInt(const std::string& key, int value) {
    if (key == kSeedKey) {
        seed_ = static_cast<uint32_t>(std::max(0, value));
        return;
    }
    if (key == kWidthKey) {
        width_ = value > 0 ? value : 1;
        return;
    }
    if (key == kHeightKey) {
        height_ = value > 0 ? value : 1;
        return;
    }
    values_[key] = eve::Value(static_cast<std::int64_t>(value));
}

void Params::setFloat(const std::string& key, float value) {
    values_[key] = eve::Value(static_cast<double>(value));
}

void Params::setBool(const std::string& key, bool value) { values_[key] = eve::Value(value); }

void Params::setString(const std::string& key, const std::string& value) {
    values_[key] = eve::Value(value);
}

bool Params::has(const std::string& key) const {
    if (key == kSeedKey || key == kWidthKey || key == kHeightKey) return true;
    return values_.find(key) != values_.end();
}

int Params::getInt(const std::string& key, int defaultValue) const {
    if (key == kSeedKey) {
        return seed_ <= static_cast<uint32_t>(std::numeric_limits<int>::max())
                   ? static_cast<int>(seed_)
                   : defaultValue;
    }
    if (key == kWidthKey) return width_;
    if (key == kHeightKey) return height_;
    const eve::Value* value = findValue(values_, key);
    if (!value) return defaultValue;
    const auto converted = asInt(*value);
    return converted ? *converted : defaultValue;
}

float Params::getFloat(const std::string& key, float defaultValue) const {
    const eve::Value* value = findValue(values_, key);
    if (!value) return defaultValue;
    const auto converted = asFloat(*value);
    return converted ? *converted : defaultValue;
}

bool Params::getBool(const std::string& key, bool defaultValue) const {
    const eve::Value* value = findValue(values_, key);
    if (!value) return defaultValue;
    const auto converted = asBool(*value);
    return converted ? *converted : defaultValue;
}

std::string Params::getString(const std::string& key, const std::string& defaultValue) const {
    const eve::Value* value = findValue(values_, key);
    if (!value) return defaultValue;
    const auto* text = value->getIf<std::string>();
    return text ? *text : defaultValue;
}

std::string Params::canonicalString() const {
    std::string result = "params:v2;";
    // Dimensions live in their own domain so an algorithm parameter named
    // "width" or "height" cannot collide with setSize().
    appendField(result, "dimension", kHeightKey, eve::Value(static_cast<std::int64_t>(height_)));
    appendField(result, "dimension", kSeedKey, eve::Value(static_cast<std::int64_t>(seed_)));
    appendField(result, "dimension", kWidthKey, eve::Value(static_cast<std::int64_t>(width_)));
    for (const auto& [key, value] : values_) appendField(result, "parameter", key, value);
    return result;
}

}  // namespace eve::procgen
