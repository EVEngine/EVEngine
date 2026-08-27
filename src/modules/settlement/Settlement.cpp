#include "settlement/Settlement.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <utility>

namespace eve::settlement {
namespace {

template <class T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path)));
}

template <class T>
eve::Result<T> failure(eve::Status status) {
    return eve::Result<T>::failure(std::move(status));
}

eve::Result<void> success(eve::StatusCode code = eve::StatusCode::Ok) {
    return eve::Result<void>::success(eve::Status::success(code));
}

eve::Result<void> validateFiniteNonNegative(double value, std::string_view name) {
    if (!std::isfinite(value) || value < 0.0)
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             std::string(name) + " must be finite and non-negative",
                             std::string(name));
    return eve::Result<void>::success();
}

eve::Result<void> validateRequest(const SettlementRequest& request) {
    if (!request.target.isValid())
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "settlement target must be a valid SubjectRef", "target");
    if (request.kind.empty())
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "settlement kind must not be empty", "kind");

    auto magnitude = validateFiniteNonNegative(request.magnitude, "magnitude");
    if (!magnitude) {
        const auto status = magnitude.status();
        return failure<void>(status);
    }
    if (!request.causation.isCanonical())
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "settlement causation must use a canonical event or command id",
                             "causation");
    if (!request.correlation.isCanonical())
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "settlement correlation must use a canonical id", "correlation");
    return eve::Result<void>::success();
}

eve::Result<void> validateFrame(const SettlementContext& context) {
    auto magnitude = validateFiniteNonNegative(context.magnitude(), "working magnitude");
    if (!magnitude) {
        const auto status = magnitude.status();
        return failure<void>(status);
    }
    auto absorbed = validateFiniteNonNegative(context.projectedResult().absorbed, "absorbed");
    if (!absorbed) {
        const auto status = absorbed.status();
        return failure<void>(status);
    }
    auto resisted = validateFiniteNonNegative(context.projectedResult().resisted, "resisted");
    if (!resisted) {
        const auto status = resisted.status();
        return failure<void>(status);
    }
    return eve::Result<void>::success();
}

