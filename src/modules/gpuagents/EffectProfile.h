#pragma once
#include "common/Export.h"

#include "gpuagents/AgentState.h"

#include <glm/glm.hpp>

namespace eve::gpuagents {

/** @brief Shared capacity / timing / collision envelope for every effect. */
struct EVENGINE_API_DOMAINS EffectProfileBase {
    EffectKind kind         = EffectKind::Fish;
    int        maxAgents    = 256;
    float      fixedDt      = 1.f / 60.f;
    float      agentRadius  = 0.25f;
    float      obstaclePredictTime = 0.35f;
    std::uint32_t seed      = 1u;
};

/** @brief 3D Boids fish school parameters. */
struct EVENGINE_API_DOMAINS FishProfile {
    EffectProfileBase base{};

    float separationRadius = 1.5f;
    float cohesionRadius   = 4.0f;
    float alignmentRadius  = 3.0f;
    float perceptionFovDegrees = 270.f;
    int   maxNeighborSamples   = 24;

    float separationWeight = 1.4f;
    float cohesionWeight   = 0.8f;
    float alignmentWeight  = 1.0f;
    float goalWeight       = 0.6f;
    float dangerWeight     = 1.8f;
    float currentWeight    = 1.0f;
    float depthWeight      = 0.5f;

    float maxSpeed          = 4.0f;
    float maxAcceleration   = 12.0f;
    float maxHorizontalTurn = 4.0f;  ///< rad/s
    float maxVerticalTurn   = 2.5f;  ///< rad/s
    float preferredDepth    = 0.0f;
    float depthTolerance    = 2.0f;
};

/** @brief Life Network 2D surface-trail parameters. */
struct EVENGINE_API_DOMAINS LifeNetworkProfile {
    EffectProfileBase base{};

    int   fieldResolution = 64;
    float worldSize       = 32.f;
    glm::vec3 origin{-16.f, 0.f, -16.f};

    float sensorDistance = 1.5f;
    float sensorAngle    = 0.6f;  ///< radians left/right of heading
    float trailFollow    = 1.2f;
    float nutrient       = 0.4f;
    float repulsion      = 0.8f;
    float danger         = 1.5f;

    float depositStrength   = 0.35f;
    float trailDecayRate    = 0.15f;
    float freshnessHalfLife = 4.0f;
    float diffusionRate     = 0.08f;
    float moveSpeed         = 1.8f;
    float minSurfaceNormalZ = 0.35f;
};

/** @brief Bird flock flight-dynamics parameters. */
struct EVENGINE_API_DOMAINS BirdProfile {
    EffectProfileBase base{};

    float separationRadius = 2.0f;
    float cohesionRadius   = 6.0f;
    float alignmentRadius  = 5.0f;
    float perceptionFovDegrees = 300.f;
    int   maxNeighborSamples   = 20;

    float separationWeight = 1.2f;
    float cohesionWeight   = 0.7f;
    float alignmentWeight  = 1.1f;
    float goalWeight       = 0.5f;

    float stallSpeed   = 2.5f;
    float cruiseSpeed  = 6.0f;
    float maxSpeed     = 10.0f;
    float liftCoefficient = 1.4f;
    float gravityScale    = 1.0f;
    float airDrag         = 0.35f;

    float maxClimbSpeed   = 3.0f;
    float maxDescentSpeed = 5.0f;
    float maxTurnRate     = 2.8f;
    float maxBankAngle    = 0.85f;

    float timeToCollision   = 0.8f;
    float groundClearance   = 1.5f;
    float ceilingClearance  = 2.0f;
    float windResponseTime  = 0.4f;
    float gustAcceleration  = 2.0f;
    glm::vec3 uniformWindLocal{0.f, 0.f, 0.f};
};

/** @brief Passive petal rigid-body aerodynamic parameters. */
struct EVENGINE_API_DOMAINS PetalProfile {
    EffectProfileBase base{};

    float length = 0.12f;
    float width  = 0.06f;
    float thickness = 0.004f;
    float mass   = 0.002f;
    float sizeVariation = 0.25f;

    float frontalDrag = 1.8f;
    float edgeDrag    = 0.35f;
    float liftCoeff   = 0.55f;
    float angularDrag = 0.9f;
    float pressureCenterOffset = 0.15f;

    float lifetime        = 12.f;
    float settleSpeed     = 0.15f;
    float softBoundPadding = 1.0f;
    float groundY         = 0.0f;
    float gravity         = 9.8f;
    glm::vec3 worldMin{-20.f, -1.f, -20.f};
    glm::vec3 worldMax{20.f, 30.f, 20.f};
};

/** @brief Type-erased profile bundle owned by an EffectBackend. */
struct EVENGINE_API_DOMAINS EffectProfile {
    EffectKind kind = EffectKind::Fish;
    FishProfile        fish{};
    LifeNetworkProfile life{};
    BirdProfile        bird{};
    PetalProfile       petal{};

    /** @brief Returns the shared base slice for the active kind. */
    const EffectProfileBase& base() const {
        switch (kind) {
            case EffectKind::LifeNetwork: return life.base;
            case EffectKind::Bird:        return bird.base;
            case EffectKind::Petal:       return petal.base;
            case EffectKind::Fish:
            default:                      return fish.base;
        }
    }

    /** @brief Mutable shared base slice for the active kind. */
    EffectProfileBase& base() {
        switch (kind) {
            case EffectKind::LifeNetwork: return life.base;
            case EffectKind::Bird:        return bird.base;
            case EffectKind::Petal:       return petal.base;
            case EffectKind::Fish:
            default:                      return fish.base;
        }
    }

    /** @brief Factory for a default fish profile. */
    static EffectProfile makeFish(int maxAgents = 256) {
        EffectProfile p;
        p.kind            = EffectKind::Fish;
        p.fish.base.kind  = EffectKind::Fish;
        p.fish.base.maxAgents = maxAgents;
        return p;
    }

    /** @brief Factory for a default life-network profile. */
    static EffectProfile makeLife(int maxAgents = 128, int resolution = 64) {
        EffectProfile p;
        p.kind                 = EffectKind::LifeNetwork;
        p.life.base.kind       = EffectKind::LifeNetwork;
        p.life.base.maxAgents  = maxAgents;
        p.life.fieldResolution = resolution;
        return p;
    }

    /** @brief Factory for a default bird profile. */
    static EffectProfile makeBird(int maxAgents = 256) {
        EffectProfile p;
        p.kind            = EffectKind::Bird;
        p.bird.base.kind  = EffectKind::Bird;
        p.bird.base.maxAgents = maxAgents;
        return p;
    }

    /** @brief Factory for a default petal profile. */
    static EffectProfile makePetal(int maxAgents = 512) {
        EffectProfile p;
        p.kind             = EffectKind::Petal;
        p.petal.base.kind  = EffectKind::Petal;
        p.petal.base.maxAgents = maxAgents;
        return p;
    }
};

}  // namespace eve::gpuagents
