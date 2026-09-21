#include "settlement/Settlement.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <utility>

namespace eve::settlement {
namespace {

eve::Result<void> success(eve::StatusCode code = eve::StatusCode::Ok) {
    return eve::Result<void>::success(eve::Status::success(code));
}

eve::Result<void> validateFiniteNonNegative(double value, std::string_view name) {
    if (!std::isfinite(value) || value < 0.0)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 std::string(name) + " must be finite and non-negative",
                                                                 std::string(name)));
    return eve::Result<void>::success();
}

eve::Result<void> validateRequest(const SettlementRequest& request) {
    if (!request.target.isValid())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "settlement target must be a valid SubjectRef", "target"));
    if (request.kind.empty())
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "settlement kind must not be empty", "kind"));

    auto magnitude = validateFiniteNonNegative(request.magnitude, "magnitude");
    if (!magnitude) {
        const auto status = magnitude.status();
        return eve::Result<void>::failure(status);
    }
    if (!request.causation.isCanonical())
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                   "settlement causation must use a canonical event or command id", "causation"));
    if (!request.correlation.isCanonical())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "settlement correlation must use a canonical id", "correlation"));
    for (std::size_t index = 0; index < request.decisions.size(); ++index) {
        const auto& decision = request.decisions[index];
        const auto  path     = "decisions[" + std::to_string(index) + "]";
        if (!decision.stream.isValid())
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument,
                "settlement random decision stream must be a valid LogicalId", path + ".stream"));
        if (decision.sequence > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument,
                "settlement random decision sequence exceeds the event encoding range", path + ".sequence"));
        if (!std::isfinite(decision.sample) || decision.sample < 0.0 || decision.sample > 1.0)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "settlement random decision sample must be in [0,1]",
                path + ".sample"));
        if (!std::isfinite(decision.threshold) || decision.threshold < 0.0 || decision.threshold > 1.0)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "settlement random decision threshold must be in [0,1]",
                path + ".threshold"));
        for (std::size_t previous = 0; previous < index; ++previous)
            if (request.decisions[previous].stream == decision.stream &&
                request.decisions[previous].sequence == decision.sequence)
                return eve::Result<void>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::Conflict,
                    "settlement random decision stream and sequence must be unique", path + ".sequence"));
    }
    if (request.chain.depth == 0) {
        if (!request.trigger.empty() || request.chain.emittedCount != 0 || !request.chain.triggerPath.empty())
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument,
                "root settlement requests must not contain derived-chain metadata", "chain"));
    } else {
        if (request.trigger.empty())
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument,
                "derived settlement requests require a non-empty trigger key", "trigger"));
        if (request.chain.triggerPath.size() != request.chain.depth || request.chain.triggerPath.back() != request.trigger)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument,
                "derived settlement trigger path must match its depth and current trigger", "chain"));
        if (request.chain.emittedCount == 0)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument,
                "derived settlement requests require a positive emitted count", "chain.emittedCount"));
    }
    return eve::Result<void>::success();
}

const char* traceLevelName(SettlementTraceLevel level) noexcept {
    switch (level) {
        case SettlementTraceLevel::Off: return "off";
        case SettlementTraceLevel::Summary: return "summary";
        case SettlementTraceLevel::Full: return "full";
    }
    return "unknown";
}

eve::Result<void> validateFrame(const SettlementContext& context) {
    auto magnitude = validateFiniteNonNegative(context.magnitude(), "working magnitude");
    if (!magnitude) {
        const auto status = magnitude.status();
        return eve::Result<void>::failure(status);
    }
    auto absorbed = validateFiniteNonNegative(context.projectedResult().absorbed, "absorbed");
    if (!absorbed) {
        const auto status = absorbed.status();
        return eve::Result<void>::failure(status);
    }
    auto resisted = validateFiniteNonNegative(context.projectedResult().resisted, "resisted");
    if (!resisted) {
        const auto status = resisted.status();
        return eve::Result<void>::failure(status);
    }
    return eve::Result<void>::success();
}

