#pragma once
#include "common/Export.h"


#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace eve::editor {

/**
 * @brief 3D transform gizmo (Three.js TransformControls + ImGuizmo style).
 * Owns TRS + optional local bounds; interaction via world-space rays.
 * Host renders using getPart* descriptors — no GPU dependency.
 */
class EVENGINE_API_ORCHESTRATION TransformGizmo {
public:
    /** @brief Transform gizmo. */
    TransformGizmo();

    /** @brief Sets the mode. */
    void setMode(const std::string &mode);
    /** @brief Returns the mode. */
    std::string getMode() const { return mode_; }

    /** @brief Sets the space. */
    void setSpace(const std::string &space);
    /** @brief Returns the space. */
    std::string getSpace() const { return space_; }

    /** @brief Sets the size. */
    void setSize(float size);
    /** @brief Returns the size. */
    float getSize() const { return size_; }

    /** @brief Sets the position. */
    void setPosition(float x, float y, float z);
    /** @brief Returns the position x. */
    float getPositionX() const { return position_.x; }
    /** @brief Returns the position y. */
    float getPositionY() const { return position_.y; }
    /** @brief Returns the position z. */
    float getPositionZ() const { return position_.z; }

    /** @brief Euler radians, XYZ order. */
    void setRotationEuler(float x, float y, float z);
    /** @brief Returns the rotation x. */
    float getRotationX() const { return rotation_.x; }
    /** @brief Returns the rotation y. */
    float getRotationY() const { return rotation_.y; }
    /** @brief Returns the rotation z. */
    float getRotationZ() const { return rotation_.z; }

    /** @brief Sets the scale. */
    void setScale(float x, float y, float z);
    /** @brief Returns the scale x. */
    float getScaleX() const { return scale_.x; }
    /** @brief Returns the scale y. */
    float getScaleY() const { return scale_.y; }
    /** @brief Returns the scale z. */
    float getScaleZ() const { return scale_.z; }

    /** @brief Local AABB extents for bound mode (relative to object origin). */
    void setBounds(float minX, float minY, float minZ, float maxX, float maxY, float maxZ);
    /** @brief Returns the bounds min x. */
    float getBoundsMinX() const { return boundsMin_.x; }
    /** @brief Returns the bounds min y. */
    float getBoundsMinY() const { return boundsMin_.y; }
    /** @brief Returns the bounds min z. */
    float getBoundsMinZ() const { return boundsMin_.z; }
    /** @brief Returns the bounds max x. */
    float getBoundsMaxX() const { return boundsMax_.x; }
    /** @brief Returns the bounds max y. */
    float getBoundsMaxY() const { return boundsMax_.y; }
    /** @brief Returns the bounds max z. */
    float getBoundsMaxZ() const { return boundsMax_.z; }

    /** @brief Sets the snap translate. */
    void setSnapTranslate(float x, float y, float z);
    /** @brief Sets the snap rotate. */
    void setSnapRotate(float degrees);
    /** @brief Sets the snap scale. */
    void setSnapScale(float s);
    /** @brief Returns the snap translate x. */
    float getSnapTranslateX() const { return snapTranslate_.x; }
    /** @brief Returns the snap translate y. */
    float getSnapTranslateY() const { return snapTranslate_.y; }
    /** @brief Returns the snap translate z. */
    float getSnapTranslateZ() const { return snapTranslate_.z; }
    /** @brief Returns the snap rotate. */
    float getSnapRotate() const { return snapRotateDeg_; }
    /** @brief Returns the snap scale. */
    float getSnapScale() const { return snapScale_; }

    /** @brief Column-major matrix element 0..15 of current TRS. */
    float getMatrix(int index) const;

    /**
     * @brief Ray pick against active mode handles.
     * Returns axis id: "x"|"y"|"z"|"xy"|"yz"|"xz"|"xyz"|"bx"|…|"bz"|"" .
     */
    std::string pick(float ox, float oy, float oz, float dx, float dy, float dz);

    /** @brief Begins drag. */
    bool beginDrag(const std::string &axis, float ox, float oy, float oz, float dx, float dy,
                   float dz);
    /** @brief Updates drag. */
    bool updateDrag(float ox, float oy, float oz, float dx, float dy, float dz);
    /** @brief Ends drag. */
    void endDrag();

