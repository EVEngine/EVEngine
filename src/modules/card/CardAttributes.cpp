#include "card/CardAttributes.h"

#include "card/CardTypes.h"

#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace eve::card {
namespace {

template <typename T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "card.attributes"));
}

template <typename T>
eve::Result<T> failure(eve::Status status, std::string, std::string = {}) {
    return eve::Result<T>::failure(std::move(status));
}

template <typename T>
eve::Result<T> failure(eve::StatusCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(eve::Status::failure(
        code, eve::Diagnostic::error(eve::DiagnosticCode::Failed, std::move(message),
                                     std::move(path), {}, "card.attributes")));
}

eve::Result<void> failureVoid(eve::DiagnosticCode code, std::string message,
                              std::string path = {}) {
    return eve::Result<void>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "card.attributes"));
}

eve::Result<void> failureVoid(eve::Status status, std::string, std::string = {}) {
    return eve::Result<void>::failure(std::move(status));
}

eve::Result<void> failureVoid(eve::StatusCode code, std::string message,
                              std::string path = {}) {
    return eve::Result<void>::failure(eve::Status::failure(
        code, eve::Diagnostic::error(eve::DiagnosticCode::Failed, std::move(message),
                                     std::move(path), {}, "card.attributes")));
}

bool selected(std::string_view name) noexcept {
    return name == CardAttributeAdapter::attackAttribute ||
           name == CardAttributeAdapter::healthAttribute;
}

std::array<std::string_view, 2> selectedAttributes() {
    return {CardAttributeAdapter::attackAttribute, CardAttributeAdapter::healthAttribute};
}

}  // namespace

eve::Result<void> CardAttributeAdapter::ensure(CardData& card) {
    auto bound = card.attributes()->values.bindOwner(ecs::handle_of(&card));
    if (!bound) return failureVoid(bound.code(), "card attribute owner could not be bound", "owner");
    if (!card.attributes()->values.initialized()) {
        const std::array<eve::attributes::AttributeSnapshotBase, 2> bases = {
            eve::attributes::AttributeSnapshotBase{std::string(attackAttribute),
                                                    static_cast<double>(card.stats()->attack)},
            eve::attributes::AttributeSnapshotBase{std::string(healthAttribute),
                                                    static_cast<double>(card.stats()->health)}};
        auto initialized = card.attributes()->values.initialize(bases);
        if (!initialized) return failureVoid(initialized.code(),
                                             "card attribute state could not be initialized", "attributes");
    }
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
}

eve::Result<void> CardAttributeAdapter::project(CardData& card) {
    auto ready = ensure(card);
    if (!ready) return failureVoid(ready.code(), "card attributes are not available", "attributes");

    auto attack = card.attributes()->values.getFinal(attackAttribute);
    if (!attack) return failureVoid(attack.code(), "card attack attribute could not be read", "attack");
    auto health = card.attributes()->values.getFinal(healthAttribute);
    if (!health) return failureVoid(health.code(), "card health attribute could not be read", "health");
    const double values[] = {attack.value(), health.value()};
    for (const double value : values) {
        if (!std::isfinite(value) || value < static_cast<double>(std::numeric_limits<int>::min()) ||
            value > static_cast<double>(std::numeric_limits<int>::max()))
            return failureVoid(eve::DiagnosticCode::InvariantViolation,
                               "card attribute cannot be represented by the legacy integer projection",
                               "stats");
    }
    card.stats()->attack = static_cast<int>(std::llround(attack.value()));
    card.stats()->health = static_cast<int>(std::llround(health.value()));
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<double> CardAttributeAdapter::read(CardData& card, std::string_view attribute) {
    if (!selected(attribute))
        return failure<double>(eve::DiagnosticCode::Unsupported,
                               "card attribute is outside the selective combat projection", "attribute");
    auto projected = project(card);
    if (!projected)
        return failure<double>(projected.code(), "card compatibility projection failed", "stats");
    auto value = card.attributes()->values.getFinal(attribute);
    if (!value) return failure<double>(value.code(), "card attribute read failed", "attribute");
    return eve::Result<double>::success(value.value());
}

eve::Result<void> CardAttributeAdapter::setBase(CardData& card, std::string_view attribute,
                                                double value) {
    if (!selected(attribute))
        return failureVoid(eve::DiagnosticCode::Unsupported,
                           "card attribute is outside the selective combat projection", "attribute");
    auto ready = ensure(card);
    if (!ready) return failureVoid(ready.code(), "card attributes are not available", "attributes");
    auto changed = card.attributes()->values.setBase(attribute, value);
    if (!changed) return changed;
    return project(card);
}

eve::Result<eve::attributes::ModifierId> CardAttributeAdapter::addModifier(
    CardData& card, std::string id, std::string_view attribute, std::string source,
    eve::attributes::AttributeOperation operation, double value,
    eve::attributes::ModifierPriority priority) {
    if (!selected(attribute) || source.empty())
        return failure<eve::attributes::ModifierId>(
            eve::DiagnosticCode::InvalidArgument,
            "card modifier requires a selected attribute and non-empty source", "modifier");
    auto ready = ensure(card);
    if (!ready)
        return failure<eve::attributes::ModifierId>(ready.code(), "card attributes are not available",
                                                    "attributes");
    auto added = card.attributes()->values.addModifier(
        eve::attributes::AttributeModifier(std::move(id), std::string(attribute), std::move(source),
                                           operation, value, priority));
    if (!added) return added;
    auto projected = project(card);
    if (!projected)
        return failure<eve::attributes::ModifierId>(projected.code(),
                                                    "card compatibility projection failed", "stats");
    return added;
}

eve::Result<eve::attributes::AttributeProjectionSnapshot> CardAttributeAdapter::snapshot(
    CardData& card) {
    auto ready = ensure(card);
    if (!ready)
        return failure<eve::attributes::AttributeProjectionSnapshot>(
            ready.code(), "card attributes are not available", "attributes");
    const auto names = selectedAttributes();
    return card.attributes()->values.snapshot(names);
}

eve::Result<void> CardAttributeAdapter::restore(
    CardData& card, const eve::attributes::AttributeProjectionSnapshot& snapshotValue,
    eve::Revision expectedRevision) {
    auto ready = ensure(card);
    if (!ready) return failureVoid(ready.code(), "card attributes are not available", "attributes");
    auto restored = card.attributes()->values.restore(snapshotValue, expectedRevision);
    if (!restored) return restored;
    return project(card);
}

}  // namespace eve::card
