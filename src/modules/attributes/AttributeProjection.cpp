#include "attributes/AttributeProjection.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace eve::attributes {
namespace {

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
        return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::StaleHandle, std::move("attribute projection owner is not a live ECS generation"), std::move("owner"), {}, "attributes.projection"));
    if (owner_.table == nullptr) {
        owner_ = owner;
        return Result<void>::success(Status::success(StatusCode::Applied));
    }
    if (!sameOwner(owner_, owner))
        return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::Conflict, std::move("attribute projection cannot be rebound to another owner"), std::move("owner"), {}, "attributes.projection"));
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
        return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvariantViolation, std::move("attribute projection revision overflowed"), std::move("revision"), {}, "attributes.projection"));
    revision_ = *next;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> AttributeProjection::initialize(std::span<const AttributeSnapshotBase> values) {
    if (initialized_) return Result<void>::success(Status::success(StatusCode::NoOp));
    AttributeSet candidate = values_;
    for (const auto& entry : values) {
        if (entry.attribute.empty() || !std::isfinite(entry.base))
            return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move("attribute projection seed requires finite named bases"), std::move("bases"), {}, "attributes.projection"));
        candidate.setBase(entry.attribute, entry.base);
    }
    values_      = std::move(candidate);
    initialized_ = true;
    return advanceRevision();
}

Result<void> AttributeProjection::setBase(std::string_view attribute, double value) {
    if (attribute.empty() || !std::isfinite(value))
        return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move("attribute base requires a non-empty finite value"), std::move("attribute"), {}, "attributes.projection"));
    auto next = advanceRevision();
    if (!next) return next;
    values_.setBase(std::string(attribute), value);
    initialized_ = true;
    return next;
}

Result<void> AttributeProjection::modifyBase(std::string_view attribute, double delta) {
    if (attribute.empty() || !std::isfinite(delta))
        return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move("attribute delta requires a non-empty finite value"), std::move("attribute"), {}, "attributes.projection"));
    auto next = advanceRevision();
    if (!next) return next;
    values_.modifyBase(std::string(attribute), delta);
    initialized_ = true;
    return next;
}

Result<double> AttributeProjection::getFinal(std::string_view attribute, double fallback) const {
    if (attribute.empty() || !std::isfinite(fallback))
        return Result<double>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                         "attribute query requires a non-empty finite key/fallback",
                                                         "attribute", {}, "attributes.projection"));
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

Result<AttributeProjectionSnapshot> AttributeProjection::snapshot(std::span<const std::string_view> attributes) const {
    for (const auto name : attributes) {
        if (name.empty())
            return Result<AttributeProjectionSnapshot>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "snapshot attribute name is empty", "attributes", {},
                                  "attributes.projection"));
    }
    AttributeProjectionSnapshot result;
    result.owner            = owner_;
    result.capturedRevision = revision_;
    result.bases.reserve(attributes.size());
    for (const auto name : attributes)
        result.bases.push_back({std::string(name), values_.getBase(std::string(name), 0.0)});

    for (int index = 0; index < values_.modifierCount(); ++index) {
        const auto* modifier = values_.modifierAt(index);
        if (modifier != nullptr && contains(attributes, modifier->attribute)) result.modifiers.push_back(*modifier);
    }
    return Result<AttributeProjectionSnapshot>::success(std::move(result));
}

Result<void> AttributeProjection::restore(const AttributeProjectionSnapshot& snapshotValue, Revision expectedRevision) {
    if (!ownsSameLiveEntity(snapshotValue.owner))
        return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::StaleHandle, std::move("attribute snapshot owner is stale or does not match"), std::move("owner"), {}, "attributes.projection"));
    if (revision_ != expectedRevision)
        return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::Conflict, std::move("attribute snapshot revision does not match current state"), std::move("revision"), {}, "attributes.projection"));

    std::vector<std::string_view> names;
    names.reserve(snapshotValue.bases.size());
    for (const auto& base : snapshotValue.bases) {
        if (base.attribute.empty() || !std::isfinite(base.base))
            return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move("attribute snapshot contains invalid base data"), std::move("bases"), {}, "attributes.projection"));
        names.push_back(base.attribute);
    }

    AttributeSet            candidate = values_;
    std::vector<ModifierId> toRemove;
    for (int index = 0; index < candidate.modifierCount(); ++index) {
        const auto* modifier = candidate.modifierAt(index);
        if (modifier != nullptr && contains(names, modifier->attribute)) toRemove.push_back(modifier->id);
    }
    for (const auto& id : toRemove) {
        auto removed = candidate.removeModifier(id);
        if (!removed)
            return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvariantViolation, std::move("attribute snapshot could not remove candidate modifier"), std::move("modifiers"), {}, "attributes.projection"));
    }
    for (const auto& base : snapshotValue.bases) candidate.setBase(base.attribute, base.base);

    auto modifiers = snapshotValue.modifiers;
    std::stable_sort(modifiers.begin(), modifiers.end(),
                     [](const auto& left, const auto& right) { return left.sequence < right.sequence; });
    for (const auto& modifier : modifiers) {
        if (!contains(names, modifier.attribute))
            return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move("attribute snapshot modifier is outside its allow-list"), std::move("modifiers"), {}, "attributes.projection"));
        auto added = candidate.addModifier(modifier);
        if (!added)
            return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move("attribute snapshot contains an invalid modifier"), std::move("modifiers"), {}, "attributes.projection"));
    }

    auto next = advanceRevision();
    if (!next) return next;
    values_      = std::move(candidate);
    initialized_ = true;
    return next;
}

}  // namespace eve::attributes
