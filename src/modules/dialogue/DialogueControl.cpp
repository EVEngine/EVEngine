#include "dialogue/DialogueControl.h"

#include "common/Capability.h"
#include "dialogue/DialogueFlow.h"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

namespace eve::dialogue {
namespace {

template <typename T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

LogicalId id(std::string_view text) { return LogicalId::parse(text).value(); }

Result<const Value::Object*> object(const Value& value) {
    const auto* result = value.getIf<Value::Object>();
    if (!result)
        return failure<const Value::Object*>(DiagnosticCode::InvalidArgument, "dialogue parameters must be an object",
                                             "parameters");
    return Result<const Value::Object*>::success(result);
}

Result<std::string> text(const Value::Object& value, std::string_view name, std::string fallback = {}) {
    const auto found = value.find(std::string(name));
    if (found == value.end()) return Result<std::string>::success(std::move(fallback));
    if (!found->second.isString())
        return failure<std::string>(DiagnosticCode::InvalidArgument, "dialogue parameter must be a string",
                                    "parameters." + std::string(name));
    return Result<std::string>::success(found->second.asString());
}

const std::string kStart{"dialogue:start"};
const std::string kAdvance{"dialogue:advance"};
const std::string kSelect{"dialogue:select"};

}  // namespace

DialogueControl::DialogueControl(DialogueFlow& flow) : flow_(&flow) {
    cap::addListener<IGameplayControlProvider>(this);
    cap::addListener<IGameplayInstanceCatalog>(this);
}

DialogueControl::~DialogueControl() {
    cap::removeListener<IGameplayInstanceCatalog>(this);
    cap::removeListener<IGameplayControlProvider>(this);
}

Result<void> DialogueControl::publish(SubjectRef instance, SubjectRef owner) {
    if (!instance.isValid() || !owner.isValid())
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "dialogue publication needs valid instance and owner ids", "instance"));
    if (entry_.has_value())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict,
                              "the dialogue runner is already published as " + entry_->instance.format(), "instance"));
    Entry entry;
    entry.instance = instance;
    entry.owner    = owner;
    entry_.emplace(std::move(entry));
    return Result<void>::success();
}

Result<void> DialogueControl::unpublish(SubjectRef instance) {
    if (!entry_.has_value() || entry_->instance != instance)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "that dialogue instance is not published", "instance"));
    entry_.reset();
    return Result<void>::success();
}

void DialogueControl::clear() { entry_.reset(); }

int DialogueControl::count() const { return entry_.has_value() ? 1 : 0; }

std::vector<SubjectRef> DialogueControl::instances() const {
    std::vector<SubjectRef> result;
    if (entry_.has_value()) result.push_back(entry_->instance);
    return result;
}

std::string_view DialogueControl::gameplayDomain() const noexcept { return "dialogue"; }

std::vector<SubjectRef> DialogueControl::gameplayInstances() const { return instances(); }

bool DialogueControl::controls(const GameplaySession& session) const {
    if (!entry_.has_value()) return false;
    return session.access != GameplayAccess::PlayerEquivalent ||
           std::find(session.controlledSubjects.begin(), session.controlledSubjects.end(), entry_->owner) !=
               session.controlledSubjects.end();
}

Value DialogueControl::conversationState() const {
    Value::Array routes;
    if (flow_ != nullptr) {
        const int routeCount = flow_->getRouteCount();
        for (int index = 0; index < routeCount; ++index)
            routes.emplace_back(Value(Value::Object{{"index", Value(static_cast<std::int64_t>(index))},
                                                    {"route", Value(flow_->getRouteId(index))}}));
    }
    const bool active = flow_ != nullptr && flow_->isActive();
    return Value(Value::Object{{"active", Value(active)},
                               {"blocked", Value(flow_ != nullptr && flow_->isBlocked())},
                               {"conversation", Value(active ? flow_->getConversationId() : std::string{})},
                               {"node", Value(active ? flow_->getNodeId() : std::string{})},
                               {"nodeKind", Value(active ? flow_->getNodeKind() : std::string{})},
                               {"speaker", Value(active ? flow_->getSpeaker() : std::string{})},
                               {"text", Value(active ? flow_->getText() : std::string{})},
                               {"pool", Value(active ? flow_->getPool() : std::string{})},
                               {"i18nKey", Value(active ? flow_->getI18nKey() : std::string{})},
                               {"routes", Value(std::move(routes))},
                               // Automation has no Squirrel call frame, so a conversation
                               // started here sees an empty binding table.
                               {"bindings", Value("empty")}});
}

