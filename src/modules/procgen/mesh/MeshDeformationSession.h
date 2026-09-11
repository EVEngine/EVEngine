#pragma once

#include "common/Result.h"
#include "procgen/MeshBuild.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace eve::procgen {

/** @brief Value-only center of the current runtime vertex selection. */
struct MeshVertexSelectionCenter {
    float x = 0.f, y = 0.f, z = 0.f;
};

/**
 * @brief Stateful CPU sculpting session with explicit bake, restore, and undo semantics.
 *
 * The session owns original, current, and bounded undo mesh snapshots. Calls are synchronous,
 * graph-owning-thread only, and never retain caller memory. Failed edits preserve current state.
 */
class MeshDeformationSession {
public:
    /** @brief Replace the session with an owning copy of a valid source mesh. */
    [[nodiscard]] Result<void> initializeResult(const MeshBuild& mesh);
    /**
     * @brief Apply a localized brush to the current mesh.
     * @param mode inflate, dent, flatten, smooth, or directional.
     * @param x Brush center X. @param y Brush center Y. @param z Brush center Z.
     * @param radius Positive world-space radius.
     * @param strength Signed displacement scale.
     * @param falloff Positive radial exponent.
     * @param directionX Directional brush X. @param directionY Directional brush Y.
     * @param directionZ Directional brush Z.
     * @return Applied or a structured diagnostic; failure leaves the mesh unchanged.
     */
    [[nodiscard]] Result<void> applyBrushResult(std::string_view mode, float x, float y, float z, float radius,
                                                float strength, float falloff, float directionX = 0.f,
                                                float directionY = 1.f, float directionZ = 0.f);
    /**
     * @brief Apply one collision-neutral plastic impact to the current mesh.
     * @param x Impact center X. @param y Impact center Y. @param z Impact center Z.
     * @param impulseX World/local impulse X. @param impulseY Impulse Y. @param impulseZ Impulse Z.
     * @param radius Positive influence radius.
     * @param plasticity Non-negative impulse-to-displacement scale.
     * @param hardness Positive radial falloff exponent.
     * @param maxDisplacement Positive distance each vertex may move from the baked baseline.
     * @return Applied atomically, or a diagnostic without changing mesh or history.
     *
     * Physics adapters remain responsible for coordinate conversion and impulse filtering. This
     * session consumes plain values, retains no contact pointers, and is synchronous/thread-affine.
     */
    [[nodiscard]] Result<void> applyImpactResult(float x, float y, float z, float impulseX, float impulseY,
                                                 float impulseZ, float radius, float plasticity, float hardness,
                                                 float maxDisplacement);
    /** @brief Build a bounded uniform vertex-block index for repeated high-density impact queries. */
    [[nodiscard]] Result<void> prepareImpactVertexBlocksResult(int divisionsPerAxis);
    /** @brief Return the number of non-empty prepared impact blocks. */
    [[nodiscard]] int impactVertexBlockCount() const noexcept;
    /** @brief Report whether impact queries currently use a prepared vertex-block index. */
    [[nodiscard]] bool hasImpactVertexBlocks() const noexcept { return !impactVertexBlocks_.empty(); }
    /**
     * @brief Apply one Interactive Surface contact using caller-supplied local-space values.
     * @param x Contact X. @param y Contact Y. @param z Contact Z.
     * @param normalX Surface normal X. @param normalY Surface normal Y. @param normalZ Surface normal Z.
     * @param velocityX Contact velocity X. @param velocityY Contact velocity Y. @param velocityZ Contact velocity Z.
     * @param radius Positive influence radius. @param depth Non-negative indentation distance.
     * @param drag Non-negative tangential velocity scale. @param falloff Positive radial exponent.
     * @param plasticity Permanent displacement fraction in [0, 1].
     * @return Applied atomically, or a diagnostic preserving mesh, equilibrium state, and history.
     */
    [[nodiscard]] Result<void> applySurfaceContactResult(float x, float y, float z, float normalX, float normalY,
                                                         float normalZ, float velocityX, float velocityY,
                                                         float velocityZ, float radius, float depth, float drag,
                                                         float falloff, float plasticity = 0.f);
    /**
     * @brief Advance elastic recovery using explicit simulation time.
     * @param dt Non-negative injected step duration.
     * @param recoveryRate Positive exponential recovery rate.
     * @return Success without adding undo history; invalid input leaves state unchanged.
     */
    [[nodiscard]] Result<void> recoverSurfaceResult(float dt, float recoveryRate);
    /** @brief Inject one localized velocity impulse into the Mesh Slime simulation state. */
    [[nodiscard]] Result<void> applySlimeImpulseResult(float x, float y, float z, float impulseX, float impulseY,
                                                       float impulseZ, float radius, float falloff);
    /**
     * @brief Advance deterministic spring-damper Mesh Slime dynamics using injected time.
     * @param dt Positive simulation step. @param stiffness Non-negative equilibrium attraction.
     * @param damping Non-negative exponential velocity damping. @param maxSpeed Positive velocity bound.
     */
    [[nodiscard]] Result<void> stepSlimeResult(float dt, float stiffness, float damping, float maxSpeed);
    /** @brief Configure collider refresh scheduling and local position offset. */
    [[nodiscard]] Result<void> configureColliderRefreshResult(std::string_view mode, float intervalSeconds,
                                                              float offsetX = 0.f, float offsetY = 0.f,
                                                              float offsetZ = 0.f);
    /** @brief Mark a manual or once collider refresh as pending. */
    [[nodiscard]] Result<void> requestColliderRefreshResult();
    /** @brief Advance the refresh scheduler and consume at most one pending refresh. */
    [[nodiscard]] Result<bool> updateColliderRefreshResult(float dt);
    /** @brief Return an owning current triangle-mesh collider snapshot with configured offset applied. */
    [[nodiscard]] Result<MeshBuild> colliderMeshResult() const;
    /** @brief Replace or extend vertex selection using a world/local-space sphere. */
    [[nodiscard]] Result<int> selectVerticesSphereResult(float x, float y, float z, float radius, bool replace = true);
    /** @brief Replace or extend vertex selection using an axis-aligned world/local-space box. */
    [[nodiscard]] Result<int> selectVerticesBoxResult(float minX, float minY, float minZ, float maxX, float maxY,
                                                      float maxZ, bool replace = true);
    /** @brief Clear runtime vertex selection. */
    void clearVertexSelection() noexcept;
    /** @brief Return selected vertex count. */
    [[nodiscard]] int selectedVertexCount() const noexcept;
    /** @brief Return the arithmetic center of selected vertices for gizmo adapters. */
    [[nodiscard]] Result<MeshVertexSelectionCenter> selectedVertexCenterResult() const;
    /** @brief Translate selected vertices by a finite axis-gizmo delta as one undoable edit. */
    [[nodiscard]] Result<void> moveSelectedVerticesResult(float x, float y, float z);
    /**
     * @brief Apply pull, push, or grab to selected vertices as one undoable edit.
     * @param mode pull, push, or grab. @param originX Manipulator origin X.
     * @param originY Manipulator origin Y. @param originZ Manipulator origin Z.
     * @param directionX Grab direction X. @param directionY Grab direction Y. @param directionZ Grab direction Z.
     * @param distance Signed manipulation distance.
     */
    [[nodiscard]] Result<void> manipulateSelectedVerticesResult(std::string_view mode, float originX, float originY,
                                                                float originZ, float directionX, float directionY,
                                                                float directionZ, float distance);
    /** @brief Restore the initial mesh and clear undo history. */
    [[nodiscard]] Result<void> restoreResult();
    /** @brief Make the current mesh the new restore point and clear undo history. */
    [[nodiscard]] Result<void> bakeResult();
    /** @brief Restore the most recent pre-edit snapshot. */
    [[nodiscard]] Result<void> undoResult();
    /** @brief Return an independent owning snapshot of the current mesh. */
    [[nodiscard]] Result<MeshBuild> currentMeshResult() const;
    /** @brief Return the monotonic state revision. */
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    /** @brief Return the number of available undo snapshots. */
    [[nodiscard]] int undoCount() const noexcept { return static_cast<int>(undo_.size()); }
    /** @brief Report whether the session has been initialized. */
    [[nodiscard]] bool isInitialized() const noexcept { return initialized_; }

private:
    struct UndoState {
        MeshBuild          mesh;
        std::vector<float> equilibrium;
        std::vector<float> velocity;
    };
    void pushUndo();
    void invalidateImpactVertexBlocks() noexcept;
    [[nodiscard]] std::vector<std::uint32_t> impactCandidates(float x, float y, float z, float radius) const;

    MeshBuild                 original_;
    MeshBuild                 current_;
    std::vector<float>        surfaceEquilibrium_;
    std::vector<float>        surfaceVelocity_;
    std::vector<std::uint8_t> selectedVertices_;
    std::vector<std::vector<std::uint32_t>> impactVertexBlocks_;
    int                                     impactBlockDivisions_ = 0;
    float impactBlockMinX_ = 0.f, impactBlockMinY_ = 0.f, impactBlockMinZ_ = 0.f;
    float impactBlockMaxX_ = 0.f, impactBlockMaxY_ = 0.f, impactBlockMaxZ_ = 0.f;
    std::vector<UndoState>    undo_;
    std::string               colliderRefreshMode_        = "manual";
    float                     colliderRefreshInterval_    = 0.f;
    float                     colliderRefreshAccumulator_ = 0.f;
    float                     colliderOffsetX_ = 0.f, colliderOffsetY_ = 0.f, colliderOffsetZ_ = 0.f;
    bool                      colliderRefreshConfigured_ = false;
    bool                      colliderRefreshPending_    = false;
    std::uint64_t             revision_                  = 0;
    bool                      initialized_               = false;
};

}  // namespace eve::procgen
