#include "rpg/SettlementAdapter.h"

#include "rpg/AttributeSystem.h"
#include "rpg/Effect.h"
#include "rpg/VitalsSystem.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <utility>

namespace eve::rpg {
namespace {

eve::Result<void> success(eve::StatusCode code = eve::StatusCode::Ok) {
    return eve::Result<void>::success(eve::Status::success(code));
}

bool isDamageKind(std::string_view kind) noexcept { return kind == "damage"; }

bool isHealingKind(std::string_view kind) noexcept { return kind == "heal" || kind == "healing"; }

eve::Result<double> checkedNumber(double value, std::string_view name) {
    if (!std::isfinite(value))
        return eve::Result<double>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvariantViolation, std::string(name) + " must be finite", std::string(name)));
    return eve::Result<double>::success(value);
}

eve::Result<settlement::SettlementRequest> requestFailure(eve::DiagnosticCode code, std::string message,
                                                           std::string path) {
    return eve::Result<settlement::SettlementRequest>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path)));
}

eve::Result<settlement::SettlementRequest> requestFailure(eve::Status status) {
    return eve::Result<settlement::SettlementRequest>::failure(std::move(status));
}

const eve::Value* objectField(const eve::Value::Object& object, std::string_view name) {
    const auto found = object.find(std::string(name));
    return found == object.end() ? nullptr : &found->second;
}

}  // namespace

eve::Result<settlement::SettlementRequest> makeStatusTickSettlementRequest(
    const StatusTickEvent& tick, SubjectRef source, SubjectRef target, SimulationTick simulationTick) {
    if (tick.actor == nullptr || tick.effectId.empty() || tick.instanceId <= 0 || tick.stacks <= 0)
        return requestFailure(eve::DiagnosticCode::InvalidArgument, "RPG status tick identity is invalid", "tick");
    if (!target.isValid())
        return requestFailure(eve::DiagnosticCode::InvalidArgument, "RPG status tick target must be valid", "target");
    const EffectDefinition* definition = EffectRegistry::find(tick.effectId);
    if (definition == nullptr)
        return requestFailure(eve::DiagnosticCode::NotFound, "RPG status tick definition was not found",
                              "tick.effectId");
    const auto encoded = definition->extra.find("settlement.tick");
    if (encoded == definition->extra.end())
        return requestFailure(eve::DiagnosticCode::NotFound, "RPG effect has no settlement.tick configuration",
                              "effect.extra.settlement.tick");
    auto parsed = eve::Value::fromJson(encoded->second);
    if (!parsed) return requestFailure(parsed.status());
    const auto* object = parsed.value().getIf<eve::Value::Object>();
    if (object == nullptr)
        return requestFailure(eve::DiagnosticCode::InvalidArgument, "settlement.tick must be a JSON object",
                              "effect.extra.settlement.tick");
    static const std::set<std::string> fields = {"context", "kind", "magnitude", "resource", "tags"};
    for (const auto& [name, value] : *object) {
        (void)value;
        if (!fields.contains(name))
            return requestFailure(eve::DiagnosticCode::InvalidArgument, "unknown settlement.tick field",
                                  "effect.extra.settlement.tick." + name);
    }
    const auto* kindValue      = objectField(*object, "kind");
    const auto* resourceValue  = objectField(*object, "resource");
    const auto* magnitudeValue = objectField(*object, "magnitude");
    const auto* kind           = kindValue == nullptr ? nullptr : kindValue->getIf<std::string>();
    const auto* resource       = resourceValue == nullptr ? nullptr : resourceValue->getIf<std::string>();
    if (kind == nullptr || kind->empty() || resource == nullptr || resource->empty() || magnitudeValue == nullptr)
        return requestFailure(eve::DiagnosticCode::InvalidArgument,
                              "settlement.tick requires kind, resource, and magnitude",
                              "effect.extra.settlement.tick");
    double magnitude = 0.0;
    if (const auto* real = magnitudeValue->getIf<double>())
        magnitude = *real;
    else if (const auto* integer = magnitudeValue->getIf<std::int64_t>())
        magnitude = static_cast<double>(*integer);
    else
        return requestFailure(eve::DiagnosticCode::InvalidArgument, "settlement.tick magnitude must be a number",
                              "effect.extra.settlement.tick.magnitude");
    magnitude *= static_cast<double>(tick.stacks);
    if (!std::isfinite(magnitude) || magnitude < 0.0)
        return requestFailure(eve::DiagnosticCode::InvalidArgument,
                              "settlement.tick magnitude must be finite and non-negative",
                              "effect.extra.settlement.tick.magnitude");

    settlement::SettlementRequest request;
    request.source    = source;
    request.target    = target;
    request.kind      = *kind;
    request.resource  = *resource;
    request.magnitude = magnitude;
    request.tick      = simulationTick;
    request.tags      = definition->tags;
    request.tags.push_back("rpg:status-tick");
    if (const auto* tagsValue = objectField(*object, "tags")) {
        const auto* tags = tagsValue->getIf<eve::Value::Array>();
        if (tags == nullptr)
            return requestFailure(eve::DiagnosticCode::InvalidArgument, "settlement.tick tags must be an array",
                                  "effect.extra.settlement.tick.tags");
        for (std::size_t index = 0; index < tags->size(); ++index) {
            const auto* tag = (*tags)[index].getIf<std::string>();
            if (tag == nullptr || tag->empty())
                return requestFailure(eve::DiagnosticCode::InvalidArgument,
                                      "settlement.tick tag must be a non-empty string",
                                      "effect.extra.settlement.tick.tags[" + std::to_string(index) + "]");
            request.tags.push_back(*tag);
        }
    }
    std::sort(request.tags.begin(), request.tags.end());
    request.tags.erase(std::unique(request.tags.begin(), request.tags.end()), request.tags.end());
    eve::Value::Object context;
    if (const auto* contextValue = objectField(*object, "context")) {
        const auto* configured = contextValue->getIf<eve::Value::Object>();
        if (configured == nullptr)
            return requestFailure(eve::DiagnosticCode::InvalidArgument, "settlement.tick context must be an object",
                                  "effect.extra.settlement.tick.context");
        context = *configured;
    }
    context["effect_id"]         = tick.effectId;
    context["status_instance_id"] = static_cast<std::int64_t>(tick.instanceId);
    context["stacks"]              = static_cast<std::int64_t>(tick.stacks);
    request.context                 = eve::Value(std::move(context));
    return eve::Result<settlement::SettlementRequest>::success(std::move(request));
}

