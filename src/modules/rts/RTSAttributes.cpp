#include "rts/RTSAttributes.h"

#include "rts/RTSTypes.h"

#include <array>
#include <string>
#include <utility>

namespace eve::rts {
namespace {

template <typename T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(
        code, std::move(message), std::move(path), {}, "rts.attributes"));
}

template <typename T>
eve::Result<T> failure(eve::Status status, std::string, std::string = {}) {
    return eve::Result<T>::failure(std::move(status));
}

template <typename T>
eve::Result<T> failure(eve::StatusCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(eve::Status::failure(
        code, eve::Diagnostic::error(eve::DiagnosticCode::Failed, std::move(message),
                                     std::move(path), {}, "rts.attributes")));
}

eve::Result<void> failureVoid(eve::DiagnosticCode code, std::string message,
                              std::string path = {}) {
    return eve::Result<void>::failure(eve::Diagnostic::error(
        code, std::move(message), std::move(path), {}, "rts.attributes"));
}

eve::Result<void> failureVoid(eve::Status status, std::string, std::string = {}) {
    return eve::Result<void>::failure(std::move(status));
}

eve::Result<void> failureVoid(eve::StatusCode code, std::string message,
                              std::string path = {}) {
    return eve::Result<void>::failure(eve::Status::failure(
        code, eve::Diagnostic::error(eve::DiagnosticCode::Failed, std::move(message),
                                     std::move(path), {}, "rts.attributes")));
}

bool selected(std::string_view name) noexcept {
    return name == RTSUnitAttributeAdapter::attackAttribute ||
           name == RTSUnitAttributeAdapter::healthAttribute ||
           name == RTSUnitAttributeAdapter::maxHealthAttribute ||
           name == RTSUnitAttributeAdapter::armorAttribute;
}

std::array<std::string_view, 4> selectedAttributes() {
    return {RTSUnitAttributeAdapter::attackAttribute, RTSUnitAttributeAdapter::healthAttribute,
            RTSUnitAttributeAdapter::maxHealthAttribute, RTSUnitAttributeAdapter::armorAttribute};
}

}  // namespace

eve::Result<void> RTSUnitAttributeAdapter::ensure(Unit& unit) {
    auto bound = unit.attributes()->values.bindOwner(ecs::handle_of(&unit));
    if (!bound)
        return failureVoid(bound.code(), "RTS unit attribute owner could not be bound", "owner");
    if (!unit.attributes()->values.initialized()) {
        const std::array<eve::attributes::AttributeSnapshotBase, 4> bases = {
            eve::attributes::AttributeSnapshotBase{std::string(attackAttribute), 0.0},
            eve::attributes::AttributeSnapshotBase{std::string(healthAttribute), 100.0},
            eve::attributes::AttributeSnapshotBase{std::string(maxHealthAttribute), 100.0},
            eve::attributes::AttributeSnapshotBase{std::string(armorAttribute), 0.0}};
        auto initialized = unit.attributes()->values.initialize(bases);
        if (!initialized)
            return failureVoid(initialized.code(), "RTS unit attribute state could not be initialized",
                               "attributes");
    }
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
}

eve::Result<double> RTSUnitAttributeAdapter::read(Unit& unit, std::string_view attribute) {
    if (!selected(attribute))
        return failure<double>(eve::DiagnosticCode::Unsupported,
                               "RTS unit attribute is outside the selective combat projection",
                               "attribute");
    auto ready = ensure(unit);
    if (!ready) return failure<double>(ready.code(), "RTS unit attributes are not available", "attributes");
    return unit.attributes()->values.getFinal(attribute);
}

eve::Result<void> RTSUnitAttributeAdapter::setBase(Unit& unit, std::string_view attribute,
                                                   double value) {
    if (!selected(attribute))
        return failureVoid(eve::DiagnosticCode::Unsupported,
                           "RTS unit attribute is outside the selective combat projection", "attribute");
    auto ready = ensure(unit);
    if (!ready) return failureVoid(ready.code(), "RTS unit attributes are not available", "attributes");
    return unit.attributes()->values.setBase(attribute, value);
}

eve::Result<eve::attributes::ModifierId> RTSUnitAttributeAdapter::addModifier(
    Unit& unit, std::string id, std::string_view attribute, std::string source,
    eve::attributes::AttributeOperation operation, double value,
    eve::attributes::ModifierPriority priority) {
    if (!selected(attribute) || source.empty())
        return failure<eve::attributes::ModifierId>(
            eve::DiagnosticCode::InvalidArgument,
            "RTS unit modifier requires a selected attribute and non-empty source", "modifier");
    auto ready = ensure(unit);
    if (!ready)
        return failure<eve::attributes::ModifierId>(ready.code(), "RTS unit attributes are not available",
                                                    "attributes");
    return unit.attributes()->values.addModifier(
        eve::attributes::AttributeModifier(std::move(id), std::string(attribute), std::move(source),
                                           operation, value, priority));
}

eve::Result<eve::attributes::AttributeProjectionSnapshot> RTSUnitAttributeAdapter::snapshot(
    Unit& unit) {
    auto ready = ensure(unit);
    if (!ready)
        return failure<eve::attributes::AttributeProjectionSnapshot>(
            ready.code(), "RTS unit attributes are not available", "attributes");
    const auto names = selectedAttributes();
    return unit.attributes()->values.snapshot(names);
}

eve::Result<void> RTSUnitAttributeAdapter::restore(
    Unit& unit, const eve::attributes::AttributeProjectionSnapshot& snapshotValue,
    eve::Revision expectedRevision) {
    auto ready = ensure(unit);
    if (!ready) return failureVoid(ready.code(), "RTS unit attributes are not available", "attributes");
    return unit.attributes()->values.restore(snapshotValue, expectedRevision);
}

}  // namespace eve::rts
