#include "effects/EffectTypes.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace eve::effects {
namespace {

eve::Result<void> invalid(std::string message) {
    return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, std::move(message)));
}

}  // namespace

void EffectPayload::setString(const std::string& key, const std::string& value) {
    if (!key.empty()) values_[key] = eve::Value(value);
}

void EffectPayload::setNumber(const std::string& key, double value) {
    if (key.empty() || !std::isfinite(value)) return;
    values_[key] = eve::Value(value);
}

void EffectPayload::setBool(const std::string& key, bool value) {
    if (!key.empty()) values_[key] = eve::Value(value);
}

void EffectPayload::setNull(const std::string& key) {
    if (!key.empty()) values_[key] = eve::Value();
}

eve::Result<void> EffectPayload::setJson(const std::string& key, const std::string& json) {
    if (key.empty()) return invalid("effect payload key must not be empty");
    auto parsed = eve::Value::fromJson(json);
    if (!parsed.ok()) return eve::Result<void>::failure(parsed.status());
    values_[key] = std::move(parsed).takeValue();
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

bool EffectPayload::has(const std::string& key) const { return values_.contains(key); }

eve::Result<void> EffectPayload::erase(const std::string& key) {
    if (key.empty()) return invalid("effect payload key must not be empty");
    const bool removed = values_.erase(key) != 0;
    return eve::Result<void>::success(eve::Status::success(removed ? eve::StatusCode::Applied : eve::StatusCode::NoOp));
}

std::string EffectPayload::getJson(const std::string& key) const {
    const auto it = values_.find(key);
    if (it == values_.end()) return {};
    auto result = it->second.toJson();
    if (!result) return {};
    return std::move(result).takeValue();
}

std::string EffectPayload::toJson() const {
    auto result = eve::Value(values_).toJson();
    if (!result) return {};
    return std::move(result).takeValue();
}

const eve::Value::Object& EffectPayload::object() const noexcept { return values_; }

void EffectPayload::clear() { values_.clear(); }

eve::Result<void> EffectDefinition::validate() const {
    if (id.empty()) return invalid("effect definition id must not be empty");
    if (!std::isfinite(duration)) return invalid("effect definition duration must be finite");
    if (!std::isfinite(period) || period < 0.0)
        return invalid("effect definition period must be finite and non-negative");
    if (!std::isfinite(magnitude)) return invalid("effect definition magnitude must be finite");
    if (stackCount == 0) return invalid("effect definition stack count must be positive");
    if (policy.stackCount == StackCountPolicy::Set && policy.maxStacks != 0 && stackCount > policy.maxStacks &&
        policy.overflow == OverflowPolicy::Reject) {
        return invalid("effect definition stack count exceeds its maximum");
    }
    return eve::Result<void>::success();
}

eve::Result<void> EffectInstance::addTag(const std::string& tag) {
    if (tag.empty()) return invalid("effect tag must not be empty");
    const auto it = std::lower_bound(tags.begin(), tags.end(), tag);
    if (it != tags.end() && *it == tag) return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
    tags.insert(it, tag);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> EffectInstance::removeTag(const std::string& tag) {
    const auto it = std::lower_bound(tags.begin(), tags.end(), tag);
    if (it == tags.end() || *it != tag) return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
    tags.erase(it);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

bool EffectInstance::hasTag(const std::string& tag) const { return std::binary_search(tags.begin(), tags.end(), tag); }

int EffectInstance::tagCount() const { return static_cast<int>(tags.size()); }

std::string EffectInstance::tagAt(int index) const {
    return index < 0 || static_cast<std::size_t>(index) >= tags.size() ? std::string{}
                                                                       : tags[static_cast<std::size_t>(index)];
}

std::string policyName(StackPolicy policy) {
    switch (policy) {
        case StackPolicy::Replace: return "replace";
        case StackPolicy::Stack: return "stack";
        case StackPolicy::Refresh: return "refresh";
    }
    return "unknown";
}

bool parsePolicy(const std::string& name, StackPolicy& policy) {
    if (name == "replace")
        policy = StackPolicy::Replace;
    else if (name == "stack")
        policy = StackPolicy::Stack;
    else if (name == "refresh")
        policy = StackPolicy::Refresh;
    else
        return false;
    return true;
}

std::string eventKindName(EffectEventKind kind) {
    switch (kind) {
        case EffectEventKind::Applied: return "applied";
        case EffectEventKind::Refreshed: return "refreshed";
        case EffectEventKind::Stacked: return "stacked";
        case EffectEventKind::Periodic: return "periodic";
        case EffectEventKind::Expired: return "expired";
        case EffectEventKind::Removed: return "removed";
    }
    return "unknown";
}

}  // namespace eve::effects
