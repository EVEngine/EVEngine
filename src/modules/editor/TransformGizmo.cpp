#include "editor/TransformGizmo.h"

#include "common/Exception.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace eve::editor {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kAxisHitRadius = 0.12f;
constexpr float kPlaneHitSize = 0.25f;
constexpr float kRingThickness = 0.08f;

float snapValue(float v, float step) {
    if (step <= 0.f) return v;
    return std::round(v / step) * step;
}

glm::vec3 safeNormalize(const glm::vec3 &v, const glm::vec3 &fallback) {
    float len = glm::length(v);
    if (len < 1e-6f) return fallback;
    return v / len;
}

}  // namespace

TransformGizmo::TransformGizmo() { rebuildParts(); }

void TransformGizmo::setMode(const std::string &mode) {
    if (mode != "translate" && mode != "rotate" && mode != "scale" && mode != "bound") {
        throw Exception("TransformGizmo::setMode: expected translate|rotate|scale|bound");
    }
    mode_ = mode;
    rebuildParts();
}

void TransformGizmo::setSpace(const std::string &space) {
    if (space != "local" && space != "world") {
        throw Exception("TransformGizmo::setSpace: expected local|world");
    }
    space_ = space;
    rebuildParts();
}

void TransformGizmo::setSize(float size) {
    if (size <= 0.f) throw Exception("TransformGizmo::setSize: size must be > 0");
    size_ = size;
    rebuildParts();
}

void TransformGizmo::setPosition(float x, float y, float z) {
    position_ = {x, y, z};
    rebuildParts();
}

void TransformGizmo::setRotationEuler(float x, float y, float z) {
    rotation_ = {x, y, z};
    rebuildParts();
}

void TransformGizmo::setScale(float x, float y, float z) {
    scale_ = {x, y, z};
    rebuildParts();
}

void TransformGizmo::setBounds(float minX, float minY, float minZ, float maxX, float maxY,
                               float maxZ) {
    boundsMin_ = {std::min(minX, maxX), std::min(minY, maxY), std::min(minZ, maxZ)};
    boundsMax_ = {std::max(minX, maxX), std::max(minY, maxY), std::max(minZ, maxZ)};
    rebuildParts();
}

void TransformGizmo::setSnapTranslate(float x, float y, float z) { snapTranslate_ = {x, y, z}; }

void TransformGizmo::setSnapRotate(float degrees) { snapRotateDeg_ = degrees; }

void TransformGizmo::setSnapScale(float s) { snapScale_ = s; }

float TransformGizmo::getMatrix(int index) const {
    if (index < 0 || index > 15) throw Exception("TransformGizmo::getMatrix: index 0..15");
    glm::mat4 m = worldMatrix();
    const float *p = glm::value_ptr(m);
    return p[index];
}

glm::mat4 TransformGizmo::localRotationMatrix() const {
    glm::mat4 m(1.f);
    m = glm::rotate(m, rotation_.z, glm::vec3(0.f, 0.f, 1.f));
    m = glm::rotate(m, rotation_.y, glm::vec3(0.f, 1.f, 0.f));
    m = glm::rotate(m, rotation_.x, glm::vec3(1.f, 0.f, 0.f));
    return m;
}

glm::mat4 TransformGizmo::worldMatrix() const {
    glm::mat4 m(1.f);
    m = glm::translate(m, position_);
    m *= localRotationMatrix();
    m = glm::scale(m, scale_);
    return m;
}

glm::vec3 TransformGizmo::axisWorld(int axis) const {
    glm::vec3 local(0.f);
    local[axis] = 1.f;
    if (space_ == "world" && mode_ != "bound") return local;
    glm::mat3 R(localRotationMatrix());
    return safeNormalize(R * local, local);
}

