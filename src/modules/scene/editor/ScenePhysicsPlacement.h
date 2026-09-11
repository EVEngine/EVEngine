#pragma once

#include "editing/EditingResult.h"
#include "editor/EditorSimulationPreview.h"
#include "scene/editing/SceneEditingCommands.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace eve::scene_editor {

/** @brief Interaction policy used by one isolated placement candidate. */
enum class PhysicsPlacementMode { Place, Rotate, Align, Fall, Point, Drag = Place, Drop = Fall };

/** @brief Collision approximation admitted into the private preview world. */
enum class PhysicsPlacementShape { Box, Sphere, Capsule, ConvexHull, Auto };

/** @brief Provenance used when selecting preview colliders. */
enum class PhysicsPlacementColliderSource { Existing, Generated };

/** @brief Policy deciding which admitted collider sources participate in preview. */
enum class PhysicsPlacementColliderPolicy { ExistingOnly, GeneratedWhenMissing, GeneratedOnly, ExistingAndGenerated };

/** @brief One local-space shape in a possibly compound placement collider. */
struct PhysicsPlacementCollider {
    PhysicsPlacementShape          shape          = PhysicsPlacementShape::Box;
    PhysicsPlacementColliderSource source         = PhysicsPlacementColliderSource::Generated;
    double                         halfExtentX    = 0.5;
    double                         halfExtentY    = 0.5;
    double                         halfExtentZ    = 0.5;
    double                         localX         = 0.0;
    double                         localY         = 0.0;
    double                         localZ         = 0.0;
    double                         localRotationX = 0.0;
    double                         localRotationY = 0.0;
    double                         localRotationZ = 0.0;
    std::vector<float>             convexVertices;
    int                            convexMaxVertices = 64;
};

/** @brief Scene object and collision approximation admitted into an isolated placement preview. */
struct PhysicsPlacementObject {
    editing::ObjectId                     object;
    scene_editing::SceneTransformValue    transform;
    double                                halfExtentX = 0.5;
    double                                halfExtentY = 0.5;
    double                                halfExtentZ = 0.5;
    bool                                  selected    = false;
    PhysicsPlacementShape                 shape       = PhysicsPlacementShape::Box;
    std::vector<float>                    convexVertices;
    int                                   convexMaxVertices = 64;
    std::vector<PhysicsPlacementCollider> colliders;
    std::uint64_t                         layerBits = 1;
    std::vector<std::string>              tags;
    bool                                  enabled = true;
    bool                                  locked  = false;
    std::string                           colliderResourceKey;
};

/** @brief Host-supplied filters used before objects enter the private preview world. */
struct PhysicsPlacementAdmissionPolicy {
    std::uint64_t            includedLayerBits = ~std::uint64_t{0};
    std::uint64_t            excludedLayerBits = 0;
    std::vector<std::string> requiredTags;
    bool                     excludeDisabled = true;
    bool                     excludeLocked   = true;
    bool                     useBounds       = false;
    double                   minimumX        = 0.0;
    double                   minimumY        = 0.0;
    double                   minimumZ        = 0.0;
    double                   maximumX        = 0.0;
    double                   maximumY        = 0.0;
    double                   maximumZ        = 0.0;
};

/** @brief Deterministic policy for collision-aware scene placement. */
struct PhysicsPlacementSettings {
    PhysicsPlacementMode           mode                     = PhysicsPlacementMode::Drag;
    double                         gravityX                 = 0.0;
    double                         gravityY                 = -9.8;
    double                         gravityZ                 = 0.0;
    double                         maximumLinearSpeed       = 12.0;
    double                         linearDamping            = 0.0;
    double                         angularDamping           = 0.0;
    double                         contactHertz             = 30.0;
    double                         contactDampingRatio      = 10.0;
    double                         maximumPushOutSpeed      = 3.0;
    int                            subStepCount             = 4;
    bool                           preserveSelectionLayout  = true;
    PhysicsPlacementColliderPolicy colliderPolicy           = PhysicsPlacementColliderPolicy::GeneratedWhenMissing;
    bool                           freezePositionX          = false;
    bool                           freezePositionY          = false;
    bool                           freezePositionZ          = false;
    bool                           freezeRotationX          = false;
    bool                           freezeRotationY          = false;
    bool                           freezeRotationZ          = false;
    double                         maximumAngularSpeed      = 20.0;
    bool                           scaleSpeedByBounds       = true;
    double                         minimumBoundsSpeedFactor = 0.5;
    double                         maximumBoundsSpeedFactor = 2.0;
    double                         teleportDistance         = 0.0;
    bool                           softCollision            = true;
};