    /** @brief True when dragging. */
    bool isDragging() const { return dragging_; }
    /** @brief True when hovered. */
    bool isHovered() const { return !hoverAxis_.empty(); }
    /** @brief Returns the active axis. */
    std::string getActiveAxis() const { return activeAxis_; }
    /** @brief Returns the hover axis. */
    std::string getHoverAxis() const { return hoverAxis_; }

    /** @brief Rebuild draw parts for current mode/space (call after TRS/mode change). */
    void rebuildParts();

    /** @brief Returns the part count. */
    int getPartCount() const { return static_cast<int>(parts_.size()); }
    /** @brief Returns the part kind. */
    std::string getPartKind(int index) const;
    /** @brief Returns the part axis. */
    std::string getPartAxis(int index) const;
    /** @brief Returns the part color r. */
    float getPartColorR(int index) const;
    /** @brief Returns the part color g. */
    float getPartColorG(int index) const;
    /** @brief Returns the part color b. */
    float getPartColorB(int index) const;
    /** @brief Returns the part color a. */
    float getPartColorA(int index) const;
    /** @brief Returns the part origin x. */
    float getPartOriginX(int index) const;
    /** @brief Returns the part origin y. */
    float getPartOriginY(int index) const;
    /** @brief Returns the part origin z. */
    float getPartOriginZ(int index) const;
    /** @brief Returns the part dir x. */
    float getPartDirX(int index) const;
    /** @brief Returns the part dir y. */
    float getPartDirY(int index) const;
    /** @brief Returns the part dir z. */
    float getPartDirZ(int index) const;
    /** @brief Returns the part length. */
    float getPartLength(int index) const;
    /** @brief Returns the part radius. */
    float getPartRadius(int index) const;

private:
    struct Part {
        std::string kind;  // axis | plane | ring | box | center | handle
        std::string axis;
        glm::vec3 origin{0.f};
        glm::vec3 dir{1.f, 0.f, 0.f};
        float length = 1.f;
        float radius = 0.05f;
        glm::vec4 color{1.f, 0.f, 0.f, 1.f};
    };

    glm::mat4 localRotationMatrix() const;
    glm::mat4 worldMatrix() const;
    glm::vec3 axisWorld(int axis) const;  // 0=x,1=y,2=z
    void colorForAxis(const std::string &axis, glm::vec4 &out) const;

    float hitAxis(const glm::vec3 &ro, const glm::vec3 &rd, int axisIndex, float &outT) const;
    float hitPlane(const glm::vec3 &ro, const glm::vec3 &rd, int planeMask, float &outT) const;
    float hitRing(const glm::vec3 &ro, const glm::vec3 &rd, int axisIndex, float &outT) const;
    float hitBoundHandle(const glm::vec3 &ro, const glm::vec3 &rd, int handle, float &outT) const;

    glm::vec3 projectToDragPlane(const glm::vec3 &ro, const glm::vec3 &rd) const;
    void applySnapTranslate(glm::vec3 &v) const;
    float applySnapRotate(float radians) const;
    float applySnapScale(float s) const;

    bool validPart(int index) const;

    std::string mode_ = "translate";
    std::string space_ = "world";
    float size_ = 1.f;

    glm::vec3 position_{0.f};
    glm::vec3 rotation_{0.f};
    glm::vec3 scale_{1.f};
    glm::vec3 boundsMin_{-0.5f};
    glm::vec3 boundsMax_{0.5f};

    glm::vec3 snapTranslate_{0.f};
    float snapRotateDeg_ = 0.f;
    float snapScale_ = 0.f;

    bool dragging_ = false;
    std::string activeAxis_;
    std::string hoverAxis_;

    glm::vec3 dragStartPos_{0.f};
    glm::vec3 dragStartRot_{0.f};
    glm::vec3 dragStartScale_{0.f};
    glm::vec3 dragStartHit_{0.f};
    glm::vec3 dragPlaneNormal_{0.f, 1.f, 0.f};
    glm::vec3 dragAxisDir_{1.f, 0.f, 0.f};

    std::vector<Part> parts_;
};

}  // namespace eve::editor