void TransformGizmo::colorForAxis(const std::string &axis, glm::vec4 &out) const {
    bool active = (axis == activeAxis_ || axis == hoverAxis_);
    float boost = active ? 1.f : 0.75f;
    if (axis == "x")
        out = {1.f * boost, 0.2f, 0.2f, 1.f};
    else if (axis == "y")
        out = {0.2f, 1.f * boost, 0.2f, 1.f};
    else if (axis == "z")
        out = {0.25f, 0.45f, 1.f * boost, 1.f};
    else if (axis == "xy")
        out = {1.f * boost, 1.f * boost, 0.2f, 0.85f};
    else if (axis == "yz")
        out = {0.2f, 1.f * boost, 1.f * boost, 0.85f};
    else if (axis == "xz")
        out = {1.f * boost, 0.2f, 1.f * boost, 0.85f};
    else if (axis == "xyz")
        out = {0.9f * boost, 0.9f * boost, 0.9f * boost, 1.f};
    else
        out = {0.8f * boost, 0.8f * boost, 0.2f, 1.f};
}

void TransformGizmo::rebuildParts() {
    parts_.clear();
    const glm::vec3 origin = position_;

    auto addAxis = [&](const char *axisName, int ai, const char *kind) {
        Part p;
        p.kind = kind;
        p.axis = axisName;
        p.origin = origin;
        p.dir = axisWorld(ai);
        p.length = size_;
        p.radius = size_ * 0.04f;
        colorForAxis(p.axis, p.color);
        parts_.push_back(p);
    };

    if (mode_ == "translate") {
        addAxis("x", 0, "axis");
        addAxis("y", 1, "axis");
        addAxis("z", 2, "axis");
        // plane handles
        for (const char *plane : {"xy", "yz", "xz"}) {
            Part p;
            p.kind = "plane";
            p.axis = plane;
            p.origin = origin;
            if (std::strcmp(plane, "xy") == 0)
                p.dir = glm::normalize(axisWorld(0) + axisWorld(1));
            else if (std::strcmp(plane, "yz") == 0)
                p.dir = glm::normalize(axisWorld(1) + axisWorld(2));
            else
                p.dir = glm::normalize(axisWorld(0) + axisWorld(2));
            p.length = size_ * kPlaneHitSize;
            p.radius = size_ * kPlaneHitSize;
            colorForAxis(p.axis, p.color);
            parts_.push_back(p);
        }
        Part c;
        c.kind = "center";
        c.axis = "xyz";
        c.origin = origin;
        c.dir = {0.f, 1.f, 0.f};
        c.length = 0.f;
        c.radius = size_ * 0.06f;
        colorForAxis(c.axis, c.color);
        parts_.push_back(c);
    } else if (mode_ == "rotate") {
        addAxis("x", 0, "ring");
        addAxis("y", 1, "ring");
        addAxis("z", 2, "ring");
        for (auto &p : parts_) {
            p.length = 0.f;
            p.radius = size_;
        }
    } else if (mode_ == "scale") {
        addAxis("x", 0, "axis");
        addAxis("y", 1, "axis");
        addAxis("z", 2, "axis");
        Part u;
        u.kind = "center";
        u.axis = "xyz";
        u.origin = origin;
        u.dir = {0.f, 1.f, 0.f};
        u.radius = size_ * 0.08f;
        colorForAxis(u.axis, u.color);
        parts_.push_back(u);
    } else {  // bound
        const char *names[] = {"bx", "by", "bz", "bnx", "bny", "bnz"};
        glm::vec3 centers[6] = {
            {boundsMax_.x, 0.f, 0.f}, {0.f, boundsMax_.y, 0.f}, {0.f, 0.f, boundsMax_.z},
            {boundsMin_.x, 0.f, 0.f}, {0.f, boundsMin_.y, 0.f}, {0.f, 0.f, boundsMin_.z},
        };
        glm::mat3 R(localRotationMatrix());
        for (int i = 0; i < 6; ++i) {
            Part p;
            p.kind = "handle";
            p.axis = names[i];
            glm::vec3 local = centers[i];
            // scale local handle offset by object scale
            local *= scale_;
            p.origin = position_ + R * local;
            p.dir = axisWorld(i % 3);
            p.radius = size_ * 0.07f;
            colorForAxis((i % 3 == 0) ? "x" : (i % 3 == 1) ? "y" : "z", p.color);
            parts_.push_back(p);
        }
        Part box;
        box.kind = "box";
        box.axis = "bound";
        box.origin = position_;
        box.dir = {0.f, 1.f, 0.f};
        box.length = glm::length(boundsMax_ - boundsMin_) * 0.5f;
        box.radius = box.length;
        colorForAxis("xyz", box.color);
        parts_.push_back(box);
    }
}

