#include "warship/Warship.h"

#include "common/Diagnostic.h"
#include "common/ECS.h"
#include "common/SquirrelBinding.h"
#include "common/Value.h"
#include "warship/WarshipRegistry.h"
#include "warship/WarshipSystem.h"

#include <squirrel.h>
#include <simplesquirrel/simplesquirrel.hpp>

#include <cmath>
#include <cstdint>
#include <utility>

namespace eve::warship {
namespace {

template <typename T>
void registerCppEntityClassForScript(const ssq::Class& cls) {
    eve::registerCppEntityView(cls, [](ssq::Array& out) {
        HSQUIRRELVM vm = out.getHandle();
        sq_pushobject(vm, out.getRaw());
        ecs::Table* table = ecs::current();
        if (table == nullptr) {
            sq_pop(vm, 1);
            return;
        }
        ecs::IComponentManager&             cm  = table->getOrCreateManager<T>();
        auto*                               reg = cm.getOrCreateRegistryComponentBuffer<T>();
        std::vector<ecs::IComponentBuffer*> stack;
        stack.push_back(reg);
        while (!stack.empty()) {
            ecs::IComponentBuffer* buf = stack.back();
            stack.pop_back();
            auto* r = dynamic_cast<ecs::IRegistryComponentBuffer*>(buf);
            if (r != nullptr) {
                for (uint32_t i = 0; i < r->entity_count(); ++i) {
                    ecs::Entity* ent = r->entity_at(i);
                    if (ent != nullptr && ecs::is_entity_visible(ent)) {
                        ssq::detail::pushByPtr<T>(vm, static_cast<T*>(ent));
                        sq_arrayappend(vm, -2);
                    }
                }
            }
            if (buf->children != nullptr) stack.push_back(buf->children);
            if (buf->next != nullptr) stack.push_back(buf->next);
        }
        sq_pop(vm, 1);
    });
}

Result<void> requireShip(WarshipEntity* ship, const char* op) {
    if (!ship) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "warship entity is null", op));
    }
    return Result<void>::success();
}

}  // namespace

Module_IMPL(Warship, new Warship());

Result<int> Warship::registerHullsFromJson(const std::string& json) { return WarshipRegistry::loadHullsFromJson(json); }

Result<int> Warship::registerFunctionsFromJson(const std::string& json) {
    return WarshipRegistry::loadFunctionsFromJson(json);
}

void Warship::clearHullDefinitions() { WarshipRegistry::clearHulls(); }

void Warship::clearFunctionDefinitions() { WarshipRegistry::clearFunctions(); }

bool Warship::hasHullDefinition(const std::string& id) const { return WarshipRegistry::hasHull(id); }

bool Warship::hasFunctionDefinition(const std::string& id) const { return WarshipRegistry::hasFunction(id); }

int Warship::getHullDefinitionCount() const { return WarshipRegistry::hullCount(); }

int Warship::getFunctionDefinitionCount() const { return WarshipRegistry::functionCount(); }

std::string Warship::getHullDefinitionCategory(const std::string& id) const {
    const WarshipDefinition* def = WarshipRegistry::findHull(id);
    return def ? def->category : std::string{};
}

std::string Warship::getHullDefinitionMobility(const std::string& id) const {
    const WarshipDefinition* def = WarshipRegistry::findHull(id);
    return def ? def->mobility : std::string{};
}

void Warship::registerMobility(IWarshipMobility* mobility) { WarshipSystem::registerMobility(mobility); }

int Warship::getMobilityCount() { return WarshipSystem::mobilityCount(); }

Result<WarshipEntity*> Warship::newWarship(const std::string& defId, float x, float y, float heading,
                                           const std::string& faction) {
    const WarshipDefinition* def = WarshipRegistry::findHull(defId);
    if (!def) {
        return Result<WarshipEntity*>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "unknown hull definition", "warship.new"));
    }
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(heading)) {
        return Result<WarshipEntity*>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "spawn transform must be finite", "warship.new"));
    }

    WarshipEntity* ship        = WarshipEntity::createWarship();
    ship->identity()->id       = defId + "#" + std::to_string(reinterpret_cast<std::uintptr_t>(ship));
    ship->identity()->defId    = defId;
    ship->identity()->faction  = faction;
    ship->identity()->category = def->category;
    ship->definition()->def    = def;
    ship->motion()->x          = x;
    ship->motion()->y          = y;
    ship->motion()->heading    = heading;
    ship->health()->hp         = def->maxHealth;
    ship->health()->maxHp      = def->maxHealth;
    ship->hull()->length       = def->length;
    ship->hull()->beam         = def->beam;
    ship->hull()->decks        = def->grid.decks;
    ship->layout()->grid       = def->grid;
    ship->layout()->cells.assign(static_cast<std::size_t>(def->grid.cellCount()), CompartmentCell{});

    for (const LayoutSeed& seed : def->defaultLayout) {
        const int index = ship->layout()->grid.indexOf(seed.deck, seed.row, seed.col);
        if (index < 0) continue;
        if (!WarshipRegistry::hasFunction(seed.functionId)) continue;
        ship->layout()->cells[static_cast<std::size_t>(index)].functionId = seed.functionId;
    }
    WarshipSystem::recomputeCapabilities(*ship);
    return Result<WarshipEntity*>::success(ship);
}

