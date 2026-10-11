#pragma once
#include "common/Export.h"


#include "common/Identity.h"

#include <string>
#include <utility>
#include <vector>

namespace eve::scene {

class Scene;

/**
 * @brief Script handle to a scene node (hostName + nodeId). Deliberately holds strings
 * rather than arena pointers, so it stays valid across rebuilds/reconcile.
 * Node-level entity bindings are forwarded through the Scene module.
 */
class EVENGINE_API_PLATFORM SceneNodeRef {
public:
    /** @brief Scene node ref. */
    SceneNodeRef() = default;
    /** @brief Scene node ref. */
    SceneNodeRef(std::string hostName, std::string nodeId)
        /** @brief Host name. */
        : hostName_(std::move(hostName)), nodeId_(std::move(nodeId)) {}

    /** @brief Returns the host name. */
    std::string getHostName() const { return hostName_; }
    /** @brief Returns the node id. */
    std::string getNodeId() const { return nodeId_; }
    /** @brief True when host + node currently resolve. */
    [[nodiscard]] bool isValid() const;
    /** @brief Return the persisted UUID of the resolved node, or nil if absent. */
    [[nodiscard]] eve::SceneObjectId persistentId() const;
    /**
     * @brief Returns the eve.Scene module instance for entity-binding forwarding.
     * @return Borrowed nullable module singleton; null means the module is unavailable.
     * @ownership The module system owns the Scene instance; callers must not delete it.
     * @lifetime Valid while the scene module is loaded; do not retain across unload.
     * @thread Call on the scene/script thread.
     * @reentrancy The accessor invokes no callbacks and is invalid across module teardown.
     */
    Scene *getScene() const;

    // --- local transform ---
    /** @brief Sets the position. */
    bool setPosition(float x, float y, float z);
    /** @brief Returns the position x. */
    float getPositionX() const;
    /** @brief Returns the position y. */
    float getPositionY() const;
    /** @brief Returns the position z. */
    float getPositionZ() const;
    /** @brief Returns the position. */
    std::vector<float> getPosition() const;

    /** @brief Sets the rotation. */
    bool setRotation(float yaw, float pitch, float roll);
    /** @brief Returns the rotation yaw. */
    float getRotationYaw() const;
    /** @brief Returns the rotation pitch. */
    float getRotationPitch() const;
    /** @brief Returns the rotation roll. */
    float getRotationRoll() const;
    /** @brief Returns the rotation. */
    std::vector<float> getRotation() const;

    /** @brief Sets the scale. */
    bool setScale(float sx, float sy, float sz);
    /** @brief Returns the scale x. */
    float getScaleX() const;
    /** @brief Returns the scale y. */
    float getScaleY() const;
    /** @brief Returns the scale z. */
    float getScaleZ() const;
    /** @brief Returns the scale. */
    std::vector<float> getScale() const;

    /** @brief Sets the visible. */
    bool setVisible(bool v);
    /** @brief True when visible. */
    bool isVisible() const;

    // --- world (requires transforms updated via updateTransforms / scene.update) ---
    /** @brief Returns the world position x. */
    float getWorldPositionX() const;
    /** @brief Returns the world position y. */
    float getWorldPositionY() const;
    /** @brief Returns the world position z. */
    float getWorldPositionZ() const;
    /** @brief Returns the world position. */
    std::vector<float> getWorldPosition() const;
    /** @brief Column-major 4x4 world matrix, 16 floats. */
    std::vector<float> getWorldMatrix() const;
    /** @brief Normalized local axes in world space (forward = +Z). */
    std::vector<float> getForward() const;
    /** @brief Returns the right. */
    std::vector<float> getRight() const;
    /** @brief Returns the up. */
    std::vector<float> getUp() const;

    // --- structure ---
    /** @brief Returns the parent id. */
    std::string getParentId() const;
    /** @brief Returns the child count. */
    int getChildCount() const;
    /** @brief Returns the child id at. */
    std::string getChildIdAt(int ordinal) const;
    /** @brief Returns the path. */
    std::string getPath() const;

private:
    std::string hostName_;
    std::string nodeId_;
};

}  // namespace eve::scene
