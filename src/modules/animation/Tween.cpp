#include "animation/Tween.h"

#include "animation/AnimationTime.h"
#include "animation/Animation.h"

#include "common/Exception.h"

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace eve::animation {

Tween::Tween(float duration) { setDuration(duration); }

Tween::~Tween() {
    if (owner_) owner_->unregisterTween(this);
}

Tween::Track &Tween::ensureTrack(const std::string &name) {
    auto it = tracks_.find(name);
    if (it != tracks_.end()) return it->second;
    order_.push_back(name);
    return tracks_[name];
}

const Tween::Track *Tween::findTrack(const std::string &name) const {
    auto it = tracks_.find(name);
    if (it == tracks_.end()) return nullptr;
    return &it->second;
}

void Tween::setFrom(const std::string &name, float value) {
    Track &tr   = ensureTrack(name);
    tr.from     = value;
    tr.hasFrom  = true;
    tr.isAngle  = false;
    if (!tr.resolved) tr.current = value;
}

void Tween::setTo(const std::string &name, float value) {
    Track &tr    = ensureTrack(name);
    tr.to        = value;
    tr.endMode   = EndMode::Absolute;
    tr.isAngle   = false;
    tr.resolved  = false;
}

void Tween::setDelta(const std::string &name, float delta) {
    Track &tr    = ensureTrack(name);
    tr.delta     = delta;
    tr.endMode   = EndMode::Delta;
    tr.isAngle   = false;
    tr.resolved  = false;
}

void Tween::setFromAngle(const std::string &name, float radians) {
    Track &tr   = ensureTrack(name);
    tr.from     = radians;
    tr.hasFrom  = true;
    tr.isAngle  = true;
    if (!tr.resolved) tr.current = radians;
}

void Tween::setToAngle(const std::string &name, float radians) {
    Track &tr    = ensureTrack(name);
    tr.to        = radians;
    tr.endMode   = EndMode::Absolute;
    tr.isAngle   = true;
    tr.resolved  = false;
}

void Tween::setDeltaAngle(const std::string &name, float deltaRadians) {
    Track &tr    = ensureTrack(name);
    tr.delta     = deltaRadians;
    tr.endMode   = EndMode::Delta;
    tr.isAngle   = true;
    tr.resolved  = false;
}

bool Tween::has(const std::string &name) const { return findTrack(name) != nullptr; }

float Tween::get(const std::string &name) const {
    const Track *tr = findTrack(name);
    if (!tr) throw Exception("Tween.get: unknown property '%s'", name.c_str());
    return tr->current;
}

float Tween::getFrom(const std::string &name) const {
    const Track *tr = findTrack(name);
    if (!tr) throw Exception("Tween.getFrom: unknown property '%s'", name.c_str());
    return tr->from;
}

float Tween::getTo(const std::string &name) const {
    const Track *tr = findTrack(name);
    if (!tr) throw Exception("Tween.getTo: unknown property '%s'", name.c_str());
    if (tr->endMode == EndMode::Delta && !tr->resolved) return tr->from + tr->delta;
    return tr->to;
}

float Tween::getDelta(const std::string &name) const {
    const Track *tr = findTrack(name);
    if (!tr) throw Exception("Tween.getDelta: unknown property '%s'", name.c_str());
    if (tr->endMode == EndMode::Delta) return tr->delta;
    return tr->to - tr->from;
}

void Tween::setDuration(float seconds) {
    if (seconds < 0.f) throw Exception("Tween.setDuration: duration must be >= 0");
    duration_ = seconds;
}

void Tween::setDelay(float seconds) {
    if (seconds < 0.f) throw Exception("Tween.setDelay: delay must be >= 0");
    delay_ = seconds;
}

void Tween::setEase(const std::string &kind) {
    // Validate against known kinds (same set as Math.ease).
    (void)ease(0.5f, kind);
    ease_ = kind.empty() ? "linear" : kind;
}

void Tween::setRepeat(int count) {
    if (count < -1) count = -1;
    if (count == 0) count = 1;
    repeat_ = count;
}

float Tween::clamp01(float t) { return std::clamp(t, 0.f, 1.f); }

float Tween::lerpAngle(float a, float b, float t) {
    float diff = b - a;
    while (diff > float(M_PI)) diff -= float(2.0 * M_PI);
    while (diff < -float(M_PI)) diff += float(2.0 * M_PI);
    return a + diff * t;
}

float Tween::ease(float t, const std::string &kind) {
    t = clamp01(t);
    if (kind == "linear" || kind.empty()) return t;
    if (kind == "inQuad") return t * t;
    if (kind == "outQuad") return 1.f - (1.f - t) * (1.f - t);
    if (kind == "inOutQuad")
        return t < 0.5f ? 2.f * t * t : 1.f - std::pow(-2.f * t + 2.f, 2.f) * 0.5f;
    if (kind == "inCubic") return t * t * t;
    if (kind == "outCubic") return 1.f - std::pow(1.f - t, 3.f);
    if (kind == "inOutCubic")
        return t < 0.5f ? 4.f * t * t * t : 1.f - std::pow(-2.f * t + 2.f, 3.f) * 0.5f;
    if (kind == "inSine") return 1.f - std::cos(t * float(M_PI) * 0.5f);
    if (kind == "outSine") return std::sin(t * float(M_PI) * 0.5f);
    if (kind == "inOutSine") return -(std::cos(float(M_PI) * t) - 1.f) * 0.5f;
    if (kind == "inExpo") return t <= 0.f ? 0.f : std::pow(2.f, 10.f * t - 10.f);
    if (kind == "outExpo") return t >= 1.f ? 1.f : 1.f - std::pow(2.f, -10.f * t);
    if (kind == "inOutExpo") {
        if (t <= 0.f) return 0.f;
        if (t >= 1.f) return 1.f;
        return t < 0.5f ? std::pow(2.f, 20.f * t - 10.f) * 0.5f
                        : (2.f - std::pow(2.f, -20.f * t + 10.f)) * 0.5f;
    }
    throw Exception("Tween.setEase: unknown kind '%s'", kind.c_str());
}

void Tween::resolveTracks() {
    for (auto &kv : tracks_) {
        Track &tr = kv.second;
        if (!tr.hasFrom) {
            tr.from    = tr.current;
            tr.hasFrom = true;
        }
        if (tr.endMode == EndMode::Delta) tr.to = tr.from + tr.delta;
        tr.current  = tr.from;
        tr.resolved = true;
    }
}

void Tween::applyProgress(float linearT) {
    float t = reverse_ ? (1.f - clamp01(linearT)) : clamp01(linearT);
    float e = ease(t, ease_);
    for (auto &kv : tracks_) {
        Track &tr = kv.second;
        if (tr.isAngle)
            tr.current = lerpAngle(tr.from, tr.to, e);
        else
            tr.current = tr.from + (tr.to - tr.from) * e;
    }
}

float Tween::getProgress() const {
    if (duration_ <= 0.f) return state_ == State::Finished ? 1.f : 0.f;
    return clamp01(elapsed_ / duration_);
}

float Tween::getEasedProgress() const {
    float t = getProgress();
    if (reverse_) t = 1.f - t;
    return ease(t, ease_);
}

void Tween::start() {
    resolveTracks();
    elapsed_   = 0.f;
    played_    = 0;
    reverse_   = false;
    delayLeft_ = delay_;
    state_     = delay_ > 0.f ? State::Delayed : State::Running;
    if (state_ == State::Running) applyProgress(0.f);
}

void Tween::pause() {
    if (state_ == State::Running || state_ == State::Delayed) state_ = State::Paused;
}

void Tween::resume() {
    if (state_ != State::Paused) return;
    if (delayLeft_ > 0.f)
        state_ = State::Delayed;
    else
        state_ = State::Running;
}

void Tween::stop() {
    state_     = State::Stopped;
    delayLeft_ = 0.f;
}

void Tween::reset() {
    elapsed_   = 0.f;
    played_    = 0;
    reverse_   = false;
    delayLeft_ = delay_;
    state_     = State::Idle;
    for (auto &kv : tracks_) {
        Track &tr   = kv.second;
        tr.resolved = false;
        if (tr.hasFrom) tr.current = tr.from;
    }
}

void Tween::finishCycle() {
    ++played_;
    applyProgress(1.f);

    const bool infinite = repeat_ < 0;
    if (!infinite && played_ >= repeat_) {
        state_ = State::Finished;
        return;
    }

    if (yoyo_) reverse_ = !reverse_;
    elapsed_ = 0.f;
    // Snap to the start of the next cycle orientation.
    applyProgress(0.f);
    state_ = State::Running;
}

bool Tween::updateUnchecked(float dt) {
    if (dt < 0.f) dt = 0.f;

    if (state_ == State::Paused) return true;
    if (state_ != State::Delayed && state_ != State::Running) return false;

    if (state_ == State::Delayed) {
        delayLeft_ -= dt;
        if (delayLeft_ > 0.f) return true;
        dt         = -delayLeft_;
        delayLeft_ = 0.f;
        state_     = State::Running;
        applyProgress(0.f);
        if (dt <= 0.f) return true;
    }

    if (duration_ <= 0.f) {
        applyProgress(1.f);
        finishCycle();
        return isActive();
    }

    elapsed_ += dt;
    while (state_ == State::Running && elapsed_ >= duration_) {
        float over = elapsed_ - duration_;
        finishCycle();
        if (state_ != State::Running) break;
        elapsed_ = over;
    }

    if (state_ == State::Running) applyProgress(getProgress());
    return isActive();
}

eve::Result<void> Tween::advance(const eve::SimulationStep& step) {
    auto seconds = detail::secondsForStep(step, hasLastTick_, lastTick_, "Tween");
    if (!seconds) return eve::Result<void>::failure(seconds.status());
    updateUnchecked(std::move(seconds).takeValue());
    lastTick_ = step.tick;
    hasLastTick_ = true;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

bool Tween::update(float dt) {
    auto step = detail::legacyStep(dt, hasLastTick_, lastTick_, "Tween");
    if (!step) {
        step.ignore("legacy Tween update");
        return isActive();
    }
    auto result = advance(std::move(step).takeValue());
    result.ignore("legacy Tween update");
    return isActive();
}

float Tween::evaluate(const std::string &name, float t) const {
    const Track *tr = findTrack(name);
    if (!tr) throw Exception("Tween.evaluate: unknown property '%s'", name.c_str());

    float from = tr->from;
    float to   = tr->endMode == EndMode::Delta && !tr->resolved ? tr->from + tr->delta : tr->to;
    if (!tr->hasFrom && !tr->resolved) from = tr->current;

    float e = ease(clamp01(t), ease_);
    if (tr->isAngle) return lerpAngle(from, to, e);
    return from + (to - from) * e;
}

std::string Tween::getPropertyName(int index) const {
    if (index < 0 || index >= static_cast<int>(order_.size()))
        throw Exception("Tween.getPropertyName: index %d out of range (count=%d)", index,
                        static_cast<int>(order_.size()));
    return order_[static_cast<size_t>(index)];
}

}  // namespace eve::animation