RPGSettlementAdapter::RPGSettlementAdapter(RPGActor& target, SubjectRef targetRef)
    : RPGSettlementAdapter(target, std::move(targetRef), Config{}) {}

RPGSettlementAdapter::RPGSettlementAdapter(RPGActor& target, SubjectRef targetRef, Config config)
    : target_(target), targetRef_(targetRef), config_(std::move(config)) {}

RPGSettlementAdapter::RPGSettlementAdapter(RPGActor& target, SubjectRef targetRef, RPGActor* source,
                                           SubjectRef sourceRef)
    : RPGSettlementAdapter(target, std::move(targetRef), source, std::move(sourceRef), Config{}) {}

RPGSettlementAdapter::RPGSettlementAdapter(RPGActor& target, SubjectRef targetRef, RPGActor* source,
                                           SubjectRef sourceRef, Config config)
    : target_(target), targetRef_(targetRef), source_(source), sourceRef_(sourceRef), config_(std::move(config)) {}

bool RPGSettlementAdapter::isDamage(const settlement::SettlementContext& context) const noexcept {
    return isDamageKind(context.request().kind);
}

bool RPGSettlementAdapter::isHealing(const settlement::SettlementContext& context) const noexcept {
    return isHealingKind(context.request().kind);
}

const eve::Value* RPGSettlementAdapter::contextValue(const settlement::SettlementContext& context,
                                                     std::string_view                     key) noexcept {
    const auto* object = context.request().context.getIf<eve::Value::Object>();
    if (object == nullptr) return nullptr;
    const auto it = object->find(std::string(key));
    return it == object->end() ? nullptr : &it->second;
}

std::optional<double> RPGSettlementAdapter::numberValue(const eve::Value* value) noexcept {
    if (value == nullptr) return std::nullopt;
    if (const auto* number = value->getIf<double>()) return *number;
    if (const auto* integer = value->getIf<std::int64_t>()) return static_cast<double>(*integer);
    return std::nullopt;
}

std::optional<bool> RPGSettlementAdapter::boolValue(const eve::Value* value) noexcept {
    if (value == nullptr) return std::nullopt;
    if (const auto* boolean = value->getIf<bool>()) return *boolean;
    return std::nullopt;
}

std::optional<std::string> RPGSettlementAdapter::stringValue(const eve::Value* value) {
    if (value == nullptr) return std::nullopt;
    if (const auto* string = value->getIf<std::string>()) return *string;
    return std::nullopt;
}

