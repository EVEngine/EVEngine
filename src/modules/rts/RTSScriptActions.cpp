#include <algorithm>
#include <cmath>
#include <limits>
#include "rts/RTSScriptInternal.h"
#include "rts/RTSScriptRuntimeInternal.h"
namespace eve::rts {
namespace script_internal {
Result<RTSEffectDefinition> resolveEffectDefinition(definitions::DefinitionRegistry& registry, std::string_view id,
                                                    SubjectRef source, double durationOverride) {
    if (id.empty() || !source.isValid() || !std::isfinite(durationOverride))
        return Result<RTSEffectDefinition>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument,
            "RTS status effect requires an id, valid source, and finite duration override", "effect"));
    auto resolved = registry.resolve("effect", std::string(id));
    if (!resolved) return Result<RTSEffectDefinition>::failure(resolved.status());
    auto parsed = Value::fromJson(resolved.value().get().json);
    if (!parsed) return Result<RTSEffectDefinition>::failure(parsed.status());
    const auto* object = parsed.value().getIf<Value::Object>();
    if (object == nullptr)
        return Result<RTSEffectDefinition>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "RTS status effect definition must be an object", "effect"));
    const auto number = [object](std::string_view name, double fallback) -> std::optional<double> {
        const auto found = object->find(std::string(name));
        if (found == object->end()) return fallback;
        if (const auto* integer = found->second.getIf<std::int64_t>()) return static_cast<double>(*integer);
        if (const auto* real = found->second.getIf<double>()) return *real;
        return std::nullopt;
    };
    const auto duration = number("duration", 0.0);
    const auto speed    = number("speedMultiplier", 1.0);
    const auto damage   = number("damageMultiplier", 1.0);
    const auto incoming = number("incomingDamageMultiplier", 1.0);
    const auto healing  = number("healingPerSecond", 0.0);
    if (!duration || !speed || !damage || !incoming || !healing || !std::isfinite(*duration) ||
        !std::isfinite(*speed) || !std::isfinite(*damage) || !std::isfinite(*incoming) || !std::isfinite(*healing) ||
        *duration < 0.0 || *speed < 0.0 || *damage < 0.0 || *incoming < 0.0 || *healing < 0.0)
        return Result<RTSEffectDefinition>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument,
            "RTS status effect duration and modifiers must be finite non-negative numbers", "effect"));
    RTSEffectDefinition definition;
    definition.id                       = std::string(id);
    definition.source                   = source.format();
    definition.duration                 = durationOverride >= 0.0 ? durationOverride : *duration;
    definition.speedMultiplier          = *speed;
    definition.damageMultiplier         = *damage;
    definition.incomingDamageMultiplier = *incoming;
    definition.healingPerSecond         = *healing;
    definition.tags                     = {"status:" + std::string(id)};
    return Result<RTSEffectDefinition>::success(std::move(definition), Status::success(StatusCode::Applied));
}

}  // namespace script_internal
namespace {
using script_internal::resolveEffectDefinition;
template <class T>
bool owns(const std::vector<ecs::EntityHandle>& handles, const T& entity) {
    const auto live = ecs::handle_of(const_cast<T*>(&entity));
    return std::any_of(handles.begin(), handles.end(), [&](const auto& h) {
        return h.table == live.table && h.type == live.type && h.id == live.id && h.generation == live.generation;
    });
}
}  // namespace
Result<RTSPreparedProduction> RTS::prepareScriptUnit(Building& producer, SubjectRef unitSubject,
                                                     LogicalId unitDefinition, int priority) {
    if (!owns(buildings_, producer))
        return Result<RTSPreparedProduction>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "RTS producer does not belong to this facade", "producer"));
    if (!producer.integrity()->alive || producer.construction()->progress < 1.0f)
        return Result<RTSPreparedProduction>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "RTS producer must be live and complete", "producer"));
    if (!scriptRuntime_ || !scriptRuntime_->configured || definitions_ == nullptr)
        return Result<RTSPreparedProduction>::failure(Diagnostic::error(
            DiagnosticCode::Conflict, "RTS script world and content must be configured", "scriptRuntime"));
    if (!unitSubject.isValid() || ownsSubject(unitSubject, SubjectClaimScope::LiveOrReserved))
        return Result<RTSPreparedProduction>::failure(Diagnostic::error(
            DiagnosticCode::Conflict, "RTS produced unit subject is invalid or already owned", "unitSubject"));
    if (!unitDefinition.isValid())
        return Result<RTSPreparedProduction>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "RTS production requires a unit definition", "unitDefinition"));
    auto resolved = definitions_->resolve("unit", std::string(unitDefinition.name()));
    if (!resolved) return Result<RTSPreparedProduction>::failure(resolved.status());
    auto parsed = Value::fromJson(resolved.value().get().json);
    if (!parsed) return Result<RTSPreparedProduction>::failure(parsed.status());
    const auto* object = parsed.value().getIf<Value::Object>();
    if (object == nullptr)
        return Result<RTSPreparedProduction>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "RTS unit definition must be an object", "unitDefinition"));
    const auto resourceIt = object->find("costResource");
    const auto costIt     = object->find("cost");
    const auto timeIt     = object->find("buildTime");
    const auto producerIt = object->find("producer");
    if (resourceIt == object->end() || costIt == object->end() || timeIt == object->end())
        return Result<RTSPreparedProduction>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "RTS unit definition lacks production cost or duration",
                              "unitDefinition"));
    const auto* resourceName = resourceIt->second.getIf<std::string>();
    const auto  number       = [](const Value& value) -> std::optional<double> {
        if (const auto* integer = value.getIf<std::int64_t>()) return static_cast<double>(*integer);
        if (const auto* real = value.getIf<double>()) return *real;
        return std::nullopt;
    };
    const auto costNumber = number(costIt->second);
    const auto timeNumber = number(timeIt->second);
    if (resourceName == nullptr || resourceName->empty() || !costNumber || !timeNumber || !std::isfinite(*costNumber) ||
        *costNumber < 0.0 || *costNumber >= static_cast<double>(std::numeric_limits<std::int64_t>::max()) ||
        std::floor(*costNumber) != *costNumber || !std::isfinite(*timeNumber) || *timeNumber <= 0.0)
        return Result<RTSPreparedProduction>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "RTS unit production values are invalid", "unitDefinition"));
    if (producerIt != object->end()) {
        const auto* requiredProducer = producerIt->second.getIf<std::string>();
        if (requiredProducer == nullptr || *requiredProducer != producer.definition()->id.name())
            return Result<RTSPreparedProduction>::failure(Diagnostic::error(
                DiagnosticCode::Conflict, "RTS building cannot produce this unit definition", "producer"));
    }
    auto* faction = dynamic_cast<Faction*>(producer.faction()->link.resolve());
    if (faction == nullptr)
        return Result<RTSPreparedProduction>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "RTS producer faction link is stale", "producer.faction"));
    const auto economy = scriptRuntime_->economies.find(faction->identity()->subject.format());
    if (economy == scriptRuntime_->economies.end())
        return Result<RTSPreparedProduction>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "RTS producer economy was not found", "producer.faction"));
    auto cost = resource::CostSpec::single(*resourceName, static_cast<std::int64_t>(*costNumber));
    if (!cost) return Result<RTSPreparedProduction>::failure(cost.status());
    auto duration = Duration::fromSeconds(*timeNumber);
    if (!duration) return Result<RTSPreparedProduction>::failure(duration.status());
    RTSBuildRequest request;
    request.production           = producer.production()->values.queueForComposition();
    request.orders               = producer.orders()->values.queueForComposition();
    request.action               = &scriptRuntime_->actions;
    request.account              = &economy->second->account;
    request.cost                 = cost.value();
    request.owner                = producer.identity()->subject.format();
    request.product              = std::string(unitDefinition.name());
    request.productionKind       = "unit";
    request.duration             = std::move(duration).takeValue();
    request.priority             = priority;
    request.actionRequest.source = ecs::handle_of(&producer);
    for (const auto& [name, policy] : faction->productionPolicy()->resourceReserves) {
        auto item = resource::ResourceCost::create(name, policy.amount);
        if (!item) return Result<RTSPreparedProduction>::failure(item.status());
        request.resourceReserves.push_back({std::move(item).takeValue(), policy.minimumPriority});
    }
    auto prepared = RTSProductionActionAdapter::stage(std::move(request));
    if (!prepared) return Result<RTSPreparedProduction>::failure(prepared.status());
    auto        result = RTSPreparedProduction{cost.value(), std::move(prepared).takeValue()};
    class Guard final : public transaction::ITransactionParticipant {
    public:
        Guard(RTS& owner, SubjectRef producer, SubjectRef unit, std::uint64_t tick)
            : owner_(owner), producer_(producer), unit_(unit), tick_(tick) {}
        Result<void> prepare(const transaction::TransactionContext&) override { return validate(); }
        Result<void> commit(const transaction::TransactionContext&) override { return validate(); }
        Result<void> rollback(const transaction::TransactionContext&) override { return Result<void>::success(); }
        Result<void> compensate(const transaction::TransactionContext&) override { return Result<void>::success(); }

    private:
        Result<void> validate() {
            auto* producer = owner_.findBuilding(producer_);
            if (!producer || !producer->integrity()->alive || producer->construction()->progress < 1.0f ||
                owner_.scriptTick() != tick_ || owner_.ownsSubject(unit_, SubjectClaimScope::LiveOrReserved))
                return Result<void>::failure(Diagnostic::error(
                    DiagnosticCode::StaleHandle, "production request is stale or subject is reserved", "production"));
            return Result<void>::success();
        }
        RTS&          owner_;
        SubjectRef    producer_, unit_;
        std::uint64_t tick_;
    };
    class Reservation final : public transaction::ITransactionParticipant {
    public:
        Reservation(ScriptRuntime& runtime, ScriptRuntime::PaidProduction payment,
                    std::shared_ptr<RTSBuildReceipt> output)
            : runtime_(runtime), payment_(std::move(payment)), output_(std::move(output)) {}
        Result<void> prepare(const transaction::TransactionContext&) override {
            beforeSubjects_ = runtime_.pendingProductionSubjects;
            beforePaid_     = runtime_.paidProduction;
            afterSubjects_  = beforeSubjects_;
            afterPaid_      = beforePaid_;
            if (output_->productionTaskId.empty() || output_->orderId.empty())
                return Result<void>::failure(
                    Diagnostic::error(DiagnosticCode::InvariantViolation, "queue receipt absent", "production"));
            payment_.taskId  = output_->productionTaskId;
            payment_.orderId = output_->orderId;
            afterSubjects_.emplace(payment_.taskId, payment_.resultSubject);
            afterPaid_.push_back(payment_);
            return Result<void>::success();
        }
        Result<void> commit(const transaction::TransactionContext&) override {
            if (runtime_.pendingProductionSubjects != beforeSubjects_ ||
                runtime_.paidProduction.size() != beforePaid_.size())
                return Result<void>::failure(
                    Diagnostic::error(DiagnosticCode::Conflict, "production reservations changed", "production"));
            runtime_.pendingProductionSubjects.swap(afterSubjects_);
            runtime_.paidProduction.swap(afterPaid_);
            return Result<void>::success();
        }
        Result<void> rollback(const transaction::TransactionContext&) override { return Result<void>::success(); }
        Result<void> compensate(const transaction::TransactionContext&) override {
            runtime_.pendingProductionSubjects.swap(afterSubjects_);
            runtime_.paidProduction.swap(afterPaid_);
            return Result<void>::success();
        }

    private:
        ScriptRuntime&                             runtime_;
        ScriptRuntime::PaidProduction              payment_;
        std::shared_ptr<RTSBuildReceipt>           output_;
        std::map<std::string, SubjectRef>          beforeSubjects_, afterSubjects_;
        std::vector<ScriptRuntime::PaidProduction> beforePaid_, afterPaid_;
    };
    result.stage.effects.insert(
        result.stage.effects.begin(),
        std::make_unique<Guard>(*this, producer.identity()->subject, unitSubject, scriptTick()));
    result.stage.effects.push_back(
        std::make_unique<Reservation>(*scriptRuntime_,
                                      ScriptRuntime::PaidProduction{producer.identity()->subject,
                                                                    unitSubject,
                                                                    "unit",
                                                                    std::string(unitDefinition.name()),
                                                                    {},
                                                                    {},
                                                                    cost.value()},
                                      result.stage.output));
    return Result<RTSPreparedProduction>::success(std::move(result));
}

