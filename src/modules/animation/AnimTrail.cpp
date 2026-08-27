#include "animation/AnimTrail.h"

#include "animation/AnimationTime.h"
#include "animation/AnimPose.h"

#include "common/Exception.h"
#include "graphics/Graphics.h"

#include <algorithm>
#include <cmath>

namespace eve::animation {

// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

AnimTrail::AnimTrail(int capacity) {
    setCapacity(capacity);
}

void AnimTrail::setCapacity(int capacity) {
    if (capacity < 2) throw Exception("AnimTrail.setCapacity: capacity must be >= 2");
    capacity_ = capacity;
    while (static_cast<int>(points_.size()) > capacity_) {
        points_.erase(points_.begin());
    }
}

void AnimTrail::setDuration(float seconds) {
    duration_ = seconds;
}

void AnimTrail::setMinDistance(float distance) {
    if (distance < 0.f) throw Exception("AnimTrail.setMinDistance: must be >= 0");
    minDistance_ = distance;
}

void AnimTrail::setWidth(float pixels) {
    if (pixels <= 0.f) throw Exception("AnimTrail.setWidth: must be > 0");
    width_ = pixels;
}

void AnimTrail::setColor(float r, float g, float b, float a) {
    colorR_ = r;
    colorG_ = g;
    colorB_ = b;
    colorA_ = a;
}

void AnimTrail::setFade(bool enable) { fade_ = enable; }

void AnimTrail::setStyle(const std::string &style) {
    if (style == "line") {
        style_ = Style::Line;
        return;
    }
    if (style == "points") {
        style_ = Style::Points;
        return;
    }
    throw Exception("AnimTrail.setStyle: unknown style '%s' (use line|points)", style.c_str());
}

std::string AnimTrail::getStyle() const {
    return style_ == Style::Line ? "line" : "points";
}

void AnimTrail::setDrawScale(float sx, float sy) {
    drawScaleX_ = sx;
    drawScaleY_ = sy;
}

void AnimTrail::setDrawOffset(float ox, float oy) {
    drawOffsetX_ = ox;
    drawOffsetY_ = oy;
}

void AnimTrail::addPoint(float x, float y) { pushPoint(x, y, 0.f); }

void AnimTrail::addPoint3(float x, float y, float z) { pushPoint(x, y, z); }

void AnimTrail::projectPlane(float x, float y, float z, const std::string &plane, float *outX,
                             float *outY) const {
    if (!outX || !outY) return;
    if (plane == "xy" || plane.empty()) {
        *outX = x;
        *outY = y;
        return;
    }
    if (plane == "xz") {
        *outX = x;
        *outY = z;
        return;
    }
    if (plane == "yz") {
        *outX = y;
        *outY = z;
        return;
    }
    throw Exception("AnimTrail: unknown plane '%s' (use xy|xz|yz)", plane.c_str());
}

void AnimTrail::sampleBone(const AnimPose *pose, int boneIndex, const std::string &plane) {
    sampleBoneOffset(pose, boneIndex, 0.f, 0.f, 0.f, plane);
}

void AnimTrail::sampleBoneOffset(const AnimPose *pose, int boneIndex, float ox, float oy, float oz,
                                 const std::string &plane) {
    if (!pose) throw Exception("AnimTrail.sampleBone: pose is null");
    if (boneIndex < 0 || boneIndex >= pose->getBoneCount()) {
        throw Exception("AnimTrail.sampleBone: boneIndex %d out of range (count=%d)", boneIndex,
                        pose->getBoneCount());
    }
    const float wx = pose->getWorldPositionX(boneIndex) + ox;
    const float wy = pose->getWorldPositionY(boneIndex) + oy;
    const float wz = pose->getWorldPositionZ(boneIndex) + oz;
    float px = 0.f, py = 0.f;
    projectPlane(wx, wy, wz, plane, &px, &py);
    pushPoint(px, py, plane == "xy" || plane.empty() ? wz : (plane == "xz" ? wy : wx));
}

void AnimTrail::pushPoint(float x, float y, float z) {
    if (!points_.empty() && minDistance_ > 0.f) {
        const Point &last = points_.back();
        const float dx = x - last.x;
        const float dy = y - last.y;
        const float dz = z - last.z;
        if (dx * dx + dy * dy + dz * dz < minDistance_ * minDistance_) return;
    }
    Point p;
    p.x   = x;
    p.y   = y;
    p.z   = z;
    p.age = 0.f;
    points_.push_back(p);
    while (static_cast<int>(points_.size()) > capacity_) {
        points_.erase(points_.begin());
    }
}

void AnimTrail::clear() { points_.clear(); }

void AnimTrail::updateUnchecked(float dt) {
    if (dt < 0.f) dt = 0.f;
    for (Point &p : points_) p.age += dt;
    if (duration_ > 0.f) {
        auto it = points_.begin();
        while (it != points_.end()) {
            if (it->age > duration_) {
                it = points_.erase(it);
            } else {
                ++it;
            }
        }
    }
}

eve::Result<void> AnimTrail::advance(const eve::SimulationStep& step) {
    auto seconds = detail::secondsForStep(step, hasLastTick_, lastTick_, "AnimTrail");
    if (!seconds) return eve::Result<void>::failure(seconds.status());
    updateUnchecked(std::move(seconds).takeValue());
    lastTick_ = step.tick;
    hasLastTick_ = true;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

void AnimTrail::update(float dt) {
    auto step = detail::legacyStep(dt, hasLastTick_, lastTick_, "AnimTrail");
    if (!step) {
        step.ignore("legacy AnimTrail update");
        return;
    }
    advance(std::move(step).takeValue()).ignore("legacy AnimTrail update");
}

void AnimTrail::requireIndex(int index) const {
    if (index < 0 || index >= getPointCount()) {
        throw Exception("AnimTrail: point index %d out of range (count=%d)", index, getPointCount());
    }
}

float AnimTrail::getPointX(int index) const {
    requireIndex(index);
    return points_[static_cast<size_t>(index)].x;
}

float AnimTrail::getPointY(int index) const {
    requireIndex(index);
    return points_[static_cast<size_t>(index)].y;
}

float AnimTrail::getPointZ(int index) const {
    requireIndex(index);
    return points_[static_cast<size_t>(index)].z;
}

float AnimTrail::getPointAge(int index) const {
    requireIndex(index);
    return points_[static_cast<size_t>(index)].age;
}

float AnimTrail::alphaFor(const Point &p) const {
    if (!fade_ || duration_ <= 0.f) return colorA_;
    const float t = 1.f - std::min(1.f, p.age / duration_);
    return colorA_ * t;
}

float AnimTrail::getPointAlpha(int index) const {
    requireIndex(index);
    return alphaFor(points_[static_cast<size_t>(index)]);
}

void AnimTrail::toScreen(float x, float y, float *sx, float *sy) const {
    if (!sx || !sy) return;
    *sx = x * drawScaleX_ + drawOffsetX_;
    *sy = y * drawScaleY_ + drawOffsetY_;
}

void AnimTrail::drawDot(graphics::Graphics *gfx, float x, float y, float alpha) const {
    if (!gfx || alpha <= 0.f) return;
    float sx = 0.f, sy = 0.f;
    toScreen(x, y, &sx, &sy);
    const float half = width_ * 0.5f;
    gfx->drawSolidRect(sx - half, sy - half, width_, width_,
                       Color(colorR_, colorG_, colorB_, alpha));
}

void AnimTrail::drawSegment(graphics::Graphics *gfx, float x0, float y0, float x1, float y1,
                            float a0, float a1) const {
    if (!gfx) return;
    float sx0 = 0.f, sy0 = 0.f, sx1 = 0.f, sy1 = 0.f;
    toScreen(x0, y0, &sx0, &sy0);
    toScreen(x1, y1, &sx1, &sy1);
    const float dx  = sx1 - sx0;
    const float dy  = sy1 - sy0;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-4f) {
        drawDot(gfx, x1, y1, a1);
        return;
    }
    const float step = std::max(1.f, width_ * 0.75f);
    const int steps  = std::max(1, int(len / step));
    const float half = width_ * 0.5f;
    for (int i = 0; i <= steps; ++i) {
        const float t = float(i) / float(steps);
        const float a = a0 + (a1 - a0) * t;
        if (a <= 0.f) continue;
        const float px = sx0 + dx * t;
        const float py = sy0 + dy * t;
        gfx->drawSolidRect(px - half, py - half, width_, width_,
                           Color(colorR_, colorG_, colorB_, a));
    }
}

void AnimTrail::draw(graphics::Graphics *gfx) {
    if (!gfx || points_.empty()) return;

    if (style_ == Style::Points) {
        for (const Point &p : points_) {
            drawDot(gfx, p.x, p.y, alphaFor(p));
        }
        return;
    }

    // Line: connect oldest → newest with per-vertex fade.
    for (size_t i = 1; i < points_.size(); ++i) {
        const Point &a = points_[i - 1];
        const Point &b = points_[i];
        drawSegment(gfx, a.x, a.y, b.x, b.y, alphaFor(a), alphaFor(b));
    }
    // Emphasize the head.
    if (!points_.empty()) {
        const Point &head = points_.back();
        drawDot(gfx, head.x, head.y, alphaFor(head));
    }
}

}  // namespace eve::animation