eve::Result<double> RPGSettlementAdapter::readBase(std::string_view name) const {
    if (name.empty())
        return eve::Result<double>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "RPG settlement attribute name must not be empty", "attribute"));
    const auto& attributes = target_.attributes()->values;
    if (!attributes.has(std::string(name)))
        return eve::Result<double>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "RPG settlement attribute is not present", std::string(name)));
    return checkedNumber(attributes.getBase(std::string(name)), name);
}

eve::Result<double> RPGSettlementAdapter::readFinal(std::string_view name, double fallback) const {
    if (name.empty()) return eve::Result<double>::success(fallback);
    const auto& attributes = target_.attributes()->values;
    if (!attributes.has(std::string(name))) return eve::Result<double>::success(fallback);
    return checkedNumber(attributes.getFinal(std::string(name), fallback), name);
}

eve::Result<double> RPGSettlementAdapter::readSourceFinal(std::string_view name, double fallback) const {
    if (source_ == nullptr || !sourceRef_.isValid()) return eve::Result<double>::success(fallback);
    const auto& attributes = source_->attributes()->values;
    if (!attributes.has(std::string(name))) return eve::Result<double>::success(fallback);
    return checkedNumber(attributes.getFinal(std::string(name), fallback), name);
}

eve::Result<void> RPGSettlementAdapter::validate(settlement::SettlementContext& context) {
    if (!(context.request().target == targetRef_))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "RPG settlement request target does not match adapter target", "target"));
    const bool damage  = isDamage(context);
    const bool healing = isHealing(context);
    const bool trigger = context.request().kind == "trigger";
    if (!damage && !healing && !trigger)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Unsupported, "RPG settlement adapter supports damage, healing and trigger kinds only",
            "kind"));

    auto       health   = readBase(config_.healthAttribute);
    const bool healthOk = health.ok();
    if (!healthOk) {
        const auto status = health.status();
        return eve::Result<void>::failure(status);
    }
    auto       maximum   = readBase(config_.maxHealthAttribute);
    const bool maximumOk = maximum.ok();
    if (!maximumOk) {
        const auto status = maximum.status();
        return eve::Result<void>::failure(status);
    }
    if (health.value() > maximum.value())
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation, "RPG health exceeds max health", "health"));
    context.setStageDetail("policy", "rpg");
    context.setStageDetail("health_attribute", config_.healthAttribute);
    context.setStageDetail("kind", context.request().kind);
    (void)damage;
    return eve::Result<void>::success();
}

eve::Result<void> RPGSettlementAdapter::sourceModifiers(settlement::SettlementContext& context) {
    if (context.request().kind == "trigger") return context.setMagnitude(0.0);
    double multiplier = 1.0;
    if (const auto value = numberValue(contextValue(context, "source_multiplier"))) {
        multiplier = *value;
    } else {
        auto       sourceMultiplier   = readSourceFinal(config_.sourceMultiplierAttribute, 1.0);
        const bool sourceMultiplierOk = sourceMultiplier.ok();
        if (!sourceMultiplierOk) {
            const auto status = sourceMultiplier.status();
            return eve::Result<void>::failure(status);
        }
        multiplier = sourceMultiplier.value();
    }
    if (!std::isfinite(multiplier) || multiplier < 0.0)
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                   "RPG source multiplier must be finite and non-negative", "source_multiplier"));

    auto       scaled   = context.setMagnitude(context.magnitude() * multiplier);
    const bool scaledOk = scaled.ok();
    if (!scaledOk) {
        const auto status = scaled.status();
        return eve::Result<void>::failure(status);
    }

    bool critical = boolValue(contextValue(context, "critical")).value_or(false);
    if (!critical) {
        const auto roll     = numberValue(contextValue(context, "critical_roll"));
        auto       chance   = readSourceFinal(config_.criticalChanceAttribute, 0.0);
        const bool chanceOk = chance.ok();
        if (!chanceOk) {
            const auto status = chance.status();
            return eve::Result<void>::failure(status);
        }
        if (roll && std::isfinite(*roll)) {
            const double probability = std::clamp(chance.value(), 0.0, 1.0);
            critical                 = *roll >= 0.0 && *roll < probability;
        }
    }

    double criticalMultiplier = 1.0;
    if (critical) {
        if (const auto value = numberValue(contextValue(context, "critical_multiplier"))) {
            criticalMultiplier = *value;
        } else {
            auto       configured   = readSourceFinal(config_.criticalMultiplierAttribute, 2.0);
            const bool configuredOk = configured.ok();
            if (!configuredOk) {
                const auto status = configured.status();
                return eve::Result<void>::failure(status);
            }
            criticalMultiplier = configured.value();
        }
        if (!std::isfinite(criticalMultiplier) || criticalMultiplier < 1.0)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "RPG critical multiplier must be finite and at least one",
                "critical_multiplier"));
        auto       critScaled   = context.setMagnitude(context.magnitude() * criticalMultiplier);
        const bool critScaledOk = critScaled.ok();
        if (!critScaledOk) {
            const auto status = critScaled.status();
            return eve::Result<void>::failure(status);
        }
    }
    context.setCritical(critical);
    context.setStageDetail("source_multiplier", multiplier);
    context.setStageDetail("critical", critical);
    if (critical) context.setStageDetail("critical_multiplier", criticalMultiplier);
    return eve::Result<void>::success();
}

