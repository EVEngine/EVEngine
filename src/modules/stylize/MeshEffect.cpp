#include "stylize/MeshEffect.h"

#include "common/Exception.h"
#include "stylize/StyleShaders.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace eve::stylize {

MeshEffectInstance::MeshEffectInstance(std::string style) : style_(std::move(style)) {
    const StyleDefinition* definition = findStyleDefinition(style_.getStyle());
    if (!definition || !definition->mesh)
        throw eve::Exception("MeshEffectInstance: style '%s' has no mesh technique", style_.getStyle().c_str());
}

void MeshEffectInstance::bindTarget(MeshEffectTargetHandle target) {
    if (target.isInvalid()) throw eve::Exception("MeshEffectInstance.bindTarget: invalid target handle");
    target_ = target;
}

void MeshEffectInstance::unbindTarget() noexcept { target_ = MeshEffectTargetHandle::invalid(); }

void MeshEffectInstance::validateDuration(float value, const char* name) {
    if (!std::isfinite(value) || value < 0.f)
        throw eve::Exception("MeshEffectInstance: %s must be finite and non-negative", name);
}

void MeshEffectInstance::setPlayback(MeshEffectPlayback playback) {
    validateDuration(playback.fadeIn, "fadeIn");
    validateDuration(playback.duration, "duration");
    validateDuration(playback.fadeOut, "fadeOut");
    if (playback.loop && playback.fadeIn + playback.duration + playback.fadeOut <= 0.f)
        throw eve::Exception("MeshEffectInstance: a looping playback needs a positive cycle duration");
    playback_ = playback;
    if (state_ != MeshEffectState::Stopped && state_ != MeshEffectState::Finished) play();
}

void MeshEffectInstance::play() noexcept {
    elapsed_ = 0.f;
    stopElapsed_ = 0.f;
    stopDuration_ = 0.f;
    stopStartIntensity_ = 0.f;
    updateTimelineState();
}

void MeshEffectInstance::stop(float fadeOutSeconds) {
    validateDuration(fadeOutSeconds, "stop fadeOut");
    if (state_ == MeshEffectState::Stopped || state_ == MeshEffectState::Finished) return;
    if (fadeOutSeconds == 0.f) {
        state_ = MeshEffectState::Finished;
        return;
    }
    stopStartIntensity_ = intensity();
    stopElapsed_ = 0.f;
    stopDuration_ = fadeOutSeconds;
    state_ = MeshEffectState::FadingOut;
}

void MeshEffectInstance::update(float dtSeconds) {
    validateDuration(dtSeconds, "dt");
    if (state_ == MeshEffectState::Stopped || state_ == MeshEffectState::Finished) return;
    if (stopDuration_ > 0.f) {
        stopElapsed_ += dtSeconds;
        elapsed_ += dtSeconds;
        if (stopElapsed_ >= stopDuration_) state_ = MeshEffectState::Finished;
    } else {
        elapsed_ += dtSeconds;
        const float cycle = playback_.fadeIn + playback_.duration + playback_.fadeOut;
        if (playback_.loop && cycle > 0.f)
            elapsed_ = std::fmod(elapsed_, cycle);
        else if (elapsed_ >= cycle) {
            elapsed_ = cycle;
            state_ = MeshEffectState::Finished;
        }
        if (state_ != MeshEffectState::Finished) updateTimelineState();
    }
    if (style_.hasParam("time")) style_.setFloat("time", elapsed_);
}

void MeshEffectInstance::updateTimelineState() noexcept {
    if (playback_.fadeIn > 0.f && elapsed_ < playback_.fadeIn) {
        state_ = MeshEffectState::FadingIn;
    } else if (elapsed_ < playback_.fadeIn + playback_.duration) {
        state_ = MeshEffectState::Active;
    } else if (playback_.fadeOut > 0.f) {
        state_ = MeshEffectState::FadingOut;
    } else {
        state_ = playback_.loop ? MeshEffectState::Active : MeshEffectState::Finished;
    }
}

float MeshEffectInstance::intensity() const noexcept {
    if (state_ == MeshEffectState::Stopped || state_ == MeshEffectState::Finished) return 0.f;
    if (stopDuration_ > 0.f)
        return stopStartIntensity_ * std::clamp(1.f - stopElapsed_ / stopDuration_, 0.f, 1.f);
    if (playback_.fadeIn > 0.f && elapsed_ < playback_.fadeIn)
        return std::clamp(elapsed_ / playback_.fadeIn, 0.f, 1.f);
    const float fadeOutStart = playback_.fadeIn + playback_.duration;
    if (playback_.fadeOut > 0.f && elapsed_ >= fadeOutStart)
        return std::clamp(1.f - (elapsed_ - fadeOutStart) / playback_.fadeOut, 0.f, 1.f);
    return 1.f;
}

graphics::Shader* MeshEffectInstance::newMeshShader(graphics::Graphics* gfx) {
    if (style_.hasParam("time")) style_.setFloat("time", elapsed_);
    return style_.newMeshShader(gfx);
}

}  // namespace eve::stylize
