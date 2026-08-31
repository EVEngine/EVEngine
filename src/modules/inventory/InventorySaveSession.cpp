#include "inventory/InventorySaveSession.h"

#include "common/Value.h"
#include "inventory/Bag.h"
#include "inventory/Equipment.h"
#include "inventory/InventorySystem.h"
#include "inventory/Item.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_set>

namespace eve::inventory {

namespace {

template <typename T>
eve::Result<T> inventoryFailure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(code, std::move(message), std::move(path)));
}

double number(const eve::Value &value) {
    return value.isInt64() ? static_cast<double>(value.asInt()) : value.asDouble();
}

eve::Value encodeStrings(const std::vector<std::string> &values) {
    eve::Value::Array encoded;
    encoded.reserve(values.size());
    for (const auto &value : values) encoded.emplace_back(value);
    return eve::Value(std::move(encoded));
}

eve::Value encodeMap(const std::unordered_map<std::string, std::string> &values) {
    eve::Value::Object encoded;
    for (const auto &[key, value] : values) encoded.emplace(key, eve::Value(value));
    return eve::Value(std::move(encoded));
}

eve::Value encodeStack(const ItemStack &stack) {
    eve::Value::Object encoded;
    encoded.emplace("durability", eve::Value(stack.durability));
    encoded.emplace("instanceId", eve::Value(stack.instanceId));
    encoded.emplace("itemId", eve::Value(stack.itemId));
    encoded.emplace("props", encodeMap(stack.props));
    encoded.emplace("quantity", eve::Value(stack.quantity));
    encoded.emplace("tags", encodeStrings(stack.tags));
    return eve::Value(std::move(encoded));
}

eve::Result<std::vector<std::string>> decodeStrings(const eve::Value *encoded, const std::string &path) {
    if (!encoded || !encoded->isArray())
        return inventoryFailure<std::vector<std::string>>(eve::DiagnosticCode::ParseError,
                                                          "expected an array of strings", path);
    std::vector<std::string> result;
    std::unordered_set<std::string> seen;
    result.reserve(encoded->arraySize());
    for (std::size_t i = 0; i < encoded->arraySize(); ++i) {
        const auto &entry = encoded->at(i);
        if (!entry.isString() || entry.asString().empty() || !seen.emplace(entry.asString()).second)
            return inventoryFailure<std::vector<std::string>>(eve::DiagnosticCode::InvalidArgument,
                                                              "tag values must be non-empty and unique", path);
        result.push_back(entry.asString());
    }
    return eve::Result<std::vector<std::string>>::success(std::move(result));
}

eve::Result<std::unordered_map<std::string, std::string>> decodeMap(const eve::Value *encoded,
                                                                    const std::string &path) {
    if (!encoded || !encoded->isObject())
        return inventoryFailure<std::unordered_map<std::string, std::string>>(
            eve::DiagnosticCode::ParseError, "expected a string map", path);
    std::unordered_map<std::string, std::string> result;
    for (const auto &key : encoded->keys()) {
        const eve::Value *value = encoded->find(key);
        if (key.empty() || !value || !value->isString())
            return inventoryFailure<std::unordered_map<std::string, std::string>>(
                eve::DiagnosticCode::InvalidArgument, "map keys and values must be strings", path);
        result.emplace(key, value->asString());
    }
    return eve::Result<std::unordered_map<std::string, std::string>>::success(std::move(result));
}

