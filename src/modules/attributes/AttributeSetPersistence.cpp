#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <set>
#include "attributes/AttributeSet.h"
namespace eve::attributes {
namespace {
struct Invalid {
    Status status;
};
[[noreturn]] void invalid(const char* message, DiagnosticCode code = DiagnosticCode::ParseError) {
    throw Invalid{Status::failure(Diagnostic::error(code, message, "attributes.set"))};
}
const std::string& text(const Value& value) {
    const auto* result = value.getIf<std::string>();
    if (!result) invalid("Expected attribute text");
    return *result;
}
const Value::Array& array(const Value& value) {
    const auto* result = value.getIf<Value::Array>();
    if (!result) invalid("Expected attribute array");
    return *result;
}
const Value::Array& tuple(const Value& value, std::size_t size) {
    const auto& result = array(value);
    if (result.size() != size) invalid("Unknown or missing attribute tuple fields");
    return result;
}
std::uint64_t counter(const Value& value) {
    const auto&   source = text(value);
    std::uint64_t result = 0;
    const auto    parsed = std::from_chars(source.data(), source.data() + source.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != source.data() + source.size() || !result ||
        std::to_string(result) != source)
        invalid("Invalid canonical attribute counter");
    return result;
}
double number(const Value& value) {
    if (!value.isDouble() && !value.isInt64()) invalid("Expected attribute number");
    const auto result = value.isDouble() ? value.asDouble() : static_cast<double>(value.asInt());
    if (!std::isfinite(result)) invalid("Attribute number must be finite");
    return result;
}
std::int64_t integer(const Value& value, std::int64_t lo, std::int64_t hi) {
    if (!value.isInt64() || value.asInt() < lo || value.asInt() > hi) invalid("Invalid attribute integer");
    return value.asInt();
}
}  // namespace
Result<Value> AttributeSet::encodeState() const {
    try {
        std::vector<std::string_view> names;
        names.reserve(values_.size());
        for (const auto& [id, value] : values_) names.push_back(id);
        std::sort(names.begin(), names.end());
        Value::Array values;
        values.reserve(names.size());
        for (const auto id : names) {
            const auto&  state = values_.at(std::string(id));
            Value::Array modifiers;
            modifiers.reserve(state.modifiers.size());
            for (const auto& modifier : state.modifiers) {
                if (modifier.attribute != id) invalid("Modifier attribute differs from containing value");
                modifiers.push_back(Value::Array{modifier.id, modifier.source, int(modifier.operation), modifier.value,
                                                 std::int64_t(modifier.priority), std::to_string(modifier.sequence),
                                                 modifier.policyId});
            }
            values.push_back(Value::Array{std::string(id), state.base, Value(std::move(modifiers))});
        }
        auto encoded = Value::object({{"schema", "attributes.set"},
                                      {"version", 1},
                                      {"subject", subject_},
                                      {"next_sequence", std::to_string(nextSequence_)},
                                      {"next_modifier", std::to_string(nextModifierId_)},
                                      {"values", Value(std::move(values))}});
        auto valid   = prepareState(encoded);
        if (!valid) return Result<Value>::failure(valid.status());
        return Result<Value>::success(std::move(encoded));
    } catch (const Invalid& error) {
        return Result<Value>::failure(error.status);
    }
}
Result<PreparedAttributeSet> AttributeSet::prepareState(const Value& value) {
    using R = Result<PreparedAttributeSet>;
    try {
        const auto* root = value.getIf<Value::Object>();
        if (!root || root->size() != 6) invalid("Unknown or missing attribute root fields");
        for (const auto* key : {"schema", "version", "subject", "next_sequence", "next_modifier", "values"})
            if (!root->contains(key)) invalid("Missing attribute root field");
        if (text(root->at("schema")) != "attributes.set") invalid("Unknown attribute schema");
        if (!root->at("version").isInt64() || root->at("version").asInt() != 1)
            invalid("Unknown attribute state version", DiagnosticCode::UnknownVersion);
        auto  prepared         = prepareEmpty();
        auto& target           = *prepared.candidate_;
        target.subject_        = text(root->at("subject"));
        target.nextSequence_   = counter(root->at("next_sequence"));
        target.nextModifierId_ = counter(root->at("next_modifier"));
        std::set<std::string>      ids;
        std::set<ModifierSequence> sequences;
        for (const auto& value : array(root->at("values"))) {
            const auto& row  = tuple(value, 3);
            const auto& name = text(row[0]);
            if (name.empty() || target.values_.contains(name)) invalid("Invalid or duplicate attribute name");
            AttributeValue state;
            state.base             = number(row[1]);
            ModifierSequence prior = 0;
            for (const auto& value : array(row[2])) {
                const auto&       modifier = tuple(value, 7);
                AttributeModifier item;
                item.id        = text(modifier[0]);
                item.attribute = name;
                item.source    = text(modifier[1]);
                item.operation =
                    static_cast<AttributeOperation>(integer(modifier[2], 0, int(AttributeOperation::Custom)));
                item.value = number(modifier[3]);
                item.priority =
                    static_cast<ModifierPriority>(integer(modifier[4], std::numeric_limits<ModifierPriority>::min(),
                                                          std::numeric_limits<ModifierPriority>::max()));
                item.sequence = counter(modifier[5]);
                item.policyId = text(modifier[6]);
                if (item.id.empty() || !isValidOperation(item) || !ids.insert(item.id).second ||
                    !sequences.insert(item.sequence).second || item.sequence <= prior ||
                    item.sequence >= target.nextSequence_)
                    invalid("Invalid modifier identity operation or sequence");
                prior = item.sequence;
                state.modifiers.push_back(std::move(item));
            }
            target.values_.emplace(name, std::move(state));
        }
        return R::success(std::move(prepared));
    } catch (const Invalid& error) {
        return R::failure(error.status);
    }
}
}  // namespace eve::attributes
