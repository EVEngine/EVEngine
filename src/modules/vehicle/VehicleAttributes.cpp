#include "vehicle/VehicleAttributes.h"

#include "vehicle/VehicleTypes.h"

#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace eve::vehicle {
namespace {

bool selected(std::string_view name) noexcept {
    return name == VehicleAttributeAdapter::healthAttribute || name == VehicleAttributeAdapter::maxHealthAttribute ||
           name == VehicleAttributeAdapter::armorAttribute;
}

std::array<std::string_view, 3> selectedAttributes() {
    return {VehicleAttributeAdapter::healthAttribute, VehicleAttributeAdapter::maxHealthAttribute,
            VehicleAttributeAdapter::armorAttribute};
}

}  // namespace

eve::Result<void> VehicleAttributeAdapter::ensure(VehicleEntity& vehicle) {
    auto bound = vehicle.attributes()->values.bindOwner(ecs::handle_of(&vehicle));
    if (!bound) return eve::Result<void>::failure(
        eve::Status::failure(bound.code(), eve::Diagnostic::error(eve::DiagnosticCode::Failed, "vehicle attribute owner could not be bound", "owner", {}, "vehicle.attributes")));
    if (!vehicle.attributes()->values.initialized()) {
        const std::array<eve::attributes::AttributeSnapshotBase, 3> bases = {
            eve::attributes::AttributeSnapshotBase{std::string(healthAttribute),
                                                   static_cast<double>(vehicle.health()->hp)},
            eve::attributes::AttributeSnapshotBase{std::string(maxHealthAttribute),
                                                   static_cast<double>(vehicle.health()->maxHp)},
            eve::attributes::AttributeSnapshotBase{std::string(armorAttribute), 0.0}};
        auto initialized = vehicle.attributes()->values.initialize(bases);
        if (!initialized)
            return eve::Result<void>::failure(
        eve::Status::failure(initialized.code(), eve::Diagnostic::error(eve::DiagnosticCode::Failed, "vehicle attribute state could not be initialized", "attributes", {}, "vehicle.attributes")));
    }
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
}

eve::Result<void> VehicleAttributeAdapter::project(VehicleEntity& vehicle) {
    auto ready = ensure(vehicle);
    if (!ready) return eve::Result<void>::failure(
        eve::Status::failure(ready.code(), eve::Diagnostic::error(eve::DiagnosticCode::Failed, "vehicle attributes are not available", "attributes", {}, "vehicle.attributes")));
    auto hp = vehicle.attributes()->values.getFinal(healthAttribute);
    if (!hp) return eve::Result<void>::failure(
        eve::Status::failure(hp.code(), eve::Diagnostic::error(eve::DiagnosticCode::Failed, "vehicle health attribute could not be read", "health", {}, "vehicle.attributes")));
    auto maxHp = vehicle.attributes()->values.getFinal(maxHealthAttribute);
    if (!maxHp) return eve::Result<void>::failure(
        eve::Status::failure(maxHp.code(), eve::Diagnostic::error(eve::DiagnosticCode::Failed, "vehicle max health attribute could not be read", "max_health", {}, "vehicle.attributes")));
    if (!std::isfinite(hp.value()) || !std::isfinite(maxHp.value()) || hp.value() < 0.0 || maxHp.value() < 0.0 ||
        hp.value() > maxHp.value() || hp.value() > static_cast<double>(std::numeric_limits<float>::max()) ||
        maxHp.value() > static_cast<double>(std::numeric_limits<float>::max()))
        return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation, "vehicle health attributes are outside validated bounds", "health", {}, "vehicle.attributes"));
    vehicle.health()->hp    = static_cast<float>(hp.value());
    vehicle.health()->maxHp = static_cast<float>(maxHp.value());
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<double> VehicleAttributeAdapter::read(VehicleEntity& vehicle, std::string_view attribute) {
    if (!selected(attribute))
        return eve::Result<double>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Unsupported, "vehicle attribute is outside the selective combat projection",
            "attribute", {}, "vehicle.attributes"));
    auto projected = project(vehicle);
    if (!projected)
        return eve::Result<double>::failure(eve::Status::failure(
            projected.code(),
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "vehicle compatibility projection failed", "health", {},
                                   "vehicle.attributes")));
    auto value = vehicle.attributes()->values.getFinal(attribute);
    if (!value)
        return eve::Result<double>::failure(eve::Status::failure(
            value.code(), eve::Diagnostic::error(eve::DiagnosticCode::Failed, "vehicle attribute read failed",
                                                 "attribute", {}, "vehicle.attributes")));
    return eve::Result<double>::success(value.value());
}