eve::Diagnostic stageDiagnostic(std::string_view stage, const eve::Status& status) {
    eve::DiagnosticDetails details;
    details.emplace_back("stage", std::string(stage));
    details.emplace_back("status", std::string(eve::statusCodeName(status.code())));
    return eve::Diagnostic::error(eve::DiagnosticCode::CallbackFailure,
                                  "settlement stage failed: " + std::string(stage),
                                  "settlement.stage." + std::string(stage), std::move(details));
}

eve::Status stageFailureStatus(std::string_view stage, const eve::Status& status) {
    auto diagnostics = status.diagnostics();
    diagnostics.emplace_back(stageDiagnostic(stage, status));
    return eve::Status(status.code(), std::move(diagnostics));
}

eve::Result<game_event::GameEvent> makeEvent(const SettlementContext& context) {
    const auto& request = context.request();
    const auto& result  = context.projectedResult();

    auto schema = eve::LogicalId::parse("settlement:result");
    if (!schema)
        return eve::Result<game_event::GameEvent>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation,
                                   "settlement result schema id is not a valid LogicalId", "event.schemaId"));

    eve::Value::Object payload;
    payload["kind"]      = request.kind;
    payload["resource"]  = request.resource;
    payload["requested"] = result.requested;
    payload["applied"]   = result.applied;
    payload["absorbed"]  = result.absorbed;
    payload["resisted"]  = result.resisted;
    payload["clamped"]   = result.clamped;
    payload["critical"]    = result.critical;
    payload["disposition"] = settlementDispositionName(result.disposition);
    payload["tick"]      = static_cast<std::int64_t>(result.tick.value());
    payload["context"]   = request.context;

    eve::Value::Array tags;
    tags.reserve(request.tags.size());
    for (const auto& tag : request.tags) tags.emplace_back(tag);
    payload["tags"] = eve::Value(std::move(tags));

    eve::Value::Array decisions;
    decisions.reserve(request.decisions.size());
    for (const auto& decision : request.decisions) {
        eve::Value::Object value;
        value["stream"]    = decision.stream.format();
        value["sequence"]  = static_cast<std::int64_t>(decision.sequence);
        value["sample"]    = decision.sample;
        value["threshold"] = decision.threshold;
        value["accepted"]  = decision.accepted;
        decisions.emplace_back(eve::Value(std::move(value)));
    }
    payload["decisions"] = eve::Value(std::move(decisions));

    eve::Value::Array triggerPath;
    triggerPath.reserve(request.chain.triggerPath.size());
    for (const auto& trigger : request.chain.triggerPath) triggerPath.emplace_back(trigger);
    payload["trigger"] = request.trigger;
    payload["chain"]   = eve::Value(eve::Value::Object{
          {"depth", eve::Value(static_cast<std::int64_t>(request.chain.depth))},
          {"emitted_count", eve::Value(static_cast<std::int64_t>(request.chain.emittedCount))},
          {"trigger_path", eve::Value(std::move(triggerPath))},
    });
    payload["trace_level"] = traceLevelName(request.trace);

    eve::Value::Array stages;
    stages.reserve(result.stages.size());
    for (const auto& stage : result.stages) {
        eve::Value::Object stageValue;
        stageValue["name"]    = stage.name;
        stageValue["kind"]    = stageKindName(stage.kind);
        stageValue["status"]  = std::string(eve::statusCodeName(stage.status));
        stageValue["before"]  = stage.before;
        stageValue["after"]   = stage.after;
        stageValue["details"] = stage.details;
        stages.emplace_back(eve::Value(std::move(stageValue)));
    }
    payload["stages"] = eve::Value(std::move(stages));

    auto encoded = eve::Value(std::move(payload)).toJson();
    if (!encoded) {
        const auto status = encoded.status();
        return eve::Result<game_event::GameEvent>::failure(status);
    }

    game_event::GameEvent envelope;
    envelope.type          = "settlement.result";
    envelope.source        = request.source.isValid() ? request.source.format() : std::string{};
    envelope.subject       = request.target.format();
    envelope.causation     = request.causation;
    envelope.correlation   = request.correlation;
    envelope.schemaId      = *schema;
    envelope.schemaVersion = eve::SchemaVersion(1);
    envelope.tick          = request.tick;
    envelope.flags         = result.critical ? 1u : 0u;
    envelope.payload       = std::move(encoded).takeValue();
    return eve::Result<game_event::GameEvent>::success(std::move(envelope));
}

}  // namespace

