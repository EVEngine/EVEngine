#include "economy/EconomyControl.h"

#include "common/Capability.h"
#include "economy/EconomySystem.h"
#include "economy/ResourceType.h"

#include <algorithm>
#include <limits>
#include <set>
#include <string>
#include <utility>

namespace eve::economy {
namespace {

template <typename T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

LogicalId id(std::string_view text) { return LogicalId::parse(text).value(); }

Result<const Value::Object*> object(const Value& value) {
    const auto* result = value.getIf<Value::Object>();
    if (!result)
        return failure<const Value::Object*>(DiagnosticCode::InvalidArgument, "economy parameters must be an object",
                                             "parameters");
    return Result<const Value::Object*>::success(result);
}

Result<std::string> text(const Value::Object& value, std::string_view name, std::string fallback = {}) {
    const auto found = value.find(std::string(name));
    if (found == value.end()) return Result<std::string>::success(std::move(fallback));
    if (!found->second.isString())
        return failure<std::string>(DiagnosticCode::InvalidArgument, "economy parameter must be a string",
                                    "parameters." + std::string(name));
    return Result<std::string>::success(found->second.asString());
}

Result<int> integer(const Value::Object& value, std::string_view name, int fallback) {
    const auto found = value.find(std::string(name));
    if (found == value.end()) return Result<int>::success(fallback);
    if (!found->second.isInt64() || found->second.asInt() < std::numeric_limits<int>::min() ||
        found->second.asInt() > std::numeric_limits<int>::max())
        return failure<int>(DiagnosticCode::InvalidArgument, "economy parameter must be an integer",
                            "parameters." + std::string(name));
    return Result<int>::success(static_cast<int>(found->second.asInt()));
}

const std::string kCredit{"economy:credit"};
const std::string kDebit{"economy:debit"};

std::string depletionName(DepletionModel model) {
    switch (model) {
        case DepletionModel::Renewable: return "renewable";
        case DepletionModel::Infinite: return "infinite";
        case DepletionModel::Growing: return "growing";
        case DepletionModel::Finite: break;
    }
    return "finite";
}

}  // namespace

EconomyControl::EconomyControl() {
    cap::addListener<IGameplayControlProvider>(this);
    cap::addListener<IGameplayInstanceCatalog>(this);
}

EconomyControl::~EconomyControl() {
    cap::removeListener<IGameplayInstanceCatalog>(this);
    cap::removeListener<IGameplayControlProvider>(this);
}

Result<void> EconomyControl::publish(SubjectRef instance, SubjectRef owner, int player) {
    if (!instance.isValid() || !owner.isValid())
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "economy publication needs valid instance and owner ids", "instance"));
    if (find(instance) != nullptr)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Conflict, "a gameplay control already owns that economy instance", "instance"));
    Entry entry;
    entry.instance = instance;
    entry.owner    = owner;
    entry.player   = player;
    entries_.push_back(std::move(entry));
    return Result<void>::success();
}

Result<void> EconomyControl::unpublish(SubjectRef instance) {
    const auto found = std::find_if(entries_.begin(), entries_.end(),
                                    [instance](const Entry& entry) { return entry.instance == instance; });
    if (found == entries_.end())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "that economy instance is not published", "instance"));
    entries_.erase(found);
    return Result<void>::success();
}

void EconomyControl::clear() { entries_.clear(); }

int EconomyControl::count() const { return static_cast<int>(entries_.size()); }

std::vector<SubjectRef> EconomyControl::instances() const {
    std::vector<SubjectRef> result;
    result.reserve(entries_.size());
    for (const auto& entry : entries_) result.push_back(entry.instance);
    return result;
}

std::string_view EconomyControl::gameplayDomain() const noexcept { return "economy"; }

std::vector<SubjectRef> EconomyControl::gameplayInstances() const { return instances(); }

