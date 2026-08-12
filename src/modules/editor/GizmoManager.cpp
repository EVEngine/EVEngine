#include "editor/GizmoManager.h"

#include <cmath>

namespace eve::editor {

GizmoManager::GizmoManager() {
    gizmo_.setMode("translate");
    positionEnabled_ = true;
}

void GizmoManager::attach() { attached_ = true; }

void GizmoManager::detach() {
    attached_ = false;
    gizmo_.endDrag();
}

GizmoManager::Hit GizmoManager::pickMode(const std::string &mode, float ox, float oy, float oz,
                                         float dx, float dy, float dz) {
    Hit h;
    h.mode = mode;
    std::string prev = gizmo_.getMode();
    gizmo_.setMode(mode);
    h.axis = gizmo_.pick(ox, oy, oz, dx, dy, dz);
    // Approximate depth: if miss, huge t; if hit, use distance from origin to gizmo pos as proxy
    // Re-pick stores hover; we use a second geometric estimate via part origins when hit.
    if (h.axis.empty()) {
        h.t = 1e30f;
    } else {
        // Prefer closer hits: distance from ray origin to gizmo position
        float px = gizmo_.getPositionX() - ox;
        float py = gizmo_.getPositionY() - oy;
        float pz = gizmo_.getPositionZ() - oz;
        h.t = std::sqrt(px * px + py * py + pz * pz);
        // Slight bias so translate planes don't always win over rings at same center
        if (mode == "rotate") h.t -= 0.001f;
        if (mode == "scale") h.t -= 0.002f;
        if (mode == "bound") h.t -= 0.003f;
    }
    gizmo_.setMode(prev);
    return h;
}

std::string GizmoManager::pick(float ox, float oy, float oz, float dx, float dy, float dz) {
    if (!attached_) return "";

    Hit best;
    best.t = 1e30f;

    auto consider = [&](bool enabled, const char *mode) {
        if (!enabled) return;
        Hit h = pickMode(mode, ox, oy, oz, dx, dy, dz);
        if (!h.axis.empty() && h.t < best.t) best = h;
    };

    consider(positionEnabled_, "translate");
    consider(rotationEnabled_, "rotate");
    consider(scaleEnabled_, "scale");
    consider(boundEnabled_, "bound");

    if (best.axis.empty()) {
        gizmo_.pick(ox, oy, oz, dx, dy, dz);  // clear hover via empty pick on current mode
        return "";
    }

    gizmo_.setMode(best.mode);
    return gizmo_.pick(ox, oy, oz, dx, dy, dz);
}

bool GizmoManager::beginDrag(const std::string &axis, float ox, float oy, float oz, float dx,
                             float dy, float dz) {
    if (!attached_ || axis.empty()) return false;
    return gizmo_.beginDrag(axis, ox, oy, oz, dx, dy, dz);
}

bool GizmoManager::updateDrag(float ox, float oy, float oz, float dx, float dy, float dz) {
    if (!attached_) return false;
    return gizmo_.updateDrag(ox, oy, oz, dx, dy, dz);
}

void GizmoManager::endDrag() { gizmo_.endDrag(); }

}  // namespace eve::editor
