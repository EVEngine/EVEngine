#include "card/CardAttributes.h"

#include "card/CardTypes.h"

#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace eve::card {
namespace {

bool selected(std::string_view name) noexcept {
    return name == CardAttributeAdapter::attackAttribute || name == CardAttributeAdapter::healthAttribute;
}

std::array<std::string_view, 2> selectedAttributes() {
    return {CardAttributeAdapter::attackAttribute, CardAttributeAdapter::healthAttribute};
}

}  // namespace

eve::Result<void> CardAttributeAdapter::ensure(CardData& card) {
    auto bound = card.attributes()->values.bindOwner(ecs::handle_of(&card));
    if (!bound) return eve::Result<void>::failure(
        eve::Status::failure(bound.code(), eve::Diagnostic::error(eve::DiagnosticCode::Failed, "card attribute owner could not be bound", "owner", {}, "card.attributes")));
    if (!card.attributes()->values.initialized()) {
        const std::array<eve::attributes::AttributeSnapshotBase, 2> bases = {
            eve::attributes::AttributeSnapshotBase{std::string(attackAttribute),
                                                   static_cast<double>(card.stats()->attack)},
            eve::attributes::AttributeSnapshotBase{std::string(healthAttribute),
                                                   static_cast<double>(card.stats()->health)}};
        auto initialized = card.attributes()->values.initialize(bases);
        if (!initialized)
            return eve::Result<void>::failure(
        eve::Status::failure(initialized.code(), eve::Diagnostic::error(eve::DiagnosticCode::Failed, "card attribute state could not be initialized", "attributes", {}, "card.attributes")));
    }
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
}

eve::Result<void> CardAttributeAdapter::project(CardData& card) {
    auto ready = ensure(card);
    if (!ready) return eve::Result<void>::failure(
        eve::Status::failure(ready.code(), eve::Diagnostic::error(eve::DiagnosticCode::Failed, "card attributes are not available", "attributes", {}, "card.attributes")));

    auto attack = card.attributes()->values.getFinal(attackAttribute);
    if (!attack) return eve::Result<void>::failure(
        eve::Status::failure(attack.code(), eve::Diagnostic::error(eve::DiagnosticCode::Failed, "card attack attribute could not be read", "attack", {}, "card.attributes")));
    auto health = card.attributes()->values.getFinal(healthAttribute);
    if (!health) return eve::Result<void>::failure(
        eve::Status::failure(health.code(), eve::Diagnostic::error(eve::DiagnosticCode::Failed, "card health attribute could not be read", "health", {}, "card.attributes")));
    const double values[] = {attack.value(), health.value()};
    for (const double value : values) {
        if (!std::isfinite(value) || value < static_cast<double>(std::numeric_limits<int>::min()) ||
            value > static_cast<double>(std::numeric_limits<int>::max()))
            return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation, "card attribute cannot be represented by the legacy integer projection", "stats", {}, "card.attributes"));
    }
    card.stats()->attack = static_cast<int>(std::llround(attack.value()));
    card.stats()->health = static_cast<int>(std::llround(health.value()));
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<double> CardAttributeAdapter::read(CardData& card, std::string_view attribute) {
    if (!selected(attribute))
        return eve::Result<double>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Unsupported, "card attribute is outside the selective combat projection", "attribute",
            {}, "card.attributes"));
    auto projected = project(card);
    if (!projected)
        return eve::Result<double>::failure(eve::Status::failure(
            projected.code(),
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "card compatibility projection failed", "stats", {},
                                   "card.attributes")));
    auto value = card.attributes()->values.getFinal(attribute);
    if (!value)
        return eve::Result<double>::failure(eve::Status::failure(
            value.code(), eve::Diagnostic::error(eve::DiagnosticCode::Failed, "card attribute read failed", "attribute",
                                                 {}, "card.attributes")));
    return eve::Result<double>::success(value.value());
}

eve::Result<void> CardAttributeAdapter::setBase(CardData& card, std::string_view attribute, double value) {
    if (!selected(attribute))
        return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::Unsupported, "card attribute is outside the selective combat projection", "attribute", {}, "card.attributes"));
    auto ready = ensure(card);
    if (!ready) return eve::Result<void>::failure(
        eve::Status::failure(ready.code(), eve::Diagnostic::error(eve::DiagnosticCode::Failed, "card attributes are not available", "attributes", {}, "card.attributes")));
    auto changed = card.attributes()->values.setBase(attribute, value);
    if (!changed) return changed;
    return project(card);
}

eve::Result<eve::attributes::ModifierId> CardAttributeAdapter::addModifier(
    CardData& card, std::string id, std::string_view attribute, std::string source,
    eve::attributes::AttributeOperation operation, double value, eve::attributes::ModifierPriority priority) {
    if (!selected(attribute) || source.empty())
        return eve::Result<eve::attributes::ModifierId>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "card modifier requires a selected attribute and non-empty source",
            "modifier", {}, "card.attributes"));
    auto ready = ensure(card);
    if (!ready)
        return eve::Result<eve::attributes::ModifierId>::failure(eve::Status::failure(
            ready.code(), eve::Diagnostic::error(eve::DiagnosticCode::Failed, "card attributes are not available",
                                                 "attributes", {}, "card.attributes")));
    auto added = card.attributes()->values.addModifier(eve::attributes::AttributeModifier(
        std::move(id), std::string(attribute), std::move(source), operation, value, priority));
    if (!added) return added;
    auto projected = project(card);
    if (!projected)
        return eve::Result<eve::attributes::ModifierId>::failure(eve::Status::failure(
            projected.code(),
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "card compatibility projection failed", "stats", {},
                                   "card.attributes")));
    return added;
}

eve::Result<eve::attributes::AttributeProjectionSnapshot> CardAttributeAdapter::snapshot(CardData& card) {
    auto ready = ensure(card);
    if (!ready)
        return eve::Result<eve::attributes::AttributeProjectionSnapshot>::failure(eve::Status::failure(
            ready.code(), eve::Diagnostic::error(eve::DiagnosticCode::Failed, "card attributes are not available",
                                                 "attributes", {}, "card.attributes")));
    const auto names = selectedAttributes();
    return card.attributes()->values.snapshot(names);
}

eve::Result<void> CardAttributeAdapter::restore(CardData&                                           card,
                                                const eve::attributes::AttributeProjectionSnapshot& snapshotValue,
                                                eve::Revision                                       expectedRevision) {
    auto ready = ensure(card);
    if (!ready) return eve::Result<void>::failure(
        eve::Status::failure(ready.code(), eve::Diagnostic::error(eve::DiagnosticCode::Failed, "card attributes are not available", "attributes", {}, "card.attributes")));
    auto restored = card.attributes()->values.restore(snapshotValue, expectedRevision);
    if (!restored) return restored;
    return project(card);
}

}  // namespace eve::card
