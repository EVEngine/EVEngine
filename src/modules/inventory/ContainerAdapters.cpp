#include "inventory/ContainerAdapters.h"

#include "inventory/InventorySystem.h"
#include "inventory/Item.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <unordered_set>
#include <utility>

namespace eve::inventory {
namespace {

[[nodiscard]] eve::Result<void> error(eve::DiagnosticCode code, std::string message) {
    return eve::Result<void>::failure(eve::Diagnostic::error(code, std::move(message)));
}

[[nodiscard]] bool hasId(const Bag& bag, const eve::container::MembershipId& id) {
    return std::any_of(bag.slots().begin(), bag.slots().end(), [&](const ItemStack& stack) {
        return !stack.empty() && eve::container::MembershipId("inventory:" + std::to_string(stack.instanceId)) == id;
    });
}

void swapStacks(ItemStack& lhs, ItemStack& rhs) noexcept {
    using std::swap;
    swap(lhs.instanceId, rhs.instanceId);
    lhs.itemId.swap(rhs.itemId);
    swap(lhs.quantity, rhs.quantity);
    swap(lhs.durability, rhs.durability);
    lhs.props.swap(rhs.props);
    lhs.tags.swap(rhs.tags);
}

[[nodiscard]] bool sameLayout(const eve::container::ContainerSnapshot& lhs,
                              const eve::container::ContainerSnapshot& rhs) {
    if (lhs.id != rhs.id || lhs.revision != rhs.revision || lhs.entries.size() != rhs.entries.size()) return false;
    for (std::size_t index = 0; index < lhs.entries.size(); ++index) {
        const auto& left  = lhs.entries[index];
        const auto& right = rhs.entries[index];
        if (left.membership.object != right.membership.object || left.membership.slot != right.membership.slot ||
            left.membership.generation != right.membership.generation || left.object.id != right.object.id ||
            left.object.type != right.object.type || left.object.quantity != right.object.quantity)
            return false;
    }
    return true;
}

}  // namespace

class InventoryContainerAdapter::PreparedState final : public eve::container::IContainer::PreparedState {
public:
    PreparedState(InventoryContainerAdapter& owner, std::vector<ItemStack> staged, eve::Revision revision)
        : owner_(owner), staged_(std::move(staged)), revision_(revision) {}

    void commit() noexcept override {
        if (committed_) return;
        if (owner_.kind_ == InventoryContainerKind::Bag) {
            if (owner_.bag_ == nullptr || owner_.bag_->slots().size() != staged_.size()) std::terminate();
            owner_.bag_->slots().swap(staged_);
        } else {
            if (owner_.equipment_ == nullptr) std::terminate();
            for (std::size_t index = 0; index < staged_.size(); ++index) {
                auto* live = owner_.stackAt(eve::container::SlotIndex(static_cast<std::int32_t>(index)));
                if (live == nullptr) std::terminate();
                swapStacks(*live, staged_[index]);
            }
        }
        owner_.revision_ = revision_;
        committed_       = true;
    }