EconomyControl::Entry* EconomyControl::find(SubjectRef instance) {
    const auto found = std::find_if(entries_.begin(), entries_.end(),
                                    [instance](const Entry& entry) { return entry.instance == instance; });
    return found == entries_.end() ? nullptr : &*found;
}

const EconomyControl::Entry* EconomyControl::find(SubjectRef instance) const {
    const auto found = std::find_if(entries_.begin(), entries_.end(),
                                    [instance](const Entry& entry) { return entry.instance == instance; });
    return found == entries_.end() ? nullptr : &*found;
}

bool EconomyControl::controls(const GameplaySession& session, const Entry& entry) const {
    return session.access != GameplayAccess::PlayerEquivalent ||
           std::find(session.controlledSubjects.begin(), session.controlledSubjects.end(), entry.owner) !=
               session.controlledSubjects.end();
}

Value EconomyControl::ledgerState(const Entry& entry) const {
    const EconomyLedger::Snapshot snapshot = EconomySystem::snapshot(entry.player);
    // Registered types come first so a freshly published instance still reports the
    // resources the game declared, even before any balance moves.
    std::set<std::string> types;
    const int             registered = ResourceTypeRegistry::count();
    for (int index = 0; index < registered; ++index) {
        const ResourceTypeDef* def = ResourceTypeRegistry::typeAt(index);
        if (def != nullptr) types.insert(def->id);
    }
    for (const auto& [type, amount] : snapshot.current) {
        (void)amount;
        types.insert(type);
    }
    for (const auto& [type, amount] : snapshot.income) {
        (void)amount;
        types.insert(type);
    }
    for (const auto& [type, amount] : snapshot.expense) {
        (void)amount;
        types.insert(type);
    }
    for (const auto& [type, amount] : snapshot.wasted) {
        (void)amount;
        types.insert(type);
    }

    Value::Array resources;
    for (const auto& type : types) {
        const ResourceTypeDef* def     = ResourceTypeRegistry::find(type);
        const int              balance = EconomySystem::get(entry.player, type);
        Value::Object          resource{
                     {"type", Value(type)},
                     {"amount", Value(static_cast<std::int64_t>(balance))},
                     {"cap", Value(static_cast<std::int64_t>(EconomySystem::getCap(entry.player, type)))},
                     {"wasted", Value(static_cast<std::int64_t>(EconomySystem::getWasted(entry.player, type)))},
                     {"income", Value(static_cast<std::int64_t>(EconomySystem::getIncome(entry.player, type)))},
                     {"expense", Value(static_cast<std::int64_t>(EconomySystem::getExpense(entry.player, type)))}};
        if (def != nullptr) {
            resource.emplace("category", Value(def->category));
            resource.emplace("depletion", Value(depletionName(def->depletion)));
        }
        resources.emplace_back(Value(std::move(resource)));
    }
    return Value(Value::Object{{"player", Value(static_cast<std::int64_t>(entry.player))},
                               {"resources", Value(std::move(resources))}});
}

Result<GameplayObservation> EconomyControl::observeGameplay(const GameplaySession& session, SubjectRef instance) const {
    const Entry* entry = find(instance);
    if (entry == nullptr)
        return failure<GameplayObservation>(DiagnosticCode::NotFound, "economy gameplay instance was not found",
                                            "instance");
    if (!controls(session, *entry))
        return failure<GameplayObservation>(DiagnosticCode::PreconditionViolation,
                                            "session does not control the economy owner", "instance");
    GameplayObservation observation;
    observation.domain   = id("gameplay:economy");
    observation.instance = entry->instance;
    observation.tick     = entry->tick;
    observation.revision = entry->revision;
    observation.state    = ledgerState(*entry);
    return Result<GameplayObservation>::success(std::move(observation));
}

