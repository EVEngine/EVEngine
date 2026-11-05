#pragma once
#include "common/Export.h"

/**
 * @brief Warship domain data: hull templates, compartment functions, and the
 *        WarshipEntity short root.
 *
 * Analogous to vehicle, but the distinctive surface is a huge hull whose
 * internal compartment grid can be rearranged. Occupied cells project
 * thrust, power, hangar, weapons, crew, and sail capabilities. Behavior
 * lives in WarshipSystem; this header holds pure data.
 */

#include "common/ECS.h"

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace eve::warship {

/** @brief Built-in mobility model names used by hull definitions. */
inline constexpr const char* kMobilitySurface = "surface";
inline constexpr const char* kMobilitySail    = "sail";
inline constexpr const char* kMobilityCarrier = "carrier";
inline constexpr const char* kMobilitySpace   = "space";

/** @brief Documented hull category conventions (free string; not an enum). */
inline constexpr const char* kCategoryWarship  = "warship";
inline constexpr const char* kCategorySailship = "sailship";
inline constexpr const char* kCategoryCarrier  = "carrier";
inline constexpr const char* kCategoryStarship = "starship";

/** @brief One cell of a hull's compartment grid. */
struct CompartmentCell {
    /** @brief Registered compartment function id; empty means vacant. */
    std::string functionId;
};

/** @brief Rectangular compartment grid owned by a hull definition / entity. */
struct CompartmentGrid {
    int cols  = 1;
    int rows  = 1;
    int decks = 1;

    /** @brief Total cell count (cols * rows * decks). */
    [[nodiscard]] int cellCount() const noexcept { return cols * rows * decks; }

    /**
     * @brief Flat index for (deck, row, col).
     * @return Index in [0, cellCount), or -1 when out of range.
     */
    [[nodiscard]] int indexOf(int deck, int row, int col) const noexcept {
        if (deck < 0 || deck >= decks || row < 0 || row >= rows || col < 0 || col >= cols) return -1;
        return (deck * rows + row) * cols + col;
    }
};

/** @brief One default layout entry applied when a hull is spawned. */
struct LayoutSeed {
    int         deck = 0;
    int         row  = 0;
    int         col  = 0;
    std::string functionId;
};

/**
 * @brief Compartment function catalogue entry.
 *
 * Functions are the rearrangeable "modules" inside a hull: bridge, reactor,
 * hangar, weapon bay, quarters, cargo, engine, sail rig, shield generator, …
 */
struct CompartmentFunctionDef {
    std::string id;
    std::string displayName;
    /** @brief Power drawn from the ship net (positive = consumer). */
    float powerDraw = 0.f;
    /** @brief Power contributed to the ship net (reactors / generators). */
    float powerProvide = 0.f;
    /** @brief Additive thrust multiplier contribution (0.1 = +10%). */
    float thrustBonus = 0.f;
    /** @brief Hangar craft capacity contributed by this cell. */
    int hangarCapacity = 0;
    /** @brief Hardpoint / weapon-bay count contributed by this cell. */
    int weaponBayCount = 0;
    /** @brief Crew berths contributed by this cell. */
    int crewCapacity = 0;
    /** @brief Sail / wind capture contribution (sailships). */
    float                    sailPower = 0.f;
    std::vector<std::string> tags;
};

/** @brief Hull template registered through Warship::registerHullsFromJson. */
struct WarshipDefinition {
    std::string id;
    /** @brief Convention: warship / sailship / carrier / starship / custom. */
    std::string category = kCategoryWarship;
    /** @brief IWarshipMobility registry name; default surface. */
    std::string mobility = kMobilitySurface;
    /** @brief Hull length in world units (large relative to typical vehicles). */
    float length = 120.f;
    /** @brief Hull beam (width) in world units. */
    float                    beam      = 28.f;
    float                    maxSpeed  = 40.f;
    float                    accel     = 12.f;
    float                    turnRate  = 25.f;  // degrees / second
    float                    maxHealth = 1000.f;
    CompartmentGrid          grid;
    std::vector<LayoutSeed>  defaultLayout;
    std::vector<std::string> tags;
};

/** @brief Aggregated capabilities projected from the live compartment layout. */
struct WarshipCapabilities {
    float powerNet       = 0.f;
    float thrustMul      = 1.f;
    int   hangarCapacity = 0;
    int   weaponBayCount = 0;
    int   crewCapacity   = 0;
    float sailPower      = 0.f;
    /** @brief True when powerNet < 0 (underpowered; thrust is reduced). */
    bool underpowered = false;
};

/** @brief Frame-buffered warship events for script polling. */
enum class WarshipEventType : uint8_t {
    Arrived,
    LayoutChanged,
    Damaged,
    Destroyed,
    Underpowered,
};

/**
 * @brief Stable event type protocol name.
 * @return Borrowed non-null pointer to process-lifetime storage.
 * @ownership Not caller-owned; do not free.
 * @lifetime Valid for the process lifetime.
 * @thread Safe (immutable text).
 * @reentrancy Does not invoke callbacks.
 */
EVENGINE_API_DOMAINS const char* warshipEventTypeName(WarshipEventType type);

inline std::ostream& operator<<(std::ostream& stream, WarshipEventType type) {
    return stream << warshipEventTypeName(type);
}

/** @brief One warship event (frame cache). */
struct WarshipEvent {
    WarshipEventType type = WarshipEventType::Arrived;
    std::string      warshipId;
    std::string      defId;
    float            x = 0.f;
    float            y = 0.f;
    std::string      detail;
};

/** @brief Pluggable hull mobility (surface / sail / carrier / space / custom). */
class EVENGINE_API_DOMAINS IWarshipMobility {
public:
    virtual ~IWarshipMobility() = default;
    /**
     * @brief Registry key matching WarshipDefinition::mobility.
     * @return Borrowed non-null pointer to process-lifetime storage.
     * @ownership Not caller-owned; do not free.
     * @lifetime Valid for the process lifetime.
     * @thread Safe when the characters are immutable.
     * @reentrancy Does not invoke callbacks.
     */
    virtual const char* name() const = 0;
    /**
     * @brief Integrate motion for one simulation step.
     * @param ship Mutable warship entity (input + motion + capabilities).
     * @param dt Seconds; must be finite and >= 0.
     * @thread Owner ECS thread.
     * @reentrancy Must not mutate the ECS table structure.
     */
    virtual void update(class WarshipEntity& ship, float dt) = 0;
};

/**
 * @brief Warship short-root entity: huge hull + rearrangeable compartments.
 *
 * Orthogonal state lives in components. Do not hang universal gameplay roots
 * off this type; cross-domain weapon / fluids links belong to later adapters.
 */
class EVENGINE_API_DOMAINS WarshipEntity : public ecs::Entity {
public:
    ENTITY(WarshipEntity, ecs::Entity)

    void release() override { ecs::DestroyEntity(this); }

    /** @brief Stable instance id / template id / faction / category. */
    struct Identity {
        std::string id;
        std::string defId;
        std::string faction;
        std::string category;
    };

    /** @brief Borrowed definition pointer (registry owns the template). */
    struct Definition {
        const WarshipDefinition* def = nullptr;
    };

    /** @brief Per-frame helm input. */
    struct Input {
        float throttle = 0.f;  // -1..1
        float steer    = 0.f;  // -1..1
    };

    /** @brief 2D kinematic motion (world XY, heading in degrees). */
    struct Motion {
        float x            = 0.f;
        float y            = 0.f;
        float heading      = 0.f;
        float speed        = 0.f;
        bool  arrived      = false;
        float targetX      = 0.f;
        float targetY      = 0.f;
        bool  hasMove      = false;
        float arriveRadius = 24.f;
    };

    /** @brief Hull integrity. */
    struct Health {
        float hp    = 0.f;
        float maxHp = 0.f;
    };

    /** @brief Physical envelope (copied from definition at spawn). */
    struct Hull {
        float length = 0.f;
        float beam   = 0.f;
        int   decks  = 1;
    };

    /** @brief Live compartment occupancy; authoritative owner of layout cells. */
    struct Layout {
        CompartmentGrid              grid;
        std::vector<CompartmentCell> cells;
    };

    /** @brief Cached projection of Layout through the function catalogue. */
    struct Capabilities {
        WarshipCapabilities values;
        /** @brief Bumps when Layout changes so consumers can detect stale caches. */
        std::uint64_t revision = 0;
    };

    /** @brief Lifecycle flags. */
    struct Flags {
        bool destroyed = false;
    };

    COMPONENT(Identity, identity)
    COMPONENT(Definition, definition)
    COMPONENT(Input, input)
    COMPONENT(Motion, motion)
    COMPONENT(Health, health)
    COMPONENT(Hull, hull)
    COMPONENT(Layout, layout)
    COMPONENT(Capabilities, capabilities)
    COMPONENT(Flags, stateFlags)

    /**
     * @brief Allocate a WarshipEntity with every component installed.
     * @ownership ECS table owns the entity; callers must not delete it.
     * @lifetime Until ECS destroys the entity or world.
     * @thread Owner ECS thread.
     * @reentrancy Does not invoke user callbacks.
     */
    [[nodiscard]] static WarshipEntity* createWarship();
};

}  // namespace eve::warship