Result<void> Warship::placeCompartment(WarshipEntity* ship, int deck, int row, int col, const std::string& functionId) {
    if (auto ok = requireShip(ship, "warship.place"); !ok) return ok;
    return WarshipSystem::placeCompartment(*ship, deck, row, col, functionId);
}

Result<void> Warship::clearCompartment(WarshipEntity* ship, int deck, int row, int col) {
    if (auto ok = requireShip(ship, "warship.clear"); !ok) return ok;
    return WarshipSystem::clearCompartment(*ship, deck, row, col);
}

Result<void> Warship::swapCompartments(WarshipEntity* ship, int deckA, int rowA, int colA, int deckB, int rowB,
                                       int colB) {
    if (auto ok = requireShip(ship, "warship.swap"); !ok) return ok;
    return WarshipSystem::swapCompartments(*ship, deckA, rowA, colA, deckB, rowB, colB);
}

Result<void> Warship::rearrangeFromJson(WarshipEntity* ship, const std::string& json) {
    if (auto ok = requireShip(ship, "warship.rearrange"); !ok) return ok;
    return WarshipSystem::rearrangeFromJson(*ship, json);
}

int Warship::getCompartmentCount(WarshipEntity* ship) const {
    return ship == nullptr ? 0 : static_cast<int>(ship->layout()->cells.size());
}

int Warship::getGridCols(WarshipEntity* ship) const { return ship == nullptr ? 0 : ship->layout()->grid.cols; }

int Warship::getGridRows(WarshipEntity* ship) const { return ship == nullptr ? 0 : ship->layout()->grid.rows; }

int Warship::getGridDecks(WarshipEntity* ship) const { return ship == nullptr ? 0 : ship->layout()->grid.decks; }

std::string Warship::getCompartmentFunction(WarshipEntity* ship, int deck, int row, int col) const {
    if (ship == nullptr) return {};
    const int index = ship->layout()->grid.indexOf(deck, row, col);
    if (index < 0 || index >= static_cast<int>(ship->layout()->cells.size())) return {};
    return ship->layout()->cells[static_cast<std::size_t>(index)].functionId;
}

float Warship::getPowerNet(WarshipEntity* ship) const {
    return ship == nullptr ? 0.f : ship->capabilities()->values.powerNet;
}

float Warship::getThrustMul(WarshipEntity* ship) const {
    return ship == nullptr ? 1.f : ship->capabilities()->values.thrustMul;
}

int Warship::getHangarCapacity(WarshipEntity* ship) const {
    return ship == nullptr ? 0 : ship->capabilities()->values.hangarCapacity;
}

int Warship::getWeaponBayCount(WarshipEntity* ship) const {
    return ship == nullptr ? 0 : ship->capabilities()->values.weaponBayCount;
}

int Warship::getCrewCapacity(WarshipEntity* ship) const {
    return ship == nullptr ? 0 : ship->capabilities()->values.crewCapacity;
}

float Warship::getSailPower(WarshipEntity* ship) const {
    return ship == nullptr ? 0.f : ship->capabilities()->values.sailPower;
}

bool Warship::isUnderpowered(WarshipEntity* ship) const {
    return ship == nullptr ? false : ship->capabilities()->values.underpowered;
}

void Warship::moveTo(WarshipEntity* ship, float x, float y) {
    if (ship == nullptr) return;
    WarshipSystem::moveTo(*ship, x, y);
}

void Warship::stop(WarshipEntity* ship) {
    if (ship == nullptr) return;
    WarshipSystem::stop(*ship);
}

void Warship::setInput(WarshipEntity* ship, float throttle, float steer) {
    if (ship == nullptr) return;
    ship->input()->throttle = throttle;
    ship->input()->steer    = steer;
    ship->motion()->hasMove = false;
}

void Warship::update(float dt) { WarshipSystem::update(dt); }

float Warship::getX(WarshipEntity* ship) const { return ship == nullptr ? 0.f : ship->motion()->x; }

float Warship::getY(WarshipEntity* ship) const { return ship == nullptr ? 0.f : ship->motion()->y; }

float Warship::getHeading(WarshipEntity* ship) const { return ship == nullptr ? 0.f : ship->motion()->heading; }