eve::Result<ItemStack> decodeStack(const eve::Value &encoded, const std::string &path, int &largestId,
                                   std::unordered_set<int> &instanceIds) {
    if (!encoded.isObject())
        return inventoryFailure<ItemStack>(eve::DiagnosticCode::ParseError, "item stack must be an object", path);
    const eve::Value *instanceId = encoded.find("instanceId");
    const eve::Value *itemId = encoded.find("itemId");
    const eve::Value *quantity = encoded.find("quantity");
    const eve::Value *durability = encoded.find("durability");
    if (!instanceId || !instanceId->isInt64() || !itemId || !itemId->isString() || !quantity ||
        !quantity->isInt64() || !durability || !durability->isNumeric() || !std::isfinite(number(*durability)))
        return inventoryFailure<ItemStack>(eve::DiagnosticCode::ParseError, "item stack fields are invalid", path);

    auto props = decodeMap(encoded.find("props"), path + ".props");
    if (!props.ok()) return eve::Result<ItemStack>::failure(props.status());
    auto tags = decodeStrings(encoded.find("tags"), path + ".tags");
    if (!tags.ok()) return eve::Result<ItemStack>::failure(tags.status());

    ItemStack stack;
    stack.instanceId = static_cast<int>(instanceId->asInt());
    stack.itemId = itemId->asString();
    stack.quantity = static_cast<int>(quantity->asInt());
    stack.durability = static_cast<float>(number(*durability));
    stack.props = std::move(props).takeValue();
    stack.tags = std::move(tags).takeValue();
    if (stack.itemId.empty() || stack.quantity <= 0) {
        if (stack.instanceId != 0 || !stack.itemId.empty() || stack.quantity != 0 || stack.durability != -1.f ||
            !stack.props.empty() || !stack.tags.empty())
            return inventoryFailure<ItemStack>(eve::DiagnosticCode::InvalidArgument,
                                               "empty item stack must use canonical empty values", path);
        return eve::Result<ItemStack>::success(std::move(stack));
    }

    const ItemDefinition *definition = ItemRegistry::find(stack.itemId);
    if (!definition)
        return inventoryFailure<ItemStack>(eve::DiagnosticCode::NotFound,
                                           "item definition referenced by snapshot is not registered", path + ".itemId");
    if (stack.instanceId <= 0 || stack.quantity > definition->maxStack || !instanceIds.emplace(stack.instanceId).second)
        return inventoryFailure<ItemStack>(eve::DiagnosticCode::Conflict,
                                           "item identity or stack quantity is invalid", path);
    largestId = std::max(largestId, stack.instanceId);
    return eve::Result<ItemStack>::success(std::move(stack));
}

bool stackMatchesEquipmentSlot(const ItemStack &stack, const std::string &slot,
                               const std::vector<std::string> &allowedTags) {
    if (stack.empty()) return true;
    const ItemDefinition *definition = ItemRegistry::find(stack.itemId);
    if (!definition || (!definition->equipSlot.empty() && definition->equipSlot != slot)) return false;
    if (allowedTags.empty()) return true;
    for (const auto &tag : allowedTags) {
        if (definition->hasTag(tag) || stack.hasTag(tag)) return true;
    }
    return false;
}

}  // namespace

InventorySaveSession::PreparedRestore::PreparedRestore() = default;
InventorySaveSession::PreparedRestore::~PreparedRestore() = default;
InventorySaveSession::PreparedRestore::PreparedRestore(PreparedRestore &&) noexcept = default;
InventorySaveSession::PreparedRestore &InventorySaveSession::PreparedRestore::operator=(PreparedRestore &&) noexcept =
    default;

void InventorySaveSession::bind(Bag &bag, EquipmentSet &equipment) noexcept {
    bag_ = &bag;
    equipment_ = &equipment;
}

eve::Result<std::string> InventorySaveSession::snapshotJson() const {
    if (!bag_ || !equipment_)
        return inventoryFailure<std::string>(eve::DiagnosticCode::PreconditionViolation,
                                             "inventory save session requires bound participants");
    eve::Value::Array slots;
    slots.reserve(bag_->slots_.size());
    for (const auto &stack : bag_->slots_) slots.emplace_back(encodeStack(stack));
    eve::Value::Object bag;
    bag.emplace("acceptRule", eve::Value(bag_->acceptRule_));
    bag.emplace("acceptTags", encodeStrings(bag_->acceptTags_));
    bag.emplace("capacityPolicy", eve::Value(bag_->capacityPolicy_));
    bag.emplace("extra", encodeMap(bag_->extra_));
    bag.emplace("id", eve::Value(bag_->id_));
    bag.emplace("kind", eve::Value(bag_->kind_));
    bag.emplace("maxVolume", eve::Value(bag_->maxVolume_));
    bag.emplace("maxWeight", eve::Value(bag_->maxWeight_));
    bag.emplace("rejectTags", encodeStrings(bag_->rejectTags_));
    bag.emplace("slots", eve::Value(std::move(slots)));
    bag.emplace("stackRule", eve::Value(bag_->stackRule_));

    eve::Value::Array equipmentSlots;
    equipmentSlots.reserve(equipment_->order_.size());
    for (const auto &name : equipment_->order_) {
        const auto &slot = equipment_->slots_.at(name);
        eve::Value::Object encoded;
        encoded.emplace("allowedTags", encodeStrings(slot.allowedTags));
        encoded.emplace("name", eve::Value(name));
        encoded.emplace("stack", encodeStack(slot.stack));
        equipmentSlots.emplace_back(std::move(encoded));
    }
    eve::Value::Object equipment;
    equipment.emplace("id", eve::Value(equipment_->id_));
    equipment.emplace("slots", eve::Value(std::move(equipmentSlots)));

    eve::Value::Object root;
    root.emplace("bag", eve::Value(std::move(bag)));
    root.emplace("equipment", eve::Value(std::move(equipment)));
    root.emplace("schema", eve::Value("eve.inventory.save-session"));
    root.emplace("version", eve::Value(1));
    auto encoded = eve::Value(std::move(root)).toJson();
    if (!encoded.ok()) return encoded;
    auto validated = prepareRestoreSnapshotJson(encoded.value());
    if (!validated.ok()) return eve::Result<std::string>::failure(validated.status());
    return encoded;
}

