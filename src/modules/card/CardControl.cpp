#include "card/CardControl.h"

#include "card/Card.h"
#include "card/CardTypes.h"
#include "common/Capability.h"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

namespace eve::card {
namespace {

template <typename T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

LogicalId id(std::string_view text) { return LogicalId::parse(text).value(); }

Result<const Value::Object*> object(const Value& value) {
    const auto* result = value.getIf<Value::Object>();
    if (!result)
        return failure<const Value::Object*>(DiagnosticCode::InvalidArgument, "card parameters must be an object",
                                             "parameters");
    return Result<const Value::Object*>::success(result);
}

Result<std::string> text(const Value::Object& value, std::string_view name, std::string fallback = {}) {
    const auto found = value.find(std::string(name));
    if (found == value.end()) return Result<std::string>::success(std::move(fallback));
    if (!found->second.isString())
        return failure<std::string>(DiagnosticCode::InvalidArgument, "card parameter must be a string",
                                    "parameters." + std::string(name));
    return Result<std::string>::success(found->second.asString());
}

Result<double> number(const Value::Object& value, std::string_view name, double fallback) {
    const auto found = value.find(std::string(name));
    if (found == value.end()) return Result<double>::success(fallback);
    if (!found->second.isNumeric())
        return failure<double>(DiagnosticCode::InvalidArgument, "card parameter must be numeric",
                               "parameters." + std::string(name));
    return Result<double>::success(found->second.isInt64() ? static_cast<double>(found->second.asInt())
                                                           : found->second.asDouble());
}

const std::string kDraw{"card:draw"};
const std::string kPlay{"card:play"};
const std::string kSetAttribute{"card:set-attribute"};

/**
 * @brief Payment boundary used when a publication binds no account.
 *
 * Cost-free cards never touch the account, so they still play; a card that
 * charges a resource fails with this explicit diagnostic instead of the whole
 * action disappearing from discovery. `observe` reports `payment: unbound`, so
 * the gap is observable rather than silent.
 */
class UnboundPaymentAccount final : public eve::resource::IResourceAccount {
public:
    [[nodiscard]] eve::Result<eve::resource::Affordability> canAfford(const eve::resource::CostSpec&) const override {
        return eve::Result<eve::resource::Affordability>::failure(diagnostic());
    }
    [[nodiscard]] eve::Result<eve::resource::Reservation> reserve(const eve::resource::CostSpec&) override {
        return eve::Result<eve::resource::Reservation>::failure(diagnostic());
    }
    [[nodiscard]] eve::Result<eve::resource::Receipt> debit(const eve::resource::CostSpec&) override {
        return eve::Result<eve::resource::Receipt>::failure(diagnostic());
    }
    [[nodiscard]] eve::Result<eve::resource::Receipt> credit(const eve::resource::CostSpec&) override {
        return eve::Result<eve::resource::Receipt>::failure(diagnostic());
    }
    [[nodiscard]] eve::Result<eve::resource::Receipt> commit(const eve::resource::Reservation&) override {
        return eve::Result<eve::resource::Receipt>::failure(diagnostic());
    }
    [[nodiscard]] eve::Result<void> rollback(const eve::resource::Reservation&) override {
        return eve::Result<void>::failure(diagnostic());
    }

private:
    static eve::Diagnostic diagnostic() {
        return eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                      "this card instance has no payment account bound", "card.account");
    }
};

eve::resource::IResourceAccount& unboundAccount() {
    static UnboundPaymentAccount account;
    return account;
}

}  // namespace

CardControl::CardControl(Card& module) : module_(&module) {
    cap::addListener<IGameplayControlProvider>(this);
    cap::addListener<IGameplayInstanceCatalog>(this);
}

CardControl::~CardControl() {
    cap::removeListener<IGameplayInstanceCatalog>(this);
    cap::removeListener<IGameplayControlProvider>(this);
}

Result<void> CardControl::publish(SubjectRef instance, SubjectRef owner, Hand& hand,
                                  eve::resource::IResourceAccount* account) {
    if (!instance.isValid() || !owner.isValid())
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "card publication needs valid instance and owner ids", "instance"));
    if (find(instance) != nullptr)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Conflict, "a gameplay control already owns that card instance", "instance"));
    Entry entry;
    entry.instance = instance;
    entry.owner    = owner;
    entry.hand     = &hand;
    entry.account  = account;
    entries_.push_back(std::move(entry));
    return Result<void>::success();
}

