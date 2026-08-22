#include "economy/Economy.h"

#include "common/Capability.h"
#include "economy/Collector.h"
#include "economy/EconomySystem.h"
#include "economy/GatherNode.h"
#include "economy/ResourceType.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::economy {

Module_IMPL(Economy, new Economy());

Economy::Economy() { eve::cap::provide<eve::economy::IEconomy>(this); }

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
}

}  // namespace eve::economy
