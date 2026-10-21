#include "avatar/Appearance.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace eve::avatar {
namespace {
using Object = Value::Object;
using Array  = Value::Array;

const Value& field(const Value& value, const char* key) {
    const auto* found = value.find(key);
    if (!found) throw std::invalid_argument(std::string("Missing field: ") + key);
    return *found;
}
std::string text(const Value& value) {
    if (!value.isString()) throw std::invalid_argument("Expected string");
    return value.asString();
}
std::string id(const Value& value) {
    auto result = text(value);
    if (result.empty() || result.size() > 128) throw std::invalid_argument("Expected nonempty id of at most 128 bytes");
    return result;
}
double number(const Value& value) {
    if (!value.isNumeric()) throw std::invalid_argument("Expected finite number");
    const double result = value.isInt64() ? static_cast<double>(value.asInt()) : value.asDouble();
    if (!std::isfinite(result)) throw std::invalid_argument("Expected finite number");
    return result;
}
const Array& array(const Value& value) {
    if (!value.isArray()) throw std::invalid_argument("Expected array");
    return *value.getIf<Array>();
}
void keys(const Value& value, std::initializer_list<std::string_view> allowed) {
    if (!value.isObject()) throw std::invalid_argument("Expected object");
    for (const auto& key : value.keys())
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
            throw std::invalid_argument("Unknown field: " + key);
}
void envelope(const Value& value, std::string_view schema) {
    if (text(field(value, "schema")) != schema || !field(value, "version").isInt64() ||
        field(value, "version").asInt() != 1)
        throw std::invalid_argument("Unsupported appearance schema or version");
}
std::vector<std::string> names(const Value& value) {
    std::vector<std::string> result;
    std::set<std::string>    unique;
    for (const auto& entry : array(value)) {
        auto name = id(entry);
        if (!unique.insert(name).second) throw std::invalid_argument("Duplicate id: " + name);
        result.push_back(std::move(name));
    }
    return result;
}
Result<void> invalid(const std::string& message) {
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message, "appearance"));
}
}  // namespace

Result<void> Appearance::configure(const Value& definition) {
    try {
        keys(definition, {"schema", "version", "profile", "parameters", "drivers", "slots", "items"});
        envelope(definition, "eve.appearance.definition");
        Appearance candidate;
        candidate.profile_ = id(field(definition, "profile"));
        for (const auto& entry : array(field(definition, "parameters"))) {
            keys(entry, {"id", "min", "max", "default"});
            const auto name = id(field(entry, "id"));
            Parameter  p{number(field(entry, "min")), number(field(entry, "max")), number(field(entry, "default"))};
            if (p.minimum >= p.maximum || p.initial < p.minimum || p.initial > p.maximum ||
                !std::isfinite(p.maximum - p.minimum) || !candidate.parameters_.emplace(name, p).second)
                throw std::invalid_argument("Invalid or duplicate parameter: " + name);
            candidate.state_.parameters.emplace(name, p.initial);
        }
        for (const auto& entry : array(field(definition, "drivers"))) {
            keys(entry, {"output", "scale", "inputs"});
            Driver driver;
            driver.output = id(field(entry, "output"));
            driver.scale  = number(field(entry, "scale"));
            // Keep the accumulated output finite even for adversarial definitions.
            if (std::abs(driver.scale) > 1000 || candidate.drivers_.size() >= 4096)
                throw std::invalid_argument("Appearance driver budget exceeded");
            for (const auto& input : array(field(entry, "inputs"))) {
                keys(input, {"parameter", "start", "end"});
                Input in{id(field(input, "parameter")), number(field(input, "start")), number(field(input, "end"))};
                auto  parameter = candidate.parameters_.find(in.parameter);
                if (parameter == candidate.parameters_.end() || in.start == in.end ||
                    !std::isfinite(in.end - in.start) || std::min(in.start, in.end) < parameter->second.minimum ||
                    std::max(in.start, in.end) > parameter->second.maximum)
                    throw std::invalid_argument("Invalid driver input: " + in.parameter);
                driver.inputs.push_back(std::move(in));
            }
            if (driver.inputs.empty()) throw std::invalid_argument("Driver needs at least one input");
            candidate.drivers_.push_back(std::move(driver));
        }
        for (const auto& entry : array(field(definition, "slots"))) {
            keys(entry, {"id", "default"});
            if (!candidate.state_.selections.emplace(id(field(entry, "id")), text(field(entry, "default"))).second)
                throw std::invalid_argument("Duplicate wardrobe slot");
        }
        std::set<std::string> allParts;
        for (const auto& entry : array(field(definition, "items"))) {
            keys(entry, {"id", "slot", "parts", "hides", "excludes"});
            Item item{id(field(entry, "slot")), names(field(entry, "parts")), names(field(entry, "hides")),
                      names(field(entry, "excludes"))};
            for (const auto& part : item.parts) allParts.insert(part);
            const auto name = id(field(entry, "id"));
            if (!candidate.state_.selections.contains(item.slot) || !candidate.items_.emplace(name, item).second)
                throw std::invalid_argument("Invalid or duplicate wardrobe item: " + name);
        }
        for (const auto& [name, item] : candidate.items_) {
            for (const auto& other : item.excludes)
                if (other == name || !candidate.items_.contains(other))
                    throw std::invalid_argument("Invalid exclusion: " + other);
            for (const auto& part : item.hides)
                if (!allParts.contains(part)) throw std::invalid_argument("Unknown hidden part: " + part);
        }
        auto valid = candidate.validate(candidate.state_);
        if (!valid.ok()) return valid;
        *this = std::move(candidate);
        return Result<void>::success();
    } catch (const std::invalid_argument& error) {
        return invalid(error.what());
    }
}