eve::Result<InventorySaveSession::PreparedRestore> InventorySaveSession::prepareRestoreSnapshotJson(
    std::string_view json) const {
    if (!bag_ || !equipment_)
        return inventoryFailure<PreparedRestore>(eve::DiagnosticCode::PreconditionViolation,
                                                 "inventory save session requires bound participants");
    InventorySystem::ensureBuiltins();
    auto parsed = eve::Value::fromJson(json);
    if (!parsed.ok()) return eve::Result<PreparedRestore>::failure(parsed.status());
    const eve::Value &root = parsed.value();
    const eve::Value *schema = root.isObject() ? root.find("schema") : nullptr;
    const eve::Value *version = root.isObject() ? root.find("version") : nullptr;
    if (!schema || !schema->isString() || schema->asString() != "eve.inventory.save-session")
        return inventoryFailure<PreparedRestore>(eve::DiagnosticCode::InvalidArgument,
                                                 "snapshot does not belong to InventorySaveSession", "$.schema");
    if (!version || !version->isInt64() || version->asInt() != 1)
        return inventoryFailure<PreparedRestore>(eve::DiagnosticCode::UnknownVersion,
                                                 "unsupported inventory snapshot version", "$.version");
    const eve::Value *bag = root.find("bag");
    const eve::Value *equipment = root.find("equipment");
    if (!bag || !bag->isObject() || !equipment || !equipment->isObject())
        return inventoryFailure<PreparedRestore>(eve::DiagnosticCode::ParseError,
                                                 "inventory participants must be objects", "$");

    PreparedRestore candidate;
    const eve::Value *bagId = bag->find("id");
    const eve::Value *kind = bag->find("kind");
    const eve::Value *maxWeight = bag->find("maxWeight");
    const eve::Value *maxVolume = bag->find("maxVolume");
    const eve::Value *acceptRule = bag->find("acceptRule");
    const eve::Value *capacityPolicy = bag->find("capacityPolicy");
    const eve::Value *stackRule = bag->find("stackRule");
    const eve::Value *slots = bag->find("slots");
    if (!bagId || !bagId->isString() || !kind || !kind->isString() || kind->asString().empty() ||
        !maxWeight || !maxWeight->isNumeric() || !maxVolume || !maxVolume->isNumeric() ||
        !std::isfinite(number(*maxWeight)) || number(*maxWeight) < 0.0 || !std::isfinite(number(*maxVolume)) ||
        number(*maxVolume) < 0.0 || !acceptRule || !acceptRule->isString() || !capacityPolicy ||
        !capacityPolicy->isString() || !stackRule || !stackRule->isString() || !slots || !slots->isArray())
        return inventoryFailure<PreparedRestore>(eve::DiagnosticCode::ParseError,
                                                 "bag configuration is invalid", "$.bag");
    if (!InventorySystem::hasAcceptRule(acceptRule->asString()) ||
        !InventorySystem::hasCapacityPolicy(capacityPolicy->asString()) ||
        !InventorySystem::hasStackRule(stackRule->asString()))
        return inventoryFailure<PreparedRestore>(eve::DiagnosticCode::NotFound,
                                                 "bag snapshot references an unregistered policy", "$.bag");
    auto preparedBag = std::make_unique<Bag>(static_cast<int>(slots->arraySize()));
    preparedBag->id_ = bagId->asString();
    preparedBag->kind_ = kind->asString();
    preparedBag->maxWeight_ = static_cast<float>(number(*maxWeight));
    preparedBag->maxVolume_ = static_cast<float>(number(*maxVolume));
    preparedBag->acceptRule_ = acceptRule->asString();
    preparedBag->capacityPolicy_ = capacityPolicy->asString();
    preparedBag->stackRule_ = stackRule->asString();
    auto acceptTags = decodeStrings(bag->find("acceptTags"), "$.bag.acceptTags");
    if (!acceptTags.ok()) return eve::Result<PreparedRestore>::failure(acceptTags.status());
    auto rejectTags = decodeStrings(bag->find("rejectTags"), "$.bag.rejectTags");
    if (!rejectTags.ok()) return eve::Result<PreparedRestore>::failure(rejectTags.status());
    auto extra = decodeMap(bag->find("extra"), "$.bag.extra");
    if (!extra.ok()) return eve::Result<PreparedRestore>::failure(extra.status());
    preparedBag->acceptTags_ = std::move(acceptTags).takeValue();
    preparedBag->rejectTags_ = std::move(rejectTags).takeValue();
    preparedBag->extra_ = std::move(extra).takeValue();

    std::unordered_set<int> instanceIds;
    for (std::size_t index = 0; index < slots->arraySize(); ++index) {
        auto stack = decodeStack(slots->at(index), "$.bag.slots[" + std::to_string(index) + "]",
                                 candidate.largestInstanceId, instanceIds);
        if (!stack.ok()) return eve::Result<PreparedRestore>::failure(stack.status());
        preparedBag->slots_[index] = std::move(stack).takeValue();
    }

    const eve::Value *equipmentId = equipment->find("id");
    const eve::Value *equipmentSlots = equipment->find("slots");
    if (!equipmentId || !equipmentId->isString() || !equipmentSlots || !equipmentSlots->isArray())
        return inventoryFailure<PreparedRestore>(eve::DiagnosticCode::ParseError,
                                                 "equipment configuration is invalid", "$.equipment");
    auto preparedEquipment = std::make_unique<EquipmentSet>();
    preparedEquipment->id_ = equipmentId->asString();
    std::unordered_set<std::string> slotNames;
    for (std::size_t index = 0; index < equipmentSlots->arraySize(); ++index) {
        const eve::Value &encoded = equipmentSlots->at(index);
        const eve::Value *name = encoded.isObject() ? encoded.find("name") : nullptr;
        const eve::Value *stackValue = encoded.isObject() ? encoded.find("stack") : nullptr;
        if (!name || !name->isString() || name->asString().empty() ||
            !slotNames.emplace(name->asString()).second || !stackValue)
            return inventoryFailure<PreparedRestore>(eve::DiagnosticCode::InvalidArgument,
                                                     "equipment slot identity is invalid", "$.equipment.slots");
        auto allowed = decodeStrings(encoded.find("allowedTags"), "$.equipment.slots.allowedTags");
        if (!allowed.ok()) return eve::Result<PreparedRestore>::failure(allowed.status());
        auto stack = decodeStack(*stackValue, "$.equipment.slots[" + std::to_string(index) + "].stack",
                                 candidate.largestInstanceId, instanceIds);
        if (!stack.ok()) return eve::Result<PreparedRestore>::failure(stack.status());
        const std::string slotName = name->asString();
        auto allowedTags = std::move(allowed).takeValue();
        auto stackState = std::move(stack).takeValue();
        if (!stackMatchesEquipmentSlot(stackState, slotName, allowedTags))
            return inventoryFailure<PreparedRestore>(eve::DiagnosticCode::Conflict,
                                                     "equipped item violates its slot contract", "$.equipment.slots");
        preparedEquipment->defineSlot(slotName);
        preparedEquipment->slots_.at(slotName).allowedTags = std::move(allowedTags);
        preparedEquipment->slots_.at(slotName).stack = std::move(stackState);
    }
    candidate.bag_ = std::move(preparedBag);
    candidate.equipment_ = std::move(preparedEquipment);
    return eve::Result<PreparedRestore>::success(std::move(candidate));
}