Result<RTSBuildReceipt> RTS::queueScriptUnit(Building& producer, SubjectRef unitSubject, LogicalId unitDefinition,
                                             int priority, std::span<transaction::ITransactionParticipant*> effects,
                                             std::optional<resource::CostSpec> additionalCost) {
    auto prepared = prepareScriptUnit(producer, unitSubject, unitDefinition, priority);
    if (!prepared) return Result<RTSBuildReceipt>::failure(prepared.status());
    auto  stage   = std::move(prepared).takeValue();
    auto* faction = dynamic_cast<Faction*>(producer.faction()->link.resolve());
    auto  account = scriptAccount(*faction);
    if (!account) return Result<RTSBuildReceipt>::failure(account.status());
    auto cost = stage.cost;
    if (additionalCost) {
        auto items = cost.items();
        items.insert(items.end(), additionalCost->items().begin(), additionalCost->items().end());
        auto merged = resource::CostSpec::create(std::move(items));
        if (!merged) return Result<RTSBuildReceipt>::failure(merged.status());
        cost = std::move(merged).takeValue();
    }
    std::vector<transaction::ITransactionParticipant*> parts;
    for (auto& effect : stage.stage.effects) parts.push_back(effect.get());
    parts.insert(parts.end(), effects.begin(), effects.end());
    auto receipt = transaction::AtomicResourcePayment::execute(
        transaction::TransactionContext("rts.script.production." + unitSubject.format()), account.value().get(), cost,
        parts);
    if (!receipt) return Result<RTSBuildReceipt>::failure(receipt.status());
    stage.stage.output->transaction = std::move(receipt).takeValue();
    return Result<RTSBuildReceipt>::success(*stage.stage.output);
}

