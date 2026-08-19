#pragma once

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
class SceneNodeRef {
public:
    SceneNodeRef() = default;
    SceneNodeRef(std::string hostName, std::string nodeId)
        : hostName_(std::move(hostName)), nodeId_(std::move(nodeId)) {}

    std::string getHostName() const { return hostName_; }
    std::string getNodeId() const { return nodeId_; }
    /** @brief True when host + node currently resolve. */
    bool isValid() const;
    /** @brief The eve.Scene module instance (for entity binding forwarding). */
    Scene *getScene() const;

    // --- local transform ---
    bool setPosition(float x, float y, float z);
    float getPositionX() const;
    float getPositionY() const;
    float getPositionZ() const;
    std::vector<float> getPosition() const;

    bool setRotation(float yaw, float pitch, float roll);
    float getRotationYaw() const;
    float getRotationPitch() const;
    float getRotationRoll() const;
    std::vector<float> getRotation() const;

    bool setScale(float sx, float sy, float sz);
    float getScaleX() const;
    float getScaleY() const;
    float getScaleZ() const;
    std::vector<float> getScale() const;

    bool setVisible(bool v);
    bool isVisible() const;

    // --- world (requires transforms updated via updateTransforms / scene.update) ---
    float getWorldPositionX() const;
    float getWorldPositionY() const;
    float getWorldPositionZ() const;
    std::vector<float> getWorldPosition() const;
    /** @brief Column-major 4x4 world matrix, 16 floats. */
    std::vector<float> getWorldMatrix() const;
    /** @brief Normalized local axes in world space (forward = +Z). */
    std::vector<float> getForward() const;
    std::vector<float> getRight() const;
    std::vector<float> getUp() const;

    // --- structure ---
    std::string getParentId() const;
    int getChildCount() const;
    std::string getChildIdAt(int ordinal) const;
    std::string getPath() const;

private:
    std::string hostName_;
    std::string nodeId_;
};

}  // namespace eve::scene