/** @brief Owning request used to construct one isolated placement candidate. */
struct PhysicsPlacementRequest {
    editing::Revision                   sourceRevision = 0;
    editing::ObjectId                   primaryObject;
    std::vector<PhysicsPlacementObject> objects;
    PhysicsPlacementSettings            settings;
    PhysicsPlacementAdmissionPolicy     admission;
};

/** @brief Captured placement state suitable for drawing and final transaction planning. */
struct PhysicsPlacementFrame {
    std::uint64_t                       tick           = 0;
    bool                                colliding      = false;
    bool                                settled        = false;
    bool                                surfaceAligned = false;
    double                              surfaceNormalX = 0.0;
    double                              surfaceNormalY = 1.0;
    double                              surfaceNormalZ = 0.0;
    std::vector<PhysicsPlacementObject> objects;
};

/**
 * @brief Real Box3D-backed, non-destructive simulation backend for scene placement.
 *
 * The backend owns a private physics world and wrapper objects. Scene state is
 * copied at admission and is never mutated by stepping. Selected objects are
 * driven toward a shared handle while collision resolution remains authoritative
 * inside the preview. The class is owner-thread-only and invokes no callbacks.
 */
class ScenePhysicsPlacementBackend final : public editor::IEditorSimulationBackend {
public:
    ~ScenePhysicsPlacementBackend() override;
    ScenePhysicsPlacementBackend(const ScenePhysicsPlacementBackend&)            = delete;
    ScenePhysicsPlacementBackend& operator=(const ScenePhysicsPlacementBackend&) = delete;

    /** @brief Validate and build an isolated placement world. */
    [[nodiscard]] static editing::Result<std::unique_ptr<ScenePhysicsPlacementBackend>> create(
        PhysicsPlacementRequest request);

    /** @brief Rebuild an independent preview with the same admitted scene and handle. */
    [[nodiscard]] std::unique_ptr<editor::IEditorSimulationBackend> cloneForPreview() const override;
    /** @brief Advance exactly one increasing fixed tick. */
    [[nodiscard]] editing::Result<void> step(std::uint64_t tick, double fixedDelta) override;
    /** @brief Capture stable selected-object transforms for generic preview tooling. */
    [[nodiscard]] editing::Result<std::vector<editor::SimulationObjectSample>> capture() const override;

    /** @brief Move the shared world-space placement handle without advancing simulation. */
    [[nodiscard]] editing::Result<void> setHandlePosition(double x, double y, double z);
    /** @brief Rotate the shared placement handle using XYZ Euler radians without advancing simulation. */
    [[nodiscard]] editing::Result<void> setHandleRotation(double x, double y, double z);
    /** @brief Scale the shared placement handle, resizing selected colliders without advancing simulation. */
    [[nodiscard]] editing::Result<void> setHandleScale(double x, double y, double z);
    /** @brief Align selection up to the nearest non-selected ray hit and place it outside the surface. */
    [[nodiscard]] editing::Result<void> alignHandleToSurface(double fromX, double fromY, double fromZ, double toX,
                                                             double toY, double toZ, double offset = 0.0);
    /** @brief Capture all admitted objects, retaining bounds and selection metadata. */
    [[nodiscard]] editing::Result<PhysicsPlacementFrame> placementFrame() const;
    /** @brief Revision copied from the scene when this candidate was admitted. */
    [[nodiscard]] editing::Revision sourceRevision() const noexcept;

private:
    struct Impl;
    explicit ScenePhysicsPlacementBackend(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace eve::scene_editor