Result<AbilitySpec> RTS::resolveScriptAbility(Unit& caster, const std::string& ability) {
    if (!owns(units_, caster))
        return Result<AbilitySpec>::failure(Diagnostic::error(
            DiagnosticCode::StaleHandle, "RTS ability caster does not belong to this facade", "caster"));
    if (!scriptRuntime_ || !scriptRuntime_->configured || definitions_ == nullptr || ability.empty())
        return Result<AbilitySpec>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "RTS ability requires configured script content", "ability"));
    auto resolved = definitions_->resolve("ability", ability);
    if (!resolved) return Result<AbilitySpec>::failure(resolved.status());
    auto parsed = Value::fromJson(resolved.value().get().json);
    if (!parsed) return Result<AbilitySpec>::failure(parsed.status());
    const auto* object = parsed.value().getIf<Value::Object>();
    if (object == nullptr)
        return Result<AbilitySpec>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "RTS ability definition must be an object", "ability"));
    const auto textField = [object](std::string_view name, std::string fallback = {}) {
        const auto found = object->find(std::string(name));
        if (found == object->end()) return fallback;
        const auto* value = found->second.getIf<std::string>();
        return value == nullptr ? fallback : *value;
    };
    const auto numberField = [object](std::string_view name, double fallback) {
        const auto found = object->find(std::string(name));
        if (found == object->end()) return std::optional<double>{fallback};
        if (const auto* integer = found->second.getIf<std::int64_t>())
            return std::optional<double>{static_cast<double>(*integer)};
        if (const auto* real = found->second.getIf<double>()) return std::optional<double>{*real};
        return std::optional<double>{};
    };
    const auto boolField = [object](std::string_view name, bool fallback) {
        const auto found = object->find(std::string(name));
        if (found == object->end()) return std::optional<bool>{fallback};
        const auto* value = found->second.getIf<bool>();
        return value == nullptr ? std::optional<bool>{} : std::optional<bool>{*value};
    };
    AbilitySpec spec;
    spec.id                            = ability;
    const std::string casterDefinition = textField("casterUnit");
    if (!casterDefinition.empty()) {
        const auto id = LogicalId::fromParts("unit", casterDefinition);
        if (!id)
            return Result<AbilitySpec>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "RTS ability caster definition is invalid", "casterUnit"));
        spec.casterDefinition = *id;
    }
    const std::string targetType = textField("targetType", "enemy");
    if (targetType == "self")
        spec.target = AbilityTarget::Self;
    else if (targetType == "ally")
        spec.target = AbilityTarget::Ally;
    else if (targetType == "enemy")
        spec.target = AbilityTarget::Enemy;
    else if (targetType == "point")
        spec.target = AbilityTarget::Point;
    else
        return Result<AbilitySpec>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "RTS ability target type is invalid", "targetType"));
    const auto range        = numberField("range", 0.0);
    const auto radius       = numberField("radius", 0.0);
    const auto cooldown     = numberField("cooldown", 0.0);
    const auto damage       = numberField("damage", 0.0);
    const auto healing      = numberField("healing", 0.0);
    const auto castTime     = numberField("castTime", 0.0);
    const auto tickInterval = numberField("tickInterval", 0.0);
    const auto resourceCost = numberField("resourceCost", 0.0);
    const auto interrupt    = boolField("interruptOnDamage", true);
    if (!range || !radius || !cooldown || !damage || !healing || !castTime || !tickInterval || !resourceCost ||
        !interrupt || !std::isfinite(*resourceCost) || *resourceCost < 0.0 ||
        *resourceCost >= static_cast<double>(std::numeric_limits<std::int64_t>::max()) ||
        std::floor(*resourceCost) != *resourceCost)
        return Result<AbilitySpec>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "RTS ability definition contains an invalid field", "ability"));
    spec.range                     = static_cast<float>(*range);
    spec.radius                    = static_cast<float>(*radius);
    spec.cooldown                  = static_cast<float>(*cooldown);
    spec.damage                    = static_cast<float>(*damage);
    spec.healing                   = static_cast<float>(*healing);
    spec.castTime                  = static_cast<float>(*castTime);
    spec.channelTickInterval       = static_cast<float>(*tickInterval);
    spec.resourceCost              = static_cast<std::int64_t>(*resourceCost);
    spec.resourceType              = textField("resourceType");
    spec.damageType                = textField("damageType", "normal");
    spec.interruptOnDamage         = *interrupt;
    const std::string statusEffect = textField("statusEffect");
    if (!statusEffect.empty()) {
        auto effectDefinition = resolveEffectDefinition(*definitions_, statusEffect, caster.identity()->subject);
        if (!effectDefinition) return Result<AbilitySpec>::failure(effectDefinition.status());
        spec.appliesEffect = true;
        spec.effect        = std::move(effectDefinition).takeValue();
    }
    return Result<AbilitySpec>::success(std::move(spec));
}
Result<void> RTS::castScriptAbility(Unit& caster, std::string ability, SubjectRef target, WorldPosition point) {
    auto parsedSpec = resolveScriptAbility(caster, ability);
    if (!parsedSpec) return Result<void>::failure(parsedSpec.status());
    auto              spec = std::move(parsedSpec).takeValue();
    ecs::EntityHandle targetHandle{};
    if (target.isValid()) {
        ecs::Entity* entity = findUnit(target);
        if (entity == nullptr) entity = findBuilding(target);
        if (entity == nullptr)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::NotFound, "RTS ability target was not found", "target"));
        targetHandle = ecs::handle_of(entity);
    }
    const AbilityResourceDebit debit = [this](Unit& unit, const resource::CostSpec& cost) -> Result<resource::Receipt> {
        auto* faction = dynamic_cast<Faction*>(unit.faction()->link.resolve());
        if (faction == nullptr || !scriptRuntime_)
            return Result<resource::Receipt>::failure(Diagnostic::error(
                DiagnosticCode::StaleHandle, "RTS ability caster faction is stale", "caster.faction"));
        const auto economy = scriptRuntime_->economies.find(faction->identity()->subject.format());
        if (economy == scriptRuntime_->economies.end())
            return Result<resource::Receipt>::failure(
                Diagnostic::error(DiagnosticCode::NotFound, "RTS ability economy was not found", "caster.faction"));
        return economy->second->account.debit(cost);
    };
    const DamageEventSink damageEvents =
        [this](const combat::DamageRequest& request, const combat::DamageOutcome& outcome, SimulationTick tick,
               DamageChannel channel) { recordDamageEvent(request, outcome, tick, channel); };
    const LifecycleEventSink lifecycleEvents = [this](const LifecycleEvent& event, SimulationTick tick) {
        recordLifecycleEvent(event, tick);
    };
    return AbilitySystem::cast(caster, spec, targetHandle, point, scriptRuntime_->damage, debit, damageEvents,
                               SimulationTick(scriptTick()), lifecycleEvents);
}