const char* stageKindName(StageKind kind) noexcept {
    switch (kind) {
        case StageKind::Validate: return "validate";
        case StageKind::Decision: return "decision";
        case StageKind::SourceModifiers: return "source_modifiers";
        case StageKind::TargetMitigation: return "target_mitigation";
        case StageKind::ArmorShield: return "armor_shield";
        case StageKind::Clamp: return "clamp";
        case StageKind::Apply: return "apply";
        case StageKind::Event: return "event";
        case StageKind::Trigger: return "trigger";
    }
    return "unknown";
}

const char* settlementDispositionName(SettlementDisposition disposition) noexcept {
    switch (disposition) {
        case SettlementDisposition::Applied: return "applied";
        case SettlementDisposition::NoOp: return "no_op";
        case SettlementDisposition::Immune: return "immune";
        case SettlementDisposition::Resisted: return "resisted";
        case SettlementDisposition::PartiallyApplied: return "partially_applied";
        case SettlementDisposition::Blocked: return "blocked";
        case SettlementDisposition::InvalidTarget: return "invalid_target";
    }
    return "unknown";
}

bool SettlementResult::hasStage(std::string_view name) const noexcept { return stage(name) != nullptr; }

std::size_t SettlementResult::ruleEvaluationCount() const noexcept {
    return static_cast<std::size_t>(std::count_if(stages.begin(), stages.end(), [](const auto& stage) {
        return stage.name.starts_with("zz_rule.");
    }));
}

std::size_t SettlementResult::ruleMatchCount() const noexcept {
    return static_cast<std::size_t>(std::count_if(stages.begin(), stages.end(), [](const auto& stage) {
        return stage.name.starts_with("zz_rule.") && stage.status == eve::StatusCode::Applied;
    }));
}

const SettlementStageResult* SettlementResult::stage(std::string_view name) const noexcept {
    const auto it = std::find_if(stages.begin(), stages.end(), [&](const auto& value) { return value.name == name; });
    return it == stages.end() ? nullptr : &*it;
}

PreparedApply::PreparedApply(CommitFunction commit, RollbackFunction rollback)
    : commit_(std::move(commit)), rollback_(std::move(rollback)) {}

PreparedApply::PreparedApply(PreparedApply&& other) noexcept
    : commit_(std::move(other.commit_)),
      rollback_(std::move(other.rollback_)),
      committed_(other.committed_),
      rolledBack_(other.rolledBack_) {
    other.commit_     = {};
    other.rollback_   = {};
    other.committed_  = true;
    other.rolledBack_ = true;
}

PreparedApply& PreparedApply::operator=(PreparedApply&& other) noexcept {
    if (this == &other) return *this;
    rollback();
    commit_           = std::move(other.commit_);
    rollback_         = std::move(other.rollback_);
    committed_        = other.committed_;
    rolledBack_       = other.rolledBack_;
    other.commit_     = {};
    other.rollback_   = {};
    other.committed_  = true;
    other.rolledBack_ = true;
    return *this;
}

PreparedApply::~PreparedApply() {
    // A successful commit remains published.  The pipeline invokes rollback
    // explicitly only when a later event append fails.
    if (!committed_) rollback();
}

bool PreparedApply::isValid() const noexcept {
    return static_cast<bool>(commit_) && static_cast<bool>(rollback_) && !rolledBack_;
}

eve::Result<void> PreparedApply::commit() {
    if (committed_) return success(eve::StatusCode::Applied);
    if (!isValid())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvariantViolation, "settlement mutation is empty or already rolled back", "apply"));

    try {
        auto       outcome = commit_();
        const bool passed  = outcome.ok();
        if (passed) {
            committed_ = true;
            return outcome;
        }
        const auto status = outcome.status();
        return eve::Result<void>::failure(status);
    } catch (const std::exception& exception) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, std::string("settlement apply commit threw: ") + exception.what(), "apply"));
    } catch (...) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, "settlement apply commit threw an unknown exception", "apply"));
    }
}