float Warship::getSpeed(WarshipEntity* ship) const { return ship == nullptr ? 0.f : ship->motion()->speed; }

float Warship::getLength(WarshipEntity* ship) const { return ship == nullptr ? 0.f : ship->hull()->length; }

float Warship::getBeam(WarshipEntity* ship) const { return ship == nullptr ? 0.f : ship->hull()->beam; }

std::string Warship::getId(WarshipEntity* ship) const { return ship == nullptr ? std::string{} : ship->identity()->id; }

std::string Warship::getDefId(WarshipEntity* ship) const {
    return ship == nullptr ? std::string{} : ship->identity()->defId;
}

std::string Warship::getCategory(WarshipEntity* ship) const {
    return ship == nullptr ? std::string{} : ship->identity()->category;
}

std::string Warship::getFaction(WarshipEntity* ship) const {
    return ship == nullptr ? std::string{} : ship->identity()->faction;
}

float Warship::getHealth(WarshipEntity* ship) const { return ship == nullptr ? 0.f : ship->health()->hp; }

float Warship::getMaxHealth(WarshipEntity* ship) const { return ship == nullptr ? 0.f : ship->health()->maxHp; }

bool Warship::isArrived(WarshipEntity* ship) const { return ship != nullptr && ship->motion()->arrived; }

bool Warship::isDestroyed(WarshipEntity* ship) const { return ship != nullptr && ship->stateFlags()->destroyed; }

void Warship::applyDamage(WarshipEntity* ship, float amount) {
    if (!ship) return;
    WarshipSystem::applyDamage(*ship, amount);
}

void Warship::clearEvents() { WarshipSystem::clearEvents(); }

int Warship::getEventCount() const { return WarshipSystem::eventCount(); }

std::string Warship::getEventType(int index) const {
    const WarshipEvent* event = WarshipSystem::eventAt(index);
    return event ? warshipEventTypeName(event->type) : std::string{};
}

std::string Warship::getEventWarshipId(int index) const {
    const WarshipEvent* event = WarshipSystem::eventAt(index);
    return event ? event->warshipId : std::string{};
}

std::string Warship::getEventDefId(int index) const {
    const WarshipEvent* event = WarshipSystem::eventAt(index);
    return event ? event->defId : std::string{};
}

std::string Warship::getEventDetail(int index) const {
    const WarshipEvent* event = WarshipSystem::eventAt(index);
    return event ? event->detail : std::string{};
}

void Warship::expose(ssq::Table& table) {
    auto cls = table.addClass(name, Warship::create, false);
    expose(cls);

    auto shipCls = table.addClass<WarshipEntity>(
        "WarshipEntity", std::function<WarshipEntity*()>([]() -> WarshipEntity* { return nullptr; }), false);
    registerCppEntityClassForScript<WarshipEntity>(shipCls);
}

