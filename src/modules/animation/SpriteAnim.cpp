#include "animation/SpriteAnim.h"
#include <algorithm>
#include <cmath>
#include "animation/Animation.h"
#include "animation/AnimationTime.h"
#include "animation/SpriteClip.h"
#include "animation/SpriteSheet.h"
#include "common/Exception.h"
#include "graphics/Quad.h"
#include "graphics/RenderSystem.h"

// SpriteAnim.cpp
namespace eve::animation {

SpriteAnim::SpriteAnim() = default;

SpriteAnim::~SpriteAnim() {
    if (owner_) owner_->unregisterSpriteAnim(this);
}

void SpriteAnim::setSheet(SpriteSheet *sheet) { sheet_ = sheet; }

void SpriteAnim::play(SpriteClip *clip) {
    if (!clip) throw Exception("SpriteAnim.play: clip is null");
    clip_            = clip;
    time_            = 0.f;
    playing_         = true;
    paused_          = false;
    finished_        = false;
    loopCount_       = 0;
    pendingLoops_    = 0;
    pendingComplete_ = false;
    speedCurveTime_  = 0.f;
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

void SpriteAnim::setSpeed(float speed) { speed_ = speed; }

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
    if (speedCurve_.size() == 1 || seconds <= speedCurve_.front().time) return speedCurve_.front().value;

    const float end = speedCurve_.back().time;
    float       t   = seconds;
    if (speedCurveLoop_ && end > 0.f)
        t = std::fmod(t, end);
    else if (t >= end)
        return speedCurve_.back().value;

    for (size_t i = 1; i < speedCurve_.size(); ++i) {
        if (t <= speedCurve_[i].time) {
            const SpeedKey &a    = speedCurve_[i - 1];
            const SpeedKey &b    = speedCurve_[i];
            const float     span = b.time - a.time;
            if (span <= 0.f) return b.value;
            float u = (t - a.time) / span;
            if (speedCurveInterpolation_ == "smooth")
                u = u * u * (3.f - 2.f * u);
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
        target          = ((target % count) + count) % count;
    } else
        target = std::max(0, std::min(target, clip_->getFrameCount() - 1));
    setFrame(target);
}

void SpriteAnim::playOnce(SpriteClip *clip) {
    play(clip);
    setLoop(false);
}
void SpriteAnim::queue(SpriteClip *clip) {
    if (!clip) throw Exception("SpriteAnim.queue: clip is null");
    queuedClip_ = clip;
}
std::string SpriteAnim::consumeEvent() {
    std::string value = pendingEvent_;
    pendingEvent_.clear();
    return value;
}

bool SpriteAnim::consumeCompleted() {
    const bool value = pendingComplete_;
    pendingComplete_ = false;
    return value;
}

int SpriteAnim::consumeLooped() {
    const int value = pendingLoops_;
    pendingLoops_   = 0;
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

bool SpriteAnim::getLoop() const { return loopOverride_ ? loopValue_ : (clip_ && clip_->getLoop()); }

int SpriteAnim::getSheetFrame() const {
    if (!clip_ || clipFrame_ < 0) return -1;
    return clip_->getSheetFrame(clipFrame_);
}

void SpriteAnim::bindQuad(graphics::Quad *quad) {
    boundQuad_ = quad;
    syncBoundQuad();
}

void SpriteAnim::unbindQuad() { boundQuad_ = nullptr; }
void SpriteAnim::bindSprite(graphics::Renderable2D *sprite) {
    boundSprite_ = sprite;
    syncBoundQuad();
}

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

    const bool loop  = getLoop();
    float      local = time_;
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
                                     sheet_->getFrameWidth(sf), sheet_->getFrameHeight(sf), sheet_->getFrameOffsetX(sf),
                                     sheet_->getFrameOffsetY(sf));
    }
}