float TransformGizmo::hitAxis(const glm::vec3 &ro, const glm::vec3 &rd, int axisIndex,
                              float &outT) const {
    glm::vec3 a = axisWorld(axisIndex);
    glm::vec3 p0 = position_;
    glm::vec3 p1 = position_ + a * size_;
    // Closest approach ray-segment
    glm::vec3 u = p1 - p0;
    glm::vec3 v = rd;
    glm::vec3 w = p0 - ro;
    float aa = glm::dot(u, u);
    float bb = glm::dot(v, v);
    float cc = glm::dot(u, v);
    float dd = glm::dot(u, w);
    float ee = glm::dot(v, w);
    float denom = aa * bb - cc * cc;
    float sn = 0.f, tn = 0.f;
    if (std::fabs(denom) > 1e-8f) {
        sn = (cc * ee - bb * dd) / denom;
        tn = (aa * ee - cc * dd) / denom;
    }
    sn = std::clamp(sn, 0.f, 1.f);
    if (tn < 0.f) return -1.f;
    glm::vec3 closestU = p0 + u * sn;
    glm::vec3 closestV = ro + v * tn;
    float dist = glm::length(closestU - closestV);
    float thresh = size_ * kAxisHitRadius;
    if (dist > thresh) return -1.f;
    outT = tn;
    return dist;
}

float TransformGizmo::hitPlane(const glm::vec3 &ro, const glm::vec3 &rd, int planeMask,
                               float &outT) const {
    // planeMask: bit0=x, bit1=y, bit2=z included in plane (two bits set)
    glm::vec3 n(0.f);
    if ((planeMask & 1) == 0) n = axisWorld(0);
    else if ((planeMask & 2) == 0) n = axisWorld(1);
    else n = axisWorld(2);
    float denom = glm::dot(n, rd);
    if (std::fabs(denom) < 1e-6f) return -1.f;
    float t = glm::dot(position_ - ro, n) / denom;
    if (t < 0.f) return -1.f;
    glm::vec3 hit = ro + rd * t;
    glm::vec3 d = hit - position_;
    // project onto the two axes
    int a0 = (planeMask & 1) ? 0 : ((planeMask & 2) ? 1 : 2);
    int a1 = (planeMask & 4) ? 2 : ((planeMask & 2) && a0 != 1 ? 1 : (a0 == 0 ? 1 : 0));
    if (planeMask == 3) {
        a0 = 0;
        a1 = 1;
    } else if (planeMask == 6) {
        a0 = 1;
        a1 = 2;
    } else if (planeMask == 5) {
        a0 = 0;
        a1 = 2;
    }
    float u = glm::dot(d, axisWorld(a0));
    float v = glm::dot(d, axisWorld(a1));
    float lim = size_ * kPlaneHitSize;
    float lo = size_ * 0.05f;
    if (u < lo || v < lo || u > lim || v > lim) return -1.f;
    outT = t;
    return 0.f;
}