void Warship::expose(ssq::Class& cls) {
    auto intProjector = [](int count) { return eve::Value(static_cast<std::int64_t>(count)); };

    cls.addFunc("registerHullsFromJson", [vm = cls.getHandle(), intProjector](Warship* self, const std::string& json) {
        if (!self) {
            return eve::script::projectResult(
                vm,
                Result<int>::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument, "warship module is null", "warship")),
                intProjector);
        }
        return eve::script::projectResult(vm, self->registerHullsFromJson(json), intProjector);
    });
    cls.addFunc(
        "registerFunctionsFromJson", [vm = cls.getHandle(), intProjector](Warship* self, const std::string& json) {
            if (!self) {
                return eve::script::projectResult(
                    vm,
                    Result<int>::failure(
                        Diagnostic::error(DiagnosticCode::InvalidArgument, "warship module is null", "warship")),
                    intProjector);
            }
            return eve::script::projectResult(vm, self->registerFunctionsFromJson(json), intProjector);
        });
    cls.addFunc("clearHullDefinitions", &Warship::clearHullDefinitions);
    cls.addFunc("clearFunctionDefinitions", &Warship::clearFunctionDefinitions);
    cls.addFunc("hasHullDefinition", &Warship::hasHullDefinition);
    cls.addFunc("hasFunctionDefinition", &Warship::hasFunctionDefinition);
    cls.addFunc("getHullDefinitionCount", &Warship::getHullDefinitionCount);
    cls.addFunc("getFunctionDefinitionCount", &Warship::getFunctionDefinitionCount);
    cls.addFunc("getHullDefinitionCategory", &Warship::getHullDefinitionCategory);
    cls.addFunc("getHullDefinitionMobility", &Warship::getHullDefinitionMobility);
    cls.addFunc("getMobilityCount", &Warship::getMobilityCount);

    cls.addFunc("newWarship",
                [](Warship* self, const std::string& defId, float x, float y, float heading,
                   const std::string& faction) -> WarshipEntity* {
                    if (!self) return nullptr;
                    auto created = self->newWarship(defId, x, y, heading, faction);
                    return created ? created.value() : nullptr;
                });

    cls.addFunc("placeCompartment", [vm = cls.getHandle()](Warship* self, WarshipEntity* ship, int deck, int row,
                                                           int col, const std::string& functionId) {
        if (!self) {
            return eve::script::projectResult(
                vm, Result<void>::failure(
                        Diagnostic::error(DiagnosticCode::InvalidArgument, "warship module is null", "warship")));
        }
        return eve::script::projectResult(vm, self->placeCompartment(ship, deck, row, col, functionId));
    });
    cls.addFunc(
        "clearCompartment", [vm = cls.getHandle()](Warship* self, WarshipEntity* ship, int deck, int row, int col) {
            if (!self) {
                return eve::script::projectResult(
                    vm, Result<void>::failure(
                            Diagnostic::error(DiagnosticCode::InvalidArgument, "warship module is null", "warship")));
            }
            return eve::script::projectResult(vm, self->clearCompartment(ship, deck, row, col));
        });
    cls.addFunc("swapCompartments", [vm = cls.getHandle()](Warship* self, WarshipEntity* ship, int deckA, int rowA,
                                                           int colA, int deckB, int rowB, int colB) {
        if (!self) {
            return eve::script::projectResult(
                vm, Result<void>::failure(
                        Diagnostic::error(DiagnosticCode::InvalidArgument, "warship module is null", "warship")));
        }
        return eve::script::projectResult(vm, self->swapCompartments(ship, deckA, rowA, colA, deckB, rowB, colB));
    });
    cls.addFunc(
        "rearrangeFromJson", [vm = cls.getHandle()](Warship* self, WarshipEntity* ship, const std::string& json) {
            if (!self) {
                return eve::script::projectResult(
                    vm, Result<void>::failure(
                            Diagnostic::error(DiagnosticCode::InvalidArgument, "warship module is null", "warship")));
            }
            return eve::script::projectResult(vm, self->rearrangeFromJson(ship, json));
        });

    cls.addFunc("getCompartmentCount", &Warship::getCompartmentCount);
    cls.addFunc("getGridCols", &Warship::getGridCols);
    cls.addFunc("getGridRows", &Warship::getGridRows);
    cls.addFunc("getGridDecks", &Warship::getGridDecks);
    cls.addFunc("getCompartmentFunction", &Warship::getCompartmentFunction);
    cls.addFunc("getPowerNet", &Warship::getPowerNet);
    cls.addFunc("getThrustMul", &Warship::getThrustMul);
    cls.addFunc("getHangarCapacity", &Warship::getHangarCapacity);
    cls.addFunc("getWeaponBayCount", &Warship::getWeaponBayCount);
    cls.addFunc("getCrewCapacity", &Warship::getCrewCapacity);
    cls.addFunc("getSailPower", &Warship::getSailPower);
    cls.addFunc("isUnderpowered", &Warship::isUnderpowered);

    cls.addFunc("moveTo", &Warship::moveTo);
    cls.addFunc("stop", &Warship::stop);
    cls.addFunc("setInput", &Warship::setInput);
    cls.addFunc("update", &Warship::update);

    cls.addFunc("getX", &Warship::getX);
    cls.addFunc("getY", &Warship::getY);
    cls.addFunc("getHeading", &Warship::getHeading);
    cls.addFunc("getSpeed", &Warship::getSpeed);
    cls.addFunc("getLength", &Warship::getLength);
    cls.addFunc("getBeam", &Warship::getBeam);
    cls.addFunc("getId", &Warship::getId);
    cls.addFunc("getDefId", &Warship::getDefId);
    cls.addFunc("getCategory", &Warship::getCategory);
    cls.addFunc("getFaction", &Warship::getFaction);
    cls.addFunc("getHealth", &Warship::getHealth);
    cls.addFunc("getMaxHealth", &Warship::getMaxHealth);
    cls.addFunc("isArrived", &Warship::isArrived);
    cls.addFunc("isDestroyed", &Warship::isDestroyed);
    cls.addFunc("applyDamage", &Warship::applyDamage);

    cls.addFunc("clearEvents", &Warship::clearEvents);
    cls.addFunc("getEventCount", &Warship::getEventCount);
    cls.addFunc("getEventType", &Warship::getEventType);
    cls.addFunc("getEventWarshipId", &Warship::getEventWarshipId);
    cls.addFunc("getEventDefId", &Warship::getEventDefId);
    cls.addFunc("getEventDetail", &Warship::getEventDetail);
}

}  // namespace eve::warship
