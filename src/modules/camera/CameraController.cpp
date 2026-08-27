#include "camera/CameraController.h"

#include "common/CameraObstruction.h"
#include "common/Capability.h"
#include "common/Json.h"
#include "platform_event/PlatformEvent.h"
#include "graphics/RenderSystem3D.h"
#include "scene/SceneNodeRef.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace eve::camera {

namespace {

std::string jsonString(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (const unsigned char c : value) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20)
                    out << "\\u00" << "0123456789abcdef"[c >> 4] << "0123456789abcdef"[c & 0xf];
                else
                    out << static_cast<char>(c);
        }
    }
    out << '"';
    return out.str();
}

glm::vec3 jsonVec3(eve::json::Value value, glm::vec3 fallback) {
    if (!value.isArray() || value.size() != 3) return fallback;
    return {value.at(0).asFloat(fallback.x), value.at(1).asFloat(fallback.y),
            value.at(2).asFloat(fallback.z)};
}

}  // namespace

Module_IMPL(Camera, new Camera());

CameraController::CameraController() = default;

void CameraController::setCamera(graphics::Camera3D* cam) { cam_ = cam; }

graphics::Camera3D* CameraController::getCamera() const { return cam_; }

void CameraController::setTarget(float x, float y, float z) {
    targetNodeHost_.clear();
    targetNodeId_.clear();
    target_ = glm::vec3(x, y, z);
}

void CameraController::setTargetNode(scene::SceneNodeRef* node) {
    targetNodeHost_ = node ? node->getHostName() : std::string();
    targetNodeId_   = node ? node->getNodeId() : std::string();
}

float CameraController::getTargetX() const { return target_.x; }
float CameraController::getTargetY() const { return target_.y; }
float CameraController::getTargetZ() const { return target_.z; }

void CameraController::setOffset(float x, float y, float z) { offset_ = glm::vec3(x, y, z); }

void CameraController::setLookAhead(float x, float y, float z) { lookAhead_ = glm::vec3(x, y, z); }

bool CameraController::validMode(const std::string& mode) {
    return mode == "follow" || mode == "orbit" || mode == "topdown" || mode == "firstperson" || mode == "cinematic";
}

void CameraController::setMode(const std::string& mode) {
    if (validMode(mode)) mode_ = mode;
}

std::string CameraController::getMode() const { return mode_; }

void CameraController::setRadius(float r) { radius_ = std::max(0.01f, r); }
void CameraController::setAzimuth(float deg) { azimuthDeg_ = deg; }
void CameraController::setElevation(float deg) { elevationDeg_ = glm::clamp(deg, -89.f, 89.f); }
void CameraController::setOrbitSpeed(float degPerSec) { orbitSpeedDeg_ = degPerSec; }

void CameraController::setYaw(float deg) { yawDeg_ = deg; }
void CameraController::setPitch(float deg) { pitchDeg_ = glm::clamp(deg, -89.f, 89.f); }
void CameraController::addInput(float yawDeltaDeg, float pitchDeltaDeg, float zoomDelta) {
    yawDeg_ += yawDeltaDeg;
    azimuthDeg_ += yawDeltaDeg;
    setPitch(pitchDeg_ + pitchDeltaDeg);
    setElevation(elevationDeg_ + pitchDeltaDeg);
    setRadius(radius_ + zoomDelta);
}

void CameraController::setComposition(float screenX, float screenY) {
    composition_ = glm::clamp(glm::vec2(screenX, screenY), glm::vec2(-0.9f), glm::vec2(0.9f));
}

void  CameraController::setDeadZone(float radius) { deadZone_ = std::max(0.f, radius); }
void  CameraController::setFov(float degrees) { fovDeg_ = glm::clamp(degrees, 1.f, 179.f); }
float CameraController::getFov() const { return fovDeg_; }

void CameraController::setSmooth(float damping) {
    smooth_         = std::max(0.f, damping);
    positionSmooth_ = smooth_;
    targetSmooth_   = smooth_;
}
void CameraController::setPositionSmooth(float damping) { positionSmooth_ = std::max(0.f, damping); }
void CameraController::setTargetSmooth(float damping) { targetSmooth_ = std::max(0.f, damping); }

void CameraController::setMaxSpeed(float unitsPerSec) { maxSpeed_ = std::max(0.f, unitsPerSec); }