void PreparedApply::rollback() noexcept {
    if (rolledBack_ || !rollback_) return;
    try {
        rollback_();
    } catch (...) {
        // A rollback exception would make the public failure contract false:
        // the caller could observe a failure while domain state remained
        // partially applied.  There is no safe recovery path from noexcept
        // rollback (including the destructor path), so fail closed.
        std::terminate();
    }
    rolledBack_ = true;
}

SettlementContext::SettlementContext(const SettlementRequest& request, SettlementResult& result,
                                     ISettlementPolicy& policy)
    : request_(request), result_(result), policy_(policy), magnitude_(request.magnitude) {}

eve::Result<void> SettlementContext::setMagnitude(double value) {
    if (applyPrepared_) {
        recordMutationViolation("settlement magnitude cannot change after apply preparation", "magnitude");
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "settlement magnitude is frozen after apply preparation", "magnitude"));
    }
    auto valid = validateFiniteNonNegative(value, "working magnitude");
    if (!valid) {
        const auto status = valid.status();
        return eve::Result<void>::failure(status);
    }
    magnitude_ = value;
    return eve::Result<void>::success();
}

eve::Result<void> SettlementContext::addAbsorbed(double value) {
    if (applyPrepared_) {
        recordMutationViolation("settlement absorbed amount cannot change after apply preparation", "absorbed");
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "settlement absorbed amount is frozen after apply preparation", "absorbed"));
    }
    auto valid = validateFiniteNonNegative(value, "absorbed amount");
    if (!valid) {
        const auto status = valid.status();
        return eve::Result<void>::failure(status);
    }
    if (value > std::numeric_limits<double>::max() - absorbed_)
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation, "absorbed amount overflowed", "absorbed"));
    absorbed_ += value;
    return eve::Result<void>::success();
}

eve::Result<void> SettlementContext::addResisted(double value) {
    if (applyPrepared_) {
        recordMutationViolation("settlement resisted amount cannot change after apply preparation", "resisted");
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "settlement resisted amount is frozen after apply preparation", "resisted"));
    }
    auto valid = validateFiniteNonNegative(value, "resisted amount");
    if (!valid) {
        const auto status = valid.status();
        return eve::Result<void>::failure(status);
    }
    if (value > std::numeric_limits<double>::max() - resisted_)
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation, "resisted amount overflowed", "resisted"));
    resisted_ += value;
    return eve::Result<void>::success();
}

eve::Result<void> SettlementContext::addClamped(double value) {
    if (applyPrepared_) {
        recordMutationViolation("settlement clamped amount cannot change after apply preparation", "clamped");
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "settlement clamped amount is frozen after apply preparation", "clamped"));
    }
    auto valid = validateFiniteNonNegative(value, "clamped amount");
    if (!valid) {
        const auto status = valid.status();
        return eve::Result<void>::failure(status);
    }
    if (value > std::numeric_limits<double>::max() - clamped_)
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation, "clamped amount overflowed", "clamped"));
    clamped_ += value;
    return eve::Result<void>::success();
}

eve::Result<void> SettlementContext::setDisposition(SettlementDisposition disposition) {
    if (applyPrepared_) {
        recordMutationViolation("settlement disposition cannot change after apply preparation", "disposition");
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "settlement disposition is frozen after apply preparation",
            "disposition"));
    }
    disposition_         = disposition;
    dispositionExplicit_ = true;
    switch (disposition) {
        case SettlementDisposition::Immune:
        case SettlementDisposition::Resisted:
        case SettlementDisposition::Blocked:
        case SettlementDisposition::InvalidTarget: magnitude_ = 0.0; break;
        case SettlementDisposition::Applied:
        case SettlementDisposition::NoOp:
        case SettlementDisposition::PartiallyApplied: break;
    }
    return eve::Result<void>::success();
}

eve::Result<void> SettlementContext::setClampMax(std::optional<double> value) {
    if (applyPrepared_) {
        recordMutationViolation("settlement clamp cannot change after apply preparation", "clamp");
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "settlement clamp is frozen after apply preparation", "clamp"));
    }
    if (value) {
        auto valid = validateFiniteNonNegative(*value, "clamp maximum");
        if (!valid) {
            const auto status = valid.status();
            return eve::Result<void>::failure(status);
        }
    }
    clampMax_ = value;
    return eve::Result<void>::success();
}