bool SpriteAnim::updateUnchecked(float dt) {
    if (dt < 0.f) throw Exception("SpriteAnim.update: dt must be >= 0");
    if (!playing_ || paused_ || !clip_) return playing_ || paused_;

    const float previous      = time_;
    const int   previousFrame = clipFrame_;
    const float curveValue    = sampleSpeedCurve(speedCurveTime_ + dt * 0.5f);
    speedCurveTime_ += dt;
    time_ += dt * speed_ * curveValue;
    const float dur  = clip_->getDuration();
    const bool  loop = getLoop();

    if (dur <= 0.f) {
        refreshFrame();
        syncBoundQuad();
        return true;
    }

    if (loop) {
        const int beforeCycle = static_cast<int>(std::floor(previous / dur));
        const int afterCycle  = static_cast<int>(std::floor(time_ / dur));
        const int crossed     = std::abs(afterCycle - beforeCycle);
        if (crossed > 0) {
            loopCount_ += crossed;
            pendingLoops_ += crossed;
        }
    } else if ((speed_ >= 0.f && time_ >= dur) || (speed_ < 0.f && time_ <= 0.f)) {
        time_            = speed_ < 0.f ? 0.f : dur;
        finished_        = true;
        playing_         = false;
        pendingComplete_ = true;
        if (queuedClip_) {
            SpriteClip *next = queuedClip_;
            queuedClip_      = nullptr;
            loopOverride_    = false;
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

// SpriteClip.cpp
namespace eve::animation {

SpriteClip::SpriteClip(const std::string &name) : name_(name) {}

void SpriteClip::setName(const std::string &name) { name_ = name; }

void SpriteClip::checkIndex(int index) const {
    if (index < 0 || index >= getFrameCount())
        throw Exception("SpriteClip: frame index %d out of range (count=%d)", index, getFrameCount());
}

void SpriteClip::addFrame(int sheetFrameIndex, float duration) {
    if (sheetFrameIndex < 0) throw Exception("SpriteClip.addFrame: sheetFrameIndex must be >= 0");
    if (duration < 0.f) throw Exception("SpriteClip.addFrame: duration must be >= 0");
    frames_.push_back(Entry{sheetFrameIndex, duration});
}

void SpriteClip::addFrameByName(SpriteSheet *sheet, const std::string &frameName, float duration) {
    if (!sheet) throw Exception("SpriteClip.addFrameByName: sheet is null");
    int idx = sheet->findFrame(frameName);
    if (idx < 0) throw Exception("SpriteClip.addFrameByName: unknown frame '%s'", frameName.c_str());
    addFrame(idx, duration);
}

void SpriteClip::addRange(int firstSheetFrame, int lastSheetFrame, float fps) {
    if (firstSheetFrame < 0 || lastSheetFrame < firstSheetFrame)
        throw Exception("SpriteClip.addRange: expected 0 <= first <= last");
    if (fps <= 0.f) throw Exception("SpriteClip.addRange: fps must be > 0");
    const float duration = 1.f / fps;
    for (int frame = firstSheetFrame; frame <= lastSheetFrame; ++frame) addFrame(frame, duration);
}

void SpriteClip::setFPS(float fps) {
    if (fps <= 0.f) throw Exception("SpriteClip.setFPS: fps must be > 0");
    const float duration = 1.f / fps;
    for (Entry &entry : frames_) entry.duration = duration;
}

float SpriteClip::getFPS() const {
    if (frames_.empty()) return 0.f;
    const float duration = frames_.front().duration;
    if (duration <= 0.f) return 0.f;
    for (const Entry &entry : frames_) {
        if (std::fabs(entry.duration - duration) > 1e-6f) return 0.f;
    }
    return 1.f / duration;
}

void SpriteClip::addEvent(int clipFrame, const std::string &name) {
    checkIndex(clipFrame);
    events_[clipFrame] = name;
}

std::string SpriteClip::getEvent(int clipFrame) const {
    auto it = events_.find(clipFrame);
    return it == events_.end() ? std::string() : it->second;
}

void SpriteClip::clear() { frames_.clear(); }

int SpriteClip::getSheetFrame(int index) const {
    checkIndex(index);
    return frames_[static_cast<size_t>(index)].sheetFrame;
}

float SpriteClip::getFrameDuration(int index) const {
    checkIndex(index);
    return frames_[static_cast<size_t>(index)].duration;
}

float SpriteClip::getDuration() const {
    float sum = 0.f;
    for (const Entry &e : frames_) sum += e.duration;
    return sum;
}

float SpriteClip::wrapTime(float timeSeconds) const {
    if (timeSeconds < 0.f) timeSeconds = 0.f;
    const float dur = getDuration();
    if (dur <= 0.f) return 0.f;
    if (loop_) {
        float t = std::fmod(timeSeconds, dur);
        if (t < 0.f) t += dur;
        return t;
    }
    return timeSeconds > dur ? dur : timeSeconds;
}

int SpriteClip::frameAtTime(float timeSeconds) const {
    if (frames_.empty()) return -1;
    float       t   = wrapTime(timeSeconds);
    const float dur = getDuration();
    if (!loop_ && timeSeconds >= dur) return getFrameCount() - 1;

    float acc = 0.f;
    for (int i = 0; i < getFrameCount(); ++i) {
        acc += frames_[static_cast<size_t>(i)].duration;
        if (t < acc || i == getFrameCount() - 1) return i;
    }
    return getFrameCount() - 1;
}

}  // namespace eve::animation

// SpriteSheet.cpp
namespace eve::animation {

void SpriteSheet::checkIndex(int index) const {
    if (index < 0 || index >= getFrameCount())
        throw Exception("SpriteSheet: frame index %d out of range (count=%d)", index, getFrameCount());
}

int SpriteSheet::addFrame(const std::string &name, int x, int y, int w, int h) {
    return addFrameTrimmed(name, x, y, w, h, w, h, 0, 0);
}

int SpriteSheet::addFrameTrimmed(const std::string &name, int x, int y, int w, int h, int sourceW, int sourceH,
                                 int offsetX, int offsetY) {
    if (w <= 0 || h <= 0) throw Exception("SpriteSheet.addFrame: width/height must be > 0");
    Frame f;
    f.name = name.empty() ? ("frame" + std::to_string(frames_.size())) : name;
    if (byName_.count(f.name)) throw Exception("SpriteSheet.addFrame: duplicate frame name '%s'", f.name.c_str());
    f.x             = x;
    f.y             = y;
    f.w             = w;
    f.h             = h;
    f.sourceW       = sourceW;
    f.sourceH       = sourceH;
    f.offsetX       = offsetX;
    f.offsetY       = offsetY;
    int idx         = static_cast<int>(frames_.size());
    byName_[f.name] = idx;
    frames_.push_back(std::move(f));
    return idx;
}

int SpriteSheet::getFrameSourceWidth(int i) const {
    checkIndex(i);
    return frames_[size_t(i)].sourceW;
}
int SpriteSheet::getFrameSourceHeight(int i) const {
    checkIndex(i);
    return frames_[size_t(i)].sourceH;
}
int SpriteSheet::getFrameOffsetX(int i) const {
    checkIndex(i);
    return frames_[size_t(i)].offsetX;
}
int SpriteSheet::getFrameOffsetY(int i) const {
    checkIndex(i);
    return frames_[size_t(i)].offsetY;
}

int SpriteSheet::setGrid(int columns, int rows, int frameW, int frameH, int margin, int spacing, int originX,
                         int originY) {
    if (columns <= 0 || rows <= 0) throw Exception("SpriteSheet.setGrid: columns/rows must be > 0");
    if (frameW <= 0 || frameH <= 0) throw Exception("SpriteSheet.setGrid: frameW/frameH must be > 0");
    if (margin < 0 || spacing < 0) throw Exception("SpriteSheet.setGrid: margin/spacing must be >= 0");

    clear();
    int added = 0;
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < columns; ++col) {
            int x = originX + margin + col * (frameW + spacing);
            int y = originY + margin + row * (frameH + spacing);
            addFrame("", x, y, frameW, frameH);
            ++added;
        }
    }
    return added;
}

void SpriteSheet::clear() {
    frames_.clear();
    byName_.clear();
}

int SpriteSheet::findFrame(const std::string &name) const {
    auto it = byName_.find(name);
    return it == byName_.end() ? -1 : it->second;
}

std::string SpriteSheet::getFrameName(int index) const {
    checkIndex(index);
    return frames_[static_cast<size_t>(index)].name;
}

int SpriteSheet::getFrameX(int index) const {
    checkIndex(index);
    return frames_[static_cast<size_t>(index)].x;
}

int SpriteSheet::getFrameY(int index) const {
    checkIndex(index);
    return frames_[static_cast<size_t>(index)].y;
}

int SpriteSheet::getFrameWidth(int index) const {
    checkIndex(index);
    return frames_[static_cast<size_t>(index)].w;
}

int SpriteSheet::getFrameHeight(int index) const {
    checkIndex(index);
    return frames_[static_cast<size_t>(index)].h;
}

void SpriteSheet::applyToQuad(graphics::Quad *quad, int frameIndex) const {
    if (!quad) throw Exception("SpriteSheet.applyToQuad: quad is null");
    checkIndex(frameIndex);
    const Frame &f = frames_[static_cast<size_t>(frameIndex)];
    quad->setViewport(f.x, f.y, f.w, f.h);
}

SpriteSheet *SpriteSheet::clone() const {
    auto *copy     = new SpriteSheet();
    copy->frames_  = frames_;
    copy->byName_  = byName_;
    copy->texture_ = texture_;
    return copy;
}

}  // namespace eve::animation