eve::Result<void> RPGSettlementAdapter::targetMitigation(settlement::SettlementContext& context) {
    if (!isDamage(context)) return eve::Result<void>::success();

    std::string       element             = stringValue(contextValue(context, "element")).value_or("damage");
    const std::string resistanceAttribute = config_.resistancePrefix + element;
    auto              resistance          = readFinal(resistanceAttribute, 0.0);
    const bool        resistanceOk        = resistance.ok();
    if (!resistanceOk) {
        const auto status = resistance.status();
        return eve::Result<void>::failure(status);
    }
    const double factor    = 1.0 - std::clamp(resistance.value(), 0.0, 1.0);
    const double before    = context.magnitude();
    auto         changed   = context.setMagnitude(before * factor);
    const bool   changedOk = changed.ok();
    if (!changedOk) {
        const auto status = changed.status();
        return eve::Result<void>::failure(status);
    }
    auto       recorded   = context.addResisted(before - context.magnitude());
    const bool recordedOk = recorded.ok();
    if (!recordedOk) {
        const auto status = recorded.status();
        return eve::Result<void>::failure(status);
    }
    context.setStageDetail("element", element);
    context.setStageDetail("resistance", resistance.value());
    return eve::Result<void>::success();
}

eve::Result<void> RPGSettlementAdapter::armorShield(settlement::SettlementContext& context) {
    if (!isDamage(context)) return eve::Result<void>::success();

    auto       armor   = readFinal(config_.armorAttribute, 0.0);
    const bool armorOk = armor.ok();
    if (!armorOk) {
        const auto status = armor.status();
        return eve::Result<void>::failure(status);
    }
    const double penetration    = std::max(0.0, numberValue(contextValue(context, "penetration")).value_or(0.0));
    const double effectiveArmor = std::max(0.0, armor.value() - penetration);
    const double armorFactor    = 100.0 / (100.0 + effectiveArmor);
    const double beforeArmor    = context.magnitude();
    auto         armorChanged   = context.setMagnitude(beforeArmor * armorFactor);
    const bool   armorChangedOk = armorChanged.ok();
    if (!armorChangedOk) {
        const auto status = armorChanged.status();
        return eve::Result<void>::failure(status);
    }
    auto       armorLoss   = context.addResisted(beforeArmor - context.magnitude());
    const bool armorLossOk = armorLoss.ok();
    if (!armorLossOk) {
        const auto status = armorLoss.status();
        return eve::Result<void>::failure(status);
    }

    auto   shield      = readBase(config_.shieldAttribute);
    double shieldValue = 0.0;
    if (shield.ok()) {
        shieldValue = shield.value();
    } else if (shield.code() != eve::StatusCode::NotFound) {
        const auto status = shield.status();
        return eve::Result<void>::failure(status);
    }
    const double absorbed        = std::min(shieldValue, context.magnitude());
    auto         shieldChanged   = context.setMagnitude(context.magnitude() - absorbed);
    const bool   shieldChangedOk = shieldChanged.ok();
    if (!shieldChangedOk) {
        const auto status = shieldChanged.status();
        return eve::Result<void>::failure(status);
    }
    auto       absorbedRecorded   = context.addAbsorbed(absorbed);
    const bool absorbedRecordedOk = absorbedRecorded.ok();
    if (!absorbedRecordedOk) {
        const auto status = absorbedRecorded.status();
        return eve::Result<void>::failure(status);
    }
    context.setStageDetail("armor", armor.value());
    context.setStageDetail("penetration", penetration);
    context.setStageDetail("shield", shieldValue);
    context.setStageDetail("absorbed", absorbed);
    return eve::Result<void>::success();
}