void SettlementContext::setStageDetail(std::string key, Value value) {
    if (key.empty()) return;
    if (request_.trace != SettlementTraceLevel::Full) return;
    if (eventPrepared_) {
        recordMutationViolation("settlement stage details cannot change after event preparation", "stage.details");
        return;
    }
    stageDetails_[std::move(key)] = std::move(value);
}

eve::Result<void> SettlementContext::applyClamp() {
    double next = std::max(0.0, magnitude_);
    if (clampMax_) next = std::min(next, *clampMax_);
    const double lost = magnitude_ - next;
    if (lost > 0.0) {
        auto recorded = addClamped(lost);
        if (!recorded) {
            const auto status = recorded.status();
            return eve::Result<void>::failure(status);
        }
    }
    magnitude_ = next;
    return eve::Result<void>::success();
}

eve::Result<void> SettlementContext::prepareApply() {
    if (applyPrepared_)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "settlement apply preparation may run only once", "apply"));
    auto       prepared   = policy().prepareApply(*this);
    const bool preparedOk = prepared.ok();
    if (!preparedOk) {
        const auto status = prepared.status();
        return eve::Result<void>::failure(status);
    }
    auto mutation = std::move(prepared).takeValue();
    if (!mutation.isValid())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvariantViolation, "settlement policy returned an invalid mutation", "apply"));
    pendingApply_.emplace(std::move(mutation));
    applyPrepared_ = true;
    return eve::Result<void>::success();
}

eve::Result<void> SettlementContext::prepareEvent() {
    if (eventPrepared_)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "settlement event preparation may run only once", "event"));
    synchronizeResult();
    auto       event   = makeEvent(*this);
    const bool eventOk = event.ok();
    if (!eventOk) {
        const auto status = event.status();
        return eve::Result<void>::failure(status);
    }
    pendingEvent_.emplace(std::move(event).takeValue());
    eventPrepared_ = true;
    return eve::Result<void>::success();
}

void SettlementContext::synchronizeResult() noexcept {
    result_.applied  = magnitude_;
    result_.absorbed = absorbed_;
    result_.resisted = resisted_;
    result_.clamped  = clamped_;
    result_.critical = critical_;
    if (dispositionExplicit_) {
        result_.disposition = disposition_;
    } else if (magnitude_ > 0.0 && (absorbed_ > 0.0 || resisted_ > 0.0 || clamped_ > 0.0)) {
        result_.disposition = SettlementDisposition::PartiallyApplied;
    } else if (magnitude_ > 0.0) {
        result_.disposition = SettlementDisposition::Applied;
    } else if (resisted_ > 0.0) {
        result_.disposition = SettlementDisposition::Resisted;
    } else if (absorbed_ > 0.0) {
        result_.disposition = SettlementDisposition::Blocked;
    } else {
        result_.disposition = SettlementDisposition::NoOp;
    }
}

eve::Result<void> SettlementContext::emitDerived(SettlementRequest request) {
    if (request.trigger.empty())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "derived settlement request requires a non-empty trigger key", "derived.trigger"));
    if (request.chain.depth != 0 || request.chain.emittedCount != 0 || !request.chain.triggerPath.empty())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "derived settlement request must not pre-populate chain metadata", "derived.chain"));
    result_.derived.push_back(std::move(request));
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

void SettlementContext::recordMutationViolation(std::string message, std::string path) noexcept {
    if (mutationViolation_) return;
    try {
        mutationViolation_.emplace(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict, std::move(message), std::move(path)));
    } catch (...) {
        // A late mutation is a contract violation. If recording its
        // diagnostic itself cannot allocate, fail closed instead of allowing
        // a possibly stale successful result to escape.
        std::terminate();
    }
}

eve::Result<void> ISettlementPolicy::decide(SettlementContext&) { return eve::Result<void>::success(); }

eve::Result<std::vector<SettlementRequest>> ISettlementPolicy::prepareTrigger(const SettlementContext&,
                                                                               const SettlementResult&) {
    return eve::Result<std::vector<SettlementRequest>>::success({});
}

