#pragma once
#include "common/Export.h"

/**
 * @brief Warship module entry (`eve.Warship`): hull/function catalogues, spawn,
 *        compartment remodel, helm orders, and frame update.
 *
 * Design note: `docs/dev/通用战舰系统设计.md`
 */

#include "common/Module.h"
#include "common/Result.h"
#include "warship/WarshipTypes.h"

#include <string>

namespace eve::warship {

/** @brief Warship module (`eve.Warship`). */
class EVENGINE_API_DOMAINS Warship : public Module {
public:
    Module_REG(Warship);
    Warship()           = default;
    ~Warship() override = default;

    /** @brief Register hull templates from a JSON object or array. */
    [[nodiscard]] Result<int> registerHullsFromJson(const std::string& json);
    /** @brief Register compartment function catalogue entries from JSON. */
    [[nodiscard]] Result<int> registerFunctionsFromJson(const std::string& json);
    void                      clearHullDefinitions();
    void                      clearFunctionDefinitions();
    [[nodiscard]] bool        hasHullDefinition(const std::string& id) const;
    [[nodiscard]] bool        hasFunctionDefinition(const std::string& id) const;
    [[nodiscard]] int         getHullDefinitionCount() const;
    [[nodiscard]] int         getFunctionDefinitionCount() const;
    [[nodiscard]] std::string getHullDefinitionCategory(const std::string& id) const;
    [[nodiscard]] std::string getHullDefinitionMobility(const std::string& id) const;

    /** @brief Register a custom mobility plugin (ownership transferred). */
    static void              registerMobility(IWarshipMobility* mobility);
    [[nodiscard]] static int getMobilityCount();

    /**
     * @brief Spawn a hull from a registered definition.
     * @ownership ECS owns the entity; callers must not delete it.
     * @lifetime Until ECS destroy / world teardown.
     * @thread Owner ECS thread.
     * @cost Allocates entity + grid cells; amortize across spawn actions.
     */
    [[nodiscard]] Result<WarshipEntity*> newWarship(const std::string& defId, float x, float y, float heading = 0.f,
                                                    const std::string& faction = "");

    [[nodiscard]] Result<void> placeCompartment(WarshipEntity* ship, int deck, int row, int col,
                                                const std::string& functionId);
    [[nodiscard]] Result<void> clearCompartment(WarshipEntity* ship, int deck, int row, int col);
    [[nodiscard]] Result<void> swapCompartments(WarshipEntity* ship, int deckA, int rowA, int colA, int deckB, int rowB,
                                                int colB);
    [[nodiscard]] Result<void> rearrangeFromJson(WarshipEntity* ship, const std::string& json);

    [[nodiscard]] int         getCompartmentCount(WarshipEntity* ship) const;
    [[nodiscard]] int         getGridCols(WarshipEntity* ship) const;
    [[nodiscard]] int         getGridRows(WarshipEntity* ship) const;
    [[nodiscard]] int         getGridDecks(WarshipEntity* ship) const;
    [[nodiscard]] std::string getCompartmentFunction(WarshipEntity* ship, int deck, int row, int col) const;
    [[nodiscard]] float       getPowerNet(WarshipEntity* ship) const;
    [[nodiscard]] float       getThrustMul(WarshipEntity* ship) const;
    [[nodiscard]] int         getHangarCapacity(WarshipEntity* ship) const;
    [[nodiscard]] int         getWeaponBayCount(WarshipEntity* ship) const;
    [[nodiscard]] int         getCrewCapacity(WarshipEntity* ship) const;
    [[nodiscard]] float       getSailPower(WarshipEntity* ship) const;
    [[nodiscard]] bool        isUnderpowered(WarshipEntity* ship) const;

    void moveTo(WarshipEntity* ship, float x, float y);
    void stop(WarshipEntity* ship);
    void setInput(WarshipEntity* ship, float throttle, float steer);
    void update(float dt);

    [[nodiscard]] float       getX(WarshipEntity* ship) const;
    [[nodiscard]] float       getY(WarshipEntity* ship) const;
    [[nodiscard]] float       getHeading(WarshipEntity* ship) const;
    [[nodiscard]] float       getSpeed(WarshipEntity* ship) const;
    [[nodiscard]] float       getLength(WarshipEntity* ship) const;
    [[nodiscard]] float       getBeam(WarshipEntity* ship) const;
    [[nodiscard]] std::string getId(WarshipEntity* ship) const;
    [[nodiscard]] std::string getDefId(WarshipEntity* ship) const;
    [[nodiscard]] std::string getCategory(WarshipEntity* ship) const;
    [[nodiscard]] std::string getFaction(WarshipEntity* ship) const;
    [[nodiscard]] float       getHealth(WarshipEntity* ship) const;
    [[nodiscard]] float       getMaxHealth(WarshipEntity* ship) const;
    [[nodiscard]] bool        isArrived(WarshipEntity* ship) const;
    [[nodiscard]] bool        isDestroyed(WarshipEntity* ship) const;
    void                      applyDamage(WarshipEntity* ship, float amount);

    void                      clearEvents();
    [[nodiscard]] int         getEventCount() const;
    [[nodiscard]] std::string getEventType(int index) const;
    [[nodiscard]] std::string getEventWarshipId(int index) const;
    [[nodiscard]] std::string getEventDefId(int index) const;
    [[nodiscard]] std::string getEventDetail(int index) const;
};

}  // namespace eve::warship