Result<void> CardControl::unpublish(SubjectRef instance) {
    const auto found = std::find_if(entries_.begin(), entries_.end(),
                                    [instance](const Entry& entry) { return entry.instance == instance; });
    if (found == entries_.end())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "that card instance is not published", "instance"));
    entries_.erase(found);
    return Result<void>::success();
}

void CardControl::clear() { entries_.clear(); }

int CardControl::count() const { return static_cast<int>(entries_.size()); }

std::vector<SubjectRef> CardControl::instances() const {
    std::vector<SubjectRef> result;
    result.reserve(entries_.size());
    for (const auto& entry : entries_) result.push_back(entry.instance);
    return result;
}

std::string_view CardControl::gameplayDomain() const noexcept { return "card"; }

std::vector<SubjectRef> CardControl::gameplayInstances() const { return instances(); }

CardControl::Entry* CardControl::find(SubjectRef instance) {
    const auto found = std::find_if(entries_.begin(), entries_.end(),
                                    [instance](const Entry& entry) { return entry.instance == instance; });
    return found == entries_.end() ? nullptr : &*found;
}

const CardControl::Entry* CardControl::find(SubjectRef instance) const {
    const auto found = std::find_if(entries_.begin(), entries_.end(),
                                    [instance](const Entry& entry) { return entry.instance == instance; });
    return found == entries_.end() ? nullptr : &*found;
}

bool CardControl::controls(const GameplaySession& session, const Entry& entry) const {
    return session.access != GameplayAccess::PlayerEquivalent ||
           std::find(session.controlledSubjects.begin(), session.controlledSubjects.end(), entry.owner) !=
               session.controlledSubjects.end();
}

Value CardControl::handState(const Entry& entry) const {
    Value::Array hand;
    if (entry.hand != nullptr) {
        const int count = entry.hand->count();
        for (int index = 0; index < count; ++index) {
            CardData* card = entry.hand->get(index);
            if (card == nullptr) continue;
            hand.emplace_back(Value(Value::Object{
                {"card", Value(card->identity()->id)},
                {"definition", Value(card->identity()->definitionId)},
                {"name", Value(card->identity()->name)},
                {"kind", Value(card->identity()->kind)},
                {"cost", Value(static_cast<std::int64_t>(card->stats()->cost))},
                {"attack", Value(static_cast<std::int64_t>(card->stats()->attack))},
                {"health", Value(static_cast<std::int64_t>(card->stats()->health))},
                {"state",
                 Value(cardStateName(card->state()->phase) != nullptr ? std::string(cardStateName(card->state()->phase))
                                                                      : std::string{})},
                {"disabled", Value(card->visual()->disabled)},
            }));
        }
    }
    std::int64_t deckSize = 0;
    Deck*        deck     = module_ != nullptr ? module_->getDeck() : nullptr;
    if (deck != nullptr) deckSize = static_cast<std::int64_t>(deck->count());
    return Value(Value::Object{
        {"owner", Value(entry.hand != nullptr ? entry.hand->meta()->owner : std::string{})},
        {"hand", Value(std::move(hand))},
        {"handSize", Value(static_cast<std::int64_t>(entry.hand != nullptr ? entry.hand->count() : 0))},
        {"deck", Value(deckSize)},
        // `unbound` means this publication carries no payment account, so
        // `card:play` is deliberately unavailable rather than silently absent.
        {"payment", Value(entry.account != nullptr ? std::string("bound") : std::string("unbound"))},
    });
}

Result<GameplayObservation> CardControl::observeGameplay(const GameplaySession& session, SubjectRef instance) const {
    const Entry* entry = find(instance);
    if (entry == nullptr)
        return failure<GameplayObservation>(DiagnosticCode::NotFound, "card gameplay instance was not found",
                                            "instance");
    if (!controls(session, *entry))
        return failure<GameplayObservation>(DiagnosticCode::PreconditionViolation,
                                            "session does not control the card owner", "instance");
    GameplayObservation observation;
    observation.domain   = id("gameplay:card");
    observation.instance = entry->instance;
    observation.tick     = entry->tick;
    observation.revision = entry->revision;
    observation.state    = handState(*entry);
    return Result<GameplayObservation>::success(std::move(observation));
}