float TransformGizmo::hitRing(const glm::vec3 &ro, const glm::vec3 &rd, int axisIndex,
                              float &outT) const {
    glm::vec3 n = axisWorld(axisIndex);
    float denom = glm::dot(n, rd);
    if (std::fabs(denom) < 1e-6f) return -1.f;
    float t = glm::dot(position_ - ro, n) / denom;
    if (t < 0.f) return -1.f;
    glm::vec3 hit = ro + rd * t;
    float dist = glm::length(hit - position_);
    float target = size_;
    float thick = size_ * kRingThickness;
    if (std::fabs(dist - target) > thick) return -1.f;
    outT = t;
    return std::fabs(dist - target);
}

float TransformGizmo::hitBoundHandle(const glm::vec3 &ro, const glm::vec3 &rd, int handle,
                                     float &outT) const {
    if (handle < 0 || handle >= static_cast<int>(parts_.size())) return -1.f;
    const Part &p = parts_[handle];
    if (p.kind != "handle") return -1.f;
    // sphere hit
    glm::vec3 oc = ro - p.origin;
    float b = glm::dot(oc, rd);
    float c = glm::dot(oc, oc) - p.radius * p.radius;
    float disc = b * b - c;
    if (disc < 0.f) return -1.f;
    float t = -b - std::sqrt(disc);
    if (t < 0.f) t = -b + std::sqrt(disc);
    if (t < 0.f) return -1.f;
    outT = t;
    return 0.f;
}

std::string TransformGizmo::pick(float ox, float oy, float oz, float dx, float dy, float dz) {
    glm::vec3 ro(ox, oy, oz);
    glm::vec3 rd = safeNormalize(glm::vec3(dx, dy, dz), glm::vec3(0.f, 0.f, -1.f));
    hoverAxis_.clear();
    float bestT = 1e30f;
    std::string best;

    auto consider = [&](const std::string &axis, float score, float t) {
        if (score < 0.f) return;
        if (t < bestT) {
            bestT = t;
            best = axis;
        }
    };

    if (mode_ == "translate") {
        float t;
        for (int i = 0; i < 3; ++i) {
            float s = hitAxis(ro, rd, i, t);
            consider(i == 0 ? "x" : i == 1 ? "y" : "z", s, t);
        }
        float tp;
        if (hitPlane(ro, rd, 3, tp) >= 0.f) consider("xy", 0.f, tp);
        if (hitPlane(ro, rd, 6, tp) >= 0.f) consider("yz", 0.f, tp);
        if (hitPlane(ro, rd, 5, tp) >= 0.f) consider("xz", 0.f, tp);
        // center sphere
        glm::vec3 oc = ro - position_;
        float b = glm::dot(oc, rd);
        float c = glm::dot(oc, oc) - (size_ * 0.06f) * (size_ * 0.06f);
        float disc = b * b - c;
        if (disc >= 0.f) {
            float t = -b - std::sqrt(disc);
            if (t >= 0.f) consider("xyz", 0.f, t);
        }
    } else if (mode_ == "rotate") {
        float t;
        for (int i = 0; i < 3; ++i) {
            float s = hitRing(ro, rd, i, t);
            consider(i == 0 ? "x" : i == 1 ? "y" : "z", s, t);
        }
    } else if (mode_ == "scale") {
        float t;
        for (int i = 0; i < 3; ++i) {
            float s = hitAxis(ro, rd, i, t);
            consider(i == 0 ? "x" : i == 1 ? "y" : "z", s, t);
        }
        glm::vec3 oc = ro - position_;
        float rad = size_ * 0.08f;
        float b = glm::dot(oc, rd);
        float c = glm::dot(oc, oc) - rad * rad;
        float disc = b * b - c;
        if (disc >= 0.f) {
            float t = -b - std::sqrt(disc);
            if (t >= 0.f) consider("xyz", 0.f, t);
        }
    } else {
        for (int i = 0; i < static_cast<int>(parts_.size()); ++i) {
            float t;
            float s = hitBoundHandle(ro, rd, i, t);
            if (s >= 0.f) consider(parts_[i].axis, s, t);
        }
    }

    hoverAxis_ = best;
    rebuildParts();
    return best;
}