void CameraController::setCollisionEnabled(bool enabled) { collisionEnabled_ = enabled; }
void CameraController::setCollisionRadius(float radius) { collisionRadius_ = std::max(0.f, radius); }
void CameraController::setCollisionRecovery(float damping) { collisionRecovery_ = std::max(0.f, damping); }
void CameraController::setCollisionMask(uint64_t maskBits) { collisionMask_ = maskBits; }
void CameraController::setCollisionIgnoredBody(int bodyId) { collisionIgnoredBody_ = bodyId; }
void CameraController::clearCollisionBoxes() { collisionBoxes_.clear(); }
void CameraController::addCollisionBox(float minX, float minY, float minZ, float maxX, float maxY, float maxZ) {
    CollisionBox b;
    b.min = glm::min(glm::vec3(minX, minY, minZ), glm::vec3(maxX, maxY, maxZ));
    b.max = glm::max(glm::vec3(minX, minY, minZ), glm::vec3(maxX, maxY, maxZ));
    collisionBoxes_.push_back(b);
}
bool CameraController::isObstructed() const { return obstructed_; }
int CameraController::getCollisionBodyId() const { return collisionBodyId_; }

bool CameraController::addRig(const std::string& name, const std::string& mode, int priority) {
    if (name.empty() || !validMode(mode)) return false;
    for (const auto& rig : rigs_)
        if (rig.name == name) return false;
    Rig rig;
    rig.name     = name;
    rig.mode     = mode;
    rig.priority = priority;
    rigs_.push_back(rig);
    saveRigState(name);
    directorEnabled_ = true;
    return true;
}

bool CameraController::removeRig(const std::string& name) {
    auto it = std::find_if(rigs_.begin(), rigs_.end(), [&](const Rig& r) { return r.name == name; });
    if (it == rigs_.end()) return false;
    rigs_.erase(it);
    if (activeRig_ == name) activeRig_.clear();
    return true;
}

bool CameraController::setRigPriority(const std::string& name, int priority) {
    for (auto& rig : rigs_)
        if (rig.name == name) {
            rig.priority = priority;
            return true;
        }
    return false;
}

bool CameraController::setRigEnabled(const std::string& name, bool enabled) {
    for (auto& rig : rigs_)
        if (rig.name == name) {
            rig.enabled = enabled;
            return true;
        }
    return false;
}

bool CameraController::saveRigState(const std::string& name) {
    for (auto& rig : rigs_) {
        if (rig.name != name) continue;
        rig.target      = target_;
        rig.offset      = offset_;
        rig.lookAhead   = lookAhead_;
        rig.composition = composition_;
        rig.radius      = radius_;
        rig.azimuth     = azimuthDeg_;
        rig.elevation   = elevationDeg_;
        rig.yaw         = yawDeg_;
        rig.pitch       = pitchDeg_;
        rig.fov         = fovDeg_;
        rig.smooth      = smooth_;
        rig.maxSpeed    = maxSpeed_;
        return true;
    }
    return false;
}

bool CameraController::activateRig(const std::string& name, float blendTime) {
    auto it = std::find_if(rigs_.begin(), rigs_.end(), [&](const Rig& r) { return r.name == name && r.enabled; });
    if (it == rigs_.end()) return false;
    const View from = initialized_ ? cur_ : desired();
    activeRig_      = name;
    mode_           = it->mode;
    target_         = it->target;
    offset_         = it->offset;
    lookAhead_      = it->lookAhead;
    composition_    = it->composition;
    radius_         = it->radius;
    azimuthDeg_     = it->azimuth;
    elevationDeg_   = it->elevation;
    yawDeg_         = it->yaw;
    pitchDeg_       = it->pitch;
    fovDeg_         = it->fov;
    setSmooth(it->smooth);
    maxSpeed_              = it->maxSpeed;
    const bool wasBlending = blending_;
    blending_              = false;
    const View to          = desired();
    blending_              = wasBlending;
    if (blendTime > 0.f) {
        blendFrom_ = from;
        blendTo_   = to;
        blendDur_  = blendTime;
        blendT_    = 0.f;
        blending_  = true;
    } else {
        blending_ = false;
    }
    return true;
}

std::string CameraController::getActiveRig() const { return activeRig_; }

void CameraController::addImpulse(float positionAmplitude, float rotationAmplitude, float duration, unsigned int seed) {
    if (duration <= 0.f || (positionAmplitude <= 0.f && rotationAmplitude <= 0.f)) return;
    impulses_.push_back({std::max(0.f, positionAmplitude), std::max(0.f, rotationAmplitude), duration, 0.f, seed, 0.f});
}
void CameraController::addFovImpulse(float degrees, float duration) {
    if (duration <= 0.f || degrees == 0.f) return;
    impulses_.push_back({0.f, 0.f, duration, 0.f, 0u, degrees});
}
void CameraController::clearImpulses() { impulses_.clear(); }

void CameraController::addView(const std::string& name, float ex, float ey, float ez, float tx, float ty, float tz) {
    View v;
    v.name   = name;
    v.eye    = glm::vec3(ex, ey, ez);
    v.target = glm::vec3(tx, ty, tz);
    v.fov    = fovDeg_;
    views_.push_back(v);
}

