#include "warship/WarshipSystem.h"

#include "common/Diagnostic.h"
#include "common/Value.h"
#include "common/ValueRead.h"
#include "warship/WarshipRegistry.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_map>
#include <utility>

namespace eve::warship {
namespace {

constexpr float kPi = 3.14159265358979323846f;

std::unordered_map<std::string, std::unique_ptr<IWarshipMobility>>& mobilityTable() {
    static std::unordered_map<std::string, std::unique_ptr<IWarshipMobility>> table;
    return table;
}

float normalizeHeading(float deg) {
    deg = std::fmod(deg, 360.f);
    if (deg < 0.f) deg += 360.f;
    return deg;
}

float shortestAngleDelta(float fromDeg, float toDeg) {
    float delta = toDeg - fromDeg;
    while (delta > 180.f) delta -= 360.f;
    while (delta < -180.f) delta += 360.f;
    return delta;
}

void integrateHelm(WarshipEntity& ship, float dt, float speedScale, float turnScale) {
    const WarshipDefinition* def = ship.definition()->def;
    if (def == nullptr) return;
    auto input  = ship.input();
    auto motion = ship.motion();
    auto caps   = ship.capabilities();

    float thrustMul = caps->values.thrustMul;
    if (caps->values.underpowered) thrustMul *= 0.55f;

    float maxSpeed = def->maxSpeed * speedScale * thrustMul;
    if (maxSpeed < 0.f) maxSpeed = 0.f;

    if (motion->hasMove) {
        const float dx   = motion->targetX - motion->x;
        const float dy   = motion->targetY - motion->y;
        const float dist = std::sqrt(dx * dx + dy * dy);
        if (dist <= motion->arriveRadius) {
            motion->hasMove = false;
            motion->arrived = true;
            input->throttle = 0.f;
            input->steer    = 0.f;
            motion->speed   = 0.f;
            WarshipEvent event;
            event.type      = WarshipEventType::Arrived;
            event.warshipId = ship.identity()->id;
            event.defId     = ship.identity()->defId;
            event.x         = motion->x;
            event.y         = motion->y;
            WarshipSystem::pushEvent(std::move(event));
        } else {
            const float desired = normalizeHeading(std::atan2(dy, dx) * 180.f / kPi);
            const float delta   = shortestAngleDelta(motion->heading, desired);
            input->steer        = std::clamp(delta / 45.f, -1.f, 1.f);
            input->throttle     = dist < motion->arriveRadius * 3.f ? 0.45f : 1.f;
            motion->arrived     = false;
        }
    }

    const float turn = def->turnRate * turnScale * input->steer * dt;
    motion->heading  = normalizeHeading(motion->heading + turn);

    float       targetSpeed = maxSpeed * std::clamp(input->throttle, -1.f, 1.f);
    const float accel       = def->accel * speedScale * (targetSpeed >= motion->speed ? 1.f : 1.5f);
    if (motion->speed < targetSpeed)
        motion->speed = std::min(motion->speed + accel * dt, targetSpeed);
    else
        motion->speed = std::max(motion->speed - accel * dt, targetSpeed);

    const float rad = motion->heading * kPi / 180.f;
    motion->x += std::cos(rad) * motion->speed * dt;
    motion->y += std::sin(rad) * motion->speed * dt;
}

class SurfaceMobility final : public IWarshipMobility {
public:
    const char* name() const override { return kMobilitySurface; }
    void        update(WarshipEntity& ship, float dt) override { integrateHelm(ship, dt, 1.f, 1.f); }
};

class SailMobility final : public IWarshipMobility {
public:
    const char* name() const override { return kMobilitySail; }
    void        update(WarshipEntity& ship, float dt) override {
        float sailScale = 0.7f;
        sailScale += std::clamp(ship.capabilities()->values.sailPower, 0.f, 1.5f) * 0.4f;
        integrateHelm(ship, dt, sailScale, 0.85f);
    }
};

class CarrierMobility final : public IWarshipMobility {
public:
    const char* name() const override { return kMobilityCarrier; }
    void        update(WarshipEntity& ship, float dt) override { integrateHelm(ship, dt, 0.75f, 0.55f); }
};

class SpaceMobility final : public IWarshipMobility {
public:
    const char* name() const override { return kMobilitySpace; }
    void        update(WarshipEntity& ship, float dt) override { integrateHelm(ship, dt, 1.35f, 1.1f); }
};

Result<void> validateCell(WarshipEntity& ship, int deck, int row, int col, int& outIndex) {
    auto layout = ship.layout();
    outIndex    = layout->grid.indexOf(deck, row, col);
    if (outIndex < 0 || outIndex >= static_cast<int>(layout->cells.size())) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "compartment cell out of range", "warship.layout"));
    }
    return Result<void>::success();
}

}  // namespace

