#include "inventory/InventoryControl.h"

#include "common/Capability.h"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

namespace eve::inventory {
namespace {

template <typename T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

LogicalId id(std::string_view text) { return LogicalId::parse(text).value(); }

Result<const Value::Object*> object(const Value& value) {
    const auto* result = value.getIf<Value::Object>();
    if (!result)
        return failure<const Value::Object*>(DiagnosticCode::InvalidArgument, "inventory parameters must be an object",
                                             "parameters");
    return Result<const Value::Object*>::success(result);
}

Result<std::string> text(const Value::Object& value, std::string_view name, std::string fallback = {}) {
    const auto found = value.find(std::string(name));
    if (found == value.end()) return Result<std::string>::success(std::move(fallback));
    if (!found->second.isString())
        return failure<std::string>(DiagnosticCode::InvalidArgument, "inventory parameter must be a string",
                                    "parameters." + std::string(name));
    return Result<std::string>::success(found->second.asString());
}

Result<int> integer(const Value::Object& value, std::string_view name, int fallback) {
    const auto found = value.find(std::string(name));
    if (found == value.end()) return Result<int>::success(fallback);
    if (!found->second.isInt64() || found->second.asInt() < std::numeric_limits<int>::min() ||
        found->second.asInt() > std::numeric_limits<int>::max())
        return failure<int>(DiagnosticCode::InvalidArgument, "inventory parameter must be an integer",
                            "parameters." + std::string(name));
    return Result<int>::success(static_cast<int>(found->second.asInt()));
}

const std::string kAddItem{"inventory:add-item"};
const std::string kRemoveItem{"inventory:remove-item"};
const std::string kMoveSlot{"inventory:move-slot"};
const std::string kEquip{"inventory:equip"};
const std::string kUnequip{"inventory:unequip"};

}  // namespace

InventoryControl::InventoryControl() {
    cap::addListener<IGameplayControlProvider>(this);
    cap::addListener<IGameplayInstanceCatalog>(this);
}

InventoryControl::~InventoryControl() {
    cap::removeListener<IGameplayInstanceCatalog>(this);
    cap::removeListener<IGameplayControlProvider>(this);
}

Result<void> InventoryControl::publish(SubjectRef instance, SubjectRef owner, Bag& bag, EquipmentSet* equipment) {
    if (!instance.isValid() || !owner.isValid())
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "inventory publication needs valid instance and owner ids", "instance"));
    if (find(instance) != nullptr)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Conflict, "a gameplay control already owns that inventory instance", "instance"));
    Entry entry;
    entry.instance  = instance;
    entry.owner     = owner;
    entry.bag       = &bag;
    entry.equipment = equipment;
    entries_.push_back(std::move(entry));
    return Result<void>::success();
}

Result<void> InventoryControl::unpublish(SubjectRef instance) {
    const auto found = std::find_if(entries_.begin(), entries_.end(),
                                    [instance](const Entry& entry) { return entry.instance == instance; });
    if (found == entries_.end())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "that inventory instance is not published", "instance"));
    entries_.erase(found);
    return Result<void>::success();
}

void InventoryControl::clear() { entries_.clear(); }

int InventoryControl::count() const { return static_cast<int>(entries_.size()); }

std::vector<SubjectRef> InventoryControl::instances() const {
    std::vector<SubjectRef> result;
    result.reserve(entries_.size());
    for (const auto& entry : entries_) result.push_back(entry.instance);
    return result;
}

InventoryControl::Entry* InventoryControl::find(SubjectRef instance) {
    const auto found = std::find_if(entries_.begin(), entries_.end(),
                                    [instance](const Entry& entry) { return entry.instance == instance; });
    return found == entries_.end() ? nullptr : &*found;
}

const InventoryControl::Entry* InventoryControl::find(SubjectRef instance) const {
    const auto found = std::find_if(entries_.begin(), entries_.end(),
                                    [instance](const Entry& entry) { return entry.instance == instance; });
    return found == entries_.end() ? nullptr : &*found;
}

