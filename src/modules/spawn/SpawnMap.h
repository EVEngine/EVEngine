#pragma once

#include "spawn/SpawnDefinition.h"

#include <memory>
#include <variant>

namespace eve::spawn {
/** @brief World2D point, in explicit world units rather than map pixels. */
struct Point {
    double x = 0;
    double y = 0;
};
/** @brief Choose among explicit world-space candidate points. */
struct PointRegion {
    std::vector<Point> points;
};
/** @brief Uniform area sampling of a world-space disk. */
struct CircleRegion {
    Point  center;
    double radius = 1;
};
/** @brief Simple, non-degenerate world-space polygon with no repeated closing vertex. */
struct PolygonRegion {
    std::vector<Point> vertices;
};
/** @brief Closed region vocabulary for first-version World2D placement. */
using Region = std::variant<PointRegion, CircleRegion, PolygonRegion>;
/** @brief Activate when the map is attached. */
struct MapLoadedActivation {};
/** @brief External event name is resolved to a typed binding during attach. */
struct EventActivation {
    std::string event;
};
/** @brief Hysteresis distances measured from the region boundary. */
struct ProximityActivation {
    double enterDistance = 0;
    double exitDistance  = 0;
};
/** @brief Typed activation predicate. */
using Activation = std::variant<MapLoadedActivation, EventActivation, ProximityActivation>;
/** @brief Map-owned shared spawn count limit. */
struct BudgetDefinition {
    std::string   id;
    std::uint32_t alive = 1;
};
/** @brief Validated instance data; runtime population is owned separately by SpawnWorld. */
struct InstanceDefinition {
    std::string                             id;
    std::shared_ptr<const CompiledTemplate> revision;
    Region                                  region;
    Activation                              activation;
    std::size_t                             budgetIndex = 0;
};
/** @brief Synchronous immutable template source for the map compiler; never retained. */
class ITemplateResolver {
public:
    /** @brief Release the resolver interface. */
    virtual ~ITemplateResolver() = default;
    /** @brief Return an owning revision or a structured missing/stale definition error. */
    [[nodiscard]] virtual Result<CompiledTemplate> resolve(const DefinitionRef& reference) const = 0;
};
class EVENGINE_API_FOUNDATION CompiledMap;

/**
 * @brief Compile map sidecar instances, constrained overrides and shared budgets atomically.
 * @param source Borrowed untrusted owning tree; copied values are independent of source lifetime.
 * @param templates Borrowed immutable template source, called synchronously without locks.
 * @param units Borrowed validator for overridden template recompilation.
 * @return Owning validated map or a field-path diagnostic; never publishes partial map state.
 * @cost Linear in map data plus per-instance template validation, amortized per asset revision.
 * @thread Content owner thread; no mutable shared state; provider callbacks must not mutate input.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<CompiledMap> compileMap(const Value&                   source,
                                                                     const ITemplateResolver&       templates,
                                                                     const IUnitDefinitionResolver& units);

/** @brief Owning validated map data; only the map compiler can create it. */
class EVENGINE_API_FOUNDATION CompiledMap {
public:
    /** @brief Borrow map id until this compiled map is moved, assigned or destroyed. */
    [[nodiscard]] const std::string& id() const noexcept { return id_; }
    /** @brief Borrow immutable budgets for this compiled map's stable lifetime. */
    [[nodiscard]] const std::vector<BudgetDefinition>& budgets() const noexcept { return budgets_; }
    /** @brief Borrow immutable instances for this compiled map's stable lifetime. */
    [[nodiscard]] const std::vector<InstanceDefinition>& instances() const noexcept { return instances_; }
    /** @brief Borrow canonical asset text for this compiled map's stable lifetime. */
    [[nodiscard]] const std::string& canonicalJson() const noexcept { return canonicalJson_; }

private:
    friend Result<CompiledMap> compileMap(const Value&, const ITemplateResolver&, const IUnitDefinitionResolver&);
    CompiledMap(std::string id, std::vector<BudgetDefinition> budgets, std::vector<InstanceDefinition> instances,
                std::string canonicalJson)
        : id_(std::move(id)),
          budgets_(std::move(budgets)),
          instances_(std::move(instances)),
          canonicalJson_(std::move(canonicalJson)) {}
    std::string                     id_;
    std::vector<BudgetDefinition>   budgets_;
    std::vector<InstanceDefinition> instances_;
    std::string                     canonicalJson_;
};

}  // namespace eve::spawn