eve::Diagnostic stageDiagnostic(std::string_view stage, const eve::Status& status) {
    eve::DiagnosticDetails details;
    details.emplace_back("stage", std::string(stage));
    details.emplace_back("status", std::string(eve::statusCodeName(status.code())));
    return eve::Diagnostic::error(
        eve::DiagnosticCode::CallbackFailure,
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
    const auto& result = context.projectedResult();

    auto schema = eve::LogicalId::parse("settlement:result");
    if (!schema)
        return failure<game_event::GameEvent>(
            eve::DiagnosticCode::InvariantViolation,
            "settlement result schema id is not a valid LogicalId", "event.schemaId");

    eve::Value::Object payload;
    payload["kind"]       = request.kind;
    payload["requested"]  = result.requested;
    payload["applied"]    = result.applied;
    payload["absorbed"]   = result.absorbed;
    payload["resisted"]   = result.resisted;
    payload["clamped"]    = result.clamped;
    payload["critical"]   = result.critical;
    payload["tick"]       = static_cast<std::int64_t>(result.tick.value());
    payload["context"]    = request.context;

    eve::Value::Array tags;
    tags.reserve(request.tags.size());
    for (const auto& tag : request.tags) tags.emplace_back(tag);
    payload["tags"] = eve::Value(std::move(tags));

    eve::Value::Array stages;
    stages.reserve(result.stages.size());
    for (const auto& stage : result.stages) {
        eve::Value::Object stageValue;
        stageValue["name"]   = stage.name;
        stageValue["kind"]   = stageKindName(stage.kind);
        stageValue["status"] = std::string(eve::statusCodeName(stage.status));
        stageValue["before"] = stage.before;
        stageValue["after"]  = stage.after;
        stageValue["details"] = stage.details;
        stages.emplace_back(eve::Value(std::move(stageValue)));
    }
    payload["stages"] = eve::Value(std::move(stages));

    auto encoded = eve::Value(std::move(payload)).toJson();
    if (!encoded) {
        const auto status = encoded.status();
        return failure<game_event::GameEvent>(status);
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
    case StageKind::Validate:
        return "validate";
    case StageKind::SourceModifiers:
        return "source_modifiers";
    case StageKind::TargetMitigation:
        return "target_mitigation";
    case StageKind::ArmorShield:
        return "armor_shield";
    case StageKind::Clamp:
        return "clamp";
    case StageKind::Apply:
        return "apply";
    case StageKind::Event:
        return "event";
    case StageKind::Trigger:
        return "trigger";
    }
    return "unknown";
}

bool SettlementResult::hasStage(std::string_view name) const noexcept {
    return stage(name) != nullptr;
}

const SettlementStageResult* SettlementResult::stage(std::string_view name) const noexcept {
    const auto it = std::find_if(stages.begin(), stages.end(), [&](const auto& value) {
        return value.name == name;
    });
    return it == stages.end() ? nullptr : &*it;
}

PreparedApply::PreparedApply(CommitFunction commit, RollbackFunction rollback)
    : commit_(std::move(commit)), rollback_(std::move(rollback)) {}

PreparedApply::PreparedApply(PreparedApply&& other) noexcept
    : commit_(std::move(other.commit_)), rollback_(std::move(other.rollback_)),
      committed_(other.committed_), rolledBack_(other.rolledBack_) {
    other.commit_     = {};
    other.rollback_   = {};
    other.committed_  = true;
    other.rolledBack_ = true;
}

PreparedApply& PreparedApply::operator=(PreparedApply&& other) noexcept {
    if (this == &other) return *this;
    rollback();
    commit_     = std::move(other.commit_);
    rollback_   = std::move(other.rollback_);
    committed_  = other.committed_;
    rolledBack_ = other.rolledBack_;
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
        return failure<void>(eve::DiagnosticCode::InvariantViolation,
                             "settlement mutation is empty or already rolled back", "apply");

    try {
        auto outcome = commit_();
        const bool passed = outcome.ok();
        if (passed) {
            committed_ = true;
            return outcome;
        }
        const auto status = outcome.status();
        return failure<void>(status);
    } catch (const std::exception& exception) {
        return failure<void>(eve::DiagnosticCode::Failed,
                             std::string("settlement apply commit threw: ") + exception.what(),
                             "apply");
    } catch (...) {
        return failure<void>(eve::DiagnosticCode::Failed,
                             "settlement apply commit threw an unknown exception", "apply");
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
        recordMutationViolation("settlement magnitude cannot change after apply preparation",
                                "magnitude");
        return failure<void>(eve::DiagnosticCode::Conflict,
                             "settlement magnitude is frozen after apply preparation",
                             "magnitude");
    }
    auto valid = validateFiniteNonNegative(value, "working magnitude");
    if (!valid) {
        const auto status = valid.status();
        return failure<void>(status);
    }
    magnitude_ = value;
    return eve::Result<void>::success();
}

eve::Result<void> SettlementContext::addAbsorbed(double value) {
    if (applyPrepared_) {
        recordMutationViolation("settlement absorbed amount cannot change after apply preparation",
                                "absorbed");
        return failure<void>(eve::DiagnosticCode::Conflict,
                             "settlement absorbed amount is frozen after apply preparation",
                             "absorbed");
    }
    auto valid = validateFiniteNonNegative(value, "absorbed amount");
    if (!valid) {
        const auto status = valid.status();
        return failure<void>(status);
    }
    if (value > std::numeric_limits<double>::max() - absorbed_)
        return failure<void>(eve::DiagnosticCode::InvariantViolation,
                             "absorbed amount overflowed", "absorbed");
    absorbed_ += value;
    return eve::Result<void>::success();
}

eve::Result<void> SettlementContext::addResisted(double value) {
    if (applyPrepared_) {
        recordMutationViolation("settlement resisted amount cannot change after apply preparation",
                                "resisted");
        return failure<void>(eve::DiagnosticCode::Conflict,
                             "settlement resisted amount is frozen after apply preparation",
                             "resisted");
    }
    auto valid = validateFiniteNonNegative(value, "resisted amount");
    if (!valid) {
        const auto status = valid.status();
        return failure<void>(status);
    }
    if (value > std::numeric_limits<double>::max() - resisted_)
        return failure<void>(eve::DiagnosticCode::InvariantViolation,
                             "resisted amount overflowed", "resisted");
    resisted_ += value;
    return eve::Result<void>::success();
}

eve::Result<void> SettlementContext::addClamped(double value) {
    if (applyPrepared_) {
        recordMutationViolation("settlement clamped amount cannot change after apply preparation",
                                "clamped");
        return failure<void>(eve::DiagnosticCode::Conflict,
                             "settlement clamped amount is frozen after apply preparation",
                             "clamped");
    }
    auto valid = validateFiniteNonNegative(value, "clamped amount");
    if (!valid) {
        const auto status = valid.status();
        return failure<void>(status);
    }
    if (value > std::numeric_limits<double>::max() - clamped_)
        return failure<void>(eve::DiagnosticCode::InvariantViolation,
                             "clamped amount overflowed", "clamped");
    clamped_ += value;
    return eve::Result<void>::success();
}

eve::Result<void> SettlementContext::setClampMax(std::optional<double> value) {
    if (applyPrepared_) {
        recordMutationViolation("settlement clamp cannot change after apply preparation", "clamp");
        return failure<void>(eve::DiagnosticCode::Conflict,
                             "settlement clamp is frozen after apply preparation", "clamp");
    }
    if (value) {
        auto valid = validateFiniteNonNegative(*value, "clamp maximum");
        if (!valid) {
            const auto status = valid.status();
            return failure<void>(status);
        }
    }
    clampMax_ = value;
    return eve::Result<void>::success();
}

void SettlementContext::setStageDetail(std::string key, Value value) {
    if (key.empty()) return;
    if (eventPrepared_) {
        recordMutationViolation("settlement stage details cannot change after event preparation",
                                "stage.details");
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
            return failure<void>(status);
        }
    }
    magnitude_ = next;
    return eve::Result<void>::success();
}

