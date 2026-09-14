#include "climbing/ClimbingControl.h"

#include "common/Capability.h"

#include <algorithm>

namespace eve::climbing {
namespace {

template <typename T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

LogicalId id(std::string_view text) { return LogicalId::parse(text).value(); }

std::string phaseName(ClimbingPhase phase) {
    switch (phase) {
        case ClimbingPhase::Idle: return "idle";
        case ClimbingPhase::Requested: return "requested";
        case ClimbingPhase::Aligning: return "aligning";
        case ClimbingPhase::Launching: return "launching";
        case ClimbingPhase::Climbing: return "climbing";
        case ClimbingPhase::Landing: return "landing";
        case ClimbingPhase::Recovering: return "recovering";
        case ClimbingPhase::Hanging: return "hanging";
        case ClimbingPhase::Dropping: return "dropping";
        case ClimbingPhase::Completed: return "completed";
        case ClimbingPhase::Cancelled: return "cancelled";
        case ClimbingPhase::Failed: return "failed";
        case ClimbingPhase::Balanced: return "balanced";
        case ClimbingPhase::Swinging: return "swinging";
    }
    return "unknown";
}

std::string eventName(ClimbingEventKind kind) {
    switch (kind) {
        case ClimbingEventKind::Started: return "started";
        case ClimbingEventKind::AnchorTransitionStarted: return "anchor-transition-started";
        case ClimbingEventKind::AnchorReached: return "anchor-reached";
        case ClimbingEventKind::ContactLeftHand: return "contact-left-hand";
        case ClimbingEventKind::ContactRightHand: return "contact-right-hand";
        case ClimbingEventKind::Landed: return "landed";
        case ClimbingEventKind::Hanging: return "hanging";
        case ClimbingEventKind::Dropped: return "dropped";
        case ClimbingEventKind::Completed: return "completed";
        case ClimbingEventKind::Cancelled: return "cancelled";
        case ClimbingEventKind::Failed: return "failed";
    }
    return "unknown";
}

bool terminal(ClimbingPhase phase) {
    return phase == ClimbingPhase::Idle || phase == ClimbingPhase::Completed ||
           phase == ClimbingPhase::Cancelled || phase == ClimbingPhase::Failed;
}

}  // namespace

ClimbingControl::ClimbingControl(SubjectRef instance, SubjectRef character,
                                 ClimbingRuntime& runtime, physics::World3D& world,
                                 const ClimbingPose& pose)
    : instance_(instance), character_(character), runtime_(&runtime), world_(&world), pose_(&pose) {
    cap::addListener<IGameplayControlProvider>(this);
}

ClimbingControl::~ClimbingControl() { cap::removeListener<IGameplayControlProvider>(this); }

std::string_view ClimbingControl::gameplayDomain() const noexcept { return "climbing"; }

bool ClimbingControl::controls(const GameplaySession& session) const {
    return session.access != GameplayAccess::PlayerEquivalent ||
           std::find(session.controlledSubjects.begin(), session.controlledSubjects.end(), character_) !=
               session.controlledSubjects.end();
}

Result<std::uint64_t> ClimbingControl::refreshRevision() const {
    if (!runtime_ || !world_ || !pose_)
        return failure<std::uint64_t>(DiagnosticCode::StaleHandle,
                                      "climbing control collaborators are invalid", "instance");
    auto snapshot = runtime_->snapshotJson();
    if (!snapshot) return Result<std::uint64_t>::failure(snapshot.status());
    if (snapshot.value() != stateFingerprint_) {
        stateFingerprint_ = snapshot.value();
        ++revision_;
    }
    return Result<std::uint64_t>::success(revision_);
}

Result<GameplayObservation> ClimbingControl::observeGameplay(const GameplaySession& session,
                                                              SubjectRef instance) const {
    if (instance != instance_ || !instance_.isValid())
        return failure<GameplayObservation>(DiagnosticCode::NotFound,
                                            "climbing gameplay instance was not found", "instance");
    if (!controls(session))
        return failure<GameplayObservation>(DiagnosticCode::PreconditionViolation,
                                            "session does not control this character", "instance");
    auto revision = refreshRevision();
    if (!revision) return Result<GameplayObservation>::failure(revision.status());
    auto snapshot = runtime_->snapshot();
    if (!snapshot) return Result<GameplayObservation>::failure(snapshot.status());
    GameplayObservation observation;
    observation.domain = id("gameplay:climbing");
    observation.instance = instance_;
    observation.tick = tick_;
    observation.revision = revision.value();
    observation.state = Value(Value::Object{
        {"phase", Value(phaseName(runtime_->phase()))},
        {"runtime", std::move(snapshot).takeValue()},
    });
    return Result<GameplayObservation>::success(std::move(observation));
}

Result<std::vector<GameplayActionDescriptor>> ClimbingControl::availableGameplayActions(
    const GameplaySession& session, SubjectRef instance, SubjectRef subject) const {
    auto observed = observeGameplay(session, instance);
    if (!observed) return Result<std::vector<GameplayActionDescriptor>>::failure(observed.status());
    std::move(observed).takeValue();
    if (subject != character_)
        return failure<std::vector<GameplayActionDescriptor>>(
            DiagnosticCode::PreconditionViolation, "climbing action subject must be the character", "subject");
    const Value emptySchema(Value::Object{});
    std::vector<GameplayActionDescriptor> actions;
    const auto phase = runtime_->phase();
    if (terminal(phase)) actions.push_back({id("climbing:begin-best"), emptySchema});
    if (phase == ClimbingPhase::Hanging) {
        actions.push_back({id("climbing:climb-up"), emptySchema});
        actions.push_back({id("climbing:drop"), emptySchema});
    }
    if (!terminal(phase)) actions.push_back({id("climbing:cancel"), emptySchema});
    return Result<std::vector<GameplayActionDescriptor>>::success(std::move(actions));
}

Result<void> ClimbingControl::captureEvents(std::string_view commandId) {
    auto drained = runtime_->drainEvents();
    if (!drained) return Result<void>::failure(drained.status());
    for (auto& source : std::move(drained).takeValue()) {
        GameplayEvent event;
        event.sequence = nextEventSequence_++;
        event.tick = source.tick;
        event.type = "climbing." + eventName(source.kind);
        event.subject = character_;
        event.causationCommandId = std::string(commandId);
        event.correlationId = std::string(commandId);
        Value::Array metadata;
        for (auto& item : source.metadata) metadata.emplace_back(std::move(item));
        event.payload = Value(Value::Object{
            {"actionId", Value(std::move(source.actionId))},
            {"executionId", Value(static_cast<std::int64_t>(source.executionId.value()))},
            {"metadata", Value(std::move(metadata))},
        });
        events_.push_back(std::move(event));
    }
    return Result<void>::success();
}

Result<GameplayCommandReceipt> ClimbingControl::submitGameplay(const GameplaySession& session,
                                                                SubjectRef instance,
                                                                const GameplayCommand& command) {
    if (instance != instance_)
        return failure<GameplayCommandReceipt>(DiagnosticCode::NotFound,
                                               "climbing gameplay instance was not found", "instance");
    if (!controls(session) || command.subject != character_)
        return failure<GameplayCommandReceipt>(DiagnosticCode::PreconditionViolation,
                                               "session does not control this character", "command.subject");
    if (command.id.empty())
        return failure<GameplayCommandReceipt>(DiagnosticCode::InvalidArgument,
                                               "command id must not be empty", "command.id");
    auto revision = refreshRevision();
    if (!revision) return Result<GameplayCommandReceipt>::failure(revision.status());
    if (command.observedTick != tick_ || command.expectedRevision != revision.value())
        return failure<GameplayCommandReceipt>(DiagnosticCode::Conflict,
                                               "climbing command was based on a stale observation",
                                               "command.expectedRevision");

    std::string operation;
    if (command.action == id("climbing:begin-best")) {
        auto begun = runtime_->tryBegin(*world_, *pose_, tick_);
        if (!begun) return Result<GameplayCommandReceipt>::failure(begun.status());
        std::move(begun).takeValue();
        operation = "begin-best";
    } else if (command.action == id("climbing:climb-up")) {
        auto climbed = runtime_->climbUp(tick_);
        if (!climbed) return Result<GameplayCommandReceipt>::failure(climbed.status());
        operation = "climb-up";
    } else if (command.action == id("climbing:drop")) {
        auto dropped = runtime_->drop(tick_);
        if (!dropped) return Result<GameplayCommandReceipt>::failure(dropped.status());
        operation = "drop";
    } else if (command.action == id("climbing:cancel")) {
        auto cancelled = runtime_->cancel(ClimbingCancelReason::PlayerRequest, tick_);
        if (!cancelled) return Result<GameplayCommandReceipt>::failure(cancelled.status());
        operation = "cancel";
    } else {
        return failure<GameplayCommandReceipt>(DiagnosticCode::Unsupported,
                                               "unsupported climbing action", "command.action");
    }
    auto captured = captureEvents(command.id);
    if (!captured) return Result<GameplayCommandReceipt>::failure(captured.status());
    stateFingerprint_.clear();
    auto resultingRevision = refreshRevision();
    if (!resultingRevision) return Result<GameplayCommandReceipt>::failure(resultingRevision.status());
    GameplayCommandReceipt receipt;
    receipt.commandId = command.id;
    receipt.executionId = "climbing-" + std::to_string(runtime_->executionId().value());
    receipt.acceptedTick = tick_;
    receipt.resultingRevision = resultingRevision.value();
    receipt.details = Value(Value::Object{{"operation", Value(operation)},
                                          {"phase", Value(phaseName(runtime_->phase()))}});
    return Result<GameplayCommandReceipt>::success(std::move(receipt),
                                                   Status::success(StatusCode::Applied));
}

Result<GameplayObservation> ClimbingControl::advanceGameplay(const GameplaySession& session,
                                                              SubjectRef instance,
                                                              const SimulationStep& step) {
    if (step.tick <= tick_)
        return failure<GameplayObservation>(DiagnosticCode::Conflict,
                                            "climbing simulation tick must increase", "step.tick");
    auto observed = observeGameplay(session, instance);
    if (!observed) return Result<GameplayObservation>::failure(observed.status());
    std::move(observed).takeValue();
    if (!terminal(runtime_->phase())) {
        auto advanced = runtime_->advance(*world_, step);
        if (!advanced) return Result<GameplayObservation>::failure(advanced.status());
        std::move(advanced).takeValue();
        auto captured = captureEvents("advance");
        if (!captured) return Result<GameplayObservation>::failure(captured.status());
    }
    tick_ = step.tick;
    stateFingerprint_.clear();
    return observeGameplay(session, instance);
}

Result<std::vector<GameplayEvent>> ClimbingControl::gameplayEvents(const GameplaySession& session,
                                                                    SubjectRef instance,
                                                                    std::uint64_t afterSequence) const {
    auto observed = observeGameplay(session, instance);
    if (!observed) return Result<std::vector<GameplayEvent>>::failure(observed.status());
    std::move(observed).takeValue();
    std::vector<GameplayEvent> result;
    for (const auto& event : events_)
        if (event.sequence > afterSequence) result.push_back(event);
    const bool empty = result.empty();
    return Result<std::vector<GameplayEvent>>::success(
        std::move(result), Status::success(empty ? StatusCode::NoOp : StatusCode::Applied));
}

}  // namespace eve::climbing
