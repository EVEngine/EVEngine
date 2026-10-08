#pragma once
#include "common/Export.h"


#include "editor/TransformGizmo.h"

#include <string>

namespace eve::editor {

/**
 * @brief Babylon.js-style manager: toggles which transform modes are enabled and
 * routes pick/drag to the owned TransformGizmo (switching mode on best hit).
 */
class EVENGINE_API_ORCHESTRATION GizmoManager {
public:
    /** @brief Gizmo manager. */
    GizmoManager();

    /** @brief Returns the gizmo. */
    TransformGizmo *getGizmo() { return &gizmo_; }

    /** @brief Sets the position enabled. */
    void setPositionEnabled(bool enabled) { positionEnabled_ = enabled; }
    /** @brief Sets the rotation enabled. */
    void setRotationEnabled(bool enabled) { rotationEnabled_ = enabled; }
    /** @brief Sets the scale enabled. */
    void setScaleEnabled(bool enabled) { scaleEnabled_ = enabled; }
    /** @brief Sets the bound enabled. */
    void setBoundEnabled(bool enabled) { boundEnabled_ = enabled; }

    /** @brief Returns the position enabled. */
    bool getPositionEnabled() const { return positionEnabled_; }
    /** @brief Returns the rotation enabled. */
    bool getRotationEnabled() const { return rotationEnabled_; }
    /** @brief Returns the scale enabled. */
    bool getScaleEnabled() const { return scaleEnabled_; }
    /** @brief Returns the bound enabled. */
    bool getBoundEnabled() const { return boundEnabled_; }

    /** @brief Attaches . */
    void attach();
    /** @brief Detaches . */
    void detach();
    /** @brief True when attached. */
    bool isAttached() const { return attached_; }

    /** @brief Pick across enabled modes; sets gizmo mode to the winning tool. */
    std::string pick(float ox, float oy, float oz, float dx, float dy, float dz);

    /** @brief Begins drag. */
    bool beginDrag(const std::string &axis, float ox, float oy, float oz, float dx, float dy,
                   float dz);
    /** @brief Updates drag. */
    bool updateDrag(float ox, float oy, float oz, float dx, float dy, float dz);
    /** @brief Ends drag. */
    void endDrag();

    /** @brief True when dragging. */
    bool isDragging() const { return gizmo_.isDragging(); }
    /** @brief True when hovered. */
    bool isHovered() const { return gizmo_.isHovered(); }

private:
    struct Hit {
        std::string mode;
        std::string axis;
        float t = 1e30f;
    };

    Hit pickMode(const std::string &mode, float ox, float oy, float oz, float dx, float dy,
                 float dz);

    TransformGizmo gizmo_;
    bool attached_ = false;
    bool positionEnabled_ = true;
    bool rotationEnabled_ = false;
    bool scaleEnabled_ = false;
    bool boundEnabled_ = false;
};

}  // namespace eve::editor
