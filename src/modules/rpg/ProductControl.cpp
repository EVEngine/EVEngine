#include "rpg/ProductControl.h"

#include "common/Capability.h"
#include "inventory/Bag.h"
#include "rpg/GameState.h"
#include "rpg/QuestReward.h"
#include "rpg/ShopTransaction.h"
#include "rpg/Tracker.h"
#include "rpg/WorldInteraction.h"

#include <algorithm>
#include <limits>
#include <optional>

namespace eve::rpg {
namespace {

template <typename T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

LogicalId gameplayId(std::string_view value) { return LogicalId::parse(value).value(); }

Result<const Value::Object*> parameters(const Value& value) {
    const auto* object = value.getIf<Value::Object>();
    if (!object)
        return failure<const Value::Object*>(DiagnosticCode::InvalidArgument,
                                             "RPG product parameters must be an object", "parameters");
    return Result<const Value::Object*>::success(object);
}

Result<std::string> stringParameter(const Value& value, std::string_view name,
                                    bool optional = false) {
    auto object = parameters(value);
    if (!object) return Result<std::string>::failure(object.status());
    const auto found = object.value()->find(std::string(name));
    if (found == object.value()->end() && optional) return Result<std::string>::success({});
    if (found == object.value()->end() || !found->second.isString())
        return failure<std::string>(DiagnosticCode::InvalidArgument,
                                    "RPG product parameter must be a string",
                                    "parameters." + std::string(name));
    return Result<std::string>::success(found->second.asString());
}

Result<int> integerParameter(const Value& value, std::string_view name, int fallback = 0,
                             bool optional = false) {
    auto object = parameters(value);
    if (!object) return Result<int>::failure(object.status());
    const auto found = object.value()->find(std::string(name));
    if (found == object.value()->end() && optional) return Result<int>::success(fallback);
    if (found == object.value()->end() || !found->second.isInt64())
        return failure<int>(DiagnosticCode::InvalidArgument,
                            "RPG product parameter must be an integer",
                            "parameters." + std::string(name));
    const auto number = found->second.asInt();
    if (number < std::numeric_limits<int>::min() || number > std::numeric_limits<int>::max())
        return failure<int>(DiagnosticCode::InvalidArgument,
                            "RPG product integer parameter is out of range",
                            "parameters." + std::string(name));
    return Result<int>::success(static_cast<int>(number));
}

Result<double> numericParameter(const Value& value, std::string_view name,
                                double fallback = 0.0, bool optional = false) {
    auto object = parameters(value);
    if (!object) return Result<double>::failure(object.status());
    const auto found = object.value()->find(std::string(name));
    if (found == object.value()->end() && optional) return Result<double>::success(fallback);
    if (found == object.value()->end() || !found->second.isNumeric())
        return failure<double>(DiagnosticCode::InvalidArgument,
                               "RPG product parameter must be numeric",
                               "parameters." + std::string(name));
    return Result<double>::success(found->second.isInt64()
                                       ? static_cast<double>(found->second.asInt())
                                       : found->second.asDouble());
}

Value schema(std::initializer_list<std::pair<const std::string, Value>> fields) {
    return Value(Value::Object(fields));
}

}  // namespace

ProductControl::ProductControl(SubjectRef instance, GameState& gameState, Tracker& tracker,
                               inventory::Bag& bag)
    : instance_(instance), gameState_(&gameState), tracker_(&tracker), bag_(&bag) {
    cap::addListener<IGameplayControlProvider>(this);
}

ProductControl::~ProductControl() { cap::removeListener<IGameplayControlProvider>(this); }

std::string_view ProductControl::gameplayDomain() const noexcept { return "rpg.product"; }

bool ProductControl::controls(const GameplaySession& session) const {
    return session.access != GameplayAccess::PlayerEquivalent ||
           std::find(session.controlledSubjects.begin(), session.controlledSubjects.end(), instance_) !=
               session.controlledSubjects.end();
}

Result<Value> ProductControl::stateProjection() const {
    if (!instance_.isValid() || !gameState_ || !tracker_ || !bag_)
        return failure<Value>(DiagnosticCode::StaleHandle,
                              "RPG product control participants are invalid", "instance");
    auto gameJson = gameState_->snapshotJson();
    if (!gameJson) return Result<Value>::failure(gameJson.status());
    auto questJson = tracker_->snapshotJson();
    if (!questJson) return Result<Value>::failure(questJson.status());
    auto game = Value::fromJson(gameJson.value());
    if (!game) return Result<Value>::failure(game.status());
    auto quests = Value::fromJson(questJson.value());
    if (!quests) return Result<Value>::failure(quests.status());
    Value::Array slots;
    slots.reserve(static_cast<std::size_t>(bag_->getSlotCount()));
    for (int index = 0; index < bag_->getSlotCount(); ++index) {
        slots.emplace_back(Value::Object{
            {"index", Value(index)},
            {"itemId", Value(bag_->getSlotItemId(index))},
            {"quantity", Value(bag_->getSlotQuantity(index))},
        });
    }
    return Result<Value>::success(Value(Value::Object{
        {"bag", Value(Value::Object{{"id", Value(bag_->getId())},
                                     {"kind", Value(bag_->getKind())},
                                     {"slots", Value(std::move(slots))}})},
        {"gameState", std::move(game).takeValue()},
        {"quests", std::move(quests).takeValue()},
    }));
}

Result<std::uint64_t> ProductControl::refreshRevision() const {
    auto state = stateProjection();
    if (!state) return Result<std::uint64_t>::failure(state.status());
    auto json = state.value().toJson();
    if (!json) return Result<std::uint64_t>::failure(json.status());
    if (stateFingerprint_ != json.value()) {
        stateFingerprint_ = json.value();
        ++revision_;
    }
    return Result<std::uint64_t>::success(revision_);
}

Result<GameplayObservation> ProductControl::observeGameplay(const GameplaySession& session,
                                                             SubjectRef instance) const {
    if (instance != instance_ || !instance_.isValid())
        return failure<GameplayObservation>(DiagnosticCode::NotFound,
                                            "RPG product gameplay instance was not found", "instance");
    if (!controls(session))
        return failure<GameplayObservation>(DiagnosticCode::PreconditionViolation,
                                            "session does not control this RPG product state", "instance");
    auto revision = refreshRevision();
    if (!revision) return Result<GameplayObservation>::failure(revision.status());
    auto state = stateProjection();
    if (!state) return Result<GameplayObservation>::failure(state.status());
    GameplayObservation observation;
    observation.domain = gameplayId("gameplay:rpg-product");
    observation.instance = instance_;
    observation.tick = tick_;
    observation.revision = revision.value();
    observation.state = std::move(state).takeValue();
    return Result<GameplayObservation>::success(std::move(observation));
}

Result<std::vector<GameplayActionDescriptor>> ProductControl::availableGameplayActions(
    const GameplaySession& session, SubjectRef instance, SubjectRef subject) const {
    auto observed = observeGameplay(session, instance);
    if (!observed)
        return Result<std::vector<GameplayActionDescriptor>>::failure(observed.status());
    std::move(observed).takeValue();
    if (subject != instance_)
        return failure<std::vector<GameplayActionDescriptor>>(
            DiagnosticCode::PreconditionViolation,
            "RPG product actions are owned by the product-state subject", "subject");
    const Value stringType = schema({{"type", Value("string")}});
    const Value integerType = schema({{"type", Value("integer")}});
    const Value numberType = schema({{"type", Value("number")}});
    return Result<std::vector<GameplayActionDescriptor>>::success({
        {gameplayId("rpg:buy-offer"),
         schema({{"currencyId", stringType}, {"offerId", stringType}, {"quantity", integerType}})},
        {gameplayId("rpg:sell-offer"),
         schema({{"currencyId", stringType}, {"offerId", stringType}, {"quantity", integerType}})},
        {gameplayId("rpg:claim-quest"), schema({{"questId", stringType}})},
        {gameplayId("rpg:collect-loot"),
         schema({{"attributeAmount", numberType}, {"attributeId", stringType},
                 {"itemId", stringType}, {"itemQuantity", integerType},
                 {"mapId", stringType}, {"notifyAmount", integerType},
                 {"notifyTarget", stringType}, {"notifyTopic", stringType},
                 {"objectId", stringType}, {"requiredQuestId", stringType}})},
    });
}

void ProductControl::publishEvent(std::string type, const GameplayCommand& command,
                                  Value payload) {
    GameplayEvent event;
    event.sequence = nextEventSequence_++;
    event.tick = tick_;
    event.type = std::move(type);
    event.subject = instance_;
    event.causationCommandId = command.id;
    event.correlationId = command.id;
    event.payload = std::move(payload);
    events_.push_back(std::move(event));
}

Result<GameplayCommandReceipt> ProductControl::submitGameplay(const GameplaySession& session,
                                                               SubjectRef instance,
                                                               const GameplayCommand& command) {
    if (instance != instance_)
        return failure<GameplayCommandReceipt>(DiagnosticCode::NotFound,
                                               "RPG product gameplay instance was not found", "instance");
    if (!controls(session) || command.subject != instance_)
        return failure<GameplayCommandReceipt>(DiagnosticCode::PreconditionViolation,
                                               "session does not control this RPG product state",
                                               "command.subject");
    if (command.id.empty())
        return failure<GameplayCommandReceipt>(DiagnosticCode::InvalidArgument,
                                               "command id must not be empty", "command.id");
    auto currentRevision = refreshRevision();
    if (!currentRevision) return Result<GameplayCommandReceipt>::failure(currentRevision.status());
    if (command.observedTick != tick_ || command.expectedRevision != currentRevision.value())
        return failure<GameplayCommandReceipt>(DiagnosticCode::Conflict,
                                               "RPG product command was based on a stale observation",
                                               "command.expectedRevision");

    std::optional<Result<int>> applied;
    std::string operation;
    if (command.action == gameplayId("rpg:buy-offer") ||
        command.action == gameplayId("rpg:sell-offer")) {
        auto currency = stringParameter(command.parameters, "currencyId");
        auto offer = stringParameter(command.parameters, "offerId");
        auto quantity = integerParameter(command.parameters, "quantity");
        if (!currency) return Result<GameplayCommandReceipt>::failure(currency.status());
        if (!offer) return Result<GameplayCommandReceipt>::failure(offer.status());
        if (!quantity) return Result<GameplayCommandReceipt>::failure(quantity.status());
        if (command.action == gameplayId("rpg:buy-offer")) {
            operation = "buy-offer";
            applied.emplace(ShopTransaction::buyOffer(*gameState_, *bag_, currency.value(),
                                                      offer.value(), quantity.value()));
        } else {
            operation = "sell-offer";
            applied.emplace(ShopTransaction::sellOffer(*gameState_, *bag_, currency.value(),
                                                       offer.value(), quantity.value()));
        }
    } else if (command.action == gameplayId("rpg:claim-quest")) {
        auto quest = stringParameter(command.parameters, "questId");
        if (!quest) return Result<GameplayCommandReceipt>::failure(quest.status());
        operation = "claim-quest";
        applied.emplace(QuestReward::claim(*tracker_, *gameState_, *bag_, quest.value()));
    } else if (command.action == gameplayId("rpg:collect-loot")) {
        WorldLootRequest request;
        auto map = stringParameter(command.parameters, "mapId");
        auto object = stringParameter(command.parameters, "objectId");
        auto requiredQuest = stringParameter(command.parameters, "requiredQuestId", true);
        auto item = stringParameter(command.parameters, "itemId", true);
        auto itemQuantity = integerParameter(command.parameters, "itemQuantity", 0, true);
        auto attribute = stringParameter(command.parameters, "attributeId", true);
        auto attributeAmount = numericParameter(command.parameters, "attributeAmount", 0.0, true);
        auto topic = stringParameter(command.parameters, "notifyTopic", true);
        auto target = stringParameter(command.parameters, "notifyTarget", true);
        auto amount = integerParameter(command.parameters, "notifyAmount", 0, true);
        if (!map) return Result<GameplayCommandReceipt>::failure(map.status());
        if (!object) return Result<GameplayCommandReceipt>::failure(object.status());
        if (!requiredQuest) return Result<GameplayCommandReceipt>::failure(requiredQuest.status());
        if (!item) return Result<GameplayCommandReceipt>::failure(item.status());
        if (!itemQuantity) return Result<GameplayCommandReceipt>::failure(itemQuantity.status());
        if (!attribute) return Result<GameplayCommandReceipt>::failure(attribute.status());
        if (!attributeAmount) return Result<GameplayCommandReceipt>::failure(attributeAmount.status());
        if (!topic) return Result<GameplayCommandReceipt>::failure(topic.status());
        if (!target) return Result<GameplayCommandReceipt>::failure(target.status());
        if (!amount) return Result<GameplayCommandReceipt>::failure(amount.status());
        request.mapId = map.value();
        request.objectId = object.value();
        request.requiredQuestId = requiredQuest.value();
        request.itemId = item.value();
        request.itemQuantity = itemQuantity.value();
        request.attributeId = attribute.value();
        request.attributeAmount = attributeAmount.value();
        request.notifyTopic = topic.value();
        request.notifyTarget = target.value();
        request.notifyAmount = amount.value();
        operation = "collect-loot";
        applied.emplace(WorldInteraction::collectLoot(*gameState_, *tracker_, *bag_, request));
    }
    if (!applied)
        return failure<GameplayCommandReceipt>(DiagnosticCode::Unsupported,
                                               "unsupported RPG product action", "command.action");
    if (!*applied) return Result<GameplayCommandReceipt>::failure(applied->status());
    const int effectCount = std::move(*applied).takeValue();
    stateFingerprint_.clear();
    auto resultingRevision = refreshRevision();
    if (!resultingRevision)
        return Result<GameplayCommandReceipt>::failure(resultingRevision.status());
    publishEvent("rpg.product." + operation, command,
                 Value(Value::Object{{"effectCount", Value(effectCount)},
                                     {"revision", Value(static_cast<std::int64_t>(resultingRevision.value()))}}));
    GameplayCommandReceipt receipt;
    receipt.commandId = command.id;
    receipt.executionId = operation + "-" + std::to_string(nextEventSequence_ - 1);
    receipt.acceptedTick = tick_;
    receipt.resultingRevision = resultingRevision.value();
    receipt.details = Value(Value::Object{{"effectCount", Value(effectCount)},
                                          {"operation", Value(operation)}});
    return Result<GameplayCommandReceipt>::success(std::move(receipt),
                                                   Status::success(StatusCode::Applied));
}

Result<GameplayObservation> ProductControl::advanceGameplay(const GameplaySession& session,
                                                             SubjectRef instance,
                                                             const SimulationStep& step) {
    if (step.tick <= tick_)
        return failure<GameplayObservation>(DiagnosticCode::Conflict,
                                            "RPG product simulation tick must increase", "step.tick");
    auto observed = observeGameplay(session, instance);
    if (!observed) return Result<GameplayObservation>::failure(observed.status());
    std::move(observed).takeValue();
    tick_ = step.tick;
    return observeGameplay(session, instance);
}

Result<std::vector<GameplayEvent>> ProductControl::gameplayEvents(const GameplaySession& session,
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

}  // namespace eve::rpg