bool CameraController::switchTo(const std::string& name, float blendTime) {
    int idx = -1;
    for (int i = 0; i < static_cast<int>(views_.size()); ++i) {
        if (views_[i].name == name) {
            idx = i;
            break;
        }
    }
    if (idx < 0) return false;

    if (viewIndex_ >= 0 && viewIndex_ < static_cast<int>(views_.size())) {
        blendFrom_ = views_[viewIndex_];
    } else {
        blendFrom_ = cur_;
    }
    blendTo_   = views_[idx];
    viewIndex_ = idx;

    if (blendTime > 0.f) {
        blending_ = true;
        blendDur_ = blendTime;
        blendT_   = 0.f;
    } else {
        blending_ = false;
        blendDur_ = 1.f;
        blendT_   = 0.f;
    }
    return true;
}

void CameraController::playSequence(float stepTime) {
    playing_  = true;
    stepTime_ = stepTime > 0.f ? stepTime : 1.f;
    timer_    = 0.f;
    if (!blending_ && views_.empty()) return;
    if (viewIndex_ < 0 || viewIndex_ >= static_cast<int>(views_.size())) {
        switchTo(views_.front().name, 0.f);
    }
}

void CameraController::stopSequence() { playing_ = false; }

bool CameraController::isPlaying() const { return playing_; }

void CameraController::clearTimeline() {
    timelineCuts_.clear();
    timelineEvents_.clear();
    timelineFloats_.clear();
    pendingTimelineEvents_.clear();
    timelineDuration_ = 0.f;
    stopTimeline();
}

bool CameraController::addTimelineFloat(float time, const std::string& property, float value) {
    if (time < 0.f || (property != "fov" && property != "radius" && property != "smooth" &&
                       property != "compositionX" && property != "compositionY"))
        return false;
    timelineFloats_.push_back({time, property, value});
    std::stable_sort(timelineFloats_.begin(), timelineFloats_.end(), [](const auto& a, const auto& b) {
        return a.time < b.time;
    });
    timelineDuration_ = std::max(timelineDuration_, time);
    return true;
}

bool CameraController::addTimelineCut(float time, const std::string& rigName, float blendTime) {
    if (time < 0.f || blendTime < 0.f) return false;
    const bool known = std::any_of(rigs_.begin(), rigs_.end(), [&](const Rig& r) { return r.name == rigName; });
    if (!known) return false;
    timelineCuts_.push_back({time, rigName, blendTime, false});
    std::stable_sort(timelineCuts_.begin(), timelineCuts_.end(),
                     [](const TimelineCut& a, const TimelineCut& b) { return a.time < b.time; });
    timelineDuration_ = std::max(timelineDuration_, time);
    return true;
}

bool CameraController::addTimelineEvent(float time, const std::string& name, const std::string& data) {
    if (time < 0.f || name.empty()) return false;
    timelineEvents_.push_back({time, name, data, false});
    std::stable_sort(timelineEvents_.begin(), timelineEvents_.end(),
                     [](const TimelineEvent& a, const TimelineEvent& b) { return a.time < b.time; });
    timelineDuration_ = std::max(timelineDuration_, time);
    return true;
}

void CameraController::setEventSink(platform_event::PlatformEvent* sink) { eventSink_ = sink; }

void CameraController::playTimeline(bool loop) {
    timelineLoop_    = loop;
    timelinePlaying_ = !timelineCuts_.empty() || !timelineEvents_.empty();
}
void CameraController::pauseTimeline() { timelinePlaying_ = false; }
void CameraController::stopTimeline() {
    timelinePlaying_ = false;
    timelineTime_    = 0.f;
    for (auto& cut : timelineCuts_) cut.fired = false;
    for (auto& event : timelineEvents_) event.fired = false;
}
void CameraController::seekTimeline(float time, bool fireEvents) {
    timelineTime_ = glm::clamp(time, 0.f, std::max(0.f, timelineDuration_));
    for (auto& cut : timelineCuts_) cut.fired = cut.time <= timelineTime_;
    const TimelineCut* latest = nullptr;
    for (const auto& cut : timelineCuts_)
        if (cut.time <= timelineTime_) latest = &cut;
    if (latest) activateRig(latest->rig, 0.f);
    for (auto& marker : timelineEvents_) {
        marker.fired = marker.time <= timelineTime_;
        if (fireEvents && marker.fired) emitTimelineEvent(marker);
    }
    evaluateTimelineFloats();
}
bool        CameraController::isTimelinePlaying() const { return timelinePlaying_; }
float       CameraController::getTimelineTime() const { return timelineTime_; }
std::string CameraController::consumeTimelineEvent() {
    if (pendingTimelineEvents_.empty()) return {};
    auto [out, data] = std::move(pendingTimelineEvents_.front());
    pendingTimelineEvents_.pop_front();
    consumedTimelineEventData_ = std::move(data);
    return out;
}
std::string CameraController::getTimelineEventData() const { return consumedTimelineEventData_; }
int CameraController::getPendingTimelineEventCount() const { return static_cast<int>(pendingTimelineEvents_.size()); }
float CameraController::getTimelineDuration() const { return timelineDuration_; }
int CameraController::getRigCount() const { return static_cast<int>(rigs_.size()); }

