#include "rpg/BattleControl.h"

#include "common/Capability.h"
#include "rpg/Battle.h"
#include "rpg/RPGActor.h"

#include <algorithm>

namespace eve::rpg {
namespace {

template <typename T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

LogicalId gameplayId(std::string_view value) { return LogicalId::parse(value).value(); }

Result<std::string> stringParameter(const Value& parameters, std::string_view name,
                                    bool optional = false) {
    const auto* object = parameters.getIf<Value::Object>();
    if (!object)
        return failure<std::string>(DiagnosticCode::InvalidArgument,
                                    "RPG gameplay parameters must be an object", "parameters");
    const auto found = object->find(std::string(name));
    if (found == object->end() && optional) return Result<std::string>::success({});
    if (found == object->end() || !found->second.isString())
        return failure<std::string>(DiagnosticCode::InvalidArgument,
                                    "RPG gameplay parameter must be a string",
                                    "parameters." + std::string(name));
    return Result<std::string>::success(found->second.asString());
}

}  // namespace

BattleControl::BattleControl(Battle& battle, SubjectRef instance)
    : battle_(battle), instance_(instance) {
    cap::addListener<IGameplayControlProvider>(this);
}

BattleControl::~BattleControl() { cap::removeListener<IGameplayControlProvider>(this); }

Result<void> BattleControl::bindParticipant(SubjectRef subject, RPGActor* actor) {
    if (!instance_.isValid() || !subject.isValid() || !actor)
        return failure<void>(DiagnosticCode::InvalidArgument,
                             "battle, subject, and actor identities must be valid", "participant");
    bool participant = false;
    for (int index = 0; index < battle_.getActorCount(); ++index)
        if (battle_.getActor(index) == actor) participant = true;
    if (!participant)
        return failure<void>(DiagnosticCode::NotFound,
                             "actor is not a participant in the adapted battle", "actor");
    const auto existing = actors_.find(subject);
    if (existing != actors_.end() && existing->second != actor)
        return failure<void>(DiagnosticCode::Conflict,
                             "subject is already bound to another participant", "subject");
    if (const auto reverse = subjectOf(actor); reverse.isValid() && reverse != subject)
        return failure<void>(DiagnosticCode::Conflict,
                             "actor is already bound to another subject", "actor");
    actors_[subject] = actor;
    ++revision_;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

std::string_view BattleControl::gameplayDomain() const noexcept { return "rpg.battle"; }

bool BattleControl::controls(const GameplaySession& session, SubjectRef subject) const {
    return session.access != GameplayAccess::PlayerEquivalent ||
           std::find(session.controlledSubjects.begin(), session.controlledSubjects.end(), subject) !=
               session.controlledSubjects.end();
}

RPGActor* BattleControl::resolve(SubjectRef subject) const {
    const auto found = actors_.find(subject);
    return found == actors_.end() ? nullptr : found->second;
}

SubjectRef BattleControl::subjectOf(RPGActor* actor) const {
    for (const auto& [subject, candidate] : actors_)
        if (candidate == actor) return subject;
    return SubjectRef::nil();
}

Result<GameplayObservation> BattleControl::observeGameplay(const GameplaySession& session,
                                                            SubjectRef instance) const {
    if (instance != instance_ || !instance_.isValid())
        return failure<GameplayObservation>(DiagnosticCode::NotFound,
                                            "RPG battle gameplay instance was not found", "instance");
    bool ownsParticipant = false;
    Value::Array participants;
    for (int index = 0; index < battle_.getActorCount(); ++index) {
        RPGActor* actor = battle_.getActor(index);
        const auto subject = subjectOf(actor);
        if (!subject.isValid()) continue;
        const bool controlled = controls(session, subject);
        ownsParticipant = ownsParticipant || controlled;
        participants.emplace_back(Value::Object{
            {"alive", Value(battle_.isActorAlive(actor))},
            {"controlled", Value(controlled)},
            {"hp", Value(actor->getCurrent("hp"))},
            {"maxHp", Value(actor->getMax("hp"))},
            {"side", Value(battle_.getSide(index))},
            {"subject", Value(subject.format())},
        });
    }
    if (session.access == GameplayAccess::PlayerEquivalent && !ownsParticipant)
        return failure<GameplayObservation>(DiagnosticCode::PreconditionViolation,
                                            "session controls no participant in this RPG battle",
                                            "session.controlledSubjects");
    GameplayObservation observation;
    observation.domain = gameplayId("gameplay:rpg-battle");
    observation.instance = instance_;
    observation.tick = tick_;
    observation.revision = revision_;
    observation.state = Value(Value::Object{
        {"defeat", Value(battle_.isDefeat())},
        {"finished", Value(battle_.isFinished())},
        {"participants", Value(std::move(participants))},
        {"turn", Value(battle_.getTurn())},
        {"victory", Value(battle_.isVictory())},
    });
    return Result<GameplayObservation>::success(std::move(observation));
}

Result<std::vector<GameplayActionDescriptor>> BattleControl::availableGameplayActions(
    const GameplaySession& session, SubjectRef instance, SubjectRef subject) const {
    if (instance != instance_)
        return failure<std::vector<GameplayActionDescriptor>>(
            DiagnosticCode::NotFound, "RPG battle gameplay instance was not found", "instance");
    RPGActor* actor = resolve(subject);
    if (!actor)
        return failure<std::vector<GameplayActionDescriptor>>(
            DiagnosticCode::NotFound, "RPG battle participant was not found", "subject");
    if (!controls(session, subject))
        return failure<std::vector<GameplayActionDescriptor>>(
            DiagnosticCode::PreconditionViolation, "session does not control this RPG participant", "subject");
    if (!battle_.isActorAlive(actor) || battle_.isFinished())
        return Result<std::vector<GameplayActionDescriptor>>::success({});
    const Value targetSchema(Value::Object{{"type", Value("subject")}});
    const Value skillSchema(Value::Object{{"skillId", Value(Value::Object{{"type", Value("string")}})},
                                          {"target", targetSchema}});
    return Result<std::vector<GameplayActionDescriptor>>::success({
        {gameplayId("rpg:attack"), Value(Value::Object{{"target", targetSchema}})},
        {gameplayId("rpg:skill"), skillSchema},
    });
}

Result<GameplayCommandReceipt> BattleControl::submitGameplay(const GameplaySession& session,
                                                              SubjectRef instance,
                                                              const GameplayCommand& command) {
    if (instance != instance_)
        return failure<GameplayCommandReceipt>(DiagnosticCode::NotFound,
                                               "RPG battle gameplay instance was not found", "instance");
    RPGActor* actor = resolve(command.subject);
    if (!actor)
        return failure<GameplayCommandReceipt>(DiagnosticCode::NotFound,
                                               "RPG battle participant was not found", "command.subject");
    if (!controls(session, command.subject))
        return failure<GameplayCommandReceipt>(DiagnosticCode::PreconditionViolation,
                                               "session does not control this RPG participant",
                                               "command.subject");
    if (command.id.empty())
        return failure<GameplayCommandReceipt>(DiagnosticCode::InvalidArgument,
                                               "command id must not be empty", "command.id");
    if (command.observedTick != tick_ || command.expectedRevision != revision_)
        return failure<GameplayCommandReceipt>(DiagnosticCode::Conflict,
                                               "RPG command was based on a stale observation",
                                               "command.expectedRevision");
    auto targetText = stringParameter(command.parameters, "target");
    if (!targetText) return Result<GameplayCommandReceipt>::failure(targetText.status());
    const auto targetId = PersistentId::parse(targetText.value());
    RPGActor* target = targetId ? resolve(SubjectRef::fromPersistentId(*targetId)) : nullptr;
    if (!target)
        return failure<GameplayCommandReceipt>(DiagnosticCode::NotFound,
                                               "RPG command target was not found", "parameters.target");
    std::string skillId;
    if (command.action == gameplayId("rpg:skill")) {
        auto skill = stringParameter(command.parameters, "skillId");
        if (!skill) return Result<GameplayCommandReceipt>::failure(skill.status());
        skillId = std::move(skill).takeValue();
    } else if (command.action != gameplayId("rpg:attack")) {
        return failure<GameplayCommandReceipt>(DiagnosticCode::Unsupported,
                                               "unsupported RPG gameplay action", "command.action");
    }
    auto queued = battle_.setActionChecked(actor, skillId, target);
    if (!queued) return Result<GameplayCommandReceipt>::failure(queued.status());
    ++revision_;
    lastCommandByActor_[actor] = command.id;
    GameplayEvent accepted;
    accepted.sequence = nextEventSequence_++;
    accepted.tick = tick_;
    accepted.type = "rpg.command.accepted";
    accepted.subject = command.subject;
    accepted.causationCommandId = command.id;
    accepted.correlationId = command.id;
    accepted.payload = Value(Value::Object{{"action", Value(command.action.format())}});
    events_.push_back(std::move(accepted));

    GameplayCommandReceipt receipt;
    receipt.commandId = command.id;
    receipt.executionId = "rpg-round-" + std::to_string(battle_.getTurn() + 1);
    receipt.acceptedTick = tick_;
    receipt.resultingRevision = revision_;
    receipt.details = Value(Value::Object{{"skillId", Value(skillId)},
                                          {"target", Value(targetText.value())}});
    return Result<GameplayCommandReceipt>::success(std::move(receipt),
                                                   Status::success(StatusCode::Applied));
}

Result<GameplayObservation> BattleControl::advanceGameplay(const GameplaySession& session,
                                                            SubjectRef instance,
                                                            const SimulationStep& step) {
    if (instance != instance_)
        return failure<GameplayObservation>(DiagnosticCode::NotFound,
                                            "RPG battle gameplay instance was not found", "instance");
    if (step.tick <= tick_)
        return failure<GameplayObservation>(DiagnosticCode::Conflict,
                                            "RPG simulation tick must increase", "step.tick");
    auto authorized = observeGameplay(session, instance);
    if (!authorized) return Result<GameplayObservation>::failure(authorized.status());
    std::move(authorized).takeValue();
    battle_.autoEnemyActions();
    battle_.startRound();
    while (battle_.executeNextAction() && !battle_.isFinished()) {}
    battle_.pollEvents();
    tick_ = step.tick;
    ++revision_;
    for (int index = 0; index < battle_.getEventCount(); ++index) {
        RPGActor* caster = battle_.getEventCaster(index);
        GameplayEvent event;
        event.sequence = nextEventSequence_++;
        event.tick = tick_;
        event.type = "rpg.battle." + battle_.getEventAction(index);
        event.subject = subjectOf(caster ? caster : battle_.getEventTarget(index));
        if (const auto found = lastCommandByActor_.find(caster); found != lastCommandByActor_.end()) {
            event.causationCommandId = found->second;
            event.correlationId = found->second;
        }
        event.payload = Value(Value::Object{
            {"amount", Value(battle_.getEventAmount(index))},
            {"critical", Value(battle_.getEventCrit(index))},
            {"skillId", Value(battle_.getEventSkillId(index))},
            {"target", Value(subjectOf(battle_.getEventTarget(index)).format())},
        });
        events_.push_back(std::move(event));
    }
    return observeGameplay(session, instance);
}

Result<std::vector<GameplayEvent>> BattleControl::gameplayEvents(const GameplaySession& session,
                                                                  SubjectRef instance,
                                                                  std::uint64_t afterSequence) const {
    auto authorized = observeGameplay(session, instance);
    if (!authorized) return Result<std::vector<GameplayEvent>>::failure(authorized.status());
    std::move(authorized).takeValue();
    std::vector<GameplayEvent> result;
    for (const auto& event : events_)
        if (event.sequence > afterSequence) result.push_back(event);
    const bool empty = result.empty();
    return Result<std::vector<GameplayEvent>>::success(
        std::move(result), Status::success(empty ? StatusCode::NoOp : StatusCode::Applied));
}

}  // namespace eve::rpg