Result<std::vector<GameplayActionDescriptor>> EconomyControl::availableGameplayActions(const GameplaySession& session,
                                                                                       SubjectRef             instance,
                                                                                       SubjectRef subject) const {
    auto observed = observeGameplay(session, instance);
    if (!observed) return Result<std::vector<GameplayActionDescriptor>>::failure(observed.status());
    const Entry* entry = find(instance);
    if (entry == nullptr)
        return failure<std::vector<GameplayActionDescriptor>>(DiagnosticCode::NotFound,
                                                              "economy gameplay instance was not found", "instance");
    if (subject != entry->owner)
        return failure<std::vector<GameplayActionDescriptor>>(DiagnosticCode::PreconditionViolation,
                                                              "economy action subject must be its owner", "subject");

    const Value                           stringType(Value::Object{{"type", Value("string")}});
    const Value                           integerType(Value::Object{{"type", Value("integer")}});
    std::vector<GameplayActionDescriptor> actions{
        {id(kDebit), Value(Value::Object{{"type", stringType}, {"amount", integerType}})},
    };
    // A grant is not a player action; it is advertised only to the profiles allowed
    // to use it, so discovery never offers an action the same session would refuse.
    if (session.access != GameplayAccess::PlayerEquivalent)
        actions.push_back({id(kCredit), Value(Value::Object{{"type", stringType}, {"amount", integerType}})});
    return Result<std::vector<GameplayActionDescriptor>>::success(std::move(actions));
}

