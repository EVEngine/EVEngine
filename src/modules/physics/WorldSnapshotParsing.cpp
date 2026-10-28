#include "physics/WorldSnapshotParsing.h"

#include <charconv>
#include <cmath>
#include <limits>

namespace eve::physics::snapshot_detail {

bool hasExactFields(const eve::Value::Object& object, std::initializer_list<std::string_view> expected) {
    if (object.size() != expected.size()) return false;
    for (const std::string_view field : expected) {
        if (!object.contains(std::string(field))) return false;
    }
    return true;
}

const eve::Value* field(const eve::Value::Object& object, std::string_view name) {
    const auto found = object.find(std::string(name));
    return found == object.end() ? nullptr : &found->second;
}

eve::Result<std::string> readString(const eve::Value::Object& object, std::string_view name) {
    const eve::Value* value = field(object, name);
    if (!value || !value->isString())
        return eve::Result<std::string>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, "snapshot field must be a string", std::string(name)));
    return eve::Result<std::string>::success(value->asString());
}

eve::Result<std::uint64_t> readUint64(const eve::Value::Object& object, std::string_view name) {
    auto text = readString(object, name);
    if (!text) return eve::Result<std::uint64_t>::failure(text.status());
    std::uint64_t      output = 0;
    const std::string& value  = text.value();
    const auto [end, error]   = std::from_chars(value.data(), value.data() + value.size(), output);
    if (value.empty() || error != std::errc{} || end != value.data() + value.size())
        return eve::Result<std::uint64_t>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, "snapshot integer is not a uint64 decimal string", std::string(name)));
    return eve::Result<std::uint64_t>::success(output);
}

eve::Result<std::int64_t> readInt64(const eve::Value::Object& object, std::string_view name) {
    auto text = readString(object, name);
    if (!text) return eve::Result<std::int64_t>::failure(text.status());
    std::int64_t       output = 0;
    const std::string& value  = text.value();
    const auto [end, error]   = std::from_chars(value.data(), value.data() + value.size(), output);
    if (value.empty() || error != std::errc{} || end != value.data() + value.size())
        return eve::Result<std::int64_t>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, "snapshot integer is not an int64 decimal string", std::string(name)));
    return eve::Result<std::int64_t>::success(output);
}

eve::Result<double> readNumber(const eve::Value::Object& object, std::string_view name) {
    const eve::Value* value = field(object, name);
    if (!value || !value->isNumeric())
        return eve::Result<double>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, "snapshot field must be numeric", std::string(name)));
    const double result = value->isDouble() ? value->asDouble() : static_cast<double>(value->asInt());
    if (!std::isfinite(result))
        return eve::Result<double>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "snapshot numeric field must be finite", std::string(name)));
    return eve::Result<double>::success(result);
}

eve::Result<float> readFloat(const eve::Value::Object& object, std::string_view name) {
    auto number = readNumber(object, name);
    if (!number) return eve::Result<float>::failure(number.status());
    if (number.value() < -std::numeric_limits<float>::max() || number.value() > std::numeric_limits<float>::max())
        return eve::Result<float>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "snapshot numeric field is outside float range", std::string(name)));
    return eve::Result<float>::success(static_cast<float>(number.value()));
}

eve::Result<bool> readBool(const eve::Value::Object& object, std::string_view name) {
    const eve::Value* value = field(object, name);
    if (!value || !value->isBool())
        return eve::Result<bool>::failure(eve::Diagnostic::error(eve::DiagnosticCode::ParseError,
                                                                 "snapshot field must be boolean", std::string(name)));
    return eve::Result<bool>::success(value->asBool());
}

}  // namespace eve::physics::snapshot_detail