std::string CameraController::serializeAsset() const {
    std::ostringstream out;
    out << "{\"version\":1,\"rigs\":[";
    bool first = true;
    for (const auto& rig : rigs_) {
        if (!first) out << ',';
        first = false;
        out << "{\"name\":" << jsonString(rig.name) << ",\"mode\":" << jsonString(rig.mode)
            << ",\"priority\":" << rig.priority << ",\"enabled\":" << (rig.enabled ? "true" : "false")
            << ",\"target\":[" << rig.target.x << ',' << rig.target.y << ',' << rig.target.z
            << "],\"offset\":[" << rig.offset.x << ',' << rig.offset.y << ',' << rig.offset.z
            << "],\"lookAhead\":[" << rig.lookAhead.x << ',' << rig.lookAhead.y << ',' << rig.lookAhead.z
            << "],\"composition\":[" << rig.composition.x << ',' << rig.composition.y << "]"
            << ",\"radius\":" << rig.radius << ",\"azimuth\":" << rig.azimuth
            << ",\"elevation\":" << rig.elevation << ",\"yaw\":" << rig.yaw << ",\"pitch\":" << rig.pitch
            << ",\"fov\":" << rig.fov << ",\"smooth\":" << rig.smooth << ",\"maxSpeed\":" << rig.maxSpeed << '}';
    }
    out << "],\"cuts\":[";
    first = true;
    for (const auto& cut : timelineCuts_) {
        if (!first) out << ',';
        first = false;
        out << "{\"time\":" << cut.time << ",\"rig\":" << jsonString(cut.rig) << ",\"blend\":" << cut.blend << '}';
    }
    out << "],\"events\":[";
    first = true;
    for (const auto& marker : timelineEvents_) {
        if (!first) out << ',';
        first = false;
        out << "{\"time\":" << marker.time << ",\"name\":" << jsonString(marker.name)
            << ",\"data\":" << jsonString(marker.data) << '}';
    }
    out << "],\"floats\":[";
    first = true;
    for (const auto& key : timelineFloats_) {
        if (!first) out << ',';
        first = false;
        out << "{\"time\":" << key.time << ",\"property\":" << jsonString(key.property)
            << ",\"value\":" << key.value << '}';
    }
    out << "]}";
    return out.str();
}

bool CameraController::deserializeAsset(const std::string& text) {
    const auto document = eve::json::Document::parse(text);
    const auto root     = document.root();
    if (!root.isObject() || root.getInt("version", 0) != 1) return false;
    std::vector<Rig> newRigs;
    const auto rigs = root.get("rigs");
    if (rigs && !rigs.isArray()) return false;
    for (size_t i = 0; i < rigs.size(); ++i) {
        const auto item = rigs.at(i);
        if (!item.isObject()) return false;
        Rig rig;
        rig.name = item.getString("name");
        rig.mode = item.getString("mode");
        if (rig.name.empty() || !validMode(rig.mode)) return false;
        rig.priority   = item.getInt("priority");
        rig.enabled    = item.getBool("enabled", true);
        rig.target     = jsonVec3(item.get("target"), rig.target);
        rig.offset     = jsonVec3(item.get("offset"), rig.offset);
        rig.lookAhead  = jsonVec3(item.get("lookAhead"), rig.lookAhead);
        const auto composition = item.get("composition");
        if (composition.isArray() && composition.size() == 2)
            rig.composition = {composition.at(0).asFloat(), composition.at(1).asFloat()};
        rig.radius    = item.getFloat("radius", rig.radius);
        rig.azimuth   = item.getFloat("azimuth", rig.azimuth);
        rig.elevation = item.getFloat("elevation", rig.elevation);
        rig.yaw       = item.getFloat("yaw", rig.yaw);
        rig.pitch     = item.getFloat("pitch", rig.pitch);
        rig.fov       = item.getFloat("fov", rig.fov);
        rig.smooth    = item.getFloat("smooth", rig.smooth);
        rig.maxSpeed  = item.getFloat("maxSpeed", rig.maxSpeed);
        newRigs.push_back(std::move(rig));
    }
    clearTimeline();
    rigs_ = std::move(newRigs);
    activeRig_.clear();
    directorEnabled_ = !rigs_.empty();
    const auto cuts = root.get("cuts");
    if (cuts && !cuts.isArray()) return false;
    for (size_t i = 0; i < cuts.size(); ++i) {
        const auto item = cuts.at(i);
        if (!item.isObject() || !addTimelineCut(item.getFloat("time", -1.f), item.getString("rig"),
                                                item.getFloat("blend", 0.f)))
            return false;
    }
    const auto events = root.get("events");
    if (events && !events.isArray()) return false;
    for (size_t i = 0; i < events.size(); ++i) {
        const auto item = events.at(i);
        if (!item.isObject() || !addTimelineEvent(item.getFloat("time", -1.f), item.getString("name"),
                                                  item.getString("data")))
            return false;
    }
    const auto floats = root.get("floats");
    if (floats && !floats.isArray()) return false;
    for (size_t i = 0; i < floats.size(); ++i) {
        const auto item = floats.at(i);
        if (!item.isObject() || !addTimelineFloat(item.getFloat("time", -1.f), item.getString("property"),
                                                  item.getFloat("value")))
            return false;
    }
    return true;
}

