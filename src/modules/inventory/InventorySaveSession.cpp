#include "inventory/InventorySaveSession.h"

#include "inventory/Bag.h"
#include "inventory/Equipment.h"
#include "inventory/InventorySystem.h"
#include "inventory/Item.h"
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4267)  // Generated protobuf accessors narrow size_t to int.
#endif
#include "inventory_save.pb.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <google/protobuf/message.h>
#include <google/protobuf/repeated_ptr_field.h>
#include <google/protobuf/unknown_field_set.h>
#include <google/protobuf/util/json_util.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <unordered_set>
#include <utility>

namespace eve::inventory {

namespace {

constexpr std::string_view kInventorySchema  = "eve.inventory.save-session";
constexpr std::uint32_t    kInventoryVersion = 1;

template <typename T>
eve::Result<T> fail(eve::DiagnosticCode code, std::string message, std::string path) {
    return eve::Result<T>::failure(eve::Diagnostic::error(code, std::move(message), std::move(path)));
}

void encodeStack(const ItemStack &source, proto::ItemStack *destination) {
    destination->set_instance_id(source.instanceId);
    destination->set_item_id(source.itemId);
    destination->set_quantity(source.quantity);
    destination->set_durability(source.durability);
    for (const auto &[key, value] : source.props) (*destination->mutable_props())[key] = value;
    for (const auto &tag : source.tags) destination->add_tags(tag);
}

eve::Result<std::vector<std::string>> decodeTags(const google::protobuf::RepeatedPtrField<std::string> &encoded,
                                                 const std::string                                     &path) {
    std::vector<std::string> result;
    std::unordered_set<std::string> seen;
    result.reserve(static_cast<std::size_t>(encoded.size()));
    for (const auto &tag : encoded) {
        if (tag.empty() || !seen.emplace(tag).second)
            return fail<std::vector<std::string>>(eve::DiagnosticCode::InvalidArgument,
                                                  "tag values must be non-empty and unique", path);
        result.push_back(tag);
    }
    return eve::Result<std::vector<std::string>>::success(std::move(result));
}

eve::Result<ItemStack> decodeStack(const proto::ItemStack &encoded, const std::string &path, int &largestId,
                                   std::unordered_set<int> &instanceIds) {
    if (!encoded.has_instance_id() || !encoded.has_item_id() || !encoded.has_quantity() || !encoded.has_durability() ||
        !std::isfinite(encoded.durability()))
        return fail<ItemStack>(eve::DiagnosticCode::ParseError, "item stack fields are invalid", path);
    ItemStack stack;
    stack.instanceId = encoded.instance_id();
    stack.itemId     = encoded.item_id();
    stack.quantity   = encoded.quantity();
    stack.durability = encoded.durability();
    for (const auto &[key, value] : encoded.props()) {
        if (key.empty())
            return fail<ItemStack>(eve::DiagnosticCode::InvalidArgument, "item property key is empty", path);
        stack.props.emplace(key, value);
    }
    auto tags = decodeTags(encoded.tags(), path + ".tags");
    if (!tags.ok()) return eve::Result<ItemStack>::failure(tags.status());
    stack.tags = std::move(tags).takeValue();
    if (stack.itemId.empty() || stack.quantity <= 0) {
        if (stack.instanceId != 0 || !stack.itemId.empty() || stack.quantity != 0 || stack.durability != -1.f ||
            !stack.props.empty() || !stack.tags.empty())
            return fail<ItemStack>(eve::DiagnosticCode::InvalidArgument,
                                   "empty item stack must use canonical empty values", path);
        return eve::Result<ItemStack>::success(std::move(stack));
    }
    const ItemDefinition *definition = ItemRegistry::find(stack.itemId);
    if (!definition)
        return fail<ItemStack>(eve::DiagnosticCode::NotFound,
                               "item definition referenced by snapshot is not registered", path + ".itemId");
    if (stack.instanceId <= 0 || stack.quantity > definition->maxStack || !instanceIds.emplace(stack.instanceId).second)
        return fail<ItemStack>(eve::DiagnosticCode::Conflict, "item identity or stack quantity is invalid", path);
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

bool hasUnknownFields(const google::protobuf::Message &message) {
    const auto *reflection = message.GetReflection();
    if (!reflection->GetUnknownFields(message).empty()) return true;
    std::vector<const google::protobuf::FieldDescriptor *> fields;
    reflection->ListFields(message, &fields);
    for (const auto *field : fields) {
        if (field->cpp_type() != google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE) continue;
        if (field->is_repeated()) {
            for (int index = 0; index < reflection->FieldSize(message, field); ++index) {
                if (hasUnknownFields(reflection->GetRepeatedMessage(message, field, index))) return true;
            }
        } else if (hasUnknownFields(reflection->GetMessage(message, field))) {
            return true;
        }
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

eve::Result<std::string> InventorySaveSession::snapshotBinary() const {
    if (!bag_ || !equipment_)
        return fail<std::string>(eve::DiagnosticCode::PreconditionViolation,
                                 "inventory save session requires bound participants", {});
    proto::InventorySave snapshot;
    snapshot.set_schema(std::string(kInventorySchema));
    snapshot.set_version(kInventoryVersion);
    auto *bag = snapshot.mutable_bag();
    bag->set_id(bag_->id_);
    bag->set_kind(bag_->kind_);
    bag->set_max_weight(bag_->maxWeight_);
    bag->set_max_volume(bag_->maxVolume_);
    bag->set_accept_rule(bag_->acceptRule_);
    bag->set_capacity_policy(bag_->capacityPolicy_);
    bag->set_stack_rule(bag_->stackRule_);
    for (const auto &tag : bag_->acceptTags_) bag->add_accept_tags(tag);
    for (const auto &tag : bag_->rejectTags_) bag->add_reject_tags(tag);
    for (const auto &[key, value] : bag_->extra_) (*bag->mutable_extra())[key] = value;
    for (const auto &stack : bag_->slots_) encodeStack(stack, bag->add_slots());
    auto *equipment = snapshot.mutable_equipment();
    equipment->set_id(equipment_->id_);
    for (const auto &name : equipment_->order_) {
        const auto &slot = equipment_->slots_.at(name);
        auto       *encoded = equipment->add_slots();
        encoded->set_name(name);
        for (const auto &tag : slot.allowedTags) encoded->add_allowed_tags(tag);
        encodeStack(slot.stack, encoded->mutable_stack());
    }
    std::string bytes;
    if (!snapshot.SerializeToString(&bytes))
        return fail<std::string>(eve::DiagnosticCode::SerializationError, "inventory protobuf serialization failed",
                                 {});
    auto checked = prepareRestoreSnapshotBinary(bytes);
    if (!checked.ok()) return eve::Result<std::string>::failure(checked.status());
    return eve::Result<std::string>::success(std::move(bytes));
}

eve::Result<std::string> InventorySaveSession::rewriteSnapshotBinaryId(std::string_view bytes, InventorySaveIdKind kind,
                                                                       std::string_view oldId, std::string_view newId) {
    if (oldId.empty() || newId.empty() || oldId == newId)
        return fail<std::string>(eve::DiagnosticCode::InvalidArgument,
                                 "inventory migration requires distinct non-empty identities", "identity");
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return fail<std::string>(eve::DiagnosticCode::InvalidArgument, "inventory snapshot is too large", "snapshot");
    proto::InventorySave snapshot;
    if (!snapshot.ParseFromArray(bytes.data(), static_cast<int>(bytes.size())) || hasUnknownFields(snapshot))
        return fail<std::string>(eve::DiagnosticCode::ParseError, "inventory snapshot is malformed", "snapshot");
    if (!snapshot.has_schema() || snapshot.schema() != kInventorySchema)
        return fail<std::string>(eve::DiagnosticCode::InvalidArgument,
                                 "snapshot does not belong to InventorySaveSession", "schema");
    if (!snapshot.has_version() || snapshot.version() != kInventoryVersion)
        return fail<std::string>(eve::DiagnosticCode::UnknownVersion, "unsupported inventory snapshot version",
                                 "version");
    if (!snapshot.has_bag() || !snapshot.has_equipment())
        return fail<std::string>(eve::DiagnosticCode::ParseError, "inventory snapshot requires bag and equipment",
                                 "snapshot");
    if (kind != InventorySaveIdKind::Item && kind != InventorySaveIdKind::EquipmentSlot)
        return fail<std::string>(eve::DiagnosticCode::InvalidArgument, "unsupported inventory identity kind", "kind");
    if (kind == InventorySaveIdKind::Item) {
        for (auto &slot : *snapshot.mutable_bag()->mutable_slots()) {
            if (slot.item_id() == oldId) slot.set_item_id(std::string(newId));
        }
        for (auto &slot : *snapshot.mutable_equipment()->mutable_slots()) {
            if (slot.has_stack() && slot.stack().item_id() == oldId)
                slot.mutable_stack()->set_item_id(std::string(newId));
        }
    } else {
        for (auto &slot : *snapshot.mutable_equipment()->mutable_slots()) {
            if (slot.name() == oldId) slot.set_name(std::string(newId));
        }
    }
    std::string rewritten;
    if (!snapshot.SerializeToString(&rewritten))
        return fail<std::string>(eve::DiagnosticCode::SerializationError, "inventory migration serialization failed",
                                 "snapshot");
    return eve::Result<std::string>::success(std::move(rewritten));
}

eve::Result<std::string> InventorySaveSession::snapshotJson() const {
    auto binary = snapshotBinary();
    if (!binary.ok()) return binary;
    proto::InventorySave snapshot;
    if (!snapshot.ParseFromString(binary.value()))
        return fail<std::string>(eve::DiagnosticCode::ParseError, "inventory protobuf parse failed", {});
    std::string json;
    const auto  status = google::protobuf::util::MessageToJsonString(snapshot, &json);
    if (!status.ok())
        return fail<std::string>(eve::DiagnosticCode::ParseError, "inventory protobuf JSON conversion failed", {});
    return eve::Result<std::string>::success(std::move(json));
}

eve::Result<InventorySaveSession::PreparedRestore> InventorySaveSession::prepareRestoreSnapshotBinary(
    std::string_view bytes) const {
    if (!bag_ || !equipment_)
        return fail<PreparedRestore>(eve::DiagnosticCode::PreconditionViolation,
                                     "inventory save session requires bound participants", {});
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return fail<PreparedRestore>(eve::DiagnosticCode::InvalidArgument, "inventory snapshot is too large", {});
    proto::InventorySave snapshot;
    if (!snapshot.ParseFromArray(bytes.data(), static_cast<int>(bytes.size())))
        return fail<PreparedRestore>(eve::DiagnosticCode::ParseError, "invalid inventory protobuf bytes", {});
    if (hasUnknownFields(snapshot))
        return fail<PreparedRestore>(eve::DiagnosticCode::UnknownVersion,
                                     "inventory snapshot contains unknown protobuf fields", {});
    if (!snapshot.has_schema() || snapshot.schema() != kInventorySchema)
        return fail<PreparedRestore>(eve::DiagnosticCode::InvalidArgument,
                                     "snapshot does not belong to InventorySaveSession", "$.schema");
    if (!snapshot.has_version() || snapshot.version() != kInventoryVersion)
        return fail<PreparedRestore>(eve::DiagnosticCode::UnknownVersion, "unsupported inventory snapshot version",
                                     "$.version");
    if (!snapshot.has_bag() || !snapshot.has_equipment())
        return fail<PreparedRestore>(eve::DiagnosticCode::ParseError, "inventory participants must be present", "$");
    InventorySystem::ensureBuiltins();
    const auto &bag       = snapshot.bag();
    const auto &equipment = snapshot.equipment();
    if (!bag.has_id() || !bag.has_kind() || bag.kind().empty() || !bag.has_max_weight() || !bag.has_max_volume() ||
        !std::isfinite(bag.max_weight()) || bag.max_weight() < 0.f || !std::isfinite(bag.max_volume()) ||
        bag.max_volume() < 0.f || !bag.has_accept_rule() || !bag.has_capacity_policy() || !bag.has_stack_rule())
        return fail<PreparedRestore>(eve::DiagnosticCode::ParseError, "bag configuration is invalid", "$.bag");
    if (!InventorySystem::hasAcceptRule(bag.accept_rule()) ||
        !InventorySystem::hasCapacityPolicy(bag.capacity_policy()) || !InventorySystem::hasStackRule(bag.stack_rule()))
        return fail<PreparedRestore>(eve::DiagnosticCode::NotFound, "bag snapshot references an unregistered policy",
                                     "$.bag");
    PreparedRestore candidate;
    candidate.bag_                  = std::make_unique<Bag>(bag.slots_size());
    candidate.bag_->id_             = bag.id();
    candidate.bag_->kind_           = bag.kind();
    candidate.bag_->maxWeight_      = bag.max_weight();
    candidate.bag_->maxVolume_      = bag.max_volume();
    candidate.bag_->acceptRule_     = bag.accept_rule();
    candidate.bag_->capacityPolicy_ = bag.capacity_policy();
    candidate.bag_->stackRule_      = bag.stack_rule();
    auto acceptTags                 = decodeTags(bag.accept_tags(), "$.bag.acceptTags");
    if (!acceptTags.ok()) return eve::Result<PreparedRestore>::failure(acceptTags.status());
    auto rejectTags = decodeTags(bag.reject_tags(), "$.bag.rejectTags");
    if (!rejectTags.ok()) return eve::Result<PreparedRestore>::failure(rejectTags.status());
    candidate.bag_->acceptTags_ = std::move(acceptTags).takeValue();
    candidate.bag_->rejectTags_ = std::move(rejectTags).takeValue();
    for (const auto &[key, value] : bag.extra()) {
        if (key.empty())
            return fail<PreparedRestore>(eve::DiagnosticCode::InvalidArgument, "bag extra key is empty", "$.bag.extra");
        candidate.bag_->extra_.emplace(key, value);
    }
    std::unordered_set<int> instanceIds;
    for (int index = 0; index < bag.slots_size(); ++index) {
        auto stack = decodeStack(bag.slots(index), "$.bag.slots[" + std::to_string(index) + "]",
                                 candidate.largestInstanceId, instanceIds);
        if (!stack.ok()) return eve::Result<PreparedRestore>::failure(stack.status());
        candidate.bag_->slots_[static_cast<std::size_t>(index)] = std::move(stack).takeValue();
    }
    if (!equipment.has_id())
        return fail<PreparedRestore>(eve::DiagnosticCode::ParseError, "equipment configuration is invalid",
                                     "$.equipment");
    candidate.equipment_      = std::make_unique<EquipmentSet>();
    candidate.equipment_->id_ = equipment.id();
    std::unordered_set<std::string> slotNames;
    for (int index = 0; index < equipment.slots_size(); ++index) {
        const auto &encoded = equipment.slots(index);
        if (!encoded.has_name() || encoded.name().empty() || !slotNames.emplace(encoded.name()).second ||
            !encoded.has_stack())
            return fail<PreparedRestore>(eve::DiagnosticCode::InvalidArgument, "equipment slot identity is invalid",
                                         "$.equipment.slots");
        auto allowed = decodeTags(encoded.allowed_tags(), "$.equipment.slots.allowedTags");
        if (!allowed.ok()) return eve::Result<PreparedRestore>::failure(allowed.status());
        auto stack = decodeStack(encoded.stack(), "$.equipment.slots[" + std::to_string(index) + "].stack",
                                 candidate.largestInstanceId, instanceIds);
        if (!stack.ok()) return eve::Result<PreparedRestore>::failure(stack.status());
        auto allowedTags = std::move(allowed).takeValue();
        auto stackState = std::move(stack).takeValue();
        if (!stackMatchesEquipmentSlot(stackState, encoded.name(), allowedTags))
            return fail<PreparedRestore>(eve::DiagnosticCode::Conflict, "equipped item violates its slot contract",
                                         "$.equipment.slots");
        candidate.equipment_->defineSlot(encoded.name());
        candidate.equipment_->slots_.at(encoded.name()).allowedTags = std::move(allowedTags);
        candidate.equipment_->slots_.at(encoded.name()).stack       = std::move(stackState);
    }
    return eve::Result<PreparedRestore>::success(std::move(candidate));
}

eve::Result<InventorySaveSession::PreparedRestore> InventorySaveSession::prepareRestoreSnapshotJson(
    std::string_view json) const {
    if (!bag_ || !equipment_)
        return fail<PreparedRestore>(eve::DiagnosticCode::PreconditionViolation,
                                     "inventory save session requires bound participants", {});
    proto::InventorySave snapshot;
    const auto           status = google::protobuf::util::JsonStringToMessage(std::string(json), &snapshot);
    if (!status.ok())
        return fail<PreparedRestore>(eve::DiagnosticCode::ParseError, "invalid inventory protobuf JSON", {});
    std::string bytes;
    if (!snapshot.SerializeToString(&bytes))
        return fail<PreparedRestore>(eve::DiagnosticCode::ParseError, "inventory protobuf serialization failed", {});
    return prepareRestoreSnapshotBinary(bytes);
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

eve::Result<void> InventorySaveSession::restoreSnapshotBinary(std::string_view bytes) {
    auto prepared = prepareRestoreSnapshotBinary(bytes);
    if (!prepared.ok()) return eve::Result<void>::failure(prepared.status());
    commitPrepared(std::move(prepared).takeValue());
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> InventorySaveSession::restoreSnapshotJson(std::string_view json) {
    auto prepared = prepareRestoreSnapshotJson(json);
    if (!prepared.ok()) return eve::Result<void>::failure(prepared.status());
    commitPrepared(std::move(prepared).takeValue());
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace eve::inventory
