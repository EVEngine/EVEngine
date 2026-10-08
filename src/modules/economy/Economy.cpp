#include "economy/Economy.h"

#include "common/Capability.h"
#include "common/SubjectRef.h"
#include "economy/Collector.h"
#include "economy/EconomyControl.h"
#include "economy/EconomySystem.h"
#include "economy/GatherNode.h"
#include "economy/ResourceType.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::economy {

Module_IMPL(Economy, new Economy());

Economy::Economy() { eve::cap::provide<eve::economy::IEconomy>(this); }

Economy::~Economy() { clearGameplayControls(); }

int Economy::credit(int player, const std::string& type, int amount) {
    return EconomySystem::credit(player, type, amount);
}

bool Economy::debit(int player, const std::string& type, int amount) {
    return EconomySystem::debit(player, type, amount);
}

int Economy::get(int player, const std::string& type) const { return EconomySystem::get(player, type); }

int Economy::getCap(int player, const std::string& type) const {
    return EconomySystem::getCap(player, type);
}

int Economy::getWasted(int player, const std::string& type) const {
    return EconomySystem::getWasted(player, type);
}

int Economy::getIncome(int player, const std::string& type) const {
    return EconomySystem::getIncome(player, type);
}

int Economy::getExpense(int player, const std::string& type) const {
    return EconomySystem::getExpense(player, type);
}

bool Economy::registerResourceType(const std::string& id, const std::string& category, int stockMax,
                                   const std::string& depletion) {
    ResourceTypeDef def;
    def.id          = id;
    def.category    = category;
    def.stockMax    = stockMax;
    def.displayName = id;
    if (depletion == "renewable")
        def.depletion = DepletionModel::Renewable;
    else if (depletion == "infinite")
        def.depletion = DepletionModel::Infinite;
    else if (depletion == "growing")
        def.depletion = DepletionModel::Growing;
    else
        def.depletion = DepletionModel::Finite;
    return ResourceTypeRegistry::registerType(def);
}

void Economy::clearTypes() { ResourceTypeRegistry::clear(); }

int Economy::typeCount() { return ResourceTypeRegistry::count(); }

bool Economy::hasType(const std::string& id) { return ResourceTypeRegistry::find(id) != nullptr; }

int Economy::getStockMax(const std::string& id) {
    const auto* def = ResourceTypeRegistry::find(id);
    return def ? def->stockMax : 0;
}

std::string Economy::getTypeId(int index) {
    const auto* def = ResourceTypeRegistry::typeAt(index);
    return def ? def->id : std::string{};
}

void Economy::clearEvents() { EconomySystem::clearEvents(); }

int Economy::eventCount() { return EconomySystem::eventCount(); }

std::string Economy::eventAction(int index) {
    if (index < 0 || index >= EconomySystem::eventCount()) return {};
    return EconomySystem::eventAt(index).action;
}

int Economy::eventPlayer(int index) {
    if (index < 0 || index >= EconomySystem::eventCount()) return 0;
    return EconomySystem::eventAt(index).player;
}

std::string Economy::eventType(int index) {
    if (index < 0 || index >= EconomySystem::eventCount()) return {};
    return EconomySystem::eventAt(index).type;
}

int Economy::eventAmount(int index) {
    if (index < 0 || index >= EconomySystem::eventCount()) return 0;
    return EconomySystem::eventAt(index).amount;
}

void Economy::expose(ssq::Table& table) {
    auto cls = table.addClass(name, Economy::create, false);
    expose(cls);

    auto node = table.addClass<GatherNode>(
        "GatherNode",
        std::function<GatherNode*(std::string, int, int, int)>(
            [](std::string type, int capacity, int slots, int regen) {
                return new GatherNode(std::move(type), capacity, slots, regen);
            }),
        true);
    node.addFunc("destroy", &GatherNode::destroy);
    node.addFunc("resourceType", &GatherNode::resourceType);
    node.addFunc("amount", &GatherNode::amount);
    node.addFunc("capacity", &GatherNode::capacity);
    node.addFunc("depleted", &GatherNode::depleted);
    node.addFunc("freeSlots", &GatherNode::freeSlots);
    node.addFunc("regenTick", &GatherNode::regenTick);

    auto collector = table.addClass<Collector>(
        "Collector",
        std::function<Collector*(int, int, int)>(
            [](int capacity, int rate, int travel) { return new Collector(capacity, rate, travel); }),
        true);
    collector.addFunc("destroy", &Collector::destroy);
    collector.addFunc("assign", &Collector::assign);
    collector.addFunc("clearAssignment", &Collector::clearAssignment);
    collector.addFunc("tick", &Collector::tick);
    collector.addFunc("isIdle", &Collector::isIdle);
    collector.addFunc("stateName", &Collector::stateName);
    collector.addFunc("cargo", &Collector::cargo);
    collector.addFunc("carryCapacity", &Collector::carryCapacity);
    collector.addFunc("resourceType", &Collector::resourceType);
    collector.addFunc("totalGathered", &Collector::totalGathered);
    collector.addFunc("trips", &Collector::trips);
}