SettlementPipeline::SettlementPipeline() {
    auto install = [&](StageKind kind, const char* name, StageFunction function, bool terminal = false) {
        const int priority = terminal ? 0 : std::numeric_limits<int>::min();
        stages_.push_back(StageEntry{kind, name, priority, nextRegistration_++, terminal, std::move(function)});
    };

    install(StageKind::Validate, "validate", [](SettlementContext& context) {
        auto       generic   = validateRequest(context.request());
        const bool genericOk = generic.ok();
        if (!genericOk) {
            const auto status = generic.status();
            return eve::Result<void>::failure(status);
        }
        return context.policy().validate(context);
    });
    install(StageKind::Decision, "decision",
            [](SettlementContext& context) { return context.policy().decide(context); });
    install(StageKind::SourceModifiers, "source_modifiers",
            [](SettlementContext& context) { return context.policy().sourceModifiers(context); });
    install(StageKind::TargetMitigation, "target_mitigation",
            [](SettlementContext& context) { return context.policy().targetMitigation(context); });
    install(StageKind::ArmorShield, "armor_shield",
            [](SettlementContext& context) { return context.policy().armorShield(context); });
    install(
        StageKind::Clamp, "clamp",
        [](SettlementContext& context) {
            auto       configured   = context.policy().clamp(context);
            const bool configuredOk = configured.ok();
            if (!configuredOk) {
                const auto status = configured.status();
                return eve::Result<void>::failure(status);
            }
            return context.applyClamp();
        },
        true);
    install(
        StageKind::Apply, "apply",
        [](SettlementContext& context) {
            context.synchronizeResult();
            return context.prepareApply();
        },
        true);
    install(
        StageKind::Trigger, "trigger",
        [](SettlementContext& context) {
            context.synchronizeResult();
            auto derived = context.policy().prepareTrigger(context, context.projectedResult());
            if (!derived) return eve::Result<void>::failure(derived.status());
            for (auto& request : std::move(derived).takeValue()) {
                auto emitted = context.emitDerived(std::move(request));
                if (!emitted) return emitted;
            }
            return eve::Result<void>::success(context.result_.derived.empty()
                                                  ? eve::Status::success(eve::StatusCode::NoOp)
                                                  : eve::Status::success(eve::StatusCode::Applied));
        },
        true);
    install(StageKind::Event, "event", [](SettlementContext& context) { return context.prepareEvent(); }, true);
}

eve::Result<void> SettlementPipeline::addStage(StageKind kind, std::string name, int priority, StageFunction function) {
    if (name.empty())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "settlement stage name must not be empty", "stage.name"));
    if (!function)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "settlement stage function must not be empty", "stage.function"));
    const auto duplicate =
        std::find_if(stages_.begin(), stages_.end(), [&](const auto& stage) { return stage.name == name; });
    if (duplicate != stages_.end())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::AlreadyExists, "settlement stage name is already registered", "stage.name"));
    if (nextRegistration_ == std::numeric_limits<std::uint64_t>::max())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvariantViolation, "settlement stage registration sequence exhausted", "stage"));
    stages_.push_back(StageEntry{kind, std::move(name), priority, nextRegistration_++, false, std::move(function)});
    return eve::Result<void>::success();
}