    void rollback() noexcept override {
        if (!committed_) staged_.clear();
    }

private:
    InventoryContainerAdapter& owner_;
    std::vector<ItemStack>     staged_;
    eve::Revision              revision_;
    bool                       committed_ = false;
};

InventoryContainerAdapter::InventoryContainerAdapter(eve::container::ContainerId id, Bag* bag,
                                                     eve::container::Capacity capacity)
    : descriptor_{std::move(id),
                  bag != nullptr && capacity.isUnlimited()
                      ? eve::container::Capacity::fixed(static_cast<std::size_t>(bag->getSlotCount()))
                      : (bag != nullptr ? capacity : eve::container::Capacity::fixed(0)),
                  eve::container::Ordering::ExplicitSlots, eve::container::Filter{}},
      kind_(InventoryContainerKind::Bag),
      bag_(bag) {}

InventoryContainerAdapter::InventoryContainerAdapter(eve::container::ContainerId id, EquipmentSet* equipment)
    : descriptor_{std::move(id),
                  eve::container::Capacity::fixed(equipment ? static_cast<std::size_t>(equipment->getSlotCount()) : 0u),
                  eve::container::Ordering::ExplicitSlots, eve::container::Filter{}},
      kind_(InventoryContainerKind::Equipment),
      equipment_(equipment) {}

eve::container::MembershipId InventoryContainerAdapter::objectId(const ItemStack& stack) {
    return eve::container::MembershipId("inventory:" + std::to_string(stack.instanceId));
}

eve::container::ContainerObject InventoryContainerAdapter::describe(const ItemStack& stack, std::string equipmentSlot) {
    eve::container::ContainerObject object;
    object.id       = objectId(stack);
    object.type     = stack.itemId;
    object.quantity = stack.quantity > 0 ? static_cast<std::uint32_t>(stack.quantity) : 0u;
    object.tags     = stack.tags;
    if (const auto* definition = ItemRegistry::find(stack.itemId)) {
        for (const auto& tag : definition->tags)
            if (std::find(object.tags.begin(), object.tags.end(), tag) == object.tags.end()) object.tags.push_back(tag);
    }
    auto payload           = std::make_shared<InventoryContainerObject>();
    payload->stack         = stack;
    payload->equipmentSlot = std::move(equipmentSlot);
    object.payload         = std::move(payload);
    return object;
}

const ItemStack* InventoryContainerAdapter::stackAt(eve::container::SlotIndex slot) const {
    if (!slot.isValid()) return nullptr;
    if (kind_ == InventoryContainerKind::Bag) {
        if (bag_ == nullptr || slot.value() >= bag_->getSlotCount()) return nullptr;
        return &bag_->slots()[static_cast<std::size_t>(slot.value())];
    }
    if (equipment_ == nullptr || slot.value() >= equipment_->getSlotCount()) return nullptr;
    const std::string name = equipment_->getSlotName(slot.value());
    return equipment_->stackAt(name);
}

ItemStack* InventoryContainerAdapter::stackAt(eve::container::SlotIndex slot) {
    return const_cast<ItemStack*>(static_cast<const InventoryContainerAdapter*>(this)->stackAt(slot));
}

std::string InventoryContainerAdapter::slotName(eve::container::SlotIndex slot) const {
    if (kind_ != InventoryContainerKind::Equipment || equipment_ == nullptr || !slot.isValid() ||
        slot.value() >= equipment_->getSlotCount())
        return {};
    return equipment_->getSlotName(slot.value());
}

eve::Result<eve::container::ContainerSnapshot> InventoryContainerAdapter::snapshot() const {
    if (!descriptor_.id.isValid() || (kind_ == InventoryContainerKind::Bag ? bag_ == nullptr : equipment_ == nullptr))
        return eve::Result<eve::container::ContainerSnapshot>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "inventory container adapter is not bound"));
    eve::container::ContainerSnapshot                snapshot{descriptor_.id, revision_, {}};
    std::unordered_set<eve::container::MembershipId> ids;
    const int count = kind_ == InventoryContainerKind::Bag ? bag_->getSlotCount() : equipment_->getSlotCount();
    for (int index = 0; index < count; ++index) {
        const auto  slot  = eve::container::SlotIndex(index);
        const auto* stack = stackAt(slot);
        if (stack == nullptr || stack->empty()) continue;
        if (!descriptor_.capacity.isUnlimited() && static_cast<std::size_t>(index) >= descriptor_.capacity.value())
            return eve::Result<eve::container::ContainerSnapshot>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvariantViolation, "inventory membership exceeds configured capacity"));
        if (stack->instanceId <= 0)
            return eve::Result<eve::container::ContainerSnapshot>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvariantViolation, "inventory stack has no instance identity"));
        const auto id = objectId(*stack);
        if (!ids.insert(id).second)
            return eve::Result<eve::container::ContainerSnapshot>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "inventory container contains duplicate instance identity"));
        snapshot.entries.push_back({{id, slot, eve::Generation(1)}, describe(*stack, slotName(slot))});
    }
    if (!descriptor_.capacity.isUnlimited() && snapshot.entries.size() > descriptor_.capacity.value())
        return eve::Result<eve::container::ContainerSnapshot>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvariantViolation, "inventory container exceeds configured capacity"));
    return eve::Result<eve::container::ContainerSnapshot>::success(std::move(snapshot));
}