void CameraController::emitTimelineEvent(const TimelineEvent& marker) {
    pendingTimelineEvents_.emplace_back(marker.name, marker.data);
    if (eventSink_) eventSink_->pushData(marker.name, marker.data);
}

void CameraController::evaluateTimelineFloats() {
    for (const std::string property : {"fov", "radius", "smooth", "compositionX", "compositionY"}) {
        const TimelineFloat* before = nullptr;
        const TimelineFloat* after  = nullptr;
        for (const auto& key : timelineFloats_) {
            if (key.property != property) continue;
            if (key.time <= timelineTime_) before = &key;
            if (key.time >= timelineTime_) {
                after = &key;
                break;
            }
        }
        if (!before && !after) continue;
        if (!before) before = after;
        if (!after) after = before;
        const float span = after->time - before->time;
        const float t = span > 1e-6f ? (timelineTime_ - before->time) / span : 0.f;
        const float value = glm::mix(before->value, after->value, glm::clamp(t, 0.f, 1.f));
        if (property == "fov") setFov(value);
        else if (property == "radius") setRadius(value);
        else if (property == "smooth") setSmooth(value);
        else if (property == "compositionX") setComposition(value, composition_.y);
        else setComposition(composition_.x, value);
    }
}

void CameraController::updateTrackedTarget() {
    if (targetNodeHost_.empty() || targetNodeId_.empty()) return;
    const scene::SceneNodeRef node(targetNodeHost_, targetNodeId_);
    if (!node.isValid()) return;
    const glm::vec3 observed(node.getWorldPositionX(), node.getWorldPositionY(), node.getWorldPositionZ());
    if (!deadZoneInitialized_ || deadZone_ <= 0.f) {
        deadZoneTarget_      = observed;
        deadZoneInitialized_ = true;
    } else {
        const glm::vec3 delta  = observed - deadZoneTarget_;
        const float     length = glm::length(delta);
        if (length > deadZone_ && length > 1e-6f) deadZoneTarget_ += delta * ((length - deadZone_) / length);
    }
    target_ = deadZoneTarget_;
}

void CameraController::updateDirector() {
    if (!directorEnabled_ || timelinePlaying_) return;
    const Rig* best   = nullptr;
    const Rig* active = nullptr;
    for (const auto& rig : rigs_) {
        if (rig.name == activeRig_ && rig.enabled) active = &rig;
        if (rig.enabled && (!best || rig.priority > best->priority)) best = &rig;
    }
    if (active && best && active->priority >= best->priority) best = active;
    if (best && best->name != activeRig_) activateRig(best->name, initialized_ ? 0.25f : 0.f);
}

void CameraController::updateTimeline(float dt) {
    if (!timelinePlaying_) return;
    float remaining = std::max(0.f, dt);
    if (timelineDuration_ <= 0.f) remaining = 0.f;
    timelineTime_ += remaining;
    for (auto& cut : timelineCuts_) {
        if (!cut.fired && cut.time <= timelineTime_) {
            cut.fired = true;
            activateRig(cut.rig, cut.blend);
        }
    }
    for (auto& marker : timelineEvents_) {
        if (!marker.fired && marker.time <= timelineTime_) {
            marker.fired = true;
            emitTimelineEvent(marker);
        }
    }
    evaluateTimelineFloats();
    if (timelineTime_ < timelineDuration_) return;
    if (!timelineLoop_) {
        timelinePlaying_ = false;
        return;
    }
    timelineTime_ = timelineDuration_ > 0.f ? std::fmod(timelineTime_, timelineDuration_) : 0.f;
    for (auto& cut : timelineCuts_) cut.fired = false;
    for (auto& marker : timelineEvents_) marker.fired = false;
    for (auto& cut : timelineCuts_)
        if (cut.time <= timelineTime_) {
            cut.fired = true;
            activateRig(cut.rig, cut.blend);
        }
    for (auto& marker : timelineEvents_)
        if (marker.time <= timelineTime_) {
            marker.fired = true;
            emitTimelineEvent(marker);
        }
    evaluateTimelineFloats();
}

