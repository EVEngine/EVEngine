#include "animation/AnimPlayer.h"
#include "animation/AnimClip.h"
#include "animation/AnimSkeleton.h"

#include "common/Exception.h"

namespace eve::animation {

AnimPlayer::AnimPlayer(AnimSkeleton *skeleton) : skeleton_(skeleton) {
    if (!skeleton_) throw Exception("AnimPlayer: skeleton is null");
    pose_.resize(skeleton_->getBoneCount());
    prevPose_.resize(skeleton_->getBoneCount());
    skeleton_->applyBindPose(&pose_);
}

bool AnimPlayer::effectiveLoop() const {
    if (hasLoopOverride_) return loopOverride_;
    return clip_ ? clip_->getLoop() : true;
}

bool AnimPlayer::getLoop() const { return effectiveLoop(); }

void AnimPlayer::setSpeed(float speed) {
    if (speed < 0.f) throw Exception("AnimPlayer.setSpeed: speed must be >= 0");
    speed_ = speed;
}

void AnimPlayer::setTime(float seconds) {
    if (seconds < 0.f) throw Exception("AnimPlayer.setTime: time must be >= 0");
    time_ = seconds;
    if (clip_) {
        clip_->sample(time_, &pose_, skeleton_);
    }
}

void AnimPlayer::play(AnimClip *clip) {
    if (!clip) throw Exception("AnimPlayer.play: clip is null");
    clip_         = clip;
    prevClip_     = nullptr;
    blending_     = false;
    time_         = 0.f;
    playing_      = true;
    paused_       = false;
    clip_->sample(0.f, &pose_, skeleton_);
}

void AnimPlayer::crossFade(AnimClip *clip, float blendSeconds) {
    if (!clip) throw Exception("AnimPlayer.crossFade: clip is null");
    if (blendSeconds < 0.f) throw Exception("AnimPlayer.crossFade: blendSeconds must be >= 0");
    if (!clip_ || blendSeconds <= 1e-6f) {
        play(clip);
        return;
    }
    prevClip_      = clip_;
    prevTime_      = time_;
    prevPose_.copyFrom(&pose_);
    clip_          = clip;
    time_          = 0.f;
    blendDuration_ = blendSeconds;
    blendElapsed_  = 0.f;
    blending_      = true;
    playing_       = true;
    paused_        = false;
}

void AnimPlayer::stop() {
    playing_  = false;
    paused_   = false;
    blending_ = false;
    clip_     = nullptr;
    prevClip_ = nullptr;
    time_     = 0.f;
    if (skeleton_) skeleton_->applyBindPose(&pose_);
}

void AnimPlayer::pause() {
    if (playing_) paused_ = true;
}

void AnimPlayer::resume() {
    if (playing_) paused_ = false;
}

AnimPose *AnimPlayer::getPose() { return &pose_; }

void AnimPlayer::update(float dt) {
    if (dt < 0.f) throw Exception("AnimPlayer.update: dt must be >= 0");
    if (!playing_ || paused_ || !clip_) return;

    time_ += dt * speed_;
    if (!effectiveLoop() && clip_->getDuration() > 0.f && time_ >= clip_->getDuration()) {
        time_    = clip_->getDuration();
        playing_ = false;
    }

    AnimPose sampled;
    clip_->sample(time_, &sampled, skeleton_);

    if (blending_ && prevClip_) {
        blendElapsed_ += dt;
        float t = blendDuration_ > 1e-8f ? blendElapsed_ / blendDuration_ : 1.f;
        if (t >= 1.f) {
            blending_ = false;
            prevClip_ = nullptr;
            pose_.copyFrom(&sampled);
        } else {
            // Advance previous clip time during fade for continuity.
            prevTime_ += dt * speed_;
            prevClip_->sample(prevTime_, &prevPose_, skeleton_);
            pose_.blendFrom(&prevPose_, &sampled, t);
        }
    } else {
        pose_.copyFrom(&sampled);
    }
}

}  // namespace eve::animation