const char* warshipEventTypeName(WarshipEventType type) {
    switch (type) {
        case WarshipEventType::Arrived: return "arrived";
        case WarshipEventType::LayoutChanged: return "layout_changed";
        case WarshipEventType::Damaged: return "damaged";
        case WarshipEventType::Destroyed: return "destroyed";
        case WarshipEventType::Underpowered: return "underpowered";
    }
    return "unknown";
}

WarshipEntity* WarshipEntity::createWarship() {
    WarshipEntity* ship = WarshipEntity::create();
    ship->identity();
    ship->definition();
    ship->input();
    ship->motion();
    ship->health();
    ship->hull();
    ship->layout();
    ship->capabilities();
    ship->stateFlags();
    return ship;
}

void WarshipSystem::registerMobility(IWarshipMobility* mobility) {
    if (mobility == nullptr) return;
    mobilityTable()[mobility->name()] = std::unique_ptr<IWarshipMobility>(mobility);
}

int WarshipSystem::mobilityCount() {
    ensureBuiltinMobility();
    return static_cast<int>(mobilityTable().size());
}

void WarshipSystem::ensureBuiltinMobility() {
    auto& table = mobilityTable();
    if (table.count(kMobilitySurface) == 0) table[kMobilitySurface] = std::make_unique<SurfaceMobility>();
    if (table.count(kMobilitySail) == 0) table[kMobilitySail] = std::make_unique<SailMobility>();
    if (table.count(kMobilityCarrier) == 0) table[kMobilityCarrier] = std::make_unique<CarrierMobility>();
    if (table.count(kMobilitySpace) == 0) table[kMobilitySpace] = std::make_unique<SpaceMobility>();
}

void WarshipSystem::recomputeCapabilities(WarshipEntity& ship) {
    auto layout = ship.layout();
    auto caps   = ship.capabilities();

    WarshipCapabilities values;
    values.thrustMul           = 1.f;
    const bool hadUnderpowered = caps->values.underpowered;

    for (const CompartmentCell& cell : layout->cells) {
        if (cell.functionId.empty()) continue;
        const CompartmentFunctionDef* fn = WarshipRegistry::findFunction(cell.functionId);
        if (fn == nullptr) continue;
        values.powerNet += fn->powerProvide - fn->powerDraw;
        values.thrustMul += fn->thrustBonus;
        values.hangarCapacity += fn->hangarCapacity;
        values.weaponBayCount += fn->weaponBayCount;
        values.crewCapacity += fn->crewCapacity;
        values.sailPower += fn->sailPower;
    }
    if (values.thrustMul < 0.05f) values.thrustMul = 0.05f;
    values.underpowered = values.powerNet < 0.f;
    caps->values        = values;
    ++caps->revision;

    if (values.underpowered && !hadUnderpowered) {
        WarshipEvent event;
        event.type      = WarshipEventType::Underpowered;
        event.warshipId = ship.identity()->id;
        event.defId     = ship.identity()->defId;
        event.detail    = "power_deficit";
        pushEvent(std::move(event));
    }
}

