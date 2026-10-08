#pragma once
#include "common/Export.h"

#include "common/Result.h"
#include "gpuagents/EffectBackend.h"
#include "gpuagents/EnvironmentSnapshot.h"
#include "gpuagents/ObstacleField.h"
#include "gpuagents/SurfaceField.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::gpuagents {

/**
 * @brief World-level registry for GPU agent simulations and environment semantics.
 *
 * Analogous to a World Subsystem: owns obstacle/surface fields, registers named
 * EffectBackends that stay caller-owned, and prepares per-tick EnvironmentSnapshot
 * values for every registered simulation.
 *
 * @ownership Backends are borrowed; the caller / script VM retains ownership for
 *            the full World registration lifetime.
 * @lifetime World must not outlive registered backends; unregister before destroy.
 */
class EVENGINE_API_DOMAINS GpuAgentWorld {
public:
    GpuAgentWorld() = default;

    /** @brief Access the shared obstacle field (mutable for baking). */
    ObstacleField& obstacles() { return obstacles_; }
    /** @brief Const obstacle field. */
    const ObstacleField& obstacles() const { return obstacles_; }

    /** @brief Shared surface field for Life Network effects. */
    SurfaceField& surface() { return surface_; }
    /** @brief Const surface field. */
    const SurfaceField& surface() const { return surface_; }

    /** @brief Set uniform water current. */
    void setWaterCurrent(float x, float y, float z) { waterCurrent_ = {x, y, z}; }
    /** @brief Set uniform wind. */
    void setWind(float x, float y, float z) { wind_ = {x, y, z}; }
    /** @brief Set ground / ceiling heights for bird clearance. */
    void setVerticalBounds(float groundY, float ceilingY) {
        groundY_  = groundY;
        ceilingY_ = ceilingY;
    }

    /** @brief Replace goal markers. */
    void setGoals(std::vector<EnvironmentMarker> goals) { goals_ = std::move(goals); }
    /** @brief Replace danger markers. */
    void setDangers(std::vector<EnvironmentMarker> dangers) { dangers_ = std::move(dangers); }
    /** @brief Replace nutrient markers. */
    void setNutrients(std::vector<EnvironmentMarker> nutrients) { nutrients_ = std::move(nutrients); }

    /**
     * @brief Register a borrowed backend under a unique name.
     * @param name Non-empty unique key.
     * @param backend Borrowed backend; must outlive the World registration.
     * @ownership Borrowed; caller retains ownership.
     * @lifetime `backend` must remain valid until unregisterBackend or World destruction.
     */
    [[nodiscard("check backend register")]] Result<void> registerBackend(std::string name, EffectBackend* backend);

    /**
     * @brief Remove a named backend registration without destroying the backend.
     * @ownership Does not take or release ownership of the backend instance.
     */
    [[nodiscard("check backend unregister")]] Result<void> unregisterBackend(const std::string& name);

    /**
     * @brief Look up a registered backend by name.
     * @ownership Borrowed; World does not own the returned pointer.
     * @lifetime Valid until unregisterBackend or World destruction; nullptr when missing.
     * @nullable Yes when the name is not registered.
     */
    [[nodiscard("check find result before use")]] EffectBackend* find(const std::string& name) const;

    /** @brief Number of registered backends. */
    int backendCount() const { return static_cast<int>(backends_.size()); }

    /**
     * @brief Build an environment snapshot for the current World state.
     * @param includeSurface When true, attaches the mutable surface pointer.
     * @ownership Snapshot borrows obstacle/surface pointers owned by this World.
     * @lifetime Valid only while this World and its fields remain alive for the step.
     */
    EnvironmentSnapshot makeSnapshot(bool includeSurface) const;

    /**
     * @brief Step every registered backend with a shared environment snapshot.
     * @param dt Frame delta seconds.
     */
    [[nodiscard("check world step")]] Result<void> stepAll(float dt);

private:
    ObstacleField obstacles_;
    SurfaceField  surface_;
    glm::vec3     waterCurrent_{0.f};
    glm::vec3     wind_{0.f};
    float         groundY_  = 0.f;
    float         ceilingY_ = 40.f;
    std::vector<EnvironmentMarker> goals_;
    std::vector<EnvironmentMarker> dangers_;
    std::vector<EnvironmentMarker> nutrients_;
    std::unordered_map<std::string, EffectBackend*> backends_;
};

}  // namespace eve::gpuagents
