#include "inventory/Inventory.h"
#include "inventory/InventoryControl.h"
#include "inventory/InventorySaveSession.h"
#include "inventory/InventorySystem.h"
#include "inventory/Item.h"

#include "common/SubjectRef.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::inventory {

Module_IMPL(Inventory, new Inventory());

int Inventory::registerItemsFromJson(const std::string &json) {
    InventorySystem::ensureBuiltins();
    return ItemRegistry::loadFromJson(json, nullptr);
}

void Inventory::clearItemDefinitions() { ItemRegistry::clear(); }

int Inventory::getItemDefinitionCount() { return ItemRegistry::count(); }

bool Inventory::hasItemDefinition(const std::string &itemId) {
    return ItemRegistry::find(itemId) != nullptr;
}

std::string Inventory::getItemDisplayName(const std::string &itemId) {
    const auto *def = ItemRegistry::find(itemId);
    return def ? def->displayName : std::string{};
}

int Inventory::getItemMaxStack(const std::string &itemId) {
    const auto *def = ItemRegistry::find(itemId);
    return def ? def->maxStack : 0;
}

float Inventory::getItemWeight(const std::string &itemId) {
    const auto *def = ItemRegistry::find(itemId);
    return def ? def->weight : 0.f;
}

float Inventory::getItemVolume(const std::string &itemId) {
    const auto *def = ItemRegistry::find(itemId);
    return def ? def->volume : 0.f;
}

std::string Inventory::getItemCategory(const std::string &itemId) {
    const auto *def = ItemRegistry::find(itemId);
    return def ? def->category : std::string{};
}

std::string Inventory::getItemEquipSlot(const std::string &itemId) {
    const auto *def = ItemRegistry::find(itemId);
    return def ? def->equipSlot : std::string{};
}

bool Inventory::itemHasTag(const std::string &itemId, const std::string &tag) {
    const auto *def = ItemRegistry::find(itemId);
    return def ? def->hasTag(tag) : false;
}

std::string Inventory::getItemExtra(const std::string &itemId, const std::string &key,
                                    const std::string &fallback) {
    const auto *def = ItemRegistry::find(itemId);
    return def ? def->getExtra(key, fallback) : fallback;
}

Bag *Inventory::newBag(int slotCount) {
    InventorySystem::ensureBuiltins();
    return new Bag(slotCount);
}

EquipmentSet *Inventory::newEquipmentSet() {
    InventorySystem::ensureBuiltins();
    return new EquipmentSet();
}

int Inventory::transferItem(Bag *from, Bag *to, const std::string &itemId, int quantity) {
    return InventorySystem::transfer(from, to, itemId, quantity);
}

int Inventory::transferSlot(Bag *from, int fromSlot, Bag *to, int quantity) {
    return InventorySystem::transferSlot(from, fromSlot, to, quantity);
}

bool Inventory::hasAcceptRule(const std::string &ruleName) {
    return InventorySystem::hasAcceptRule(ruleName);
}

bool Inventory::hasCapacityPolicy(const std::string &policyName) {
    return InventorySystem::hasCapacityPolicy(policyName);
}

bool Inventory::hasStackRule(const std::string &ruleName) {
    return InventorySystem::hasStackRule(ruleName);
}

void Inventory::clearChangeEvents() { InventorySystem::clearEvents(); }

int Inventory::getChangeEventCount() const {
    return int(InventorySystem::events().size());
}

std::string Inventory::getChangeEventAction(int index) const {
    const auto &evs = InventorySystem::events();
    if (index < 0 || size_t(index) >= evs.size()) return {};
    return evs[size_t(index)].action;
}

std::string Inventory::getChangeEventBagId(int index) const {
    const auto &evs = InventorySystem::events();
    if (index < 0 || size_t(index) >= evs.size()) return {};
    return evs[size_t(index)].bagId;
}

std::string Inventory::getChangeEventOtherBagId(int index) const {
    const auto &evs = InventorySystem::events();
    if (index < 0 || size_t(index) >= evs.size()) return {};
    return evs[size_t(index)].otherBagId;
}

std::string Inventory::getChangeEventItemId(int index) const {
    const auto &evs = InventorySystem::events();
    if (index < 0 || size_t(index) >= evs.size()) return {};
    return evs[size_t(index)].itemId;
}

int Inventory::getChangeEventQuantity(int index) const {
    const auto &evs = InventorySystem::events();
    if (index < 0 || size_t(index) >= evs.size()) return 0;
    return evs[size_t(index)].quantity;
}

