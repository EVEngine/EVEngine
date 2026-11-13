#include "combat/DamageCapture.h"
#include <utility>
#include "combat/DamageStateInternal.h"
namespace eve::combat {
struct DamageCapture::Data {
    HitReactionPolicy policy;
    std::string       rules;
};
DamageCapture::DamageCapture(std::unique_ptr<Data> data) : data_(std::move(data)) {}
DamageCapture::~DamageCapture()                                          = default;
DamageCapture::DamageCapture(DamageCapture&&) noexcept                   = default;
DamageCapture&        DamageCapture::operator=(DamageCapture&&) noexcept = default;
Result<DamageCapture> DamageRuntime::capture() const {
    using R = Result<DamageCapture>;
    if (!settlement_)
        return R::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                            "Damage runtime was moved; configure it before capture"));
    if (rule_)
        return R::failure(Diagnostic::error(
            DiagnosticCode::Unsupported, "External damage rule implementation requires explicit identity admission"));
    auto valid = policy_.validate();
    if (!valid) return R::failure(valid.status());
    auto rules = settlement_->rules.canonicalJson();
    if (!rules) return R::failure(rules.status());
    return R::success(DamageCapture(
        std::make_unique<DamageCapture::Data>(DamageCapture::Data{policy_, std::move(rules).takeValue()})));
}
Result<std::unique_ptr<DamageRuntime>> DamageRuntime::prepareRestored(const DamageCapture& captured) {
    using R = Result<std::unique_ptr<DamageRuntime>>;
    if (!captured.data_)
        return R::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation, "Damage capture was moved"));
    auto rules = settlement::SettlementRuleSet::fromJson(captured.data_->rules);
    if (!rules) return R::failure(rules.status());
    auto candidate  = std::make_unique<DamageRuntime>(nullptr, captured.data_->policy);
    auto configured = candidate->configureSettlementRules(rules.value());
    if (!configured) return R::failure(configured.status());
    return R::success(std::move(candidate));
}
}  // namespace eve::combat

namespace eve::combat {
namespace {
LogicalId damageSchema() { return *LogicalId::fromParts("combat", "damage-runtime"); }
template <class T>
Result<T> invalidDamage(const char* message) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::ParseError, message, "combat.damage-runtime"));
}
}  // namespace
Result<SnapshotEnvelope> DamageCapture::seal(PersistentId epoch, Revision revision, SimulationTick tick,
                                             const SnapshotHashProvider& hash) const {
    using R = Result<SnapshotEnvelope>;
    if (!data_ || epoch.isNil() || revision.isZero())
        return R::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                            "Damage sealing requires live capture and nonnil epoch/revision"));
    auto rules = Value::fromJson(data_->rules);
    if (!rules) return R::failure(rules.status());
    auto payload =
        Value::object({{"reaction", Value::object({{"flinch_damage_threshold", data_->policy.flinchDamageThreshold},
                                                   {"stagger_poise_fraction", data_->policy.staggerPoiseFraction}})},
                       {"rules", std::move(rules).takeValue()}});
    return makeSnapshotEnvelope("eve.damage-runtime", damageSchema(), SchemaVersion(1), epoch, revision, tick,
                                std::move(payload), hash);
}
Result<DamageCapture> DamageCapture::parse(SnapshotEnvelope snapshot, PersistentId epoch,
                                           const SnapshotHashProvider& hash) {
    using R       = Result<DamageCapture>;
    auto verified = verifySnapshotEnvelope(snapshot, hash);
    if (!verified) return R::failure(verified.status());
    if (snapshot.schemaVersion != SchemaVersion(1))
        return R::failure(Diagnostic::error(DiagnosticCode::UnknownVersion, "Unknown damage runtime version"));
    if (snapshot.type != "eve.damage-runtime" || snapshot.schema != damageSchema() || epoch.isNil() ||
        snapshot.instanceId != epoch || snapshot.revision.isZero())
        return R::failure(Diagnostic::error(DiagnosticCode::Conflict, "Damage participant boundary mismatch"));
    const auto* root = snapshot.payload.getIf<Value::Object>();
    if (!root || root->size() != 2 || !root->contains("reaction") || !root->contains("rules"))
        return invalidDamage<DamageCapture>("Unknown or missing damage payload fields");
    const auto* reaction = root->at("reaction").getIf<Value::Object>();
    if (!reaction || reaction->size() != 2 || !reaction->contains("flinch_damage_threshold") ||
        !reaction->contains("stagger_poise_fraction"))
        return invalidDamage<DamageCapture>("Unknown or missing reaction policy fields");
    const auto& flinch  = reaction->at("flinch_damage_threshold");
    const auto& stagger = reaction->at("stagger_poise_fraction");
    if ((!flinch.isDouble() && !flinch.isInt64()) || (!stagger.isDouble() && !stagger.isInt64()))
        return invalidDamage<DamageCapture>("Reaction policy requires numbers");
    HitReactionPolicy policy{flinch.isDouble() ? flinch.asDouble() : static_cast<double>(flinch.asInt()),
                             stagger.isDouble() ? stagger.asDouble() : static_cast<double>(stagger.asInt())};
    auto valid = policy.validate();
    if (!valid) return R::failure(valid.status());
    auto text = root->at("rules").toJson();
    if (!text) return R::failure(text.status());
    auto rules = settlement::SettlementRuleSet::fromJson(text.value());
    if (!rules) return R::failure(rules.status());
    DamageRuntime candidate(nullptr, policy);
    auto          configured = candidate.configureSettlementRules(rules.value());
    if (!configured) return R::failure(configured.status());
    return candidate.capture();
}
}  // namespace eve::combat