std::string_view InventoryControl::gameplayDomain() const noexcept { return "inventory"; }

std::vector<SubjectRef> InventoryControl::gameplayInstances() const { return instances(); }

bool InventoryControl::controls(const GameplaySession& session, const Entry& entry) const {
    return session.access != GameplayAccess::PlayerEquivalent ||
           std::find(session.controlledSubjects.begin(), session.controlledSubjects.end(), entry.owner) !=
               session.controlledSubjects.end();
}

Value InventoryControl::bagState(const Entry& entry) const {
    Bag*         bag = entry.bag;
    Value::Array slots;
    if (bag != nullptr) {
        const int slotCount = bag->getSlotCount();
        for (int slot = 0; slot < slotCount; ++slot) {
            const std::string itemId = bag->getSlotItemId(slot);
            if (itemId.empty()) continue;
            slots.emplace_back(
                Value::Object{{"slot", Value(static_cast<std::int64_t>(slot))},
                              {"itemId", Value(itemId)},
                              {"quantity", Value(static_cast<std::int64_t>(bag->getSlotQuantity(slot)))},
                              {"instanceId", Value(static_cast<std::int64_t>(bag->getSlotInstanceId(slot)))}});
        }
    }
    Value::Object state{{"bagId", Value(bag != nullptr ? bag->getId() : std::string{})},
                        {"kind", Value(bag != nullptr ? bag->getKind() : std::string{})},
                        {"slots", Value(std::move(slots))}};
    if (bag != nullptr) {
        state.emplace("slotCount", Value(static_cast<std::int64_t>(bag->getSlotCount())));
        state.emplace("usedWeight", Value(static_cast<double>(bag->getUsedWeight())));
        state.emplace("maxWeight", Value(static_cast<double>(bag->getMaxWeight())));
        state.emplace("usedVolume", Value(static_cast<double>(bag->getUsedVolume())));
        state.emplace("maxVolume", Value(static_cast<double>(bag->getMaxVolume())));
        state.emplace("capacityPolicy", Value(bag->getCapacityPolicy()));
    }
    state.emplace("equipment", equipmentState(entry));
    return Value(std::move(state));
}

Value InventoryControl::equipmentState(const Entry& entry) const {
    EquipmentSet* equipment = entry.equipment;
    Value::Array  slots;
    if (equipment != nullptr) {
        const int slotCount = equipment->getSlotCount();
        for (int index = 0; index < slotCount; ++index) {
            const std::string slotName = equipment->getSlotName(index);
            if (slotName.empty()) continue;
            Value::Object     slot{{"slot", Value(slotName)}};
            const std::string itemId = equipment->getSlotItemId(slotName);
            if (!itemId.empty()) {
                slot.emplace("itemId", Value(itemId));
                slot.emplace("quantity", Value(static_cast<std::int64_t>(equipment->getSlotQuantity(slotName))));
                slot.emplace("instanceId", Value(static_cast<std::int64_t>(equipment->getSlotInstanceId(slotName))));
            }
            slots.emplace_back(std::move(slot));
        }
    }
    return Value(std::move(slots));
}

Result<GameplayObservation> InventoryControl::observeGameplay(const GameplaySession& session,
                                                              SubjectRef             instance) const {
    const Entry* entry = find(instance);
    if (entry == nullptr)
        return failure<GameplayObservation>(DiagnosticCode::NotFound, "inventory gameplay instance was not found",
                                            "instance");
    if (!controls(session, *entry))
        return failure<GameplayObservation>(DiagnosticCode::PreconditionViolation,
                                            "session does not control the inventory owner", "instance");
    GameplayObservation observation;
    observation.domain   = id("gameplay:inventory");
    observation.instance = entry->instance;
    observation.tick     = entry->tick;
    observation.revision = entry->revision;
    observation.state    = bagState(*entry);
    return Result<GameplayObservation>::success(std::move(observation));
}

