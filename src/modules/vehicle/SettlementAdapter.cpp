#include "vehicle/SettlementAdapter.h"

#include "vehicle/VehicleAttributes.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>

namespace eve::vehicle {
namespace {

template <class T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(code, std::move(message), std::move(path)));
}

template <class T>
eve::Result<T> failure(eve::Status status) {
    return eve::Result<T>::failure(std::move(status));
}

eve::Result<void> success(eve::StatusCode code = eve::StatusCode::Ok) {
    return eve::Result<void>::success(eve::Status::success(code));
}

bool isDamageKind(std::string_view kind) noexcept { return kind == "damage"; }

bool isHealingKind(std::string_view kind) noexcept { return kind == "heal" || kind == "healing"; }

}  // namespace

VehicleSettlementAdapter::VehicleSettlementAdapter(VehicleEntity& vehicle, SubjectRef targetRef)
    : vehicle_(vehicle), targetRef_(targetRef) {}

const eve::Value* VehicleSettlementAdapter::contextValue(const settlement::SettlementContext& context,
                                                         std::string_view                     key) noexcept {
    const auto* object = context.request().context.getIf<eve::Value::Object>();
    if (object == nullptr) return nullptr;
    const auto it = object->find(std::string(key));
    return it == object->end() ? nullptr : &it->second;
}

std::optional<double> VehicleSettlementAdapter::numberValue(const eve::Value* value) noexcept {
    if (value == nullptr) return std::nullopt;
    if (const auto* number = value->getIf<double>()) return *number;
    if (const auto* integer = value->getIf<std::int64_t>()) return static_cast<double>(*integer);
    return std::nullopt;
}

std::optional<bool> VehicleSettlementAdapter::boolValue(const eve::Value* value) noexcept {
    if (value == nullptr) return std::nullopt;
    if (const auto* boolean = value->getIf<bool>()) return *boolean;
    return std::nullopt;
}

std::optional<std::string> VehicleSettlementAdapter::stringValue(const eve::Value* value) {
    if (value == nullptr) return std::nullopt;
    if (const auto* string = value->getIf<std::string>()) return *string;
    return std::nullopt;
}

bool VehicleSettlementAdapter::isDamage(const settlement::SettlementContext& context) const noexcept {
    return isDamageKind(context.request().kind);
}

bool VehicleSettlementAdapter::isHealing(const settlement::SettlementContext& context) const noexcept {
    return isHealingKind(context.request().kind);
}

eve::Result<void> VehicleSettlementAdapter::validate(settlement::SettlementContext& context) {
    if (!(context.request().target == targetRef_))
        return failure<void>(eve::DiagnosticCode::Conflict,
                             "Vehicle settlement request target does not match adapter target", "target");
    if (!isDamage(context) && !isHealing(context))
        return failure<void>(eve::DiagnosticCode::Unsupported,
                             "Vehicle settlement adapter supports damage and healing kinds only", "kind");
    if (vehicle_.health().operator->() == nullptr || vehicle_.stateFlags().operator->() == nullptr)
        return failure<void>(eve::DiagnosticCode::InvariantViolation,
                             "Vehicle settlement requires health and state components", "vehicle");
    auto hpResult = VehicleAttributeAdapter::read(vehicle_, VehicleAttributeAdapter::healthAttribute);
    if (!hpResult) return failure<void>(hpResult.status());
    auto maxHpResult = VehicleAttributeAdapter::read(vehicle_, VehicleAttributeAdapter::maxHealthAttribute);
    if (!maxHpResult) return failure<void>(maxHpResult.status());
    const double hp    = hpResult.value();
    const double maxHp = maxHpResult.value();
    if (!std::isfinite(hp) || !std::isfinite(maxHp) || hp < 0.0 || maxHp < 0.0 || hp > maxHp)
        return failure<void>(eve::DiagnosticCode::InvariantViolation,
                             "Vehicle health component is outside validated bounds", "health");
    context.setStageDetail("policy", "vehicle");
    context.setStageDetail("health_component", "VehicleAttributeAdapter::health");
    return eve::Result<void>::success();
}