Result<GameplayCommandReceipt> EconomyControl::submitGameplay(const GameplaySession& session, SubjectRef instance,
                                                              const GameplayCommand& command) {
    Entry* entry = find(instance);
    if (entry == nullptr)
        return failure<GameplayCommandReceipt>(DiagnosticCode::NotFound, "economy gameplay instance was not found",
                                               "instance");
    if (!controls(session, *entry) || command.subject != entry->owner)
        return failure<GameplayCommandReceipt>(DiagnosticCode::PreconditionViolation,
                                               "session does not control the economy owner", "command.subject");
    if (command.id.empty())
        return failure<GameplayCommandReceipt>(DiagnosticCode::InvalidArgument, "command id must not be empty",
                                               "command.id");
    if (command.observedTick != entry->tick || command.expectedRevision != entry->revision)
        return failure<GameplayCommandReceipt>(
            DiagnosticCode::Conflict, "economy command was based on a stale observation", "command.expectedRevision");
    auto values = object(command.parameters);
    if (!values) return Result<GameplayCommandReceipt>::failure(values.status());
    const Value::Object& parameters = *values.value();

    auto type   = text(parameters, "type");
    auto amount = integer(parameters, "amount", 0);
    if (!type) return Result<GameplayCommandReceipt>::failure(type.status());
    if (!amount) return Result<GameplayCommandReceipt>::failure(amount.status());
    if (type.value().empty() || amount.value() <= 0)
        return failure<GameplayCommandReceipt>(DiagnosticCode::InvalidArgument,
                                               "economy commands need a resource type and a positive amount",
                                               "command.parameters");

    const std::string action = command.action.format();
    std::string       detail;
    std::string       eventType;
    std::int64_t      applied = 0;
    std::string       wastedType;
    std::int64_t      wastedAmount = 0;
    if (action == kDebit) {
        const EconomyLedger::Snapshot snapshot = EconomySystem::snapshot(entry->player);
        const bool                    known    = ResourceTypeRegistry::find(type.value()) != nullptr ||
                           snapshot.current.contains(type.value()) || snapshot.income.contains(type.value()) ||
                           snapshot.expense.contains(type.value());
        if (!known)
            return failure<GameplayCommandReceipt>(
                DiagnosticCode::NotFound, "the ledger does not know that resource type", "command.parameters.type");
        if (EconomySystem::get(entry->player, type.value()) < amount.value())
            return failure<GameplayCommandReceipt>(DiagnosticCode::PreconditionViolation,
                                                   "the ledger cannot afford that spend", "command.parameters.amount");
        if (!EconomySystem::debit(entry->player, type.value(), amount.value()))
            return failure<GameplayCommandReceipt>(DiagnosticCode::Failed, "the ledger rejected that spend",
                                                   "command.parameters");
        detail    = "spent " + std::to_string(amount.value()) + " " + type.value();
        eventType = "economy.debited";
        applied   = amount.value();
    } else if (action == kCredit) {
        if (session.access == GameplayAccess::PlayerEquivalent)
            return failure<GameplayCommandReceipt>(
                DiagnosticCode::PreconditionViolation,
                "granting resources requires the test-driver or developer-cheat profile", "session.access");
        const int accepted = EconomySystem::credit(entry->player, type.value(), amount.value());
        // A capped ledger stores what fits and books the rest as waste; both are
        // disclosed instead of reporting the requested amount as applied.
        wastedType   = type.value();
        wastedAmount = amount.value() - accepted;
        detail       = "granted " + std::to_string(accepted) + " " + type.value();
        if (wastedAmount > 0) detail += " (" + std::to_string(wastedAmount) + " wasted at cap)";
        eventType = "economy.credited";
        applied   = accepted;
    } else {
        return failure<GameplayCommandReceipt>(DiagnosticCode::Unsupported, "unsupported economy action",
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
    if (wastedAmount > 0) {
        GameplayEvent overflow;
        overflow.sequence           = entry->nextEventSequence++;
        overflow.tick               = entry->tick;
        overflow.type               = "economy.wasted";
        overflow.subject            = entry->owner;
        overflow.causationCommandId = command.id;
        overflow.correlationId      = command.id;
        overflow.payload =
            Value(Value::Object{{"instance", Value(entry->instance.format())},
                                {"detail", Value("wasted " + std::to_string(wastedAmount) + " " + wastedType)},
                                {"quantity", Value(wastedAmount)}});
        entry->events.push_back(std::move(overflow));
    }

    GameplayCommandReceipt receipt;
    receipt.commandId         = command.id;
    receipt.executionId       = command.id + ".executed";
    receipt.acceptedTick      = entry->tick;
    receipt.resultingRevision = entry->revision;
    receipt.details =
        Value(Value::Object{{"action", Value(action)}, {"detail", Value(detail)}, {"quantity", Value(applied)}});
    return Result<GameplayCommandReceipt>::success(std::move(receipt), Status::success(StatusCode::Applied));
}

Result<GameplayObservation> EconomyControl::advanceGameplay(const GameplaySession& session, SubjectRef instance,
                                                            const SimulationStep& step) {
    Entry* entry = find(instance);
    if (entry == nullptr)
        return failure<GameplayObservation>(DiagnosticCode::NotFound, "economy gameplay instance was not found",
                                            "instance");
    if (step.tick <= entry->tick)
        return failure<GameplayObservation>(DiagnosticCode::Conflict, "economy simulation tick must increase",
                                            "step.tick");
    auto observed = observeGameplay(session, instance);
    if (!observed) return Result<GameplayObservation>::failure(observed.status());
    entry->tick = step.tick;
    return observeGameplay(session, instance);
}

Result<std::vector<GameplayEvent>> EconomyControl::gameplayEvents(const GameplaySession& session, SubjectRef instance,
                                                                  std::uint64_t afterSequence) const {
    auto observed = observeGameplay(session, instance);
    if (!observed) return Result<std::vector<GameplayEvent>>::failure(observed.status());
    const Entry* entry = find(instance);
    if (entry == nullptr)
        return failure<std::vector<GameplayEvent>>(DiagnosticCode::NotFound, "economy gameplay instance was not found",
                                                   "instance");
    std::vector<GameplayEvent> result;
    for (const auto& event : entry->events)
        if (event.sequence > afterSequence) result.push_back(event);
    const bool empty = result.empty();
    return Result<std::vector<GameplayEvent>>::success(std::move(result),
                                                       Status::success(empty ? StatusCode::NoOp : StatusCode::Applied));
}

}  // namespace eve::economy
