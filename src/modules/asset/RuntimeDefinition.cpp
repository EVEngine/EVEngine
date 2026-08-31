#include "asset/RuntimeDefinition.h"

#include "common/Utf8Validation.h"

#include <bit>
#include <cmath>
#include <limits>

namespace eve::asset {
namespace {

enum class Tag : std::uint8_t {
    Null = 0,
    False = 1,
    True = 2,
    Int64 = 3,
    Double = 4,
    String = 5,
    Array = 6,
    Object = 7,
};

template <class T>
Result<T> failure(DiagnosticCode code, std::string message) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), {}, {},
                                                "asset.runtime-definition"));
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (unsigned shift = 0; shift != 32; shift += 8)
        out.push_back(static_cast<std::uint8_t>(value >> shift));
}

void put64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (unsigned shift = 0; shift != 64; shift += 8)
        out.push_back(static_cast<std::uint8_t>(value >> shift));
}

Result<void> appendString(std::vector<std::uint8_t>& out, std::string_view value,
                          const RuntimeDefinitionLimits& limits) {
    if (value.size() > limits.maximumStringBytes ||
        value.size() > std::numeric_limits<std::uint32_t>::max() || !isValidUtf8(value))
        return failure<void>(DiagnosticCode::InvalidArgument,
                             "runtime definition string is invalid or exceeds limits");
    put32(out, static_cast<std::uint32_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
    return Result<void>::success();
}

Result<void> encodeValue(const Value& value, std::vector<std::uint8_t>& out,
                         const RuntimeDefinitionLimits& limits, std::uint32_t depth,
                         std::uint32_t& count) {
    if (depth > limits.maximumDepth || count >= limits.maximumValues)
        return failure<void>(DiagnosticCode::InvalidArgument,
                             "runtime definition structure exceeds limits");
    ++count;
    switch (value.type()) {
        case Value::Type::Null: out.push_back(static_cast<std::uint8_t>(Tag::Null)); break;
        case Value::Type::Bool:
            out.push_back(static_cast<std::uint8_t>(value.asBool() ? Tag::True : Tag::False));
            break;
        case Value::Type::Int64:
            out.push_back(static_cast<std::uint8_t>(Tag::Int64));
            put64(out, std::bit_cast<std::uint64_t>(value.asInt()));
            break;
        case Value::Type::Double:
            if (!std::isfinite(value.asDouble()))
                return failure<void>(DiagnosticCode::InvalidArgument,
                                     "runtime definition contains a non-finite number");
            out.push_back(static_cast<std::uint8_t>(Tag::Double));
            put64(out, std::bit_cast<std::uint64_t>(value.asDouble()));
            break;
        case Value::Type::String: {
            out.push_back(static_cast<std::uint8_t>(Tag::String));
            auto appended = appendString(out, value.asString(), limits);
            if (!appended) return appended;
            break;
        }
        case Value::Type::Array: {
            const auto* values = value.getIf<Value::Array>();
            if (!values || values->size() > std::numeric_limits<std::uint32_t>::max())
                return failure<void>(DiagnosticCode::InvalidArgument,
                                     "runtime definition array exceeds limits");
            out.push_back(static_cast<std::uint8_t>(Tag::Array));
            put32(out, static_cast<std::uint32_t>(values->size()));
            for (const auto& child : *values) {
                auto encoded = encodeValue(child, out, limits, depth + 1, count);
                if (!encoded) return encoded;
            }
            break;
        }
        case Value::Type::Object: {
            const auto* object = value.getIf<Value::Object>();
            if (!object || object->size() > std::numeric_limits<std::uint32_t>::max())
                return failure<void>(DiagnosticCode::InvalidArgument,
                                     "runtime definition object exceeds limits");
            out.push_back(static_cast<std::uint8_t>(Tag::Object));
            put32(out, static_cast<std::uint32_t>(object->size()));
            for (const auto& [key, child] : *object) {
                auto appended = appendString(out, key, limits);
                if (!appended) return appended;
                auto encoded = encodeValue(child, out, limits, depth + 1, count);
                if (!encoded) return encoded;
            }
            break;
        }
    }
    if (out.size() > limits.maximumBytes)
        return failure<void>(DiagnosticCode::InvalidArgument,
                             "runtime definition exceeds byte budget");
    return Result<void>::success();
}

class Decoder {
public:
    Decoder(std::span<const std::uint8_t> bytes, const RuntimeDefinitionLimits& limits)
        : bytes_(bytes), limits_(limits) {}

    Result<Value> value(std::uint32_t depth = 0) {
        if (depth > limits_.maximumDepth || count_ >= limits_.maximumValues)
            return failure<Value>(DiagnosticCode::InvalidArgument,
                                  "runtime definition structure exceeds limits");
        ++count_;
        std::uint8_t rawTag = 0;
        if (!byte(rawTag))
            return failure<Value>(DiagnosticCode::ParseError,
                                  "runtime definition value is truncated");
        const auto tag = static_cast<Tag>(rawTag);
        if (tag == Tag::Null) return Result<Value>::success(Value());
        if (tag == Tag::False) return Result<Value>::success(Value(false));
        if (tag == Tag::True) return Result<Value>::success(Value(true));
        if (tag == Tag::Int64) {
            std::uint64_t raw = 0;
            if (!u64(raw)) return failure<Value>(DiagnosticCode::ParseError, "int64 is truncated");
            return Result<Value>::success(Value(std::bit_cast<std::int64_t>(raw)));
        }
        if (tag == Tag::Double) {
            std::uint64_t raw = 0;
            if (!u64(raw)) return failure<Value>(DiagnosticCode::ParseError, "double is truncated");
            const double decoded = std::bit_cast<double>(raw);
            if (!std::isfinite(decoded))
                return failure<Value>(DiagnosticCode::ParseError,
                                      "runtime definition double is non-finite");
            return Result<Value>::success(Value(decoded));
        }
        if (tag == Tag::String) {
            auto decoded = string();
            if (!decoded) return Result<Value>::failure(decoded.status());
            return Result<Value>::success(Value(std::move(decoded).takeValue()));
        }
        std::uint32_t size = 0;
        if ((tag != Tag::Array && tag != Tag::Object) || !u32(size) ||
            size > limits_.maximumValues - count_)
            return failure<Value>(DiagnosticCode::ParseError,
                                  "runtime definition collection is invalid");
        if (tag == Tag::Array) {
            Value::Array result;
            result.reserve(size);
            for (std::uint32_t i = 0; i < size; ++i) {
                auto child = value(depth + 1);
                if (!child) return child;
                result.push_back(std::move(child).takeValue());
            }
            return Result<Value>::success(Value(std::move(result)));
        }
        Value::Object result;
        std::string previous;
        for (std::uint32_t i = 0; i < size; ++i) {
            auto key = string();
            if (!key) return Result<Value>::failure(key.status());
            if (key.value().empty() || (!previous.empty() && key.value() <= previous))
                return failure<Value>(DiagnosticCode::ParseError,
                                      "runtime definition object keys are not canonical");
            previous = key.value();
            auto child = value(depth + 1);
            if (!child) return child;
            result.emplace(std::move(key).takeValue(), std::move(child).takeValue());
        }
        return Result<Value>::success(Value(std::move(result)));
    }

    std::size_t cursor() const noexcept { return cursor_; }

private:
    bool byte(std::uint8_t& value) {
        if (cursor_ == bytes_.size()) return false;
        value = bytes_[cursor_++];
        return true;
    }
    bool u32(std::uint32_t& value) {
        if (bytes_.size() - cursor_ < 4) return false;
        value = 0;
        for (unsigned shift = 0; shift != 32; shift += 8)
            value |= std::uint32_t(bytes_[cursor_++]) << shift;
        return true;
    }
    bool u64(std::uint64_t& value) {
        if (bytes_.size() - cursor_ < 8) return false;
        value = 0;
        for (unsigned shift = 0; shift != 64; shift += 8)
            value |= std::uint64_t(bytes_[cursor_++]) << shift;
        return true;
    }
    Result<std::string> string() {
        std::uint32_t size = 0;
        if (!u32(size) || size > limits_.maximumStringBytes || size > bytes_.size() - cursor_)
            return failure<std::string>(DiagnosticCode::ParseError,
                                        "runtime definition string is invalid");
        std::string result(reinterpret_cast<const char*>(bytes_.data() + cursor_), size);
        cursor_ += size;
        if (!isValidUtf8(result))
            return failure<std::string>(DiagnosticCode::ParseError,
                                        "runtime definition string is not valid UTF-8");
        return Result<std::string>::success(std::move(result));
    }

    std::span<const std::uint8_t> bytes_;
    const RuntimeDefinitionLimits& limits_;
    std::size_t cursor_ = 0;
    std::uint32_t count_ = 0;
};

}  // namespace