eve::Result<void> VehicleSettlementAdapter::sourceModifiers(settlement::SettlementContext& context) {
    double multiplier = numberValue(contextValue(context, "source_multiplier")).value_or(1.0);
    if (!std::isfinite(multiplier) || multiplier < 0.0)
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "Vehicle source multiplier must be finite and non-negative", "source_multiplier");
    auto       scaled   = context.setMagnitude(context.magnitude() * multiplier);
    const bool scaledOk = scaled.ok();
    if (!scaledOk) {
        const auto status = scaled.status();
        return failure<void>(status);
    }

    const bool   critical           = boolValue(contextValue(context, "critical")).value_or(false);
    const double criticalMultiplier = numberValue(contextValue(context, "critical_multiplier")).value_or(1.0);
    if (!std::isfinite(criticalMultiplier) || criticalMultiplier < 1.0)
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "Vehicle critical multiplier must be finite and at least one", "critical_multiplier");
    if (critical) {
        auto       criticalScaled   = context.setMagnitude(context.magnitude() * criticalMultiplier);
        const bool criticalScaledOk = criticalScaled.ok();
        if (!criticalScaledOk) {
            const auto status = criticalScaled.status();
            return failure<void>(status);
        }
    }
    context.setCritical(critical);
    context.setStageDetail("source_multiplier", multiplier);
    context.setStageDetail("critical", critical);
    if (critical) context.setStageDetail("critical_multiplier", criticalMultiplier);
    return eve::Result<void>::success();
}

eve::Result<void> VehicleSettlementAdapter::targetMitigation(settlement::SettlementContext& context) {
    // Vehicle armor is deliberately handled by the armor/shield phase. This
    // phase remains a real no-op so every policy shares the same contract.
    context.setStageDetail("resistance", 0.0);
    return eve::Result<void>::success();
}

eve::Result<void> VehicleSettlementAdapter::armorShield(settlement::SettlementContext& context) {
    if (!isDamage(context)) return eve::Result<void>::success();

    const std::string zone           = stringValue(contextValue(context, "armor_zone")).value_or("");
    double            zoneMultiplier = 1.0;
    if (!zone.empty() && vehicle_.definition().operator->() != nullptr && vehicle_.definition()->def != nullptr) {
        for (const auto& armor : vehicle_.definition()->def->armorZones) {
            if (armor.name != zone) continue;
            if (!std::isfinite(armor.mult) || armor.mult < 0.0)
                return failure<void>(eve::DiagnosticCode::InvariantViolation,
                                     "Vehicle armor-zone multiplier is invalid", "armor_zone");
            zoneMultiplier = armor.mult;
            break;
        }
    }

    const double penetration = std::clamp(numberValue(contextValue(context, "penetration")).value_or(0.0), 0.0, 1.0);
    zoneMultiplier += penetration * (1.0 - zoneMultiplier);

    const double incidence = std::clamp(numberValue(contextValue(context, "incidence")).value_or(1.0), 0.0, 1.0);
    // Incidence 1 is a square hit. A glancing hit at zero receives half the
    // zone-adjusted amount; it is deterministic and does not require physics.
    zoneMultiplier *= 0.5 + 0.5 * incidence;

    auto armorResult = VehicleAttributeAdapter::read(vehicle_, VehicleAttributeAdapter::armorAttribute);
    if (!armorResult) return failure<void>(armorResult.status());
    const double armor = armorResult.value();
    if (!std::isfinite(armor) || armor < 0.0)
        return failure<void>(eve::DiagnosticCode::InvariantViolation,
                             "Vehicle armor attribute is outside validated bounds", "armor");
    const double armorFactor = armor == 0.0 ? 1.0 : 100.0 / (100.0 + armor);
    zoneMultiplier *= armorFactor;

    const double before    = context.magnitude();
    auto         changed   = context.setMagnitude(before * zoneMultiplier);
    const bool   changedOk = changed.ok();
    if (!changedOk) {
        const auto status = changed.status();
        return failure<void>(status);
    }
    auto       resisted   = context.addResisted(before - context.magnitude());
    const bool resistedOk = resisted.ok();
    if (!resistedOk) {
        const auto status = resisted.status();
        return failure<void>(status);
    }
    context.setStageDetail("armor_zone", zone);
    context.setStageDetail("zone_multiplier", zoneMultiplier);
    context.setStageDetail("penetration", penetration);
    context.setStageDetail("incidence", incidence);
    context.setStageDetail("armor", armor);
    context.setStageDetail("armor_factor", armorFactor);
    context.setStageDetail("shield", 0.0);
    return eve::Result<void>::success();
}

