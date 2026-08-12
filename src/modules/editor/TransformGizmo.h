#pragma once

#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace eve::editor {

/**
 * 3D transform gizmo (Three.js TransformControls + ImGuizmo style).
 * Owns TRS + optional local bounds; interaction via world-space rays.
 * Host renders using getPart* descriptors — no GPU dependency.
 */
class TransformGizmo {
public:
    TransformGizmo();

    void setMode(const std::string &mode);
    std::string getMode() const { return mode_; }

    void setSpace(const std::string &space);
    std::string getSpace() const { return space_; }

    void setSize(float size);
    float getSize() const { return size_; }

    void setPosition(float x, float y, float z);
    float getPositionX() const { return position_.x; }
    float getPositionY() const { return position_.y; }
    float getPositionZ() const { return position_.z; }

    /** Euler radians, XYZ order. */
    void setRotationEuler(float x, float y, float z);
    float getRotationX() const { return rotation_.x; }
    float getRotationY() const { return rotation_.y; }
    float getRotationZ() const { return rotation_.z; }

    void setScale(float x, float y, float z);
    float getScaleX() const { return scale_.x; }
    float getScaleY() const { return scale_.y; }
    float getScaleZ() const { return scale_.z; }

    /** Local AABB extents for bound mode (relative to object origin). */
    void setBounds(float minX, float minY, float minZ, float maxX, float maxY, float maxZ);
    float getBoundsMinX() const { return boundsMin_.x; }
    float getBoundsMinY() const { return boundsMin_.y; }
    float getBoundsMinZ() const { return boundsMin_.z; }
    float getBoundsMaxX() const { return boundsMax_.x; }
    float getBoundsMaxY() const { return boundsMax_.y; }
    float getBoundsMaxZ() const { return boundsMax_.z; }

    void setSnapTranslate(float x, float y, float z);
    void setSnapRotate(float degrees);
    void setSnapScale(float s);
    float getSnapTranslateX() const { return snapTranslate_.x; }
    float getSnapTranslateY() const { return snapTranslate_.y; }
    float getSnapTranslateZ() const { return snapTranslate_.z; }
    float getSnapRotate() const { return snapRotateDeg_; }
    float getSnapScale() const { return snapScale_; }

    /** Column-major matrix element 0..15 of current TRS. */
    float getMatrix(int index) const;

    /**
     * Ray pick against active mode handles.
     * Returns axis id: "x"|"y"|"z"|"xy"|"yz"|"xz"|"xyz"|"bx"|…|"bz"|"" .
     */
    std::string pick(float ox, float oy, float oz, float dx, float dy, float dz);

    bool beginDrag(const std::string &axis, float ox, float oy, float oz, float dx, float dy,
                   float dz);
    bool updateDrag(float ox, float oy, float oz, float dx, float dy, float dz);
    void endDrag();

    bool isDragging() const { return dragging_; }
    bool isHovered() const { return !hoverAxis_.empty(); }
    std::string getActiveAxis() const { return activeAxis_; }
    std::string getHoverAxis() const { return hoverAxis_; }

    /** Rebuild draw parts for current mode/space (call after TRS/mode change). */
    void rebuildParts();

    int getPartCount() const { return static_cast<int>(parts_.size()); }
    std::string getPartKind(int index) const;
    std::string getPartAxis(int index) const;
    float getPartColorR(int index) const;
    float getPartColorG(int index) const;
    float getPartColorB(int index) const;
    float getPartColorA(int index) const;
    float getPartOriginX(int index) const;
    float getPartOriginY(int index) const;
    float getPartOriginZ(int index) const;
    float getPartDirX(int index) const;
    float getPartDirY(int index) const;
    float getPartDirZ(int index) const;
    float getPartLength(int index) const;
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
