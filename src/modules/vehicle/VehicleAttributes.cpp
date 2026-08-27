#include "vehicle/VehicleAttributes.h"

#include "vehicle/VehicleTypes.h"

#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace eve::vehicle {
namespace {

template <typename T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(
        code, std::move(message), std::move(path), {}, "vehicle.attributes"));
}

template <typename T>
eve::Result<T> failure(eve::Status status, std::string, std::string = {}) {
    return eve::Result<T>::failure(std::move(status));
}

template <typename T>
eve::Result<T> failure(eve::StatusCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(eve::Status::failure(
        code, eve::Diagnostic::error(eve::DiagnosticCode::Failed, std::move(message),
                                     std::move(path), {}, "vehicle.attributes")));
}

eve::Result<void> failureVoid(eve::DiagnosticCode code, std::string message,
                              std::string path = {}) {
    return eve::Result<void>::failure(eve::Diagnostic::error(
        code, std::move(message), std::move(path), {}, "vehicle.attributes"));
}

eve::Result<void> failureVoid(eve::Status status, std::string, std::string = {}) {
    return eve::Result<void>::failure(std::move(status));
}

eve::Result<void> failureVoid(eve::StatusCode code, std::string message,
                              std::string path = {}) {
    return eve::Result<void>::failure(eve::Status::failure(
        code, eve::Diagnostic::error(eve::DiagnosticCode::Failed, std::move(message),
                                     std::move(path), {}, "vehicle.attributes")));
}

bool selected(std::string_view name) noexcept {
    return name == VehicleAttributeAdapter::healthAttribute ||
           name == VehicleAttributeAdapter::maxHealthAttribute ||
           name == VehicleAttributeAdapter::armorAttribute;
}

std::array<std::string_view, 3> selectedAttributes() {
    return {VehicleAttributeAdapter::healthAttribute,
            VehicleAttributeAdapter::maxHealthAttribute,
            VehicleAttributeAdapter::armorAttribute};
}

}  // namespace

eve::Result<void> VehicleAttributeAdapter::ensure(VehicleEntity& vehicle) {
    auto bound = vehicle.attributes()->values.bindOwner(ecs::handle_of(&vehicle));
    if (!bound)
        return failureVoid(bound.code(), "vehicle attribute owner could not be bound", "owner");
    if (!vehicle.attributes()->values.initialized()) {
        const std::array<eve::attributes::AttributeSnapshotBase, 3> bases = {
            eve::attributes::AttributeSnapshotBase{std::string(healthAttribute),
                                                    static_cast<double>(vehicle.health()->hp)},
            eve::attributes::AttributeSnapshotBase{std::string(maxHealthAttribute),
                                                    static_cast<double>(vehicle.health()->maxHp)},
            eve::attributes::AttributeSnapshotBase{std::string(armorAttribute), 0.0}};
        auto initialized = vehicle.attributes()->values.initialize(bases);
        if (!initialized)
            return failureVoid(initialized.code(), "vehicle attribute state could not be initialized",
                               "attributes");
    }
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
}

eve::Result<void> VehicleAttributeAdapter::project(VehicleEntity& vehicle) {
    auto ready = ensure(vehicle);
    if (!ready) return failureVoid(ready.code(), "vehicle attributes are not available", "attributes");
    auto hp = vehicle.attributes()->values.getFinal(healthAttribute);
    if (!hp) return failureVoid(hp.code(), "vehicle health attribute could not be read", "health");
    auto maxHp = vehicle.attributes()->values.getFinal(maxHealthAttribute);
    if (!maxHp)
        return failureVoid(maxHp.code(), "vehicle max health attribute could not be read", "max_health");
    if (!std::isfinite(hp.value()) || !std::isfinite(maxHp.value()) || hp.value() < 0.0 ||
        maxHp.value() < 0.0 || hp.value() > maxHp.value() ||
        hp.value() > static_cast<double>(std::numeric_limits<float>::max()) ||
        maxHp.value() > static_cast<double>(std::numeric_limits<float>::max()))
        return failureVoid(eve::DiagnosticCode::InvariantViolation,
                           "vehicle health attributes are outside validated bounds", "health");
    vehicle.health()->hp = static_cast<float>(hp.value());
    vehicle.health()->maxHp = static_cast<float>(maxHp.value());
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<double> VehicleAttributeAdapter::read(VehicleEntity& vehicle,
                                                  std::string_view attribute) {
    if (!selected(attribute))
        return failure<double>(eve::DiagnosticCode::Unsupported,
                               "vehicle attribute is outside the selective combat projection",
                               "attribute");
    auto projected = project(vehicle);
    if (!projected)
        return failure<double>(projected.code(), "vehicle compatibility projection failed", "health");
    auto value = vehicle.attributes()->values.getFinal(attribute);
    if (!value) return failure<double>(value.code(), "vehicle attribute read failed", "attribute");
    return eve::Result<double>::success(value.value());
}

eve::Result<void> VehicleAttributeAdapter::setBase(VehicleEntity& vehicle, std::string_view attribute,
                                                   double value) {
    if (!selected(attribute))
        return failureVoid(eve::DiagnosticCode::Unsupported,
                           "vehicle attribute is outside the selective combat projection", "attribute");
    auto ready = ensure(vehicle);
    if (!ready) return failureVoid(ready.code(), "vehicle attributes are not available", "attributes");
    auto changed = vehicle.attributes()->values.setBase(attribute, value);
    if (!changed) return changed;
    return project(vehicle);
}

eve::Result<eve::attributes::ModifierId> VehicleAttributeAdapter::addModifier(
    VehicleEntity& vehicle, std::string id, std::string_view attribute, std::string source,
    eve::attributes::AttributeOperation operation, double value,
    eve::attributes::ModifierPriority priority) {
    if (!selected(attribute) || source.empty())
        return failure<eve::attributes::ModifierId>(
            eve::DiagnosticCode::InvalidArgument,
            "vehicle modifier requires a selected attribute and non-empty source", "modifier");
    auto ready = ensure(vehicle);
    if (!ready)
        return failure<eve::attributes::ModifierId>(ready.code(), "vehicle attributes are not available",
                                                    "attributes");
    auto added = vehicle.attributes()->values.addModifier(
        eve::attributes::AttributeModifier(std::move(id), std::string(attribute), std::move(source),
                                           operation, value, priority));
    if (!added) return added;
    auto projected = project(vehicle);
    if (!projected)
        return failure<eve::attributes::ModifierId>(projected.code(),
                                                    "vehicle compatibility projection failed", "health");
    return added;
}

eve::Result<eve::attributes::AttributeProjectionSnapshot> VehicleAttributeAdapter::snapshot(
    VehicleEntity& vehicle) {
    auto ready = ensure(vehicle);
    if (!ready)
        return failure<eve::attributes::AttributeProjectionSnapshot>(
            ready.code(), "vehicle attributes are not available", "attributes");
    const auto names = selectedAttributes();
    return vehicle.attributes()->values.snapshot(names);
}

eve::Result<void> VehicleAttributeAdapter::restore(
    VehicleEntity& vehicle, const eve::attributes::AttributeProjectionSnapshot& snapshotValue,
    eve::Revision expectedRevision) {
    auto ready = ensure(vehicle);
    if (!ready) return failureVoid(ready.code(), "vehicle attributes are not available", "attributes");
    auto restored = vehicle.attributes()->values.restore(snapshotValue, expectedRevision);
    if (!restored) return restored;
    return project(vehicle);
}

}  // namespace eve::vehicle
