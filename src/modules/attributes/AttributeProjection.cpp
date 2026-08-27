#include "attributes/AttributeProjection.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace eve::attributes {
namespace {

template <typename T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path), {},
                                                "attributes.projection"));
}

Result<void> failureVoid(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<void>::failure(Diagnostic::error(code, std::move(message), std::move(path), {},
                                                   "attributes.projection"));
}

bool sameOwner(const ecs::EntityHandle& left, const ecs::EntityHandle& right) noexcept {
    return left.table == right.table && left.type == right.type && left.id == right.id &&
           left.generation == right.generation;
}

bool contains(std::span<const std::string_view> names, std::string_view name) noexcept {
    return std::find(names.begin(), names.end(), name) != names.end();
}

}  // namespace

AttributeProjection::AttributeProjection(std::string subject) : values_(std::move(subject)) {}

Result<void> AttributeProjection::bindOwner(ecs::EntityHandle owner) {
    if (owner.table == nullptr || ecs::try_get(owner) == nullptr)
        return failureVoid(DiagnosticCode::StaleHandle,
                           "attribute projection owner is not a live ECS generation", "owner");
    if (owner_.table == nullptr) {
        owner_ = owner;
        return Result<void>::success(Status::success(StatusCode::Applied));
    }
    if (!sameOwner(owner_, owner))
        return failureVoid(DiagnosticCode::Conflict,
                           "attribute projection cannot be rebound to another owner", "owner");
    return Result<void>::success(Status::success(StatusCode::NoOp));
}

bool AttributeProjection::isStale() const noexcept {
    return owner_.table != nullptr && ecs::try_get(owner_) == nullptr;
}

bool AttributeProjection::ownsSameLiveEntity(ecs::EntityHandle owner) const noexcept {
    return sameOwner(owner_, owner) && owner_.table != nullptr && ecs::try_get(owner_) != nullptr;
}

Result<void> AttributeProjection::advanceRevision() {
    const auto next = revision_.incremented();
    if (!next)
        return failureVoid(DiagnosticCode::InvariantViolation,
                           "attribute projection revision overflowed", "revision");
    revision_ = *next;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> AttributeProjection::initialize(std::span<const AttributeSnapshotBase> values) {
    if (initialized_)
        return Result<void>::success(Status::success(StatusCode::NoOp));
    AttributeSet candidate = values_;
    for (const auto& entry : values) {
        if (entry.attribute.empty() || !std::isfinite(entry.base))
            return failureVoid(DiagnosticCode::InvalidArgument,
                               "attribute projection seed requires finite named bases", "bases");
        candidate.setBase(entry.attribute, entry.base);
    }
    values_ = std::move(candidate);
    initialized_ = true;
    return advanceRevision();
}

Result<void> AttributeProjection::setBase(std::string_view attribute, double value) {
    if (attribute.empty() || !std::isfinite(value))
        return failureVoid(DiagnosticCode::InvalidArgument,
                           "attribute base requires a non-empty finite value", "attribute");
    auto next = advanceRevision();
    if (!next) return next;
    values_.setBase(std::string(attribute), value);
    initialized_ = true;
    return next;
}

Result<void> AttributeProjection::modifyBase(std::string_view attribute, double delta) {
    if (attribute.empty() || !std::isfinite(delta))
        return failureVoid(DiagnosticCode::InvalidArgument,
                           "attribute delta requires a non-empty finite value", "attribute");
    auto next = advanceRevision();
    if (!next) return next;
    values_.modifyBase(std::string(attribute), delta);
    initialized_ = true;
    return next;
}

Result<double> AttributeProjection::getFinal(std::string_view attribute, double fallback) const {
    if (attribute.empty() || !std::isfinite(fallback))
        return failure<double>(DiagnosticCode::InvalidArgument,
                               "attribute query requires a non-empty finite key/fallback",
                               "attribute");
    return Result<double>::success(values_.getFinal(std::string(attribute), fallback));
}

bool AttributeProjection::has(std::string_view attribute) const {
    return !attribute.empty() && values_.has(std::string(attribute));
}

Result<ModifierId> AttributeProjection::addModifier(AttributeModifier modifier) {
    auto result = values_.addModifier(std::move(modifier));
    if (!result) return result;
    auto next = advanceRevision();
    if (!next) return Result<ModifierId>::failure(next.status());
    initialized_ = true;
    return result;
}

Result<AttributeProjectionSnapshot> AttributeProjection::snapshot(
    std::span<const std::string_view> attributes) const {
    for (const auto name : attributes) {
        if (name.empty())
            return failure<AttributeProjectionSnapshot>(DiagnosticCode::InvalidArgument,
                                                         "snapshot attribute name is empty", "attributes");
    }
    AttributeProjectionSnapshot result;
    result.owner = owner_;
    result.capturedRevision = revision_;
    result.bases.reserve(attributes.size());
    for (const auto name : attributes)
        result.bases.push_back({std::string(name), values_.getBase(std::string(name), 0.0)});

    for (int index = 0; index < values_.modifierCount(); ++index) {
        const auto* modifier = values_.modifierAt(index);
        if (modifier != nullptr && contains(attributes, modifier->attribute))
            result.modifiers.push_back(*modifier);
    }
    return Result<AttributeProjectionSnapshot>::success(std::move(result));
}

Result<void> AttributeProjection::restore(const AttributeProjectionSnapshot& snapshotValue,
                                          Revision expectedRevision) {
    if (!ownsSameLiveEntity(snapshotValue.owner))
        return failureVoid(DiagnosticCode::StaleHandle,
                           "attribute snapshot owner is stale or does not match", "owner");
    if (revision_ != expectedRevision)
        return failureVoid(DiagnosticCode::Conflict,
                           "attribute snapshot revision does not match current state", "revision");

    std::vector<std::string_view> names;
    names.reserve(snapshotValue.bases.size());
    for (const auto& base : snapshotValue.bases) {
        if (base.attribute.empty() || !std::isfinite(base.base))
            return failureVoid(DiagnosticCode::InvalidArgument,
                               "attribute snapshot contains invalid base data", "bases");
        names.push_back(base.attribute);
    }

    AttributeSet candidate = values_;
    std::vector<ModifierId> toRemove;
    for (int index = 0; index < candidate.modifierCount(); ++index) {
        const auto* modifier = candidate.modifierAt(index);
        if (modifier != nullptr && contains(names, modifier->attribute)) toRemove.push_back(modifier->id);
    }
    for (const auto& id : toRemove) {
        auto removed = candidate.removeModifier(id);
        if (!removed) return failureVoid(DiagnosticCode::InvariantViolation,
                                         "attribute snapshot could not remove candidate modifier", "modifiers");
    }
    for (const auto& base : snapshotValue.bases) candidate.setBase(base.attribute, base.base);

    auto modifiers = snapshotValue.modifiers;
    std::stable_sort(modifiers.begin(), modifiers.end(),
                     [](const auto& left, const auto& right) {
                         return left.sequence < right.sequence;
                     });
    for (const auto& modifier : modifiers) {
        if (!contains(names, modifier.attribute))
            return failureVoid(DiagnosticCode::InvalidArgument,
                               "attribute snapshot modifier is outside its allow-list", "modifiers");
        auto added = candidate.addModifier(modifier);
        if (!added) return failureVoid(DiagnosticCode::InvalidArgument,
                                       "attribute snapshot contains an invalid modifier", "modifiers");
    }

    auto next = advanceRevision();
    if (!next) return next;
    values_ = std::move(candidate);
    initialized_ = true;
    return next;
}

}  // namespace eve::attributes
