#pragma once

#include "editing/EditingProtocol.h"
#include "scene/editor/ScenePhysicsPlacement.h"

#include <memory>
#include <string>
#include <vector>

namespace eve::scene_editing {
class SceneTargetBase;
}

namespace eve::scene_editor {
/**
 * @brief Composable scene command/history host with no UI or renderer dependency.
 * Owns its target, command registry and history. Host supplies selection, input,
 * rendering and file I/O; snapshot JSON contains hierarchy/TRS only.
 * @thread Owner thread only. Live targets may notify host observers after publication,
 * without locks. Observers must not destroy or reenter the active session.
 */
class SceneEditorSession {
public:
    /** @brief Own a document target with a nonempty host-local identity. */
    explicit SceneEditorSession(std::string targetId);
    /** @brief Transfer a non-null scene target into this session; target ownership is exclusive. */
    explicit SceneEditorSession(std::unique_ptr<scene_editing::SceneTargetBase> target);
    ~SceneEditorSession();
    SceneEditorSession(const SceneEditorSession&)            = delete;
    SceneEditorSession& operator=(const SceneEditorSession&) = delete;
    /** @brief Execute a registered scene command through revision checks and one reversible transaction. */
    [[nodiscard]] editing::Result<editing::TransactionReceipt> execute(std::string command, editing::Value payload);
    /** @brief Restrict future commands to a host allow-list; empty denies all. Existing history remains undoable. */
    [[nodiscard]] editing::Result<void> restrictCommands(const std::vector<std::string>& commands);
    /** @brief Undo the last scene transaction; failures preserve history. */
    [[nodiscard]] editing::Result<editing::TransactionReceipt> undo();
    /** @brief Reapply the last undone scene transaction. */
    [[nodiscard]] editing::Result<editing::TransactionReceipt> redo();
    /** @brief Copy the versioned hierarchy/TRS snapshot; caller owns the returned tree. */
    [[nodiscard]] editing::Value snapshot() const;
    /** @brief Encode snapshot deterministically; file I/O and paths belong to the host. */
    [[nodiscard]] std::string saveJson() const;
    /** @brief Parse and restore schema-1 JSON as an undoable transaction; invalid input changes nothing. */
    [[nodiscard]] editing::Result<editing::TransactionReceipt> restoreJson(const std::string& json);
    /** @brief Return the current monotonic target revision. */
    [[nodiscard]] editing::Revision revision() const;
    /**
     * @brief Cache backend-neutral local-space mesh points for later automatic convex placement.
     * @param
     * object Stable scene object identity; the session owns a copy of the points.
     * @param vertices At least four
     * packed finite XYZ points.
     * @param maxVertices Convex hull simplification budget in [4, 254].
     * @return
     * Applied, or Rejected without changing the previous cached entry.
     */
    [[nodiscard]] editing::Result<void> cachePhysicsPlacementHull(const editing::ObjectId& object,
                                                                  std::vector<float> vertices, int maxVertices = 64);
    /**
     * @brief Replace one object's generated compound collider cache entry.
     * @param object Stable scene
     * object identity.
     * @param resourceKey Host-owned immutable mesh/content revision used for stale detection.

     * * @param colliders One or more generated local-space convex or primitive parts; copied by the session.
     */
    [[nodiscard]] editing::Result<void> cachePhysicsPlacementCompound(const editing::ObjectId&              object,
                                                                      std::string                           resourceKey,
                                                                      std::vector<PhysicsPlacementCollider> colliders);
    /** @brief Remove one cached automatic placement hull; missing entries return NoOp. */
    [[nodiscard]] editing::Result<void> removePhysicsPlacementHull(const editing::ObjectId& object);
    /** @brief Encode schema-2 generated collider cache; host owns file I/O and the returned string. */
    [[nodiscard]] std::string savePhysicsPlacementColliderCacheJson() const;
    /** @brief Atomically replace generated collider cache from schema-2 JSON. */
    [[nodiscard]] editing::Result<void> restorePhysicsPlacementColliderCacheJson(const std::string& json);
    /** @brief Begin an isolated Box3D placement preview using current scene transforms. */
    [[nodiscard]] editing::Result<void> beginPhysicsPlacement(PhysicsPlacementRequest request);
    /** @brief Move the preview handle and advance one fixed simulation step. */
    [[nodiscard]] editing::Result<PhysicsPlacementFrame> updatePhysicsPlacement(double x, double y, double z,
                                                                                double fixedDelta = 1.0 / 60.0);
    /** @brief Move and rotate the preview handle, then advance one fixed simulation step. */
    [[nodiscard]] editing::Result<PhysicsPlacementFrame> updatePhysicsPlacementPose(double x, double y, double z,
                                                                                    double rotationX, double rotationY,
                                                                                    double rotationZ,
                                                                                    double fixedDelta = 1.0 / 60.0);
    /** @brief Move, rotate and scale the preview handle, then advance one fixed simulation step. */
    [[nodiscard]] editing::Result<PhysicsPlacementFrame> updatePhysicsPlacementTransform(
        double x, double y, double z, double rotationX, double rotationY, double rotationZ, double scaleX,
        double scaleY, double scaleZ, double fixedDelta = 1.0 / 60.0);
    /** @brief Align the active selection to a non-selected preview surface and advance one step. */
    [[nodiscard]] editing::Result<PhysicsPlacementFrame> alignPhysicsPlacementToSurface(double fromX, double fromY,
                                                                                        double fromZ, double toX,
                                                                                        double toY, double toZ,
                                                                                        double offset     = 0.0,
                                                                                        double fixedDelta = 1.0 / 60.0);
    /** @brief Atomically publish selected preview transforms as one undoable transaction. */
    [[nodiscard]] editing::Result<editing::TransactionReceipt> commitPhysicsPlacement();
    /** @brief Discard the isolated preview without changing scene state. */
    [[nodiscard]] editing::Result<void> cancelPhysicsPlacement();
    /** @brief Report whether this session owns an active placement preview. */
    [[nodiscard]] bool physicsPlacementActive() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::scene_editor
