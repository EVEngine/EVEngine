#include "animation/SpriteAnim.h"

#include "animation/Animation.h"
#include "animation/AnimationTime.h"
#include "animation/SpriteClip.h"
#include "animation/SpriteSheet.h"
#include "common/Exception.h"
#include "graphics/Quad.h"
#include "graphics/RenderSystem.h"

#include <cmath>
#include <algorithm>

namespace eve::animation {

SpriteAnim::SpriteAnim() = default;

SpriteAnim::~SpriteAnim() {
    if (owner_) owner_->unregisterSpriteAnim(this);
}

void SpriteAnim::setSheet(SpriteSheet *sheet) { sheet_ = sheet; }

void SpriteAnim::play(SpriteClip *clip) {
    if (!clip) throw Exception("SpriteAnim.play: clip is null");
    clip_     = clip;
    time_     = 0.f;
    playing_  = true;
    paused_   = false;
    finished_ = false;
    loopCount_ = 0;
    pendingLoops_ = 0;
    pendingComplete_ = false;
    speedCurveTime_ = 0.f;
    refreshFrame();
    syncBoundQuad();
}

void SpriteAnim::playReverse(SpriteClip *clip) {
    play(clip);
    if (speed_ >= 0.f) speed_ = speed_ == 0.f ? -1.f : -speed_;
    time_ = clip_->getDuration();
    if (time_ > 0.f) time_ = std::nextafter(time_, 0.f);
    refreshFrame();
    syncBoundQuad();
}

void SpriteAnim::stop() {
    playing_   = false;
    paused_    = false;
    finished_  = false;
    time_      = 0.f;
    clipFrame_ = -1;
}

void SpriteAnim::pause() {
    if (playing_) paused_ = true;
}

void SpriteAnim::resume() {
    if (playing_ && paused_) paused_ = false;
}

void SpriteAnim::setSpeed(float speed) {
    speed_ = speed;
}

void SpriteAnim::addSpeedCurveKey(float seconds, float multiplier) {
    if (seconds < 0.f) throw Exception("SpriteAnim.addSpeedCurveKey: time must be >= 0");
    speedCurve_.push_back({seconds, multiplier});
    std::stable_sort(speedCurve_.begin(), speedCurve_.end(),
                     [](const SpeedKey &a, const SpeedKey &b) { return a.time < b.time; });
}

void SpriteAnim::clearSpeedCurve() {
    speedCurve_.clear();
    speedCurveTime_ = 0.f;
}

void SpriteAnim::resetSpeedCurve() { speedCurveTime_ = 0.f; }

float SpriteAnim::sampleSpeedCurve(float seconds) const {
    if (speedCurve_.empty()) return 1.f;
    if (speedCurve_.size() == 1 || seconds <= speedCurve_.front().time)
        return speedCurve_.front().value;

    const float end = speedCurve_.back().time;
    float t = seconds;
    if (speedCurveLoop_ && end > 0.f) t = std::fmod(t, end);
    else if (t >= end) return speedCurve_.back().value;

    for (size_t i = 1; i < speedCurve_.size(); ++i) {
        if (t <= speedCurve_[i].time) {
            const SpeedKey &a = speedCurve_[i - 1];
            const SpeedKey &b = speedCurve_[i];
            const float span = b.time - a.time;
            if (span <= 0.f) return b.value;
            float u = (t - a.time) / span;
            if (speedCurveInterpolation_ == "smooth") u = u * u * (3.f - 2.f * u);
            else if (speedCurveInterpolation_ == "cubic")
                u = u * u * u * (u * (u * 6.f - 15.f) + 10.f);
            return a.value + (b.value - a.value) * u;
        }
    }
    return speedCurve_.back().value;
}

float SpriteAnim::getSpeedCurveValue() const { return sampleSpeedCurve(speedCurveTime_); }

void SpriteAnim::setSpeedCurveInterpolation(const std::string &mode) {
    if (mode != "linear" && mode != "smooth" && mode != "cubic")
        throw Exception("SpriteAnim.setSpeedCurveInterpolation: expected linear|smooth|cubic");
    speedCurveInterpolation_ = mode;
}

void SpriteAnim::setFrame(int frame) {
    if (!clip_ || frame < 0 || frame >= clip_->getFrameCount())
        throw Exception("SpriteAnim.setFrame: frame out of range");
    float t = 0.f;
    for (int i = 0; i < frame; ++i) t += clip_->getFrameDuration(i);
    setTime(t);
}

void SpriteAnim::step(int frames) {
    if (!clip_ || clip_->getFrameCount() == 0) return;
    int target = clipFrame_ + frames;
    if (getLoop()) {
        const int count = clip_->getFrameCount();
        target = ((target % count) + count) % count;
    } else target = std::max(0, std::min(target, clip_->getFrameCount() - 1));
    setFrame(target);
}

void SpriteAnim::playOnce(SpriteClip *clip) { play(clip); setLoop(false); }
void SpriteAnim::queue(SpriteClip *clip) { if (!clip) throw Exception("SpriteAnim.queue: clip is null"); queuedClip_ = clip; }
std::string SpriteAnim::consumeEvent() { std::string value = pendingEvent_; pendingEvent_.clear(); return value; }

bool SpriteAnim::consumeCompleted() {
    const bool value = pendingComplete_;
    pendingComplete_ = false;
    return value;
}

int SpriteAnim::consumeLooped() {
    const int value = pendingLoops_;
    pendingLoops_ = 0;
    return value;
}

void SpriteAnim::setTime(float seconds) {
    if (seconds < 0.f) throw Exception("SpriteAnim.setTime: time must be >= 0");
    time_ = seconds;
    refreshFrame();
    if (clipFrame_ >= 0) {
        const std::string event = clip_->getEvent(clipFrame_);
        if (!event.empty()) pendingEvent_ = event;
    }
    syncBoundQuad();
}

void SpriteAnim::setLoop(bool loop) {
    loopOverride_ = true;
    loopValue_    = loop;
}

bool SpriteAnim::getLoop() const {
    return loopOverride_ ? loopValue_ : (clip_ && clip_->getLoop());
}

int SpriteAnim::getSheetFrame() const {
    if (!clip_ || clipFrame_ < 0) return -1;
    return clip_->getSheetFrame(clipFrame_);
}

void SpriteAnim::bindQuad(graphics::Quad *quad) {
    boundQuad_ = quad;
    syncBoundQuad();
}

void SpriteAnim::unbindQuad() { boundQuad_ = nullptr; }
void SpriteAnim::bindSprite(graphics::Renderable2D *sprite) { boundSprite_ = sprite; syncBoundQuad(); }

void SpriteAnim::applyToQuad(graphics::Quad *quad) const {
    if (!quad) throw Exception("SpriteAnim.applyToQuad: quad is null");
    if (!sheet_) throw Exception("SpriteAnim.applyToQuad: sheet is null");
    int sf = getSheetFrame();
    if (sf < 0) throw Exception("SpriteAnim.applyToQuad: no current frame");
    sheet_->applyToQuad(quad, sf);
}

void SpriteAnim::refreshFrame() {
    if (!clip_ || clip_->getFrameCount() == 0) {
        clipFrame_ = -1;
        return;
    }

    const float dur = clip_->getDuration();
    if (dur <= 0.f) {
        clipFrame_ = 0;
        return;
    }

    const bool loop = getLoop();
    float local     = time_;
    if (loop) {
        local = std::fmod(local, dur);
        if (local < 0.f) local += dur;
    } else if (local >= dur) {
        clipFrame_ = clip_->getFrameCount() - 1;
        return;
    } else if (local < 0.f) {
        local = 0.f;
    }

    float acc = 0.f;
    for (int i = 0; i < clip_->getFrameCount(); ++i) {
        acc += clip_->getFrameDuration(i);
        if (local < acc || i == clip_->getFrameCount() - 1) {
            clipFrame_ = i;
            return;
        }
    }
    clipFrame_ = clip_->getFrameCount() - 1;
}

void SpriteAnim::syncBoundQuad() {
    if (!boundQuad_ || !sheet_) return;
    int sf = getSheetFrame();
    if (sf < 0) return;
    sheet_->applyToQuad(boundQuad_, sf);
    if (boundSprite_) {
        boundSprite_->setFrameLayout(sheet_->getFrameSourceWidth(sf), sheet_->getFrameSourceHeight(sf),
                                     sheet_->getFrameWidth(sf), sheet_->getFrameHeight(sf),
                                     sheet_->getFrameOffsetX(sf), sheet_->getFrameOffsetY(sf));
    }
}

bool SpriteAnim::updateUnchecked(float dt) {
    if (dt < 0.f) throw Exception("SpriteAnim.update: dt must be >= 0");
    if (!playing_ || paused_ || !clip_) return playing_ || paused_;

    const float previous = time_;
    const int previousFrame = clipFrame_;
    const float curveValue = sampleSpeedCurve(speedCurveTime_ + dt * 0.5f);
    speedCurveTime_ += dt;
    time_ += dt * speed_ * curveValue;
    const float dur = clip_->getDuration();
    const bool loop = getLoop();

    if (dur <= 0.f) {
        refreshFrame();
        syncBoundQuad();
        return true;
    }

    if (loop) {
        const int beforeCycle = static_cast<int>(std::floor(previous / dur));
        const int afterCycle = static_cast<int>(std::floor(time_ / dur));
        const int crossed = std::abs(afterCycle - beforeCycle);
        if (crossed > 0) {
            loopCount_ += crossed;
            pendingLoops_ += crossed;
        }
    } else if ((speed_ >= 0.f && time_ >= dur) || (speed_ < 0.f && time_ <= 0.f)) {
        time_     = speed_ < 0.f ? 0.f : dur;
        finished_ = true;
        playing_  = false;
        pendingComplete_ = true;
        if (queuedClip_) {
            SpriteClip *next = queuedClip_;
            queuedClip_ = nullptr;
            loopOverride_ = false;
            play(next);
            return true;
        }
        refreshFrame();
        loopOverride_ = false;
        syncBoundQuad();
        return false;
    }

    refreshFrame();
    if (clipFrame_ != previousFrame && clipFrame_ >= 0) {
        const std::string event = clip_->getEvent(clipFrame_);
        if (!event.empty()) pendingEvent_ = event;
    }
    syncBoundQuad();
    return true;
}

eve::Result<void> SpriteAnim::advance(const eve::SimulationStep &step) {
    auto seconds = detail::secondsForStep(step, hasLastTick_, lastTick_, "SpriteAnim");
    if (!seconds) return eve::Result<void>::failure(seconds.status());
    updateUnchecked(std::move(seconds).takeValue());
    lastTick_    = step.tick;
    hasLastTick_ = true;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

bool SpriteAnim::update(float dt) {
    auto step = detail::legacyStep(dt, hasLastTick_, lastTick_, "SpriteAnim");
    if (!step) {
        step.ignore("legacy SpriteAnim update");
        return playing_ || paused_;
    }
    auto result = advance(std::move(step).takeValue());
    result.ignore("legacy SpriteAnim update");
    return playing_ || paused_;
}

}  // namespace eve::animation