eve::Result<void> VehicleSettlementAdapter::clamp(settlement::SettlementContext& context) {
    auto hpResult = VehicleAttributeAdapter::read(vehicle_, VehicleAttributeAdapter::healthAttribute);
    if (!hpResult) return failure<void>(hpResult.status());
    auto maxHpResult = VehicleAttributeAdapter::read(vehicle_, VehicleAttributeAdapter::maxHealthAttribute);
    if (!maxHpResult) return failure<void>(maxHpResult.status());
    const double hp           = hpResult.value();
    const double maxHp        = maxHpResult.value();
    const double bound        = isDamage(context) ? hp : maxHp - hp;
    auto         configured   = context.setClampMax(std::max(0.0, bound));
    const bool   configuredOk = configured.ok();
    if (!configuredOk) {
        const auto status = configured.status();
        return failure<void>(status);
    }
    context.setStageDetail("maximum", bound);
    return eve::Result<void>::success();
}

eve::Result<settlement::PreparedApply> VehicleSettlementAdapter::prepareApply(
    const settlement::SettlementContext& context) {
    auto hpResult = VehicleAttributeAdapter::read(vehicle_, VehicleAttributeAdapter::healthAttribute);
    if (!hpResult) return failure<settlement::PreparedApply>(hpResult.status());
    auto maxHpResult = VehicleAttributeAdapter::read(vehicle_, VehicleAttributeAdapter::maxHealthAttribute);
    if (!maxHpResult) return failure<settlement::PreparedApply>(maxHpResult.status());
    const double hp     = hpResult.value();
    const double maxHp  = maxHpResult.value();
    const double nextHp = isDamage(context) ? hp - context.magnitude() : hp + context.magnitude();
    if (!std::isfinite(nextHp) || nextHp < 0.0 || nextHp > maxHp)
        return failure<settlement::PreparedApply>(eve::DiagnosticCode::InvariantViolation,
                                                  "Vehicle settlement candidate health is outside its validated bounds",
                                                  "health");

    auto beforeSnapshotResult = VehicleAttributeAdapter::snapshot(vehicle_);
    if (!beforeSnapshotResult) return failure<settlement::PreparedApply>(beforeSnapshotResult.status());
    auto beforeSnapshot = std::move(beforeSnapshotResult).takeValue();

    struct Pending {
        VehicleEntity*                          vehicle = nullptr;
        attributes::AttributeProjectionSnapshot beforeAttributes;
        bool                                    beforeDestroyed = false;
        float                                   nextHp          = 0.0f;
        bool                                    nextDestroyed   = false;
        bool                                    published       = false;
    };
    const bool nextDestroyed = isDamage(context) && nextHp <= 0.0;
    auto       pending =
        std::make_shared<Pending>(Pending{&vehicle_, std::move(beforeSnapshot), vehicle_.stateFlags()->destroyed,
                                          static_cast<float>(nextHp), nextDestroyed, false});
    return eve::Result<settlement::PreparedApply>::success(settlement::PreparedApply(
        [pending]() -> eve::Result<void> {
            auto applied = VehicleAttributeAdapter::setBase(*pending->vehicle, VehicleAttributeAdapter::healthAttribute,
                                                            pending->nextHp);
            if (!applied) return applied;
            pending->vehicle->stateFlags()->destroyed = pending->nextDestroyed;
            pending->published                        = true;
            return success(eve::StatusCode::Applied);
        },
        [pending]() {
            if (!pending->published) return;
            auto restored = VehicleAttributeAdapter::restore(*pending->vehicle, pending->beforeAttributes,
                                                             pending->vehicle->attributes()->values.revision());
            if (!restored) std::terminate();
            pending->vehicle->stateFlags()->destroyed = pending->beforeDestroyed;
        }));
}

}  // namespace eve::vehicle