Result<void> WarshipSystem::placeCompartment(WarshipEntity& ship, int deck, int row, int col,
                                             const std::string& functionId) {
    if (functionId.empty() || !WarshipRegistry::hasFunction(functionId)) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "unknown compartment function", "warship.layout"));
    }
    int index = 0;
    if (auto ok = validateCell(ship, deck, row, col, index); !ok) return ok;
    ship.layout()->cells[static_cast<std::size_t>(index)].functionId = functionId;
    recomputeCapabilities(ship);
    WarshipEvent event;
    event.type      = WarshipEventType::LayoutChanged;
    event.warshipId = ship.identity()->id;
    event.defId     = ship.identity()->defId;
    event.detail    = functionId;
    pushEvent(std::move(event));
    return Result<void>::success();
}

Result<void> WarshipSystem::clearCompartment(WarshipEntity& ship, int deck, int row, int col) {
    int index = 0;
    if (auto ok = validateCell(ship, deck, row, col, index); !ok) return ok;
    ship.layout()->cells[static_cast<std::size_t>(index)].functionId.clear();
    recomputeCapabilities(ship);
    WarshipEvent event;
    event.type      = WarshipEventType::LayoutChanged;
    event.warshipId = ship.identity()->id;
    event.defId     = ship.identity()->defId;
    event.detail    = "cleared";
    pushEvent(std::move(event));
    return Result<void>::success();
}

Result<void> WarshipSystem::swapCompartments(WarshipEntity& ship, int deckA, int rowA, int colA, int deckB, int rowB,
                                             int colB) {
    int indexA = 0;
    int indexB = 0;
    if (auto ok = validateCell(ship, deckA, rowA, colA, indexA); !ok) return ok;
    if (auto ok = validateCell(ship, deckB, rowB, colB, indexB); !ok) return ok;
    auto& cells = ship.layout()->cells;
    std::swap(cells[static_cast<std::size_t>(indexA)].functionId, cells[static_cast<std::size_t>(indexB)].functionId);
    recomputeCapabilities(ship);
    WarshipEvent event;
    event.type      = WarshipEventType::LayoutChanged;
    event.warshipId = ship.identity()->id;
    event.defId     = ship.identity()->defId;
    event.detail    = "swapped";
    pushEvent(std::move(event));
    return Result<void>::success();
}

Result<void> WarshipSystem::rearrangeFromJson(WarshipEntity& ship, const std::string& json) {
    auto layout = ship.layout();
    auto parsed = Value::fromJson(json);
    if (!parsed) return Result<void>::failure(parsed.status());
    const auto* array = parsed.value().getIf<Value::Array>();
    if (array == nullptr) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "layout JSON must be an array", "warship.layout"));
    }

    std::vector<CompartmentCell> next(layout->cells.size());
    for (const Value& entry : *array) {
        const auto* object = entry.getIf<Value::Object>();
        if (object == nullptr) {
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "layout entries must be objects", "warship.layout"));
        }
        auto readCoord = [&](const char* key) -> Result<int> {
            auto it = object->find(key);
            if (it == object->end()) {
                return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                              "layout entry missing coordinate", "warship.layout"));
            }
            return readIntValue(it->second);
        };
        auto deckR = readCoord("deck");
        if (!deckR) return Result<void>::failure(deckR.status());
        auto rowR = readCoord("row");
        if (!rowR) return Result<void>::failure(rowR.status());
        auto colR = readCoord("col");
        if (!colR) return Result<void>::failure(colR.status());

        std::string functionKey = "function";
        if (object->find("function") == object->end()) functionKey = "functionId";
        auto functionId = readStringMember(*object, functionKey);
        if (!functionId) return Result<void>::failure(functionId.status());
        if (functionId.value().empty() || !WarshipRegistry::hasFunction(functionId.value())) {
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "unknown compartment function", "warship.layout"));
        }
        const int index = layout->grid.indexOf(deckR.value(), rowR.value(), colR.value());
        if (index < 0 || index >= static_cast<int>(next.size())) {
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "layout cell out of range", "warship.layout"));
        }
        if (!next[static_cast<std::size_t>(index)].functionId.empty()) {
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "duplicate layout cell", "warship.layout"));
        }
        next[static_cast<std::size_t>(index)].functionId = std::move(functionId).takeValue();
    }

    layout->cells = std::move(next);
    recomputeCapabilities(ship);
    WarshipEvent event;
    event.type      = WarshipEventType::LayoutChanged;
    event.warshipId = ship.identity()->id;
    event.defId     = ship.identity()->defId;
    event.detail    = "rearranged";
    pushEvent(std::move(event));
    return Result<void>::success();
}