glm::vec3 TransformGizmo::projectToDragPlane(const glm::vec3 &ro, const glm::vec3 &rd) const {
    float denom = glm::dot(dragPlaneNormal_, rd);
    if (std::fabs(denom) < 1e-8f) {
        // fallback: project along axis using perpendicular plane from camera-ish guess
        return dragStartHit_;
    }
    float t = glm::dot(dragStartHit_ - ro, dragPlaneNormal_) / denom;
    return ro + rd * t;
}

void TransformGizmo::applySnapTranslate(glm::vec3 &v) const {
    if (snapTranslate_.x > 0.f) v.x = snapValue(v.x, snapTranslate_.x);
    if (snapTranslate_.y > 0.f) v.y = snapValue(v.y, snapTranslate_.y);
    if (snapTranslate_.z > 0.f) v.z = snapValue(v.z, snapTranslate_.z);
}

float TransformGizmo::applySnapRotate(float radians) const {
    if (snapRotateDeg_ <= 0.f) return radians;
    float deg = radians * (180.f / kPi);
    deg = snapValue(deg, snapRotateDeg_);
    return deg * (kPi / 180.f);
}

float TransformGizmo::applySnapScale(float s) const {
    if (snapScale_ <= 0.f) return s;
    return snapValue(s, snapScale_);
}

bool TransformGizmo::beginDrag(const std::string &axis, float ox, float oy, float oz, float dx,
                               float dy, float dz) {
    if (axis.empty()) return false;
    activeAxis_ = axis;
    hoverAxis_ = axis;
    dragging_ = true;
    dragStartPos_ = position_;
    dragStartRot_ = rotation_;
    dragStartScale_ = scale_;

    glm::vec3 ro(ox, oy, oz);
    glm::vec3 rd = safeNormalize(glm::vec3(dx, dy, dz), glm::vec3(0.f, 0.f, -1.f));

    if (mode_ == "translate" || mode_ == "scale" || mode_ == "bound") {
        if (axis == "x" || axis == "bx" || axis == "bnx")
            dragAxisDir_ = axisWorld(0);
        else if (axis == "y" || axis == "by" || axis == "bny")
            dragAxisDir_ = axisWorld(1);
        else if (axis == "z" || axis == "bz" || axis == "bnz")
            dragAxisDir_ = axisWorld(2);
        else if (axis == "xy")
            dragPlaneNormal_ = axisWorld(2);
        else if (axis == "yz")
            dragPlaneNormal_ = axisWorld(0);
        else if (axis == "xz")
            dragPlaneNormal_ = axisWorld(1);
        else
            dragPlaneNormal_ = safeNormalize(glm::cross(rd, glm::vec3(0.f, 1.f, 0.f)),
                                             glm::vec3(0.f, 0.f, 1.f));

        if (axis == "x" || axis == "y" || axis == "z" || axis == "bx" || axis == "by" ||
            axis == "bz" || axis == "bnx" || axis == "bny" || axis == "bnz") {
            // plane facing camera containing axis
            glm::vec3 side = glm::cross(dragAxisDir_, rd);
            if (glm::length(side) < 1e-5f) side = glm::cross(dragAxisDir_, glm::vec3(0.f, 1.f, 0.f));
            dragPlaneNormal_ = safeNormalize(glm::cross(side, dragAxisDir_), glm::vec3(0.f, 1.f, 0.f));
        }
        if (axis == "xyz") {
            dragPlaneNormal_ = -rd;
        }
    } else {  // rotate
        int ai = axis == "x" ? 0 : axis == "y" ? 1 : 2;
        dragAxisDir_ = axisWorld(ai);
        dragPlaneNormal_ = dragAxisDir_;
    }

    float denom = glm::dot(dragPlaneNormal_, rd);
    if (std::fabs(denom) > 1e-8f) {
        float t = glm::dot(position_ - ro, dragPlaneNormal_) / denom;
        dragStartHit_ = ro + rd * t;
    } else {
        dragStartHit_ = position_;
    }
    rebuildParts();
    return true;
}