Result<std::vector<GameplayActionDescriptor>> CardControl::availableGameplayActions(const GameplaySession& session,
                                                                                    SubjectRef             instance,
                                                                                    SubjectRef subject) const {
    auto observed = observeGameplay(session, instance);
    if (!observed) return Result<std::vector<GameplayActionDescriptor>>::failure(observed.status());
    const Entry* entry = find(instance);
    if (entry == nullptr)
        return failure<std::vector<GameplayActionDescriptor>>(DiagnosticCode::NotFound,
                                                              "card gameplay instance was not found", "instance");
    if (subject != entry->owner)
        return failure<std::vector<GameplayActionDescriptor>>(DiagnosticCode::PreconditionViolation,
                                                              "card action subject must be its owner", "subject");

    const Value                           stringType(Value::Object{{"type", Value("string")}});
    const Value                           numberType(Value::Object{{"type", Value("number")}});
    std::vector<GameplayActionDescriptor> actions{
        {id(kDraw), Value(Value::Object{})},
        // Cost-free cards play through the unbound payment boundary; a charging
        // card fails there with an explicit "no payment account bound" diagnostic,
        // so the gap is reported instead of the action vanishing from discovery.
        {id(kPlay), Value(Value::Object{{"card", stringType}})},
    };
    if (session.access != GameplayAccess::PlayerEquivalent) {
        actions.push_back(
            {id(kSetAttribute),
             Value(Value::Object{{"card", stringType}, {"attribute", stringType}, {"value", numberType}})});
    }
    return Result<std::vector<GameplayActionDescriptor>>::success(std::move(actions));
}