Result<GameplayObservation> DialogueControl::observeGameplay(const GameplaySession& session,
                                                             SubjectRef             instance) const {
    if (!entry_.has_value() || entry_->instance != instance || flow_ == nullptr)
        return failure<GameplayObservation>(DiagnosticCode::NotFound, "dialogue gameplay instance was not found",
                                            "instance");
    if (!controls(session))
        return failure<GameplayObservation>(DiagnosticCode::PreconditionViolation,
                                            "session does not control the dialogue owner", "instance");
    GameplayObservation observation;
    observation.domain   = id("gameplay:dialogue");
    observation.instance = entry_->instance;
    observation.tick     = entry_->tick;
    observation.revision = entry_->revision;
    observation.state    = conversationState();
    return Result<GameplayObservation>::success(std::move(observation));
}

Result<std::vector<GameplayActionDescriptor>> DialogueControl::availableGameplayActions(const GameplaySession& session,
                                                                                        SubjectRef             instance,
                                                                                        SubjectRef subject) const {
    auto observed = observeGameplay(session, instance);
    if (!observed) return Result<std::vector<GameplayActionDescriptor>>::failure(observed.status());
    if (!entry_.has_value() || subject != entry_->owner)
        return failure<std::vector<GameplayActionDescriptor>>(DiagnosticCode::PreconditionViolation,
                                                              "dialogue action subject must be its owner", "subject");

    const Value                           stringType(Value::Object{{"type", Value("string")}});
    std::vector<GameplayActionDescriptor> actions{
        {id(kStart), Value(Value::Object{{"conversation", stringType}})},
        {id(kAdvance), Value(Value::Object{})},
        // The route vocabulary is node-dependent, so discovery advertises the
        // parameter shape and the caller reads the legal routes from `observe`.
        {id(kSelect), Value(Value::Object{{"route", stringType}})},
    };
    return Result<std::vector<GameplayActionDescriptor>>::success(std::move(actions));
}

void DialogueControl::record(Entry& entry, const GameplayCommand& command, std::string type, std::string detail,
                             std::int64_t quantity) {
    ++entry.revision;
    GameplayEvent event;
    event.sequence           = entry.nextEventSequence++;
    event.tick               = entry.tick;
    event.type               = std::move(type);
    event.subject            = entry.owner;
    event.causationCommandId = command.id;
    event.correlationId      = command.id;
    event.payload            = Value(Value::Object{
        {"instance", Value(entry.instance.format())},
        {"detail", Value(detail)},
        {"quantity", Value(quantity)},
        {"node", Value(flow_ != nullptr && flow_->isActive() ? flow_->getNodeId() : std::string{})},
        {"nodeKind", Value(flow_ != nullptr && flow_->isActive() ? flow_->getNodeKind() : std::string{})}});
    entry.events.push_back(std::move(event));
}