bool TransformGizmo::updateDrag(float ox, float oy, float oz, float dx, float dy, float dz) {
    if (!dragging_) return false;
    glm::vec3 ro(ox, oy, oz);
    glm::vec3 rd = safeNormalize(glm::vec3(dx, dy, dz), glm::vec3(0.f, 0.f, -1.f));
    glm::vec3 hit = projectToDragPlane(ro, rd);
    glm::vec3 delta = hit - dragStartHit_;

    if (mode_ == "translate") {
        glm::vec3 move(0.f);
        if (activeAxis_ == "x" || activeAxis_ == "y" || activeAxis_ == "z") {
            float dist = glm::dot(delta, dragAxisDir_);
            move = dragAxisDir_ * dist;
        } else if (activeAxis_ == "xy") {
            move = axisWorld(0) * glm::dot(delta, axisWorld(0)) +
                   axisWorld(1) * glm::dot(delta, axisWorld(1));
        } else if (activeAxis_ == "yz") {
            move = axisWorld(1) * glm::dot(delta, axisWorld(1)) +
                   axisWorld(2) * glm::dot(delta, axisWorld(2));
        } else if (activeAxis_ == "xz") {
            move = axisWorld(0) * glm::dot(delta, axisWorld(0)) +
                   axisWorld(2) * glm::dot(delta, axisWorld(2));
        } else if (activeAxis_ == "xyz") {
            move = delta;
        }
        position_ = dragStartPos_ + move;
        applySnapTranslate(position_);
    } else if (mode_ == "rotate") {
        glm::vec3 from = safeNormalize(dragStartHit_ - position_, dragAxisDir_);
        glm::vec3 to = safeNormalize(hit - position_, from);
        // angle around dragAxisDir_
        glm::vec3 c = glm::cross(from, to);
        float sinA = glm::dot(c, dragAxisDir_);
        float cosA = glm::clamp(glm::dot(from, to), -1.f, 1.f);
        float ang = std::atan2(sinA, cosA);
        ang = applySnapRotate(ang);
        rotation_ = dragStartRot_;
        if (activeAxis_ == "x")
            rotation_.x = dragStartRot_.x + ang;
        else if (activeAxis_ == "y")
            rotation_.y = dragStartRot_.y + ang;
        else
            rotation_.z = dragStartRot_.z + ang;
    } else if (mode_ == "scale") {
        float dist = glm::dot(delta, dragAxisDir_);
        float factor = 1.f + dist / std::max(size_, 1e-3f);
        if (activeAxis_ == "xyz") {
            float s = applySnapScale(std::max(0.01f, dragStartScale_.x * factor));
            scale_ = {s, s, s};
        } else {
            scale_ = dragStartScale_;
            float s = applySnapScale(std::max(0.01f, (activeAxis_ == "x"   ? dragStartScale_.x
                                                      : activeAxis_ == "y" ? dragStartScale_.y
                                                                           : dragStartScale_.z) *
                                                         factor));
            if (activeAxis_ == "x")
                scale_.x = s;
            else if (activeAxis_ == "y")
                scale_.y = s;
            else
                scale_.z = s;
        }
    } else {  // bound — move corresponding face by translating position / expanding bounds
        float dist = glm::dot(delta, dragAxisDir_);
        if (activeAxis_ == "bx")
            boundsMax_.x = std::max(boundsMin_.x + 0.01f, boundsMax_.x + dist / std::max(scale_.x, 1e-3f));
        else if (activeAxis_ == "by")
            boundsMax_.y = std::max(boundsMin_.y + 0.01f, boundsMax_.y + dist / std::max(scale_.y, 1e-3f));
        else if (activeAxis_ == "bz")
            boundsMax_.z = std::max(boundsMin_.z + 0.01f, boundsMax_.z + dist / std::max(scale_.z, 1e-3f));
        else if (activeAxis_ == "bnx")
            boundsMin_.x = std::min(boundsMax_.x - 0.01f, boundsMin_.x + dist / std::max(scale_.x, 1e-3f));
        else if (activeAxis_ == "bny")
            boundsMin_.y = std::min(boundsMax_.y - 0.01f, boundsMin_.y + dist / std::max(scale_.y, 1e-3f));
        else if (activeAxis_ == "bnz")
            boundsMin_.z = std::min(boundsMax_.z - 0.01f, boundsMin_.z + dist / std::max(scale_.z, 1e-3f));
        // reset start so incremental face edits accumulate smoothly
        dragStartHit_ = hit;
    }

    rebuildParts();
    return true;
}

