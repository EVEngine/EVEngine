#include "animation/AnimPlayer.h"
#include "animation/AnimClip.h"
#include "animation/AnimSkeleton.h"

#include "common/Exception.h"

#include <cmath>

namespace eve::animation {

AnimPlayer::AnimPlayer(AnimSkeleton* skeleton) : skeleton_(skeleton) {
    if (!skeleton_) throw Exception("AnimPlayer: skeleton is null");
    pose_.resize(skeleton_->getBoneCount());
    prevPose_.resize(skeleton_->getBoneCount());
    sampledPose_.resize(skeleton_->getBoneCount());
    rootPreviousPose_.resize(skeleton_->getBoneCount());
    rootStartPose_.resize(skeleton_->getBoneCount());
    rootEndPose_.resize(skeleton_->getBoneCount());
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

void AnimPlayer::play(AnimClip* clip) {
    if (!clip) throw Exception("AnimPlayer.play: clip is null");
    clip_     = clip;
    prevClip_ = nullptr;
    blending_ = false;
    time_     = 0.f;
    playing_  = true;
    paused_   = false;
    clip_->sample(0.f, &pose_, skeleton_);
}

void AnimPlayer::crossFade(AnimClip* clip, float blendSeconds) {
    if (!clip) throw Exception("AnimPlayer.crossFade: clip is null");
    if (blendSeconds < 0.f) throw Exception("AnimPlayer.crossFade: blendSeconds must be >= 0");
    if (!clip_ || blendSeconds <= 1e-6f) {
        play(clip);
        return;
    }
    prevClip_ = clip_;
    prevTime_ = time_;
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

AnimPose* AnimPlayer::getPose() { return &pose_; }

void AnimPlayer::setRootMotionBone(int boneIndex) {
    if (boneIndex < 0 || boneIndex >= skeleton_->getBoneCount())
        throw Exception("AnimPlayer.setRootMotionBone: invalid bone %d", boneIndex);
    rootMotionBone_ = boneIndex;
}

std::string AnimPlayer::consumeEvent() {
    if (pendingEvents_.empty()) return {};
    std::string event = std::move(pendingEvents_.front());
    pendingEvents_.erase(pendingEvents_.begin());
    return event;
}

void AnimPlayer::setUpdateRate(float hz) {
    if (hz < 0.f) throw Exception("AnimPlayer.setUpdateRate: hz must be >= 0");
    updateRate_        = hz;
    updateAccumulator_ = 0.f;
}

void AnimPlayer::update(float dt) {
    if (dt < 0.f) throw Exception("AnimPlayer.update: dt must be >= 0");
    if (!playing_ || paused_ || !clip_) return;
    if (updateRate_ > 0.f) {
        updateAccumulator_ += dt;
        const float interval = 1.f / updateRate_;
        if (updateAccumulator_ + 1e-8f < interval) {
            rootMotion_ = TransformTRS::identity();
            return;
        }
        dt                 = updateAccumulator_;
        updateAccumulator_ = 0.f;
    }

    rootMotion_              = TransformTRS::identity();
    const float previousTime = time_;
    clip_->sample(previousTime, &rootPreviousPose_, skeleton_);
    time_ += dt * speed_;
    clip_->collectEvents(previousTime, time_, effectiveLoop(), pendingEvents_);
    if (!effectiveLoop() && clip_->getDuration() > 0.f && time_ >= clip_->getDuration()) {
        time_    = clip_->getDuration();
        playing_ = false;
    }

    clip_->sample(time_, &sampledPose_, skeleton_);
    const TransformTRS& fromRoot = rootPreviousPose_.local(rootMotionBone_);
    const TransformTRS& toRoot   = sampledPose_.local(rootMotionBone_);
    rootMotion_.px               = toRoot.px - fromRoot.px;
    rootMotion_.py               = toRoot.py - fromRoot.py;
    rootMotion_.pz               = toRoot.pz - fromRoot.pz;
    if (effectiveLoop() && clip_->getDuration() > 1e-8f) {
        const float duration = clip_->getDuration();
        const int   cycles   = static_cast<int>(std::floor(time_ / duration) - std::floor(previousTime / duration));
        if (cycles != 0) {
            clip_->sample(0.f, &rootStartPose_, skeleton_);
            clip_->sampleClamped(duration, &rootEndPose_, skeleton_);
            const TransformTRS& startRoot = rootStartPose_.local(rootMotionBone_);
            const TransformTRS& endRoot   = rootEndPose_.local(rootMotionBone_);
            rootMotion_.px += static_cast<float>(cycles) * (endRoot.px - startRoot.px);
            rootMotion_.py += static_cast<float>(cycles) * (endRoot.py - startRoot.py);
            rootMotion_.pz += static_cast<float>(cycles) * (endRoot.pz - startRoot.pz);
        }
    }
    rootMotion_.qx =
        fromRoot.qw * toRoot.qx - fromRoot.qx * toRoot.qw - fromRoot.qy * toRoot.qz + fromRoot.qz * toRoot.qy;
    rootMotion_.qy =
        fromRoot.qw * toRoot.qy + fromRoot.qx * toRoot.qz - fromRoot.qy * toRoot.qw - fromRoot.qz * toRoot.qx;
    rootMotion_.qz =
        fromRoot.qw * toRoot.qz - fromRoot.qx * toRoot.qy + fromRoot.qy * toRoot.qx - fromRoot.qz * toRoot.qw;
    rootMotion_.qw =
        fromRoot.qw * toRoot.qw + fromRoot.qx * toRoot.qx + fromRoot.qy * toRoot.qy + fromRoot.qz * toRoot.qz;
    rootMotion_.normalizeRotation();

    if (blending_ && prevClip_) {
        blendElapsed_ += dt;
        float t = blendDuration_ > 1e-8f ? blendElapsed_ / blendDuration_ : 1.f;
        if (t >= 1.f) {
            blending_ = false;
            prevClip_ = nullptr;
            pose_.copyFrom(&sampledPose_);
        } else {
            // Advance previous clip time during fade for continuity.
            prevTime_ += dt * speed_;
            prevClip_->sample(prevTime_, &prevPose_, skeleton_);
            pose_.blendFrom(&prevPose_, &sampledPose_, t);
        }
    } else {
        pose_.copyFrom(&sampledPose_);
    }
}

}  // namespace eve::animation
