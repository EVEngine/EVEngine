#pragma once
#include "common/Export.h"

#include "common/Result.h"
#include "warship/WarshipTypes.h"

#include <string>
#include <vector>

namespace eve::warship {

/**
 * @brief Behavior for WarshipEntity: layout projection, mobility, orders.
 *
 * System scope: every live WarshipEntity.
 * View: WarshipEntity components Identity/Definition/Input/Motion/Health/
 *       Hull/Layout/Capabilities/Flags.
 * Reads: Layout cells + function catalogue; Input; Motion targets.
 * Writes: Motion, Capabilities, Flags, event queue.
 * Structural mutations: none while iterating (destroy deferred to callers).
 * Phase: simulation fixed-step (`Warship::update`).
 */
class EVENGINE_API_DOMAINS WarshipSystem {
public:
    /** @brief Register a mobility plugin (same name replaces). Takes ownership. */
    static void registerMobility(IWarshipMobility* mobility);
    /** @brief Number of registered mobility implementations (includes builtins). */
    [[nodiscard]] static int mobilityCount();
    /** @brief Ensure built-in surface/sail/carrier/space models are registered. */
    static void ensureBuiltinMobility();

    /**
     * @brief Recompute Capabilities from Layout + function catalogue.
     * @cost Linear in cell count; amortize across layout edits, not per pixel.
     */
    static void recomputeCapabilities(WarshipEntity& ship);

    /**
     * @brief Place or replace a function in one cell.
     * @cost One cell write + full capability recompute.
     */
    [[nodiscard]] static Result<void> placeCompartment(WarshipEntity& ship, int deck, int row, int col,
                                                       const std::string& functionId);

    /** @brief Vacate one cell. */
    [[nodiscard]] static Result<void> clearCompartment(WarshipEntity& ship, int deck, int row, int col);

    /** @brief Swap two cells atomically. */
    [[nodiscard]] static Result<void> swapCompartments(WarshipEntity& ship, int deckA, int rowA, int colA, int deckB,
                                                       int rowB, int colB);

    /**
     * @brief Replace the entire layout from a JSON array of {deck,row,col,function}.
     * @remarks Failure leaves the previous layout untouched.
     * @cost Parses JSON then rebuilds every cell; amortize across remodel actions.
     */
    [[nodiscard]] static Result<void> rearrangeFromJson(WarshipEntity& ship, const std::string& json);

    /** @brief Issue a move order toward (x, y). */
    static void moveTo(WarshipEntity& ship, float x, float y, float arriveRadius = 24.f);
    /** @brief Cancel move order and zero helm input. */
    static void stop(WarshipEntity& ship);

    /** @brief Apply damage; may mark destroyed and push events. */
    static void applyDamage(WarshipEntity& ship, float amount, const std::string& sourceId = {});

    /**
     * @brief Advance all warships one step.
     * @param dt Seconds; non-finite or negative is ignored.
     */
    static void update(float dt);

    static void              clearEvents();
    [[nodiscard]] static int eventCount();
    /**
     * @brief Borrow one buffered event by index.
     * @return Borrowed pointer into the module event queue, or nullptr when out of range.
     * @ownership Borrowed; WarshipSystem owns the queue storage. Callers must not free it.
     * @lifetime Valid until the next `clearEvents` / `pushEvent` / `update` that mutates the queue.
     * @thread Owner / composition thread only.
     * @reentrancy Does not invoke callbacks.
     */
    [[nodiscard]] static const WarshipEvent* eventAt(int index);
    static void                              pushEvent(WarshipEvent event);

private:
    static std::vector<WarshipEvent>& events();
};

}  // namespace eve::warship