eve::Result<void> SettlementContext::prepareApply() {
    if (applyPrepared_)
        return failure<void>(eve::DiagnosticCode::Conflict,
                             "settlement apply preparation may run only once", "apply");
    auto prepared = policy().prepareApply(*this);
    const bool preparedOk = prepared.ok();
    if (!preparedOk) {
        const auto status = prepared.status();
        return failure<void>(status);
    }
    auto mutation = std::move(prepared).takeValue();
    if (!mutation.isValid())
        return failure<void>(eve::DiagnosticCode::InvariantViolation,
                             "settlement policy returned an invalid mutation", "apply");
    pendingApply_.emplace(std::move(mutation));
    applyPrepared_ = true;
    return eve::Result<void>::success();
}

eve::Result<void> SettlementContext::prepareEvent() {
    if (eventPrepared_)
        return failure<void>(eve::DiagnosticCode::Conflict,
                             "settlement event preparation may run only once", "event");
    synchronizeResult();
    auto event = makeEvent(*this);
    const bool eventOk = event.ok();
    if (!eventOk) {
        const auto status = event.status();
        return failure<void>(status);
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
}

void SettlementContext::recordMutationViolation(std::string message, std::string path) noexcept {
    if (mutationViolation_) return;
    try {
        mutationViolation_.emplace(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, std::move(message), std::move(path)));
    } catch (...) {
        // A late mutation is a contract violation. If recording its
        // diagnostic itself cannot allocate, fail closed instead of allowing
        // a possibly stale successful result to escape.
        std::terminate();
    }
}

eve::Result<void> ISettlementPolicy::prepareTrigger(const SettlementContext&,
                                                    const SettlementResult&) {
    return eve::Result<void>::success();
}

SettlementPipeline::SettlementPipeline() {
    auto install = [&](StageKind kind, const char* name, StageFunction function,
                       bool terminal = false) {
        stages_.push_back(StageEntry{kind, name, 0, nextRegistration_++, terminal,
                                     std::move(function)});
    };

    install(StageKind::Validate, "validate", [](SettlementContext& context) {
        auto generic = validateRequest(context.request());
        const bool genericOk = generic.ok();
        if (!genericOk) {
            const auto status = generic.status();
            return failure<void>(status);
        }
        return context.policy().validate(context);
    });
    install(StageKind::SourceModifiers,
            "source_modifiers",
            [](SettlementContext& context) { return context.policy().sourceModifiers(context); });
    install(StageKind::TargetMitigation,
            "target_mitigation",
            [](SettlementContext& context) { return context.policy().targetMitigation(context); });
    install(StageKind::ArmorShield,
            "armor_shield",
            [](SettlementContext& context) { return context.policy().armorShield(context); });
    install(StageKind::Clamp, "clamp", [](SettlementContext& context) {
        auto configured = context.policy().clamp(context);
        const bool configuredOk = configured.ok();
        if (!configuredOk) {
            const auto status = configured.status();
            return failure<void>(status);
        }
        return context.applyClamp();
    });
    install(StageKind::Apply, "apply", [](SettlementContext& context) {
        context.synchronizeResult();
        return context.prepareApply();
    }, true);
    install(StageKind::Event, "event", [](SettlementContext& context) {
        return context.prepareEvent();
    }, true);
    install(StageKind::Trigger, "trigger", [](SettlementContext& context) {
        context.synchronizeResult();
        return context.policy().prepareTrigger(context, context.projectedResult());
    }, true);
}