Result<RTSBuildReceipt> RTS::build(Building& building, action::ActionRuntime& action,
                                   resource::IResourceAccount& account, resource::CostSpec cost, std::string product,
                                   Duration duration, std::string productionKind, int priority,
                                   std::string transactionId, definition::DefinitionHandle definition,
                                   std::span<transaction::ITransactionParticipant*> effects) {
    if (!owns(buildings_, building))
        return Result<RTSBuildReceipt>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "RTS Building does not belong to this facade", "building"));
    std::vector<RTSProductionResourceReserve> reserves;
    if (auto* faction = dynamic_cast<Faction*>(building.faction()->link.resolve()); faction != nullptr) {
        reserves.reserve(faction->productionPolicy()->resourceReserves.size());
        for (const auto& [resource, policy] : faction->productionPolicy()->resourceReserves) {
            auto item = resource::ResourceCost::create(resource, policy.amount);
            if (!item) return Result<RTSBuildReceipt>::failure(item.status());
            reserves.push_back({std::move(item).takeValue(), policy.minimumPriority});
        }
    }
    return RTSProductionActionAdapter::build(
        building, action, account, std::move(cost), std::move(product), std::move(duration), std::move(productionKind),
        priority, std::move(transactionId), std::move(reserves), std::move(definition), effects);
}

