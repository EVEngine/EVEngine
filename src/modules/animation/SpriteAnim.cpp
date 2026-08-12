#include "animation/SpriteAnim.h"

#include "animation/Animation.h"
#include "animation/SpriteClip.h"
#include "animation/SpriteSheet.h"
#include "common/Exception.h"
#include "graphics/Quad.h"

#include <cmath>

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
    if (speed < 0.f) throw Exception("SpriteAnim.setSpeed: speed must be >= 0");
    speed_ = speed;
}

void SpriteAnim::setTime(float seconds) {
    if (seconds < 0.f) throw Exception("SpriteAnim.setTime: time must be >= 0");
    time_ = seconds;
    refreshFrame();
    syncBoundQuad();
}

void SpriteAnim::setLoop(bool loop) {
    loopOverride_ = true;
    loopValue_    = loop;
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
}

bool SpriteAnim::update(float dt) {
    if (dt < 0.f) throw Exception("SpriteAnim.update: dt must be >= 0");
    if (!playing_ || paused_ || !clip_) return playing_ || paused_;

    time_ += dt * speed_;
    const float dur = clip_->getDuration();
    const bool loop = getLoop();

    if (dur <= 0.f) {
        refreshFrame();
        syncBoundQuad();
        return true;
    }

    if (!loop && time_ >= dur) {
        time_     = dur;
        finished_ = true;
        playing_  = false;
        refreshFrame();
        syncBoundQuad();
        return false;
    }

    refreshFrame();
    syncBoundQuad();
    return true;
}

}  // namespace eve::animation