void TransformGizmo::endDrag() {
    dragging_ = false;
    activeAxis_.clear();
    rebuildParts();
}

bool TransformGizmo::validPart(int index) const {
    return index >= 0 && index < static_cast<int>(parts_.size());
}

std::string TransformGizmo::getPartKind(int index) const {
    if (!validPart(index)) throw Exception("TransformGizmo::getPartKind: bad index");
    return parts_[index].kind;
}
std::string TransformGizmo::getPartAxis(int index) const {
    if (!validPart(index)) throw Exception("TransformGizmo::getPartAxis: bad index");
    return parts_[index].axis;
}
float TransformGizmo::getPartColorR(int index) const {
    if (!validPart(index)) throw Exception("TransformGizmo::getPartColorR: bad index");
    return parts_[index].color.r;
}
float TransformGizmo::getPartColorG(int index) const {
    if (!validPart(index)) throw Exception("TransformGizmo::getPartColorG: bad index");
    return parts_[index].color.g;
}
float TransformGizmo::getPartColorB(int index) const {
    if (!validPart(index)) throw Exception("TransformGizmo::getPartColorB: bad index");
    return parts_[index].color.b;
}
float TransformGizmo::getPartColorA(int index) const {
    if (!validPart(index)) throw Exception("TransformGizmo::getPartColorA: bad index");
    return parts_[index].color.a;
}
float TransformGizmo::getPartOriginX(int index) const {
    if (!validPart(index)) throw Exception("TransformGizmo::getPartOriginX: bad index");
    return parts_[index].origin.x;
}
float TransformGizmo::getPartOriginY(int index) const {
    if (!validPart(index)) throw Exception("TransformGizmo::getPartOriginY: bad index");
    return parts_[index].origin.y;
}
float TransformGizmo::getPartOriginZ(int index) const {
    if (!validPart(index)) throw Exception("TransformGizmo::getPartOriginZ: bad index");
    return parts_[index].origin.z;
}
float TransformGizmo::getPartDirX(int index) const {
    if (!validPart(index)) throw Exception("TransformGizmo::getPartDirX: bad index");
    return parts_[index].dir.x;
}
float TransformGizmo::getPartDirY(int index) const {
    if (!validPart(index)) throw Exception("TransformGizmo::getPartDirY: bad index");
    return parts_[index].dir.y;
}
float TransformGizmo::getPartDirZ(int index) const {
    if (!validPart(index)) throw Exception("TransformGizmo::getPartDirZ: bad index");
    return parts_[index].dir.z;
}
float TransformGizmo::getPartLength(int index) const {
    if (!validPart(index)) throw Exception("TransformGizmo::getPartLength: bad index");
    return parts_[index].length;
}
float TransformGizmo::getPartRadius(int index) const {
    if (!validPart(index)) throw Exception("TransformGizmo::getPartRadius: bad index");
    return parts_[index].radius;
}

}  // namespace eve::editor