Result<void> Appearance::validate(const State& candidate) const {
    for (const auto& [name, value] : candidate.parameters) {
        const auto found = parameters_.find(name);
        if (found == parameters_.end() || !std::isfinite(value) || value < found->second.minimum ||
            value > found->second.maximum)
            return invalid("Unknown or out-of-range parameter: " + name);
    }
    std::set<std::string> selected;
    for (const auto& [slot, name] : candidate.selections) {
        if (!state_.selections.contains(slot)) return invalid("Unknown wardrobe slot: " + slot);
        if (name.empty()) continue;
        const auto found = items_.find(name);
        if (found == items_.end() || found->second.slot != slot)
            return invalid("Wrong item for wardrobe slot: " + slot);
        selected.insert(name);
    }
    for (const auto& name : selected)
        for (const auto& excluded : items_.at(name).excludes)
            if (selected.contains(excluded)) return invalid("Incompatible wardrobe items: " + name + " / " + excluded);
    return Result<void>::success();
}

Result<void> Appearance::publish(State candidate) {
    auto valid = validate(candidate);
    if (!valid.ok()) return valid;
    if (candidate == state_) return Result<void>::success();
    undo_.push_back(state_);
    if (undo_.size() > 64) undo_.erase(undo_.begin());
    state_ = std::move(candidate);
    redo_.clear();
    return Result<void>::success();
}

Result<void> Appearance::apply(const Value& patch) {
    if (profile_.empty()) return invalid("Configure an appearance definition first");
    try {
        keys(patch, {"parameters", "selections"});
        auto candidate = state_;
        if (const auto* values = patch.find("parameters")) {
            if (!values->isObject()) return invalid("Expected parameter object");
            for (const auto& key : values->keys()) candidate.parameters[key] = number(field(*values, key.c_str()));
        }
        if (const auto* values = patch.find("selections")) {
            if (!values->isObject()) return invalid("Expected selection object");
            for (const auto& key : values->keys()) candidate.selections[key] = text(field(*values, key.c_str()));
        }
        return publish(std::move(candidate));
    } catch (const std::invalid_argument& error) {
        return invalid(error.what());
    }
}

Result<void> Appearance::restore(const Value& snapshot) {
    try {
        keys(snapshot, {"schema", "version", "profile", "parameters", "selections"});
        envelope(snapshot, "eve.appearance.state");
        if (profile_.empty() || text(field(snapshot, "profile")) != profile_)
            return invalid("Appearance profile mismatch");
        const auto& parameters = field(snapshot, "parameters");
        const auto& selections = field(snapshot, "selections");
        if (!parameters.isObject() || !selections.isObject() || parameters.keys().size() != parameters_.size() ||
            selections.keys().size() != state_.selections.size())
            return invalid("Incomplete appearance snapshot");
        return apply(Value(Object{{"parameters", parameters}, {"selections", selections}}));
    } catch (const std::invalid_argument& error) {
        return invalid(error.what());
    }
}

Result<void> Appearance::undo() {
    if (undo_.empty())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, "No appearance undo state"));
    redo_.push_back(state_);
    state_ = std::move(undo_.back());
    undo_.pop_back();
    return Result<void>::success();
}
Result<void> Appearance::redo() {
    if (redo_.empty())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, "No appearance redo state"));
    undo_.push_back(state_);
    state_ = std::move(redo_.back());
    redo_.pop_back();
    return Result<void>::success();
}
Value Appearance::snapshot() const {
    Object parameters, selections;
    for (const auto& [key, value] : state_.parameters) parameters.emplace(key, value);
    for (const auto& [key, value] : state_.selections) selections.emplace(key, value);
    return Value(Object{{"schema", "eve.appearance.state"},
                        {"version", 1},
                        {"profile", profile_},
                        {"parameters", Value(std::move(parameters))},
                        {"selections", Value(std::move(selections))}});
}
Value Appearance::evaluate() const {
    std::map<std::string, double> outputs;
    for (const auto& driver : drivers_) {
        double value = driver.scale;
        for (const auto& input : driver.inputs)
            value *=
                std::clamp((state_.parameters.at(input.parameter) - input.start) / (input.end - input.start), 0.0, 1.0);
        outputs[driver.output] += value;
    }
    Object channels;
    for (const auto& [key, value] : outputs) channels.emplace(key, value);
    std::set<std::string> parts, hidden;
    for (const auto& [slot, name] : state_.selections) {
        (void)slot;
        if (name.empty()) continue;
        const auto& item = items_.at(name);
        parts.insert(item.parts.begin(), item.parts.end());
        hidden.insert(item.hides.begin(), item.hides.end());
    }
    Array visible;
    for (const auto& part : parts)
        if (!hidden.contains(part)) visible.emplace_back(part);
    return Value(Object{{"channels", Value(std::move(channels))}, {"visibleParts", Value(std::move(visible))}});
}
}  // namespace eve::avatar