eve::Result<void> VehicleAttributeAdapter::setBase(VehicleEntity& vehicle, std::string_view attribute, double value) {
    if (!selected(attribute))
        return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::Unsupported, "vehicle attribute is outside the selective combat projection", "attribute", {}, "vehicle.attributes"));
    auto ready = ensure(vehicle);
    if (!ready) return eve::Result<void>::failure(
        eve::Status::failure(ready.code(), eve::Diagnostic::error(eve::DiagnosticCode::Failed, "vehicle attributes are not available", "attributes", {}, "vehicle.attributes")));
    auto changed = vehicle.attributes()->values.setBase(attribute, value);
    if (!changed) return changed;
    return project(vehicle);
}

eve::Result<eve::attributes::ModifierId> VehicleAttributeAdapter::addModifier(
    VehicleEntity& vehicle, std::string id, std::string_view attribute, std::string source,
    eve::attributes::AttributeOperation operation, double value, eve::attributes::ModifierPriority priority) {
    if (!selected(attribute) || source.empty())
        return eve::Result<eve::attributes::ModifierId>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "vehicle modifier requires a selected attribute and non-empty source",
            "modifier", {}, "vehicle.attributes"));
    auto ready = ensure(vehicle);
    if (!ready)
        return eve::Result<eve::attributes::ModifierId>::failure(eve::Status::failure(
            ready.code(), eve::Diagnostic::error(eve::DiagnosticCode::Failed, "vehicle attributes are not available",
                                                 "attributes", {}, "vehicle.attributes")));
    auto added = vehicle.attributes()->values.addModifier(eve::attributes::AttributeModifier(
        std::move(id), std::string(attribute), std::move(source), operation, value, priority));
    if (!added) return added;
    auto projected = project(vehicle);
    if (!projected)
        return eve::Result<eve::attributes::ModifierId>::failure(eve::Status::failure(
            projected.code(),
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "vehicle compatibility projection failed", "health", {},
                                   "vehicle.attributes")));
    return added;
}

eve::Result<eve::attributes::AttributeProjectionSnapshot> VehicleAttributeAdapter::snapshot(VehicleEntity& vehicle) {
    auto ready = ensure(vehicle);
    if (!ready)
        return eve::Result<eve::attributes::AttributeProjectionSnapshot>::failure(eve::Status::failure(
            ready.code(), eve::Diagnostic::error(eve::DiagnosticCode::Failed, "vehicle attributes are not available",
                                                 "attributes", {}, "vehicle.attributes")));
    const auto names = selectedAttributes();
    return vehicle.attributes()->values.snapshot(names);
}

eve::Result<void> VehicleAttributeAdapter::restore(VehicleEntity&                                      vehicle,
                                                   const eve::attributes::AttributeProjectionSnapshot& snapshotValue,
                                                   eve::Revision expectedRevision) {
    auto ready = ensure(vehicle);
    if (!ready) return eve::Result<void>::failure(
        eve::Status::failure(ready.code(), eve::Diagnostic::error(eve::DiagnosticCode::Failed, "vehicle attributes are not available", "attributes", {}, "vehicle.attributes")));
    auto restored = vehicle.attributes()->values.restore(snapshotValue, expectedRevision);
    if (!restored) return restored;
    return project(vehicle);
}

}  // namespace eve::vehicle