int Inventory::getChangeEventSlot(int index) const {
    const auto &evs = InventorySystem::events();
    if (index < 0 || size_t(index) >= evs.size()) return -1;
    return evs[size_t(index)].slot;
}

int Inventory::getChangeEventOtherSlot(int index) const {
    const auto &evs = InventorySystem::events();
    if (index < 0 || size_t(index) >= evs.size()) return -1;
    return evs[size_t(index)].otherSlot;
}

std::string Inventory::getChangeEventEquipSlot(int index) const {
    const auto &evs = InventorySystem::events();
    if (index < 0 || size_t(index) >= evs.size()) return {};
    return evs[size_t(index)].equipSlot;
}

void Inventory::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Inventory::create, false);
    expose(cls);

    auto bag = table.addClass<Bag>(
        "Bag", std::function<Bag *(int)>([](int slots) -> Bag * { return new Bag(slots); }), true);
    bag.addFunc("destroy", &Bag::destroy);
    bag.addFunc("getId", &Bag::getId);
    bag.addFunc("setId", &Bag::setId);
    bag.addFunc("getKind", &Bag::getKind);
    bag.addFunc("setKind", &Bag::setKind);
    bag.addFunc("getSlotCount", &Bag::getSlotCount);
    bag.addFunc("setSlotCount", &Bag::setSlotCount);
    bag.addFunc("getMaxWeight", &Bag::getMaxWeight);
    bag.addFunc("setMaxWeight", &Bag::setMaxWeight);
    bag.addFunc("getMaxVolume", &Bag::getMaxVolume);
    bag.addFunc("setMaxVolume", &Bag::setMaxVolume);
    bag.addFunc("getAcceptRule", &Bag::getAcceptRule);
    bag.addFunc("setAcceptRule", &Bag::setAcceptRule);
    bag.addFunc("getCapacityPolicy", &Bag::getCapacityPolicy);
    bag.addFunc("setCapacityPolicy", &Bag::setCapacityPolicy);
    bag.addFunc("getStackRule", &Bag::getStackRule);
    bag.addFunc("setStackRule", &Bag::setStackRule);
    bag.addFunc("clearAcceptTags", &Bag::clearAcceptTags);
    bag.addFunc("addAcceptTag", &Bag::addAcceptTag);
    bag.addFunc("getAcceptTagCount", &Bag::getAcceptTagCount);
    bag.addFunc("getAcceptTag", &Bag::getAcceptTag);
    bag.addFunc("clearRejectTags", &Bag::clearRejectTags);
    bag.addFunc("addRejectTag", &Bag::addRejectTag);
    bag.addFunc("getRejectTagCount", &Bag::getRejectTagCount);
    bag.addFunc("getRejectTag", &Bag::getRejectTag);
    bag.addFunc("setExtra", &Bag::setExtra);
    bag.addFunc("getExtra", &Bag::getExtra);
    bag.addFunc("canAddItem", &Bag::canAddItem);
    bag.addFunc("canAddItemReason", &Bag::canAddItemReason);
    bag.addFunc("addItem", &Bag::addItem);
    bag.addFunc("removeItem", &Bag::removeItem);
    bag.addFunc("removeAt", &Bag::removeAt);
    bag.addFunc("swapSlots", &Bag::swapSlots);
    bag.addFunc("moveSlot", &Bag::moveSlot);
    bag.addFunc("splitStack", &Bag::splitStack);
    bag.addFunc("countItem", &Bag::countItem);
    bag.addFunc("findItem", &Bag::findItem);
    bag.addFunc("findItemByTag", &Bag::findItemByTag);
    bag.addFunc("getUsedWeight", &Bag::getUsedWeight);
    bag.addFunc("getUsedVolume", &Bag::getUsedVolume);
    bag.addFunc("getUsedSlotCount", &Bag::getUsedSlotCount);
    bag.addFunc("clear", &Bag::clear);
    bag.addFunc("isSlotEmpty", &Bag::isSlotEmpty);
    bag.addFunc("getSlotItemId", &Bag::getSlotItemId);
    bag.addFunc("getSlotQuantity", &Bag::getSlotQuantity);
    bag.addFunc("getSlotInstanceId", &Bag::getSlotInstanceId);
    bag.addFunc("getSlotDurability", &Bag::getSlotDurability);
    bag.addFunc("setSlotDurability", &Bag::setSlotDurability);
    bag.addFunc("getSlotProp", &Bag::getSlotProp);
    bag.addFunc("setSlotProp", &Bag::setSlotProp);
    bag.addFunc("slotHasTag", &Bag::slotHasTag);
    bag.addFunc("addSlotTag", &Bag::addSlotTag);

    auto eq = table.addClass<EquipmentSet>(
        "EquipmentSet", std::function<EquipmentSet *()>([]() { return new EquipmentSet(); }), true);
    eq.addFunc("destroy", &EquipmentSet::destroy);
    eq.addFunc("getId", &EquipmentSet::getId);
    eq.addFunc("setId", &EquipmentSet::setId);
    eq.addFunc("defineSlot", &EquipmentSet::defineSlot);
    eq.addFunc("clearSlotAllowedTags", &EquipmentSet::clearSlotAllowedTags);
    eq.addFunc("addSlotAllowedTag", &EquipmentSet::addSlotAllowedTag);
    eq.addFunc("hasSlot", &EquipmentSet::hasSlot);
    eq.addFunc("getSlotCount", &EquipmentSet::getSlotCount);
    eq.addFunc("getSlotName", &EquipmentSet::getSlotName);
    eq.addFunc("isSlotEmpty", &EquipmentSet::isSlotEmpty);
    eq.addFunc("getSlotItemId", &EquipmentSet::getSlotItemId);
    eq.addFunc("getSlotQuantity", &EquipmentSet::getSlotQuantity);
    eq.addFunc("getSlotInstanceId", &EquipmentSet::getSlotInstanceId);
    eq.addFunc("equipFromBag", &EquipmentSet::equipFromBag);
    eq.addFunc("unequipToBag", &EquipmentSet::unequipToBag);
    eq.addFunc("clearSlot", &EquipmentSet::clearSlot);

    auto saveSession = table.addClass<InventorySaveSession>(
        "InventorySaveSession",
        std::function<InventorySaveSession *()>([]() { return new InventorySaveSession(); }), true);
    saveSession.addFunc("bind", [](InventorySaveSession *session, Bag *boundBag,
                                   EquipmentSet *boundEquipment) -> int {
        if (!session || !boundBag || !boundEquipment) return 0;
        session->bind(*boundBag, *boundEquipment);
        return 1;
    });
    saveSession.addFunc("snapshotJson", [](InventorySaveSession *session) -> std::string {
        if (!session) return {};
        auto result = session->snapshotJson();
        return result.ok() ? std::move(result).takeValue() : std::string{};
    });
    saveSession.addFunc("restoreSnapshotJson",
                        [](InventorySaveSession *session, const std::string &json) -> int {
                            return session && session->restoreSnapshotJson(json).ok() ? 1 : 0;
                        });
}