Result<GameplayCommandReceipt> DialogueControl::submitGameplay(const GameplaySession& session, SubjectRef instance,
                                                               const GameplayCommand& command) {
    if (!entry_.has_value() || entry_->instance != instance || flow_ == nullptr)
        return failure<GameplayCommandReceipt>(DiagnosticCode::NotFound, "dialogue gameplay instance was not found",
                                               "instance");
    if (!controls(session) || command.subject != entry_->owner)
        return failure<GameplayCommandReceipt>(DiagnosticCode::PreconditionViolation,
                                               "session does not control the dialogue owner", "command.subject");
    if (command.id.empty())
        return failure<GameplayCommandReceipt>(DiagnosticCode::InvalidArgument, "command id must not be empty",
                                               "command.id");
    if (command.observedTick != entry_->tick || command.expectedRevision != entry_->revision)
        return failure<GameplayCommandReceipt>(
            DiagnosticCode::Conflict, "dialogue command was based on a stale observation", "command.expectedRevision");
    auto values = object(command.parameters);
    if (!values) return Result<GameplayCommandReceipt>::failure(values.status());
    const Value::Object& parameters = *values.value();

    const std::string action = command.action.format();
    std::string       detail;
    std::string       eventType;
    std::int64_t      applied = 0;
    if (action == kStart) {
        auto conversation = text(parameters, "conversation");
        if (!conversation) return Result<GameplayCommandReceipt>::failure(conversation.status());
        if (conversation.value().empty())
            return failure<GameplayCommandReceipt>(DiagnosticCode::InvalidArgument,
                                                   "dialogue:start needs a conversation id",
                                                   "command.parameters.conversation");
        if (flow_->isActive())
            return failure<GameplayCommandReceipt>(DiagnosticCode::PreconditionViolation,
                                                   "a conversation is already active", "command.parameters");
        const auto started = flow_->startChecked(conversation.value());
        if (!started) return Result<GameplayCommandReceipt>::failure(started.status());
        detail    = "started " + conversation.value();
        eventType = "dialogue.started";
        applied   = 1;
    } else if (action == kAdvance) {
        if (!flow_->isActive())
            return failure<GameplayCommandReceipt>(DiagnosticCode::PreconditionViolation, "no conversation is active",
                                                   "command.action");
        const auto advanced = flow_->advanceChecked();
        if (!advanced) return Result<GameplayCommandReceipt>::failure(advanced.status());
        detail    = flow_->isActive() ? "advanced to " + flow_->getNodeId() : std::string("conversation finished");
        eventType = flow_->isActive() ? "dialogue.advanced" : "dialogue.finished";
        applied   = 1;
    } else if (action == kSelect) {
        auto route = text(parameters, "route");
        if (!route) return Result<GameplayCommandReceipt>::failure(route.status());
        if (route.value().empty())
            return failure<GameplayCommandReceipt>(DiagnosticCode::InvalidArgument, "dialogue:select needs a route id",
                                                   "command.parameters.route");
        if (!flow_->isActive())
            return failure<GameplayCommandReceipt>(DiagnosticCode::PreconditionViolation, "no conversation is active",
                                                   "command.action");
        const auto selected = flow_->select(route.value());
        if (!selected) return Result<GameplayCommandReceipt>::failure(selected.status());
        detail    = "selected route " + route.value();
        eventType = "dialogue.selected";
        applied   = 1;
    } else {
        return failure<GameplayCommandReceipt>(DiagnosticCode::Unsupported, "unsupported dialogue action",
                                               "command.action");
    }

    record(*entry_, command, eventType, detail, applied);
    GameplayCommandReceipt receipt;
    receipt.commandId         = command.id;
    receipt.executionId       = command.id + ".executed";
    receipt.acceptedTick      = entry_->tick;
    receipt.resultingRevision = entry_->revision;
    receipt.details =
        Value(Value::Object{{"action", Value(action)},
                            {"detail", Value(detail)},
                            {"quantity", Value(applied)},
                            {"active", Value(flow_->isActive())},
                            {"node", Value(flow_->isActive() ? flow_->getNodeId() : std::string{})},
                            {"nodeKind", Value(flow_->isActive() ? flow_->getNodeKind() : std::string{})}});
    return Result<GameplayCommandReceipt>::success(std::move(receipt), Status::success(StatusCode::Applied));
}

Result<GameplayObservation> DialogueControl::advanceGameplay(const GameplaySession& session, SubjectRef instance,
                                                             const SimulationStep& step) {
    if (!entry_.has_value() || entry_->instance != instance || flow_ == nullptr)
        return failure<GameplayObservation>(DiagnosticCode::NotFound, "dialogue gameplay instance was not found",
                                            "instance");
    if (step.tick <= entry_->tick)
        return failure<GameplayObservation>(DiagnosticCode::Conflict, "dialogue simulation tick must increase",
                                            "step.tick");
    auto observed = observeGameplay(session, instance);
    if (!observed) return Result<GameplayObservation>::failure(observed.status());
    entry_->tick = step.tick;
    return observeGameplay(session, instance);
}

Result<std::vector<GameplayEvent>> DialogueControl::gameplayEvents(const GameplaySession& session, SubjectRef instance,
                                                                   std::uint64_t afterSequence) const {
    auto observed = observeGameplay(session, instance);
    if (!observed) return Result<std::vector<GameplayEvent>>::failure(observed.status());
    std::vector<GameplayEvent> result;
    for (const auto& event : entry_->events)
        if (event.sequence > afterSequence) result.push_back(event);
    const bool empty = result.empty();
    return Result<std::vector<GameplayEvent>>::success(std::move(result),
                                                       Status::success(empty ? StatusCode::NoOp : StatusCode::Applied));
}

}  // namespace eve::dialogue