void CameraController::applyCollision(View& v, float dt) {
    obstructed_       = false;
    collisionBodyId_  = -1;
    const glm::vec3 ray             = v.eye - v.target;
    const float     desiredDistance = glm::length(ray);
    if (!collisionEnabled_ || desiredDistance < 1e-5f) {
        collisionDistance_ = -1.f;
        return;
    }
    const glm::vec3 dir     = ray / desiredDistance;
    float           nearest = desiredDistance;
    for (const auto& box : collisionBoxes_) {
        const glm::vec3 bmin = box.min - glm::vec3(collisionRadius_);
        const glm::vec3 bmax = box.max + glm::vec3(collisionRadius_);
        float           tmin = 0.f;
        float           tmax = desiredDistance;
        bool            hit  = true;
        for (int axis = 0; axis < 3; ++axis) {
            if (std::abs(dir[axis]) < 1e-6f) {
                if (v.target[axis] < bmin[axis] || v.target[axis] > bmax[axis]) {
                    hit = false;
                    break;
                }
            } else {
                float a = (bmin[axis] - v.target[axis]) / dir[axis];
                float b = (bmax[axis] - v.target[axis]) / dir[axis];
                if (a > b) std::swap(a, b);
                tmin = std::max(tmin, a);
                tmax = std::min(tmax, b);
                if (tmin > tmax) {
                    hit = false;
                    break;
                }
            }
        }
        if (hit && tmin >= 0.f && tmin < nearest) nearest = tmin;
    }
    if (auto* query = eve::cap::query<eve::ICameraObstructionQuery>()) {
        eve::CameraObstructionHit hit;
        if (query->sphereCast(v.target.x, v.target.y, v.target.z, v.eye.x, v.eye.y, v.eye.z,
                              collisionRadius_, collisionMask_, collisionIgnoredBody_, &hit) &&
            hit.hit) {
            const float distance = desiredDistance * glm::clamp(hit.fraction, 0.f, 1.f);
            if (distance < nearest) {
                nearest          = distance;
                collisionBodyId_ = hit.bodyId;
            }
        }
    }
    obstructed_              = nearest < desiredDistance;
    const float safeDistance = obstructed_ ? std::max(0.05f, nearest - 0.02f) : desiredDistance;
    if (collisionDistance_ < 0.f || safeDistance < collisionDistance_)
        collisionDistance_ = safeDistance;
    else {
        const float k = 1.f - std::exp(-collisionRecovery_ * std::max(0.f, dt));
        collisionDistance_ += (safeDistance - collisionDistance_) * k;
    }
    collisionDistance_ = std::min(collisionDistance_, desiredDistance);
    v.eye              = v.target + dir * collisionDistance_;
}

void CameraController::applyModifiers(View& v, float dt) {
    for (auto& impulse : impulses_) {
        impulse.age += std::max(0.f, dt);
        const float     life  = glm::clamp(1.f - impulse.age / impulse.duration, 0.f, 1.f);
        const float     phase = impulse.age * 37.f + static_cast<float>(impulse.seed % 997u);
        const glm::vec3 noise(std::sin(phase * 1.13f), std::sin(phase * 1.71f + 2.f), std::sin(phase * 2.07f + 4.f));
        v.eye += noise * (impulse.positionAmplitude * life);
        const glm::vec3 forward = glm::normalize(v.target - v.eye);
        glm::vec3       right   = glm::cross(forward, glm::vec3(0.f, 1.f, 0.f));
        if (glm::length(right) > 1e-5f)
            right = glm::normalize(right);
        else
            right = glm::vec3(1.f, 0.f, 0.f);
        const glm::vec3 up      = glm::normalize(glm::cross(right, forward));
        const float     angular = std::tan(glm::radians(impulse.rotationAmplitude * life));
        v.target += (right * noise.x + up * noise.y) * angular * glm::length(v.target - v.eye);
        v.fov = glm::clamp(v.fov + impulse.fovAmplitude * life, 1.f, 179.f);
    }
    impulses_.erase(
        std::remove_if(impulses_.begin(), impulses_.end(), [](const Impulse& i) { return i.age >= i.duration; }),
        impulses_.end());
}