Result<std::vector<GameplayActionDescriptor>> InventoryControl::availableGameplayActions(const GameplaySession& session,
                                                                                         SubjectRef instance,
                                                                                         SubjectRef subject) const {
    auto observed = observeGameplay(session, instance);
    if (!observed) return Result<std::vector<GameplayActionDescriptor>>::failure(observed.status());
    const Entry* entry = find(instance);
    if (entry == nullptr)
        return failure<std::vector<GameplayActionDescriptor>>(DiagnosticCode::NotFound,
                                                              "inventory gameplay instance was not found", "instance");
    if (subject != entry->owner)
        return failure<std::vector<GameplayActionDescriptor>>(DiagnosticCode::PreconditionViolation,
                                                              "inventory action subject must be its owner", "subject");

    const Value                           stringType(Value::Object{{"type", Value("string")}});
    const Value                           integerType(Value::Object{{"type", Value("integer")}});
    std::vector<GameplayActionDescriptor> actions{
        {id(kRemoveItem), Value(Value::Object{{"itemId", stringType}, {"quantity", integerType}})},
        {id(kMoveSlot),
         Value(Value::Object{{"fromSlot", integerType}, {"toSlot", integerType}, {"quantity", integerType}})},
    };
    // A set without slots carries no equipment authority, so discovery stays
    // truthful: everything advertised here can actually succeed.
    if (entry->equipment != nullptr && entry->equipment->getSlotCount() > 0) {
        actions.push_back({id(kEquip), Value(Value::Object{{"bagSlot", integerType}, {"equipSlot", stringType}})});
        actions.push_back({id(kUnequip), Value(Value::Object{{"equipSlot", stringType}})});
    }
    // Granting items is not a player action; it is advertized only to the
    // profiles that are allowed to use it, so discovery never offers an action
    // the same session would be refused.
    if (session.access != GameplayAccess::PlayerEquivalent) {
        actions.push_back({id(kAddItem), Value(Value::Object{{"itemId", stringType}, {"quantity", integerType}})});
    }
    return Result<std::vector<GameplayActionDescriptor>>::success(std::move(actions));
}