eve::Result<void> SettlementPipeline::addStage(StageKind kind, std::string name, int priority,
                                               StageFunction function) {
    if (name.empty())
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "settlement stage name must not be empty", "stage.name");
    if (!function)
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "settlement stage function must not be empty", "stage.function");
    const auto duplicate = std::find_if(stages_.begin(), stages_.end(), [&](const auto& stage) {
        return stage.name == name;
    });
    if (duplicate != stages_.end())
        return failure<void>(eve::DiagnosticCode::AlreadyExists,
                             "settlement stage name is already registered", "stage.name");
    if (nextRegistration_ == std::numeric_limits<std::uint64_t>::max())
        return failure<void>(eve::DiagnosticCode::InvariantViolation,
                             "settlement stage registration sequence exhausted", "stage");
    stages_.push_back(StageEntry{kind, std::move(name), priority, nextRegistration_++,
                                 false, std::move(function)});
    return eve::Result<void>::success();
}

eve::Result<SettlementResult> SettlementPipeline::settle(const SettlementRequest& request,
                                                         ISettlementPolicy& policy,
                                                         game_event::GameEventLog* events) const {
    SettlementResult result;
    result.requested = request.magnitude;
    result.tick      = request.tick;
    SettlementContext context(request, result, policy);

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
        const double before = context.magnitude();
        auto outcome = entry->function(context);
        const bool outcomeOk = outcome.ok();
        const auto outcomeStatus = outcome.status();

        context.synchronizeResult();
        auto frame = validateFrame(context);
        const bool frameOk = frame.ok();
        const auto frameStatus = frame.status();
        const auto* violation = context.mutationViolation();
        const bool violationPresent = violation != nullptr;
        const eve::Status violationStatus = violationPresent
                                                ? eve::Status::failure(*violation)
                                                : eve::Status::success();

        SettlementStageResult stageResult;
        stageResult.kind    = entry->kind;
        stageResult.name    = entry->name;
        stageResult.status  = !outcomeOk       ? outcomeStatus.code()
                              : !frameOk        ? frameStatus.code()
                              : violationPresent ? violationStatus.code()
                                                  : outcomeStatus.code();
        stageResult.before  = before;
        stageResult.after   = context.magnitude();
        stageResult.details = Value(context.stageDetails());
        result.stages.push_back(std::move(stageResult));

        if (!outcomeOk) {
            rollback();
            return failure<SettlementResult>(stageFailureStatus(entry->name, outcomeStatus));
        }
        if (!frameOk) {
            rollback();
            return failure<SettlementResult>(stageFailureStatus(entry->name, frameStatus));
        }
        if (violationPresent) {
            rollback();
            return failure<SettlementResult>(stageFailureStatus(entry->name, violationStatus));
        }
    }

    context.synchronizeResult();
    auto* pending = context.pendingApply();
    if (pending == nullptr)
        return failure<SettlementResult>(eve::DiagnosticCode::InvariantViolation,
                                         "settlement pipeline did not prepare an apply mutation",
                                         "apply");

    auto committed = pending->commit();
    const bool committedOk = committed.ok();
    if (!committedOk) {
        const auto status = committed.status();
        rollback();
        return failure<SettlementResult>(status);
    }

    if (const auto* preparedEvent = context.pendingEvent()) {
        game_event::GameEvent envelope = *preparedEvent;
        result.event                         = envelope;
        if (events != nullptr) {
            try {
                auto appended = events->append(envelope);
                const bool appendedOk = appended.ok();
                if (!appendedOk) {
                    const auto status = appended.status();
                    rollback();
                    return failure<SettlementResult>(status);
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
                return failure<SettlementResult>(
                    eve::DiagnosticCode::Failed,
                    std::string("settlement result event append threw: ") + exception.what(),
                    "event");
            } catch (...) {
                rollback();
                return failure<SettlementResult>(eve::DiagnosticCode::Failed,
                                                 "settlement result event append threw an unknown exception",
                                                 "event");
            }
        }
    }

    const auto status = result.applied == 0.0 ? eve::StatusCode::NoOp : eve::StatusCode::Applied;
    return eve::Result<SettlementResult>::success(std::move(result), eve::Status::success(status));
}

}  // namespace eve::settlement