CameraController::View CameraController::desired() const {
    View v;
    v.fov = fovDeg_;
    if (blending_) {
        float t  = blendDur_ > 0.f ? glm::clamp(blendT_ / blendDur_, 0.f, 1.f) : 1.f;
        t        = t * t * (3.f - 2.f * t);
        v.eye    = glm::mix(blendFrom_.eye, blendTo_.eye, t);
        v.target = glm::mix(blendFrom_.target, blendTo_.target, t);
        v.fov    = glm::mix(blendFrom_.fov, blendTo_.fov, t);
    } else if (mode_ == "orbit") {
        const float az = glm::radians(azimuthDeg_);
        const float el = glm::radians(elevationDeg_);
        glm::vec3   dir(std::cos(el) * std::sin(az), std::sin(el), std::cos(el) * std::cos(az));
        v.eye    = target_ + dir * radius_;
        v.target = target_;
    } else if (mode_ == "topdown") {
        v.eye    = target_ + glm::vec3(0.f, radius_, 0.f);
        v.target = target_;
    } else if (mode_ == "firstperson") {
        v.eye             = target_;
        const float yaw   = glm::radians(yawDeg_);
        const float pitch = glm::radians(pitchDeg_);
        glm::vec3   dir(std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw));
        v.target = v.eye + dir * 100.f;
    } else if (mode_ == "cinematic") {
        if (viewIndex_ >= 0 && viewIndex_ < static_cast<int>(views_.size())) {
            v = views_[viewIndex_];
        } else {
            v.target = target_ + lookAhead_;
            v.eye    = v.target + offset_;
        }
    } else {  // follow (默认)
        v.target = target_ + lookAhead_;
        v.eye    = v.target + offset_;
    }
    const glm::vec3 forward = glm::normalize(v.target - v.eye);
    glm::vec3       right   = glm::cross(forward, glm::vec3(0.f, 1.f, 0.f));
    if (glm::length(right) < 1e-5f)
        right = glm::vec3(1.f, 0.f, 0.f);
    else
        right = glm::normalize(right);
    glm::vec3   up       = glm::normalize(glm::cross(right, forward));
    const float distance = glm::length(v.target - v.eye);
    v.target += (right * composition_.x + up * composition_.y) * distance;
    return v;
}

void CameraController::applyView(const View& v) {
    if (!cam_) return;
    cam_->setEye(v.eye.x, v.eye.y, v.eye.z);
    cam_->setTarget(v.target.x, v.target.y, v.target.z);
    const glm::vec3 forward = glm::normalize(v.target - v.eye);
    glm::vec3       up(0.f, 1.f, 0.f);
    if (std::abs(glm::dot(forward, up)) > 0.999f) up = glm::vec3(0.f, 0.f, -1.f);
    cam_->setUp(up.x, up.y, up.z);
    cam_->setFov(v.fov);
}

void CameraController::easeToward(const View& v, float dt) {
    const float eyeK    = 1.f - std::exp(-positionSmooth_ * dt);
    const float targetK = 1.f - std::exp(-targetSmooth_ * dt);
    glm::vec3   deye    = (v.eye - cur_.eye) * eyeK;
    glm::vec3   dtgt    = (v.target - cur_.target) * targetK;
    if (maxSpeed_ > 0.f && dt > 0.f) {
        const float maxStep = maxSpeed_ * dt;
        const float le      = glm::length(deye);
        if (le > maxStep && le > 1e-6f) deye *= maxStep / le;
        const float lt = glm::length(dtgt);
        if (lt > maxStep && lt > 1e-6f) dtgt *= maxStep / lt;
    }
    cur_.eye += deye;
    cur_.target += dtgt;
    cur_.fov += (v.fov - cur_.fov) * eyeK;
}

void CameraController::snap() {
    cur_         = desired();
    initialized_ = true;
    applyView(cur_);
}

void CameraController::update(float dt) {
    if (!cam_) return;

    dt = std::max(0.f, dt);
    updateTrackedTarget();
    updateTimeline(dt);
    updateDirector();

    if (mode_ == "cinematic" && playing_ && !views_.empty()) {
        timer_ += dt;
        if (!blending_ && timer_ >= stepTime_) {
            const int next = (viewIndex_ + 1) % static_cast<int>(views_.size());
            switchTo(views_[next].name, blendDur_);
            timer_ = 0.f;
        }
    }

    if (blending_) {
        blendT_ += dt;
        if (blendT_ >= blendDur_) {
            blending_ = false;
            blendT_   = blendDur_;
        }
    }

    // orbit 自动盘旋：不断累积方位角
    if (mode_ == "orbit") {
        azimuthDeg_ += orbitSpeedDeg_ * dt;
    }

    View d = desired();
    applyCollision(d, dt);
    applyModifiers(d, dt);

    if (!initialized_) {
        cur_         = d;
        initialized_ = true;
    }
    easeToward(d, dt);
    applyView(cur_);
}

void Camera::expose(ssq::Class& cls) {
    // 模块级方法（如需要可在此添加）；当前只通过 eve.CameraController 暴露控制器。
}

