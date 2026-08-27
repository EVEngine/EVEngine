#include "weapon/WeaponAttributes.h"

#include "weapon/WeaponTypes.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace eve::weapon {
namespace {

template <typename T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(
        code, std::move(message), std::move(path), {}, "weapon.attributes"));
}

template <typename T>
eve::Result<T> failure(eve::Status status, std::string, std::string = {}) {
    return eve::Result<T>::failure(std::move(status));
}

template <typename T>
eve::Result<T> failure(eve::StatusCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(eve::Status::failure(
        code, eve::Diagnostic::error(eve::DiagnosticCode::Failed, std::move(message),
                                     std::move(path), {}, "weapon.attributes")));
}

eve::Result<void> failureVoid(eve::DiagnosticCode code, std::string message,
                              std::string path = {}) {
    return eve::Result<void>::failure(eve::Diagnostic::error(
        code, std::move(message), std::move(path), {}, "weapon.attributes"));
}

eve::Result<void> failureVoid(eve::Status status, std::string, std::string = {}) {
    return eve::Result<void>::failure(std::move(status));
}

eve::Result<void> failureVoid(eve::StatusCode code, std::string message,
                              std::string path = {}) {
    return eve::Result<void>::failure(eve::Status::failure(
        code, eve::Diagnostic::error(eve::DiagnosticCode::Failed, std::move(message),
                                     std::move(path), {}, "weapon.attributes")));
}

bool selected(std::string_view name) noexcept {
    return name == WeaponAttributeAdapter::manaAttribute ||
           name == WeaponAttributeAdapter::maxManaAttribute ||
           name == WeaponAttributeAdapter::staminaAttribute ||
           name == WeaponAttributeAdapter::maxStaminaAttribute;
}

std::array<std::string_view, 4> selectedAttributes() {
    return {WeaponAttributeAdapter::manaAttribute, WeaponAttributeAdapter::maxManaAttribute,
            WeaponAttributeAdapter::staminaAttribute, WeaponAttributeAdapter::maxStaminaAttribute};
}

bool isManaOrStamina(ResourceKind kind) noexcept {
    return kind == ResourceKind::Mana || kind == ResourceKind::Stamina;
}

std::string_view currentAttribute(ResourceKind kind) noexcept {
    return kind == ResourceKind::Mana ? WeaponAttributeAdapter::manaAttribute
                                      : WeaponAttributeAdapter::staminaAttribute;
}

std::string_view maxAttribute(ResourceKind kind) noexcept {
    return kind == ResourceKind::Mana ? WeaponAttributeAdapter::maxManaAttribute
                                      : WeaponAttributeAdapter::maxStaminaAttribute;
}

}  // namespace

eve::Result<void> WeaponAttributeAdapter::ensure(WeaponEntity& weapon) {
    auto bound = weapon.attributes()->values.bindOwner(ecs::handle_of(&weapon));
    if (!bound)
        return failureVoid(bound.code(), "weapon attribute owner could not be bound", "owner");
    if (!weapon.attributes()->values.initialized()) {
        const Resource& resource = weapon.state()->resource;
        const double mana = resource.kind == ResourceKind::Mana ? resource.value : 0.0;
        const double maxMana = resource.kind == ResourceKind::Mana ? resource.max : 0.0;
        const double stamina = resource.kind == ResourceKind::Stamina ? resource.value : 0.0;
        const double maxStamina = resource.kind == ResourceKind::Stamina ? resource.max : 0.0;
        const std::array<eve::attributes::AttributeSnapshotBase, 4> bases = {
            eve::attributes::AttributeSnapshotBase{std::string(manaAttribute), mana},
            eve::attributes::AttributeSnapshotBase{std::string(maxManaAttribute), maxMana},
            eve::attributes::AttributeSnapshotBase{std::string(staminaAttribute), stamina},
            eve::attributes::AttributeSnapshotBase{std::string(maxStaminaAttribute), maxStamina}};
        auto initialized = weapon.attributes()->values.initialize(bases);
        if (!initialized)
            return failureVoid(initialized.code(), "weapon attribute state could not be initialized",
                               "attributes");
    }
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
}