eve::Result<void> RPGSettlementAdapter::clamp(settlement::SettlementContext& context) {
    auto       health   = readBase(config_.healthAttribute);
    const bool healthOk = health.ok();
    if (!healthOk) {
        const auto status = health.status();
        return eve::Result<void>::failure(status);
    }
    auto       maximum   = readBase(config_.maxHealthAttribute);
    const bool maximumOk = maximum.ok();
    if (!maximumOk) {
        const auto status = maximum.status();
        return eve::Result<void>::failure(status);
    }
    const double bound        = isDamage(context) ? health.value() : maximum.value() - health.value();
    auto         configured   = context.setClampMax(std::max(0.0, bound));
    const bool   configuredOk = configured.ok();
    if (!configuredOk) {
        const auto status = configured.status();
        return eve::Result<void>::failure(status);
    }
    context.setStageDetail("maximum", bound);
    return eve::Result<void>::success();
}

eve::Result<settlement::PreparedApply> RPGSettlementAdapter::prepareApply(
    const settlement::SettlementContext& context) {
    auto       health   = readBase(config_.healthAttribute);
    const bool healthOk = health.ok();
    if (!healthOk) {
        const auto status = health.status();
        return eve::Result<settlement::PreparedApply>::failure(status);
    }
    auto       maximum   = readBase(config_.maxHealthAttribute);
    const bool maximumOk = maximum.ok();
    if (!maximumOk) {
        const auto status = maximum.status();
        return eve::Result<settlement::PreparedApply>::failure(status);
    }

    auto         candidate = target_.attributes()->values;
    const double nextHealth =
        isDamage(context) ? health.value() - context.magnitude() : health.value() + context.magnitude();
    if (!std::isfinite(nextHealth) || nextHealth < 0.0 || nextHealth > maximum.value())
        return eve::Result<settlement::PreparedApply>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation,
                                   "RPG settlement candidate health is outside its validated bounds", "health"));
    candidate.setBase(config_.healthAttribute, nextHealth);

    if (isDamage(context) && !config_.shieldAttribute.empty() && context.absorbed() > 0.0) {
        auto       shield   = readBase(config_.shieldAttribute);
        const bool shieldOk = shield.ok();
        if (!shieldOk) {
            const auto status = shield.status();
            if (status.code() != eve::StatusCode::NotFound)
                return eve::Result<settlement::PreparedApply>::failure(status);
        } else {
            if (context.absorbed() > shield.value())
                return eve::Result<settlement::PreparedApply>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation,
                                           "RPG shield absorption exceeds the authoritative shield balance", "shield"));
            candidate.setBase(config_.shieldAttribute, shield.value() - context.absorbed());
        }
    }

    struct Pending {
        RPGActor*                     actor = nullptr;
        eve::attributes::AttributeSet before;
        eve::attributes::AttributeSet candidate;
        bool                          published = false;
    };
    auto pending =
        std::make_shared<Pending>(Pending{&target_, target_.attributes()->values, std::move(candidate), false});
    return eve::Result<settlement::PreparedApply>::success(settlement::PreparedApply(
        [pending]() -> eve::Result<void> {
            pending->actor->attributes()->values = std::move(pending->candidate);
            pending->published                   = true;
            return success(eve::StatusCode::Applied);
        },
        [pending]() {
            if (pending->published) pending->actor->attributes()->values = pending->before;
        }));
}

eve::Result<std::vector<settlement::SettlementRequest>> RPGSettlementAdapter::prepareTrigger(
    const settlement::SettlementContext& context, const settlement::SettlementResult& result) {
    std::vector<settlement::SettlementRequest> transitions;
    if (!isDamage(context))
        return eve::Result<std::vector<settlement::SettlementRequest>>::success(std::move(transitions));

    const auto append = [&](std::string key) {
        settlement::SettlementRequest transition = context.request();
        transition.kind                           = "trigger";
        transition.resource.clear();
        transition.magnitude = 0.0;
        transition.decisions.clear();
        transition.trigger = std::move(key);
        transition.chain   = {};
        transition.tags.push_back("trigger:" + transition.trigger);
        transitions.push_back(std::move(transition));
    };

    auto shield = readBase(config_.shieldAttribute);
    if (shield.ok() && shield.value() > 0.0 && result.absorbed >= shield.value()) append("shield_break");
    if (!shield.ok() && shield.code() != eve::StatusCode::NotFound)
        return eve::Result<std::vector<settlement::SettlementRequest>>::failure(shield.status());

    auto health = readBase(config_.healthAttribute);
    if (!health) return eve::Result<std::vector<settlement::SettlementRequest>>::failure(health.status());
    const bool died = config_.emitLifeTransitions && health.value() > 0.0 && result.applied >= health.value();
    if (died) {
        append("death");
        if (context.request().source.isValid() && context.request().source != context.request().target) append("kill");
    }
    return eve::Result<std::vector<settlement::SettlementRequest>>::success(std::move(transitions));
}

}  // namespace eve::rpg
