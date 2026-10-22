#pragma once
namespace eve::physics {
class Body3D;
/** @brief CameraSphereHit3D public API. */
struct CameraSphereHit3D {
    bool  hit      = false;
    int   bodyId   = -1;
    float fraction = 1.f;
    float x = 0.f, y = 0.f, z = 0.f;
    float nx = 0.f, ny = 0.f, nz = 0.f;
};

/**
 * @brief Result of a 3D point probe: deepest non-sensor shape within radius.
 * Normal points from the shape toward the probed point (meters).
 * @ownership Compatibility projection: body is a synchronous observer, never an owning pointer.
 * @lifetime Consume on the simulation thread before destroying a body/world or restoring topology.
 * Cross-frame consumers retain a PhysicsBodyHandle and resolve it through the world instead.
 */
struct ClothContact3D {
    bool    hit         = false;
    float   nx          = 0.f;
    float   ny          = 0.f;
    float   nz          = 0.f;
    float   depth       = 0.f;  // radius - distance (meters); > 0 when inside
    Body3D *body        = nullptr;
    float   friction    = 0.f;
    float   restitution = 0.f;
};

/** @brief Named outcome for a filtered cloth particle probe. */
enum class ClothProbeStatus { Miss, Hit };


}  // namespace eve::physics