eve::Result<void> WeaponAttributeAdapter::project(WeaponEntity& weapon) {
    auto ready = ensure(weapon);
    if (!ready) return failureVoid(ready.code(), "weapon attributes are not available", "attributes");
    Resource& resource = weapon.state()->resource;
    if (!isManaOrStamina(resource.kind))
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
    auto current = weapon.attributes()->values.getFinal(currentAttribute(resource.kind));
    if (!current)
        return failureVoid(current.code(), "weapon resource attribute could not be read", "resource");
    auto maximum = weapon.attributes()->values.getFinal(maxAttribute(resource.kind));
    if (!maximum)
        return failureVoid(maximum.code(), "weapon maximum resource attribute could not be read", "resource");
    if (!std::isfinite(current.value()) || !std::isfinite(maximum.value()) || current.value() < 0.0 ||
        maximum.value() < 0.0 || current.value() > maximum.value() ||
        maximum.value() > static_cast<double>(std::numeric_limits<float>::max()))
        return failureVoid(eve::DiagnosticCode::InvariantViolation,
                           "weapon mana/stamina attributes are outside validated bounds", "resource");
    resource.value = static_cast<float>(current.value());
    resource.max = static_cast<float>(maximum.value());
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<double> WeaponAttributeAdapter::read(WeaponEntity& weapon,
                                                  std::string_view attribute) {
    if (!selected(attribute))
        return failure<double>(eve::DiagnosticCode::Unsupported,
                               "weapon attribute is outside the mana/stamina projection", "attribute");
    auto projected = project(weapon);
    if (!projected)
        return failure<double>(projected.code(), "weapon compatibility projection failed", "resource");
    auto value = weapon.attributes()->values.getFinal(attribute);
    if (!value) return failure<double>(value.code(), "weapon attribute read failed", "attribute");
    return eve::Result<double>::success(value.value());
}

eve::Result<void> WeaponAttributeAdapter::setBase(WeaponEntity& weapon, std::string_view attribute,
                                                  double value) {
    if (!selected(attribute))
        return failureVoid(eve::DiagnosticCode::Unsupported,
                           "weapon attribute is outside the mana/stamina projection", "attribute");
    auto ready = ensure(weapon);
    if (!ready) return failureVoid(ready.code(), "weapon attributes are not available", "attributes");
    auto changed = weapon.attributes()->values.setBase(attribute, value);
    if (!changed) return changed;
    return project(weapon);
}

eve::Result<void> WeaponAttributeAdapter::consumeTriggerResource(WeaponEntity& weapon) {
    auto ready = ensure(weapon);
    if (!ready) return failureVoid(ready.code(), "weapon attributes are not available", "attributes");
    const Resource& resource = weapon.state()->resource;
    if (!isManaOrStamina(resource.kind))
        return failureVoid(eve::DiagnosticCode::Unsupported,
                           "weapon trigger resource is not mana or stamina", "resource.kind");
    const auto currentName = currentAttribute(resource.kind);
    auto current = weapon.attributes()->values.getFinal(currentName);
    if (!current) return failureVoid(current.code(), "weapon trigger resource could not be read", "resource");
    if (!resource.infinite && current.value() < static_cast<double>(resource.cost))
        return failureVoid(eve::DiagnosticCode::Conflict,
                           "weapon does not have enough mana or stamina", "resource");
    if (resource.infinite) return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
    auto changed = weapon.attributes()->values.setBase(
        currentName, std::max(0.0, current.value() - static_cast<double>(resource.cost)));
    if (!changed) return changed;
    return project(weapon);
}

eve::Result<eve::attributes::ModifierId> WeaponAttributeAdapter::addModifier(
    WeaponEntity& weapon, std::string id, std::string_view attribute, std::string source,
    eve::attributes::AttributeOperation operation, double value,
    eve::attributes::ModifierPriority priority) {
    if (!selected(attribute) || source.empty())
        return failure<eve::attributes::ModifierId>(
            eve::DiagnosticCode::InvalidArgument,
            "weapon modifier requires a selected attribute and non-empty source", "modifier");
    auto ready = ensure(weapon);
    if (!ready)
        return failure<eve::attributes::ModifierId>(ready.code(), "weapon attributes are not available",
                                                    "attributes");
    auto added = weapon.attributes()->values.addModifier(
        eve::attributes::AttributeModifier(std::move(id), std::string(attribute), std::move(source),
                                           operation, value, priority));
    if (!added) return added;
    auto projected = project(weapon);
    if (!projected)
        return failure<eve::attributes::ModifierId>(projected.code(),
                                                    "weapon compatibility projection failed", "resource");
    return added;
}

eve::Result<eve::attributes::AttributeProjectionSnapshot> WeaponAttributeAdapter::snapshot(
    WeaponEntity& weapon) {
    auto ready = ensure(weapon);
    if (!ready)
        return failure<eve::attributes::AttributeProjectionSnapshot>(
            ready.code(), "weapon attributes are not available", "attributes");
    const auto names = selectedAttributes();
    return weapon.attributes()->values.snapshot(names);
}

eve::Result<void> WeaponAttributeAdapter::restore(
    WeaponEntity& weapon, const eve::attributes::AttributeProjectionSnapshot& snapshotValue,
    eve::Revision expectedRevision) {
    auto ready = ensure(weapon);
    if (!ready) return failureVoid(ready.code(), "weapon attributes are not available", "attributes");
    auto restored = weapon.attributes()->values.restore(snapshotValue, expectedRevision);
    if (!restored) return restored;
    return project(weapon);
}

}  // namespace eve::weapon