Result<GameplayCommandReceipt> CardControl::submitGameplay(const GameplaySession& session, SubjectRef instance,
                                                           const GameplayCommand& command) {
    Entry* entry = find(instance);
    if (entry == nullptr)
        return failure<GameplayCommandReceipt>(DiagnosticCode::NotFound, "card gameplay instance was not found",
                                               "instance");
    if (!controls(session, *entry) || command.subject != entry->owner)
        return failure<GameplayCommandReceipt>(DiagnosticCode::PreconditionViolation,
                                               "session does not control the card owner", "command.subject");
    if (command.id.empty())
        return failure<GameplayCommandReceipt>(DiagnosticCode::InvalidArgument, "command id must not be empty",
                                               "command.id");
    if (command.observedTick != entry->tick || command.expectedRevision != entry->revision)
        return failure<GameplayCommandReceipt>(
            DiagnosticCode::Conflict, "card command was based on a stale observation", "command.expectedRevision");
    auto values = object(command.parameters);
    if (!values) return Result<GameplayCommandReceipt>::failure(values.status());
    const Value::Object& parameters = *values.value();
    if (entry->hand == nullptr || module_ == nullptr)
        return failure<GameplayCommandReceipt>(DiagnosticCode::Failed, "card instance lost its hand", "instance");

    const std::string action = command.action.format();
    std::string       detail;
    std::string       eventType;
    std::int64_t      applied = 0;
    if (action == kDraw) {
        CardData* drawn = module_->drawCard(entry->hand->meta()->owner);
        if (drawn == nullptr)
            return failure<GameplayCommandReceipt>(DiagnosticCode::PreconditionViolation,
                                                   "the deck has no card to draw", "command.action");
        detail    = "drew " + drawn->identity()->definitionId;
        eventType = "card.drawn";
        applied   = 1;
    } else if (action == kPlay) {
        auto cardId = text(parameters, "card");
        if (!cardId) return Result<GameplayCommandReceipt>::failure(cardId.status());
        CardData* card = cardId.value().empty() ? nullptr : entry->hand->find(cardId.value());
        if (card == nullptr)
            return failure<GameplayCommandReceipt>(DiagnosticCode::NotFound, "that card is not in the hand",
                                                   "command.parameters.card");
        eve::resource::IResourceAccount& account = entry->account != nullptr ? *entry->account : unboundAccount();
        const auto                       played  = module_->play(*card, account, {}, command.id);
        if (!played) return Result<GameplayCommandReceipt>::failure(played.status());
        detail    = "played " + card->identity()->definitionId;
        eventType = "card.played";
        applied   = 1;
    } else if (action == kSetAttribute) {
        if (session.access == GameplayAccess::PlayerEquivalent)
            return failure<GameplayCommandReceipt>(DiagnosticCode::PreconditionViolation,
                                                   "rewriting card attributes requires the test-driver or "
                                                   "developer-cheat profile",
                                                   "session.access");
        auto cardId    = text(parameters, "card");
        auto attribute = text(parameters, "attribute");
        auto value     = number(parameters, "value", 0.0);
        if (!cardId) return Result<GameplayCommandReceipt>::failure(cardId.status());
        if (!attribute) return Result<GameplayCommandReceipt>::failure(attribute.status());
        if (!value) return Result<GameplayCommandReceipt>::failure(value.status());
        if (cardId.value().empty() || attribute.value().empty())
            return failure<GameplayCommandReceipt>(DiagnosticCode::InvalidArgument,
                                                   "set-attribute needs a card and an attribute", "command.parameters");
        CardData* card = entry->hand->find(cardId.value());
        if (card == nullptr)
            return failure<GameplayCommandReceipt>(DiagnosticCode::NotFound, "that card is not in the hand",
                                                   "command.parameters.card");
        const auto appliedAttribute = module_->setCardAttribute(*card, attribute.value(), value.value());
        if (!appliedAttribute) return Result<GameplayCommandReceipt>::failure(appliedAttribute.status());
        detail    = "set " + attribute.value() + " of " + card->identity()->definitionId;
        eventType = "card.attribute-changed";
        applied   = 1;
    } else {
        return failure<GameplayCommandReceipt>(DiagnosticCode::Unsupported, "unsupported card action",
                                               "command.action");
    }

    ++entry->revision;
    GameplayEvent event;
    event.sequence           = entry->nextEventSequence++;
    event.tick               = entry->tick;
    event.type               = eventType;
    event.subject            = entry->owner;
    event.causationCommandId = command.id;
    event.correlationId      = command.id;
    event.payload            = Value(Value::Object{
        {"instance", Value(entry->instance.format())}, {"detail", Value(detail)}, {"quantity", Value(applied)}});
    entry->events.push_back(std::move(event));

    GameplayCommandReceipt receipt;
    receipt.commandId         = command.id;
    receipt.executionId       = command.id + ".executed";
    receipt.acceptedTick      = entry->tick;
    receipt.resultingRevision = entry->revision;
    receipt.details =
        Value(Value::Object{{"action", Value(action)}, {"detail", Value(detail)}, {"quantity", Value(applied)}});
    return Result<GameplayCommandReceipt>::success(std::move(receipt), Status::success(StatusCode::Applied));
}

Result<GameplayObservation> CardControl::advanceGameplay(const GameplaySession& session, SubjectRef instance,
                                                         const SimulationStep& step) {
    Entry* entry = find(instance);
    if (entry == nullptr)
        return failure<GameplayObservation>(DiagnosticCode::NotFound, "card gameplay instance was not found",
                                            "instance");
    if (step.tick <= entry->tick)
        return failure<GameplayObservation>(DiagnosticCode::Conflict, "card simulation tick must increase",
                                            "step.tick");
    auto observed = observeGameplay(session, instance);
    if (!observed) return Result<GameplayObservation>::failure(observed.status());
    entry->tick = step.tick;
    return observeGameplay(session, instance);
}

Result<std::vector<GameplayEvent>> CardControl::gameplayEvents(const GameplaySession& session, SubjectRef instance,
                                                               std::uint64_t afterSequence) const {
    auto observed = observeGameplay(session, instance);
    if (!observed) return Result<std::vector<GameplayEvent>>::failure(observed.status());
    const Entry* entry = find(instance);
    if (entry == nullptr)
        return failure<std::vector<GameplayEvent>>(DiagnosticCode::NotFound, "card gameplay instance was not found",
                                                   "instance");
    std::vector<GameplayEvent> result;
    for (const auto& event : entry->events)
        if (event.sequence > afterSequence) result.push_back(event);
    const bool empty = result.empty();
    return Result<std::vector<GameplayEvent>>::success(std::move(result),
                                                       Status::success(empty ? StatusCode::NoOp : StatusCode::Applied));
}

}  // namespace eve::card