void Camera::expose(ssq::Table& table) {
    auto cls = table.addClass(name, Camera::create, false);
    expose(cls);

    auto cc = table.addClass<CameraController>(
        "CameraController", std::function<CameraController*()>([]() { return new CameraController(); }), true);

    cc.addFunc("setCamera", &CameraController::setCamera);
    cc.addFunc("getCamera", &CameraController::getCamera);

    cc.addFunc("setTarget", &CameraController::setTarget);
    cc.addFunc("setTargetNode", &CameraController::setTargetNode);
    cc.addFunc("getTargetX", &CameraController::getTargetX);
    cc.addFunc("getTargetY", &CameraController::getTargetY);
    cc.addFunc("getTargetZ", &CameraController::getTargetZ);
    cc.addFunc("setOffset", &CameraController::setOffset);
    cc.addFunc("setLookAhead", &CameraController::setLookAhead);

    cc.addFunc("setMode", &CameraController::setMode);
    cc.addFunc("getMode", &CameraController::getMode);

    cc.addFunc("setRadius", &CameraController::setRadius);
    cc.addFunc("setAzimuth", &CameraController::setAzimuth);
    cc.addFunc("setElevation", &CameraController::setElevation);
    cc.addFunc("setOrbitSpeed", &CameraController::setOrbitSpeed);

    cc.addFunc("setYaw", &CameraController::setYaw);
    cc.addFunc("setPitch", &CameraController::setPitch);
    cc.addFunc("addInput", &CameraController::addInput);
    cc.addFunc("setComposition", &CameraController::setComposition);
    cc.addFunc("setDeadZone", &CameraController::setDeadZone);
    cc.addFunc("setFov", &CameraController::setFov);
    cc.addFunc("getFov", &CameraController::getFov);

    cc.addFunc("setSmooth", &CameraController::setSmooth);
    cc.addFunc("setPositionSmooth", &CameraController::setPositionSmooth);
    cc.addFunc("setTargetSmooth", &CameraController::setTargetSmooth);
    cc.addFunc("setMaxSpeed", &CameraController::setMaxSpeed);
    cc.addFunc("snap", &CameraController::snap);
    cc.addFunc("setCollisionEnabled", &CameraController::setCollisionEnabled);
    cc.addFunc("setCollisionRadius", &CameraController::setCollisionRadius);
    cc.addFunc("setCollisionRecovery", &CameraController::setCollisionRecovery);
    cc.addFunc("setCollisionMask", &CameraController::setCollisionMask);
    cc.addFunc("setCollisionIgnoredBody", &CameraController::setCollisionIgnoredBody);
    cc.addFunc("clearCollisionBoxes", &CameraController::clearCollisionBoxes);
    cc.addFunc("addCollisionBox", &CameraController::addCollisionBox);
    cc.addFunc("getCollisionBodyId", &CameraController::getCollisionBodyId);
    cc.addFunc("isObstructed", &CameraController::isObstructed);

    cc.addFunc("addRig", &CameraController::addRig);
    cc.addFunc("removeRig", &CameraController::removeRig);
    cc.addFunc("setRigPriority", &CameraController::setRigPriority);
    cc.addFunc("setRigEnabled", &CameraController::setRigEnabled);
    cc.addFunc("saveRigState", &CameraController::saveRigState);
    cc.addFunc("activateRig", &CameraController::activateRig);
    cc.addFunc("getActiveRig", &CameraController::getActiveRig);
    cc.addFunc("addImpulse", &CameraController::addImpulse);
    cc.addFunc("addFovImpulse", &CameraController::addFovImpulse);
    cc.addFunc("clearImpulses", &CameraController::clearImpulses);

    cc.addFunc("addView", &CameraController::addView);
    cc.addFunc("switchTo", &CameraController::switchTo);
    cc.addFunc("playSequence", &CameraController::playSequence);
    cc.addFunc("stopSequence", &CameraController::stopSequence);
    cc.addFunc("isPlaying", &CameraController::isPlaying);

    cc.addFunc("clearTimeline", &CameraController::clearTimeline);
    cc.addFunc("addTimelineCut", &CameraController::addTimelineCut);
    cc.addFunc("addTimelineEvent", &CameraController::addTimelineEvent);
    cc.addFunc("addTimelineFloat", &CameraController::addTimelineFloat);
    cc.addFunc("setEventSink", &CameraController::setEventSink);
    cc.addFunc("playTimeline", &CameraController::playTimeline);
    cc.addFunc("pauseTimeline", &CameraController::pauseTimeline);
    cc.addFunc("stopTimeline", &CameraController::stopTimeline);
    cc.addFunc("seekTimeline", &CameraController::seekTimeline);
    cc.addFunc("isTimelinePlaying", &CameraController::isTimelinePlaying);
    cc.addFunc("getTimelineTime", &CameraController::getTimelineTime);
    cc.addFunc("consumeTimelineEvent", &CameraController::consumeTimelineEvent);
    cc.addFunc("getTimelineEventData", &CameraController::getTimelineEventData);
    cc.addFunc("getPendingTimelineEventCount", &CameraController::getPendingTimelineEventCount);
    cc.addFunc("getTimelineDuration", &CameraController::getTimelineDuration);
    cc.addFunc("getRigCount", &CameraController::getRigCount);
    cc.addFunc("serializeAsset", &CameraController::serializeAsset);
    cc.addFunc("deserializeAsset", &CameraController::deserializeAsset);

    cc.addFunc("update", &CameraController::update);
}

}  // namespace eve::camera