eve::Result<void> SettlementPipeline::prepare(SettlementContext& context, SettlementResult& result) const {
    std::vector<const StageEntry*> ordered;
    ordered.reserve(stages_.size());
    for (const auto& stage : stages_) ordered.push_back(&stage);
    std::sort(ordered.begin(), ordered.end(), [](const auto* left, const auto* right) {
        const auto leftKind  = static_cast<std::uint8_t>(left->kind);
        const auto rightKind = static_cast<std::uint8_t>(right->kind);
        if (leftKind != rightKind) return leftKind < rightKind;
        // Terminal preparation is an unregistrable phase boundary.  It must
        // sort after every custom stage in the same phase, even when a custom
        // stage deliberately uses a larger priority or a lexicographically
        // later name.
        if (left->terminal != right->terminal) return !left->terminal && right->terminal;
        if (left->priority != right->priority) return left->priority < right->priority;
        if (left->name != right->name) return left->name < right->name;
        return left->registration < right->registration;
    });

    auto rollback = [&]() noexcept {
        if (auto* pending = context.pendingApply()) pending->rollback();
    };

    for (const StageEntry* entry : ordered) {
        context.beginStage();
        const double before        = context.magnitude();
        auto         outcome       = entry->function(context);
        const bool   outcomeOk     = outcome.ok();
        const auto   outcomeStatus = outcome.status();

        context.synchronizeResult();
        auto              frame            = validateFrame(context);
        const bool        frameOk          = frame.ok();
        const auto        frameStatus      = frame.status();
        const auto*       violation        = context.mutationViolation();
        const bool        violationPresent = violation != nullptr;
        const eve::Status violationStatus =
            violationPresent ? eve::Status::failure(*violation) : eve::Status::success();

        if (context.request().trace != SettlementTraceLevel::Off) {
            SettlementStageResult stageResult;
            stageResult.kind    = entry->kind;
            stageResult.name    = entry->name;
            stageResult.status  = !outcomeOk         ? outcomeStatus.code()
                                  : !frameOk         ? frameStatus.code()
                                  : violationPresent ? violationStatus.code()
                                                     : outcomeStatus.code();
            stageResult.before  = before;
            stageResult.after   = context.magnitude();
            stageResult.details = Value(context.stageDetails());
            result.stages.push_back(std::move(stageResult));
        }

        if (!outcomeOk) {
            rollback();
            return eve::Result<void>::failure(stageFailureStatus(entry->name, outcomeStatus));
        }
        if (!frameOk) {
            rollback();
            return eve::Result<void>::failure(stageFailureStatus(entry->name, frameStatus));
        }
        if (violationPresent) {
            rollback();
            return eve::Result<void>::failure(stageFailureStatus(entry->name, violationStatus));
        }
    }

    context.synchronizeResult();
    if (context.pendingApply() == nullptr)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvariantViolation, "settlement pipeline did not prepare an apply mutation",
            "apply"));
    return eve::Result<void>::success();
}

eve::Result<SettlementResult> SettlementPipeline::settle(const SettlementRequest& request, ISettlementPolicy& policy,
                                                         game_event::GameEventLog* events) const {
    SettlementResult result;
    result.requested = request.magnitude;
    result.tick      = request.tick;
    SettlementContext context(request, result, policy);

    auto prepared = prepare(context, result);
    if (!prepared) return eve::Result<SettlementResult>::failure(prepared.status());

    auto rollback = [&]() noexcept {
        if (auto* pending = context.pendingApply()) pending->rollback();
    };
    auto* pending = context.pendingApply();

    auto       committed   = pending->commit();
    const bool committedOk = committed.ok();
    if (!committedOk) {
        const auto status = committed.status();
        rollback();
        return eve::Result<SettlementResult>::failure(status);
    }

    if (const auto* preparedEvent = context.pendingEvent()) {
        game_event::GameEvent envelope = *preparedEvent;
        result.event                   = envelope;
        if (events != nullptr) {
            try {
                auto       appended   = events->append(envelope);
                const bool appendedOk = appended.ok();
                if (!appendedOk) {
                    const auto status = appended.status();
                    rollback();
                    return eve::Result<SettlementResult>::failure(status);
                }
                const auto sequence = std::move(appended).takeValue();
                envelope.sequence   = sequence;
                if (const auto* stored = events->find(sequence)) {
                    result.event = *stored;
                } else {
                    result.event = envelope;
                }
            } catch (const std::exception& exception) {
                rollback();
                return eve::Result<SettlementResult>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::Failed,
                    std::string("settlement result event append threw: ") + exception.what(), "event"));
            } catch (...) {
                rollback();
                return eve::Result<SettlementResult>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::Failed, "settlement result event append threw an unknown exception", "event"));
            }
        }
    }

    const auto status = result.applied == 0.0 ? eve::StatusCode::NoOp : eve::StatusCode::Applied;
    return eve::Result<SettlementResult>::success(std::move(result), eve::Status::success(status));
}

}  // namespace eve::settlement