void InventorySaveSession::commitPrepared(PreparedRestore prepared) noexcept {
    bag_->id_.swap(prepared.bag_->id_);
    bag_->kind_.swap(prepared.bag_->kind_);
    std::swap(bag_->maxWeight_, prepared.bag_->maxWeight_);
    std::swap(bag_->maxVolume_, prepared.bag_->maxVolume_);
    bag_->acceptRule_.swap(prepared.bag_->acceptRule_);
    bag_->capacityPolicy_.swap(prepared.bag_->capacityPolicy_);
    bag_->stackRule_.swap(prepared.bag_->stackRule_);
    bag_->acceptTags_.swap(prepared.bag_->acceptTags_);
    bag_->rejectTags_.swap(prepared.bag_->rejectTags_);
    bag_->slots_.swap(prepared.bag_->slots_);
    bag_->extra_.swap(prepared.bag_->extra_);
    equipment_->id_.swap(prepared.equipment_->id_);
    equipment_->slots_.swap(prepared.equipment_->slots_);
    equipment_->order_.swap(prepared.equipment_->order_);
    InventorySystem::ensureNextInstanceIdAbove(prepared.largestInstanceId);
}

eve::Result<void> InventorySaveSession::restoreSnapshotJson(std::string_view json) {
    auto prepared = prepareRestoreSnapshotJson(json);
    if (!prepared.ok()) return eve::Result<void>::failure(prepared.status());
    commitPrepared(std::move(prepared).takeValue());
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace eve::inventory