ResultRef<resource::IResourceAccount> RTS::scriptAccount(Faction& faction) {
    if (!owns(factions_, faction))
        return ResultRef<resource::IResourceAccount>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "faction is not owned by RTS", "faction"));
    if (!scriptRuntime_ || !scriptRuntime_->configured)
        return ResultRef<resource::IResourceAccount>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "script world is not configured", "world"));
    const auto found = scriptRuntime_->economies.find(faction.identity()->subject.format());
    if (found == scriptRuntime_->economies.end())
        return ResultRef<resource::IResourceAccount>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "faction account is absent", "faction"));
    return ResultRef<resource::IResourceAccount>::success(
        std::ref(static_cast<resource::IResourceAccount&>(found->second->account)));
}
Result<RTSPreparedAbility> RTS::prepareScriptAbility(Unit& caster, std::string ability, SubjectRef target,
                                                     WorldPosition point) {
    auto resolved = resolveScriptAbility(caster, ability);
    if (!resolved) return Result<RTSPreparedAbility>::failure(resolved.status());
    auto              spec = std::move(resolved).takeValue();
    ecs::EntityHandle handle{};
    if (target.isValid()) {
        ecs::Entity* entity = findUnit(target);
        if (!entity) entity = findBuilding(target);
        if (!entity)
            return Result<RTSPreparedAbility>::failure(
                Diagnostic::error(DiagnosticCode::NotFound, "ability target is absent", "target"));
        handle = ecs::handle_of(entity);
    }
    RTSPreparedAbility prepared;
    if (spec.resourceCost > 0) {
        auto cost = resource::CostSpec::single(spec.resourceType, spec.resourceCost);
        if (!cost) return Result<RTSPreparedAbility>::failure(cost.status());
        prepared.cost = std::move(cost).takeValue();
    }
    auto effect = script_internal::stageAbility(caster, std::move(spec), handle, point);
    if (!effect) return Result<RTSPreparedAbility>::failure(effect.status());
    prepared.effect = std::move(effect).takeValue();
    return Result<RTSPreparedAbility>::success(std::move(prepared));
}
}  // namespace eve::rts
