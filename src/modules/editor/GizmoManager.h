#pragma once

#include "editor/TransformGizmo.h"

#include <string>

namespace eve::editor {

/**
 * @brief Babylon.js-style manager: toggles which transform modes are enabled and
 * routes pick/drag to the owned TransformGizmo (switching mode on best hit).
 */
class GizmoManager {
public:
    GizmoManager();

    TransformGizmo *getGizmo() { return &gizmo_; }

    void setPositionEnabled(bool enabled) { positionEnabled_ = enabled; }
    void setRotationEnabled(bool enabled) { rotationEnabled_ = enabled; }
    void setScaleEnabled(bool enabled) { scaleEnabled_ = enabled; }
    void setBoundEnabled(bool enabled) { boundEnabled_ = enabled; }

    bool getPositionEnabled() const { return positionEnabled_; }
    bool getRotationEnabled() const { return rotationEnabled_; }
    bool getScaleEnabled() const { return scaleEnabled_; }
    bool getBoundEnabled() const { return boundEnabled_; }

    void attach();
    void detach();
    bool isAttached() const { return attached_; }

    /** @brief Pick across enabled modes; sets gizmo mode to the winning tool. */
    std::string pick(float ox, float oy, float oz, float dx, float dy, float dz);

    bool beginDrag(const std::string &axis, float ox, float oy, float oz, float dx, float dy,
                   float dz);
    bool updateDrag(float ox, float oy, float oz, float dx, float dy, float dz);
    void endDrag();

    bool isDragging() const { return gizmo_.isDragging(); }
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