Result<GameplayCommandReceipt> InventoryControl::submitGameplay(const GameplaySession& session, SubjectRef instance,
                                                                const GameplayCommand& command) {
    Entry* entry = find(instance);
    if (entry == nullptr)
        return failure<GameplayCommandReceipt>(DiagnosticCode::NotFound, "inventory gameplay instance was not found",
                                               "instance");
    if (!controls(session, *entry) || command.subject != entry->owner)
        return failure<GameplayCommandReceipt>(DiagnosticCode::PreconditionViolation,
                                               "session does not control the inventory owner", "command.subject");
    if (command.id.empty())
        return failure<GameplayCommandReceipt>(DiagnosticCode::InvalidArgument, "command id must not be empty",
                                               "command.id");
    if (command.observedTick != entry->tick || command.expectedRevision != entry->revision)
        return failure<GameplayCommandReceipt>(
            DiagnosticCode::Conflict, "inventory command was based on a stale observation", "command.expectedRevision");
    auto values = object(command.parameters);
    if (!values) return Result<GameplayCommandReceipt>::failure(values.status());
    const Value::Object& parameters = *values.value();

    Bag*          bag       = entry->bag;
    EquipmentSet* equipment = entry->equipment;
    if (bag == nullptr)
        return failure<GameplayCommandReceipt>(DiagnosticCode::Failed, "inventory instance lost its container",
                                               "instance");

    const std::string action = command.action.format();
    std::string       detail;
    std::string       eventType;
    // How much the authority actually moved. `Bag` removal is "take up to N",
    // so an agent that asks for more than the container holds gets a success
    // with the applied amount disclosed here rather than a silent full grant.
    std::int64_t applied = 0;
    if (action == kAddItem) {
        if (session.access == GameplayAccess::PlayerEquivalent)
            return failure<GameplayCommandReceipt>(DiagnosticCode::PreconditionViolation,
                                                   "adding items requires the test-driver or developer-cheat profile",
                                                   "session.access");
        auto itemId   = text(parameters, "itemId");
        auto quantity = integer(parameters, "quantity", 1);
        if (!itemId) return Result<GameplayCommandReceipt>::failure(itemId.status());
        if (!quantity) return Result<GameplayCommandReceipt>::failure(quantity.status());
        if (itemId.value().empty() || quantity.value() <= 0)
            return failure<GameplayCommandReceipt>(DiagnosticCode::InvalidArgument,
                                                   "add-item needs a non-empty itemId and a positive quantity",
                                                   "command.parameters");
        if (!bag->canAddItem(itemId.value(), quantity.value()))
            return failure<GameplayCommandReceipt>(DiagnosticCode::PreconditionViolation,
                                                   bag->canAddItemReason(itemId.value(), quantity.value()),
                                                   "command.parameters.itemId");
        const int added = bag->addItem(itemId.value(), quantity.value());
        if (added <= 0)
            return failure<GameplayCommandReceipt>(DiagnosticCode::Failed, "bag rejected the granted items",
                                                   "command.parameters");
        detail    = "added " + std::to_string(added) + " " + itemId.value();
        eventType = "inventory.item-added";
        applied   = added;
    } else if (action == kRemoveItem) {
        auto itemId   = text(parameters, "itemId");
        auto quantity = integer(parameters, "quantity", 1);
        if (!itemId) return Result<GameplayCommandReceipt>::failure(itemId.status());
        if (!quantity) return Result<GameplayCommandReceipt>::failure(quantity.status());
        if (itemId.value().empty() || quantity.value() <= 0)
            return failure<GameplayCommandReceipt>(DiagnosticCode::InvalidArgument,
                                                   "remove-item needs a non-empty itemId and a positive quantity",
                                                   "command.parameters");
        const int removed = bag->removeItem(itemId.value(), quantity.value());
        if (removed <= 0)
            return failure<GameplayCommandReceipt>(DiagnosticCode::PreconditionViolation, "bag does not hold that item",
                                                   "command.parameters.itemId");
        detail    = "removed " + std::to_string(removed) + " " + itemId.value();
        eventType = "inventory.item-removed";
        applied   = removed;
    } else if (action == kMoveSlot) {
        auto fromSlot = integer(parameters, "fromSlot", -1);
        auto toSlot   = integer(parameters, "toSlot", -1);
        auto quantity = integer(parameters, "quantity", 1);
        if (!fromSlot) return Result<GameplayCommandReceipt>::failure(fromSlot.status());
        if (!toSlot) return Result<GameplayCommandReceipt>::failure(toSlot.status());
        if (!quantity) return Result<GameplayCommandReceipt>::failure(quantity.status());
        if (fromSlot.value() < 0 || toSlot.value() < 0 || quantity.value() <= 0)
            return failure<GameplayCommandReceipt>(DiagnosticCode::InvalidArgument,
                                                   "move-slot needs non-negative slots and a positive quantity",
                                                   "command.parameters");
        if (!bag->splitStack(fromSlot.value(), quantity.value(), toSlot.value()))
            return failure<GameplayCommandReceipt>(DiagnosticCode::PreconditionViolation,
                                                   "bag could not move that stack", "command.parameters.fromSlot");
        detail    = "moved " + std::to_string(quantity.value()) + " from slot " + std::to_string(fromSlot.value());
        eventType = "inventory.slot-moved";
        applied   = quantity.value();
    } else if (action == kEquip) {
        if (equipment == nullptr)
            return failure<GameplayCommandReceipt>(DiagnosticCode::Unsupported,
                                                   "this inventory instance has no equipment set", "command.action");
        auto bagSlot   = integer(parameters, "bagSlot", -1);
        auto equipSlot = text(parameters, "equipSlot");
        if (!bagSlot) return Result<GameplayCommandReceipt>::failure(bagSlot.status());
        if (!equipSlot) return Result<GameplayCommandReceipt>::failure(equipSlot.status());
        std::string reason;
        if (bagSlot.value() < 0 || equipSlot.value().empty() ||
            !equipment->canEquipFromBag(bag, bagSlot.value(), equipSlot.value(), &reason))
            return failure<GameplayCommandReceipt>(DiagnosticCode::PreconditionViolation,
                                                   reason.empty() ? "item cannot be equipped into that slot" : reason,
                                                   "command.parameters.equipSlot");
        if (!equipment->equipFromBag(equipSlot.value(), bag, bagSlot.value()))
            return failure<GameplayCommandReceipt>(DiagnosticCode::Failed, "equip was rejected by the equipment set",
                                                   "command.parameters.equipSlot");
        detail    = "equipped into " + equipSlot.value();
        eventType = "inventory.equipped";
        applied   = equipment->getSlotQuantity(equipSlot.value());
    } else if (action == kUnequip) {
        if (equipment == nullptr)
            return failure<GameplayCommandReceipt>(DiagnosticCode::Unsupported,
                                                   "this inventory instance has no equipment set", "command.action");
        auto equipSlot = text(parameters, "equipSlot");
        if (!equipSlot) return Result<GameplayCommandReceipt>::failure(equipSlot.status());
        if (equipSlot.value().empty() || equipment->isSlotEmpty(equipSlot.value()))
            return failure<GameplayCommandReceipt>(DiagnosticCode::PreconditionViolation, "equipment slot is empty",
                                                   "command.parameters.equipSlot");
        const int carried = equipment->getSlotQuantity(equipSlot.value());
        if (!equipment->unequipToBag(equipSlot.value(), bag))
            return failure<GameplayCommandReceipt>(DiagnosticCode::Failed, "unequip was rejected by the equipment set",
                                                   "command.parameters.equipSlot");
        detail    = "unequipped " + equipSlot.value();
        eventType = "inventory.unequipped";
        applied   = carried;
    } else {
        return failure<GameplayCommandReceipt>(DiagnosticCode::Unsupported, "unsupported inventory action",
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

Result<GameplayObservation> InventoryControl::advanceGameplay(const GameplaySession& session, SubjectRef instance,
                                                              const SimulationStep& step) {
    Entry* entry = find(instance);
    if (entry == nullptr)
        return failure<GameplayObservation>(DiagnosticCode::NotFound, "inventory gameplay instance was not found",
                                            "instance");
    if (step.tick <= entry->tick)
        return failure<GameplayObservation>(DiagnosticCode::Conflict, "inventory simulation tick must increase",
                                            "step.tick");
    auto observed = observeGameplay(session, instance);
    if (!observed) return Result<GameplayObservation>::failure(observed.status());
    entry->tick = step.tick;
    return observeGameplay(session, instance);
}

Result<std::vector<GameplayEvent>> InventoryControl::gameplayEvents(const GameplaySession& session, SubjectRef instance,
                                                                    std::uint64_t afterSequence) const {
    auto observed = observeGameplay(session, instance);
    if (!observed) return Result<std::vector<GameplayEvent>>::failure(observed.status());
    const Entry* entry = find(instance);
    if (entry == nullptr)
        return failure<std::vector<GameplayEvent>>(DiagnosticCode::NotFound,
                                                   "inventory gameplay instance was not found", "instance");
    std::vector<GameplayEvent> result;
    for (const auto& event : entry->events)
        if (event.sequence > afterSequence) result.push_back(event);
    const bool empty = result.empty();
    return Result<std::vector<GameplayEvent>>::success(std::move(result),
                                                       Status::success(empty ? StatusCode::NoOp : StatusCode::Applied));
}

}  // namespace eve::inventory