void Economy::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Economy::getName);
    cls.addFunc("registerResourceType",
                [](Economy*, std::string id, std::string category, int stockMax,
                   std::string depletion) {
                    return Economy::registerResourceType(id, category, stockMax, depletion);
                });
    cls.addFunc("clearTypes", [](Economy*) { Economy::clearTypes(); });
    cls.addFunc("typeCount", [](Economy*) { return Economy::typeCount(); });
    cls.addFunc("hasType", [](Economy*, std::string id) { return Economy::hasType(id); });
    cls.addFunc("getStockMax", [](Economy*, std::string id) { return Economy::getStockMax(id); });
    cls.addFunc("credit", &Economy::credit);
    cls.addFunc("debit", &Economy::debit);
    cls.addFunc("get", &Economy::get);
    cls.addFunc("getCap", &Economy::getCap);
    cls.addFunc("getWasted", &Economy::getWasted);
    cls.addFunc("getIncome", &Economy::getIncome);
    cls.addFunc("getExpense", &Economy::getExpense);
    cls.addFunc("clearEvents", [](Economy*) { Economy::clearEvents(); });
    cls.addFunc("getEventCount", [](Economy*) { return Economy::eventCount(); });
    cls.addFunc("getEventAction", [](Economy*, int index) { return Economy::eventAction(index); });
    cls.addFunc("getEventPlayer", [](Economy*, int index) { return Economy::eventPlayer(index); });
    cls.addFunc("getEventType", [](Economy*, int index) { return Economy::eventType(index); });
    cls.addFunc("getEventAmount", [](Economy*, int index) { return Economy::eventAmount(index); });
    cls.addFunc("getTypeId", &Economy::getTypeId);

    // 把玩家账本发布到共享玩法协议（`eve_gameplay` / MCP）。返回 {ok, message}：
    // 失败原因（非规范持久 id、重复实例）不被丢弃。
    cls.addFunc("publishGameplay", [vm = cls.getHandle()](Economy* self, const std::string& instanceId,
                                                          const std::string& ownerId, int player) {
        ssq::Table result(vm);
        if (self == nullptr) {
            result.set("ok", false);
            result.set("message", std::string("economy module unavailable"));
            return result;
        }
        const auto published = self->publishGameplay(instanceId, ownerId, player);
        result.set("ok", published.ok());
        result.set("message", published.ok() ? std::string("published") : published.status().describe());
        return result;
    });
    cls.addFunc("unpublishGameplay", [vm = cls.getHandle()](Economy* self, const std::string& instanceId) {
        ssq::Table result(vm);
        const auto unpublished = self == nullptr
                                     ? eve::Result<void>::failure(eve::Diagnostic::error(
                                           eve::DiagnosticCode::Failed, "economy module unavailable", "self"))
                                     : self->unpublishGameplay(instanceId);
        result.set("ok", unpublished.ok());
        result.set("message", unpublished.ok() ? std::string("unpublished") : unpublished.status().describe());
        return result;
    });
    cls.addFunc("clearGameplayControls", &Economy::clearGameplayControls);
    cls.addFunc("getGameplayControlCount", &Economy::gameplayControlCount);
}

eve::Result<void> Economy::publishGameplay(const std::string& instanceId, const std::string& ownerId, int player) {
    if (instanceId.empty() || ownerId.empty())
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "publishGameplay needs an instance id and an owner id",
                                                                 "instanceId"));
    const auto instance = eve::PersistentId::parse(instanceId);
    const auto owner    = eve::PersistentId::parse(ownerId);
    if (!instance.has_value() || !owner.has_value())
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                   "instance and owner ids must be canonical persistent ids", "instanceId"));
    if (!gameplay_) gameplay_ = std::make_unique<EconomyControl>();
    return gameplay_->publish(eve::SubjectRef::fromPersistentId(*instance), eve::SubjectRef::fromPersistentId(*owner),
                              player);
}

eve::Result<void> Economy::unpublishGameplay(const std::string& instanceId) {
    if (!gameplay_)
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "no economy instance is published", "instanceId"));
    const auto instance = eve::PersistentId::parse(instanceId);
    if (!instance.has_value())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "the instance id must be a canonical persistent id", "instanceId"));
    return gameplay_->unpublish(eve::SubjectRef::fromPersistentId(*instance));
}

void Economy::clearGameplayControls() {
    if (gameplay_) gameplay_->clear();
}

int Economy::gameplayControlCount() const { return gameplay_ ? gameplay_->count() : 0; }

std::vector<std::string> Economy::gameplayInstances() const {
    std::vector<std::string> result;
    if (!gameplay_) return result;
    for (const auto& instance : gameplay_->instances()) result.push_back(instance.format());
    return result;
}

}  // namespace eve::economy