Result<std::vector<std::uint8_t>> encodeRuntimeDefinition(
    const Value& value, const RuntimeDefinitionLimits& limits) {
    static constexpr std::uint8_t magic[] = {'E', 'V', 'D', 'E', 'F', 0, 1, 0};
    std::vector<std::uint8_t> result(std::begin(magic), std::end(magic));
    std::uint32_t count = 0;
    auto encoded = encodeValue(value, result, limits, 0, count);
    if (!encoded) return Result<std::vector<std::uint8_t>>::failure(encoded.status());
    return Result<std::vector<std::uint8_t>>::success(std::move(result));
}

Result<Value> decodeRuntimeDefinition(std::span<const std::uint8_t> bytes,
                                      const RuntimeDefinitionLimits& limits) {
    static constexpr std::uint8_t magic[] = {'E', 'V', 'D', 'E', 'F', 0, 1, 0};
    if (bytes.size() < sizeof(magic) || bytes.size() > limits.maximumBytes ||
        !std::equal(std::begin(magic), std::end(magic), bytes.begin()))
        return failure<Value>(DiagnosticCode::ParseError,
                              "runtime definition header or byte budget is invalid");
    Decoder decoder(bytes.subspan(sizeof(magic)), limits);
    auto result = decoder.value();
    if (!result) return result;
    if (decoder.cursor() != bytes.size() - sizeof(magic))
        return failure<Value>(DiagnosticCode::ParseError,
                              "runtime definition has trailing bytes");
    return result;
}

}  // namespace eve::asset