eve::Result<void> InventoryContainerAdapter::validateObject(const InventoryContainerObject& object) const {
    if (object.stack.empty() || object.stack.instanceId <= 0 || object.stack.itemId.empty())
        return error(eve::DiagnosticCode::InvalidArgument, "inventory transfer stack is incomplete");
    return eve::Result<void>::success();
}

eve::Result<void> InventoryContainerAdapter::validateInsert(
    const eve::container::ContainerObject& object, std::optional<eve::container::SlotIndex> destination,
    std::optional<eve::container::MembershipId> ignoredObject) const {
    const auto* payload = dynamic_cast<const InventoryContainerObject*>(object.payload.get());
    if (payload == nullptr) return error(eve::DiagnosticCode::StaleHandle, "inventory payload is stale");
    auto validObject = validateObject(*payload);
    if (!validObject) return validObject;
    if (object.id != objectId(payload->stack))
        return error(eve::DiagnosticCode::StaleHandle, "inventory membership identity is stale");
    if (object.type != payload->stack.itemId || object.quantity != static_cast<std::uint32_t>(payload->stack.quantity))
        return error(eve::DiagnosticCode::StaleHandle, "inventory object facts are stale");
    const auto* definition = ItemRegistry::find(payload->stack.itemId);
    if (definition == nullptr) return error(eve::DiagnosticCode::NotFound, "inventory item definition was not found");
    if (definition->maxStack <= 0 || payload->stack.quantity > definition->maxStack)
        return error(eve::DiagnosticCode::PreconditionViolation, "inventory stack exceeds item definition maxStack");

    if (kind_ == InventoryContainerKind::Bag) {
        if (bag_ == nullptr) return error(eve::DiagnosticCode::InvalidArgument, "bag adapter is unbound");
        const std::size_t slotCount = static_cast<std::size_t>(bag_->getSlotCount());
        const std::size_t slotLimit =
            descriptor_.capacity.isUnlimited() ? slotCount : std::min(slotCount, descriptor_.capacity.value());
        if (hasId(*bag_, object.id) && (!ignoredObject || *ignoredObject != object.id))
            return error(eve::DiagnosticCode::Conflict, "bag already contains inventory object");
        if (destination) {
            if (!destination->isValid() || static_cast<std::size_t>(destination->value()) >= slotLimit)
                return error(eve::DiagnosticCode::InvalidArgument, "bag destination slot is out of range");
            if (!bag_->slots()[static_cast<std::size_t>(destination->value())].empty() &&
                (!ignoredObject || *ignoredObject != object.id))
                return error(eve::DiagnosticCode::Conflict, "bag destination slot is occupied");
        } else {
            const bool hasEmptySlot =
                std::any_of(bag_->slots().begin(), bag_->slots().begin() + static_cast<std::ptrdiff_t>(slotLimit),
                            [](const ItemStack& stack) { return stack.empty(); });
            if (!hasEmptySlot && (!ignoredObject || *ignoredObject != object.id))
                return error(eve::DiagnosticCode::Conflict, "bag has no empty membership slot");
        }
        if (!ignoredObject || *ignoredObject != object.id) {
            std::string reason;
            if (!InventorySystem::canAdd(bag_, payload->stack.itemId, payload->stack.quantity, &reason))
                return error(eve::DiagnosticCode::PreconditionViolation,
                             reason.empty() ? "bag rejected inventory object" : reason);
        }
        return eve::Result<void>::success();
    }

    if (equipment_ == nullptr) return error(eve::DiagnosticCode::InvalidArgument, "equipment is unbound");
    if (!destination || !destination->isValid() ||
        static_cast<std::size_t>(destination->value()) >= static_cast<std::size_t>(equipment_->getSlotCount()))
        return error(eve::DiagnosticCode::PreconditionViolation,
                     "equipment transfer requires a valid destination slot");
    const std::string target = slotName(*destination);
    if (target.empty()) return error(eve::DiagnosticCode::NotFound, "equipment slot was not found");
    const auto* current = equipment_->stackAt(target);
    if (current != nullptr && !current->empty() && (!ignoredObject || object.id != objectId(*current)))
        return error(eve::DiagnosticCode::Conflict, "equipment destination slot is occupied");
    if (!definition->equipSlot.empty() && definition->equipSlot != target)
        return error(eve::DiagnosticCode::PreconditionViolation, "inventory item does not fit equipment slot");
    const auto* allowed = equipment_->allowedTags(target);
    if (allowed != nullptr && !allowed->empty()) {
        const bool accepted = std::any_of(allowed->begin(), allowed->end(), [&](const std::string& tag) {
            return definition->hasTag(tag) || payload->stack.hasTag(tag);
        });
        if (!accepted)
            return error(eve::DiagnosticCode::PreconditionViolation, "equipment slot rejected inventory tags");
    }
    return eve::Result<void>::success();
}

eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>> InventoryContainerAdapter::prepare(
    const eve::container::ContainerSnapshot& expected, const eve::container::ContainerSnapshot& candidate) {
    const bool bound = kind_ == InventoryContainerKind::Bag ? bag_ != nullptr : equipment_ != nullptr;
    if (!bound || !descriptor_.id.isValid())
        return eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "inventory adapter is not bound"));
    if (expected.id != descriptor_.id || candidate.id != descriptor_.id)
        return eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "inventory prepare targets another container"));
    auto currentResult = snapshot();
    if (!currentResult)
        return eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>>::failure(currentResult.status());
    if (!sameLayout(currentResult.value(), expected))
        return eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "inventory prepare expected snapshot is stale"));
    const auto nextRevision = expected.revision.incremented();
    if (!nextRevision || candidate.revision != *nextRevision)
        return eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvariantViolation, "inventory candidate revision is not the next revision"));
    const int slotCount = kind_ == InventoryContainerKind::Bag ? bag_->getSlotCount() : equipment_->getSlotCount();
    if (!descriptor_.capacity.isUnlimited() && candidate.entries.size() > descriptor_.capacity.value())
        return eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "inventory candidate exceeds container capacity"));

    std::vector<ItemStack>                           staged(static_cast<std::size_t>(slotCount));
    std::unordered_set<eve::container::MembershipId> ids;
    std::unordered_set<std::int32_t>                 slots;
    for (const auto& entry : candidate.entries) {
        if (!entry.membership.object.isValid() || !entry.membership.slot.isValid() ||
            entry.membership.slot.value() >= slotCount ||
            (!descriptor_.capacity.isUnlimited() &&
             static_cast<std::size_t>(entry.membership.slot.value()) >= descriptor_.capacity.value()) ||
            entry.membership.generation.isZero() || entry.object.id != entry.membership.object ||
            !ids.insert(entry.membership.object).second || !slots.insert(entry.membership.slot.value()).second)
            return eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation,
                                       "inventory candidate contains invalid membership"));
        const auto* payload = dynamic_cast<const InventoryContainerObject*>(entry.object.payload.get());
        if (payload == nullptr)
            return eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "inventory candidate payload is stale"));
        auto validObject = validateObject(*payload);
        if (!validObject)
            return eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>>::failure(
                validObject.status());
        if (entry.object.type != payload->stack.itemId ||
            entry.object.quantity != static_cast<std::uint32_t>(payload->stack.quantity) ||
            objectId(payload->stack) != entry.membership.object ||
            (kind_ == InventoryContainerKind::Equipment && slotName(entry.membership.slot).empty()))
            return eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "inventory candidate object facts are stale"));
        staged[static_cast<std::size_t>(entry.membership.slot.value())] = payload->stack;
    }

    std::unique_ptr<eve::container::IContainer::PreparedState> prepared =
        std::make_unique<PreparedState>(*this, std::move(staged), *nextRevision);
    return eve::Result<std::unique_ptr<eve::container::IContainer::PreparedState>>::success(std::move(prepared));
}

}  // namespace eve::inventory