void WarshipSystem::moveTo(WarshipEntity& ship, float x, float y, float arriveRadius) {
    auto motion          = ship.motion();
    motion->targetX      = x;
    motion->targetY      = y;
    motion->arriveRadius = arriveRadius > 0.f ? arriveRadius : 24.f;
    motion->hasMove      = true;
    motion->arrived      = false;
}

void WarshipSystem::stop(WarshipEntity& ship) {
    auto motion     = ship.motion();
    motion->hasMove = false;
    motion->arrived = true;
    motion->speed   = 0.f;
    auto input      = ship.input();
    input->throttle = 0.f;
    input->steer    = 0.f;
}

void WarshipSystem::applyDamage(WarshipEntity& ship, float amount, const std::string& /*sourceId*/) {
    auto health = ship.health();
    auto flags  = ship.stateFlags();
    if (flags->destroyed) return;
    if (amount < 0.f) amount = 0.f;
    health->hp = std::max(0.f, health->hp - amount);
    WarshipEvent damaged;
    damaged.type      = WarshipEventType::Damaged;
    damaged.warshipId = ship.identity()->id;
    damaged.defId     = ship.identity()->defId;
    pushEvent(std::move(damaged));
    if (health->hp <= 0.f) {
        flags->destroyed = true;
        stop(ship);
        WarshipEvent destroyed;
        destroyed.type      = WarshipEventType::Destroyed;
        destroyed.warshipId = ship.identity()->id;
        destroyed.defId     = ship.identity()->defId;
        pushEvent(std::move(destroyed));
    }
}

void WarshipSystem::update(float dt) {
    if (!std::isfinite(dt) || dt < 0.f) return;
    ensureBuiltinMobility();
    ecs::Table* table = ecs::current();
    if (table == nullptr) return;

    ecs::IComponentManager&             cm  = table->getOrCreateManager<WarshipEntity>();
    auto*                               reg = cm.getOrCreateRegistryComponentBuffer<WarshipEntity>();
    std::vector<ecs::IComponentBuffer*> stack;
    stack.push_back(reg);
    while (!stack.empty()) {
        ecs::IComponentBuffer* buf = stack.back();
        stack.pop_back();
        auto* r = dynamic_cast<ecs::IRegistryComponentBuffer*>(buf);
        if (r != nullptr) {
            for (uint32_t i = 0; i < r->entity_count(); ++i) {
                ecs::Entity* ent = r->entity_at(i);
                if (ent == nullptr || !ecs::is_entity_visible(ent)) continue;
                auto* ship = static_cast<WarshipEntity*>(ent);
                if (ship->stateFlags()->destroyed) continue;
                const WarshipDefinition* def = ship->definition()->def;
                if (def == nullptr) continue;
                auto it = mobilityTable().find(def->mobility);
                if (it == mobilityTable().end() || !it->second) {
                    it = mobilityTable().find(kMobilitySurface);
                }
                if (it != mobilityTable().end() && it->second) it->second->update(*ship, dt);
            }
        }
        if (buf->children != nullptr) stack.push_back(buf->children);
        if (buf->next != nullptr) stack.push_back(buf->next);
    }
}

std::vector<WarshipEvent>& WarshipSystem::events() {
    static std::vector<WarshipEvent> queue;
    return queue;
}

void WarshipSystem::clearEvents() { events().clear(); }

int WarshipSystem::eventCount() { return static_cast<int>(events().size()); }

const WarshipEvent* WarshipSystem::eventAt(int index) {
    if (index < 0 || index >= eventCount()) return nullptr;
    return &events()[static_cast<std::size_t>(index)];
}

void WarshipSystem::pushEvent(WarshipEvent event) { events().push_back(std::move(event)); }

}  // namespace eve::warship