void Inventory::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Inventory::getName);
    cls.addFunc("registerItemsFromJson", &Inventory::registerItemsFromJson);
    cls.addFunc("clearItemDefinitions", &Inventory::clearItemDefinitions);
    cls.addFunc("getItemDefinitionCount", &Inventory::getItemDefinitionCount);
    cls.addFunc("hasItemDefinition", &Inventory::hasItemDefinition);
    cls.addFunc("getItemDisplayName", &Inventory::getItemDisplayName);
    cls.addFunc("getItemMaxStack", &Inventory::getItemMaxStack);
    cls.addFunc("getItemWeight", &Inventory::getItemWeight);
    cls.addFunc("getItemVolume", &Inventory::getItemVolume);
    cls.addFunc("getItemCategory", &Inventory::getItemCategory);
    cls.addFunc("getItemEquipSlot", &Inventory::getItemEquipSlot);
    cls.addFunc("itemHasTag", &Inventory::itemHasTag);
    cls.addFunc("getItemExtra", &Inventory::getItemExtra);
    cls.addFunc("newBag", &Inventory::newBag);
    cls.addFunc("newEquipmentSet", &Inventory::newEquipmentSet);
    cls.addFunc("transferItem", &Inventory::transferItem);
    cls.addFunc("transferSlot", &Inventory::transferSlot);
    cls.addFunc("hasAcceptRule", &Inventory::hasAcceptRule);
    cls.addFunc("hasCapacityPolicy", &Inventory::hasCapacityPolicy);
    cls.addFunc("hasStackRule", &Inventory::hasStackRule);
    cls.addFunc("clearChangeEvents", &Inventory::clearChangeEvents);
    cls.addFunc("getChangeEventCount", &Inventory::getChangeEventCount);
    cls.addFunc("getChangeEventAction", &Inventory::getChangeEventAction);
    cls.addFunc("getChangeEventBagId", &Inventory::getChangeEventBagId);
    cls.addFunc("getChangeEventOtherBagId", &Inventory::getChangeEventOtherBagId);
    cls.addFunc("getChangeEventItemId", &Inventory::getChangeEventItemId);
    cls.addFunc("getChangeEventQuantity", &Inventory::getChangeEventQuantity);
    cls.addFunc("getChangeEventSlot", &Inventory::getChangeEventSlot);
    cls.addFunc("getChangeEventOtherSlot", &Inventory::getChangeEventOtherSlot);
    cls.addFunc("getChangeEventEquipSlot", &Inventory::getChangeEventEquipSlot);

    // 把玩家背包发布到共享玩法协议（`eve_gameplay` / MCP）。返回 {ok, message}：
    // 失败原因（非规范持久 id、重复实例、空 bag）不被丢弃。equipment 可为 null；
    // ssq 的默认指针绑定拒绝 Squirrel `null`，故按 Avatar.cpp 的既有做法接收
    // ssq::Object 并在此翻译 null -> nullptr。
    cls.addFunc("publishGameplay", [vm = cls.getHandle()](Inventory* self, const std::string& instanceId,
                                                          const std::string& ownerId, Bag* bag, ssq::Object equipment) {
        ssq::Table result(vm);
        if (self == nullptr || bag == nullptr) {
            result.set("ok", false);
            result.set("message", std::string("publishGameplay needs the inventory module and a bag"));
            return result;
        }
        EquipmentSet* equipmentSet = equipment.isNull() ? nullptr : equipment.toPtrUnsafe<EquipmentSet*>();
        const auto    published    = self->publishGameplay(instanceId, ownerId, bag, equipmentSet);
        result.set("ok", published.ok());
        result.set("message", published.ok() ? std::string("published") : published.status().describe());
        return result;
    });
    cls.addFunc("unpublishGameplay", [vm = cls.getHandle()](Inventory* self, const std::string& instanceId) {
        ssq::Table result(vm);
        const auto unpublished = self == nullptr
                                     ? eve::Result<void>::failure(eve::Diagnostic::error(
                                           eve::DiagnosticCode::Failed, "inventory module unavailable", "self"))
                                     : self->unpublishGameplay(instanceId);
        result.set("ok", unpublished.ok());
        result.set("message", unpublished.ok() ? std::string("unpublished") : unpublished.status().describe());
        return result;
    });
    cls.addFunc("clearGameplayControls", &Inventory::clearGameplayControls);
    cls.addFunc("getGameplayControlCount", &Inventory::gameplayControlCount);
}

Inventory::~Inventory() { clearGameplayControls(); }

eve::Result<void> Inventory::publishGameplay(const std::string& instanceId, const std::string& ownerId, Bag* bag,
                                             EquipmentSet* equipment) {
    if (instanceId.empty() || ownerId.empty() || bag == nullptr)
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                   "publishGameplay needs an instance id, an owner id and a bag", "instanceId"));
    const auto instance = eve::PersistentId::parse(instanceId);
    const auto owner    = eve::PersistentId::parse(ownerId);
    if (!instance.has_value() || !owner.has_value())
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                   "instance and owner ids must be canonical persistent ids", "instanceId"));
    if (!gameplay_) gameplay_ = std::make_unique<InventoryControl>();
    return gameplay_->publish(eve::SubjectRef::fromPersistentId(*instance), eve::SubjectRef::fromPersistentId(*owner),
                              *bag, equipment);
}

eve::Result<void> Inventory::unpublishGameplay(const std::string& instanceId) {
    if (!gameplay_)
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "no inventory instance is published", "instanceId"));
    const auto instance = eve::PersistentId::parse(instanceId);
    if (!instance.has_value())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "the instance id must be a canonical persistent id", "instanceId"));
    return gameplay_->unpublish(eve::SubjectRef::fromPersistentId(*instance));
}

void Inventory::clearGameplayControls() {
    if (gameplay_) gameplay_->clear();
}

int Inventory::gameplayControlCount() const { return gameplay_ ? gameplay_->count() : 0; }

std::vector<std::string> Inventory::gameplayInstances() const {
    std::vector<std::string> result;
    if (!gameplay_) return result;
    for (const auto& instance : gameplay_->instances()) result.push_back(instance.format());
    return result;
}

}  // namespace eve::inventory
