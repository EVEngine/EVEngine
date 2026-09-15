#include "animation/MontagePlayer.h"

#include "animation/AnimClip.h"
#include "animation/AnimPlayer.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace eve::animation {
namespace {

template <class T>
Result<T> invalid(std::string message, std::string path) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), std::move(path)));
}

TransformTRS filtered(TransformTRS delta, MontageRootMotionMask mask) {
    if (!mask.translationX) delta.px = 0.0f;
    if (!mask.translationY) delta.py = 0.0f;
    if (!mask.translationZ) delta.pz = 0.0f;
    if (!mask.rotation) {
        delta.qx = delta.qy = delta.qz = 0.0f;
        delta.qw                       = 1.0f;
    }
    return delta;
}

void multiplyRotation(TransformTRS& total, float x, float y, float z, float w) {
    const float tx = total.qx;
    const float ty = total.qy;
    const float tz = total.qz;
    const float tw = total.qw;
    total.qx       = tw * x + tx * w + ty * z - tz * y;
    total.qy       = tw * y - tx * z + ty * w + tz * x;
    total.qz       = tw * z + tx * y - ty * x + tz * w;
    total.qw       = tw * w - tx * x - ty * y - tz * z;
}

}  // namespace

MontagePlayer::MontagePlayer(AnimSkeleton& skeleton)
    : skeleton_(skeleton), player_(std::make_unique<AnimPlayer>(&skeleton)) {}

MontagePlayer::~MontagePlayer() = default;

Result<void> MontagePlayer::prepare(action::ActionTimeline timeline, IMontageClipProvider& provider) {
    auto valid = timeline.validate();
    if (!valid) return Result<void>::failure(valid.status());
    std::set<std::string>         seen;
    std::vector<MontageClipAsset> clips;
    for (const auto& section : timeline.animationSections) {
        if (!seen.insert(section.animationUri).second) continue;
        auto loaded = provider.load(section.animationUri, skeleton_);
        if (!loaded) return Result<void>::failure(loaded.status());
        clips.push_back({section.animationUri, std::move(loaded).takeValue()});
    }
    return prepare(std::move(timeline), std::move(clips));
}

Result<void> MontagePlayer::prepare(action::ActionTimeline timeline, std::vector<MontageClipAsset>&& clips) {
    auto valid = timeline.validate();
    if (!valid) return Result<void>::failure(valid.status());

    std::set<std::string> uris;
    for (std::size_t index = 0; index < clips.size(); ++index) {
        if (clips[index].uri.empty() || !clips[index].clip)
            return invalid<void>("montage clip asset is incomplete", "clips[" + std::to_string(index) + "]");
        if (!std::isfinite(clips[index].clip->getDuration()) || clips[index].clip->getDuration() <= 0.0f)
            return invalid<void>("montage clip duration must be positive", "clips[" + std::to_string(index) + "]");
        if (!uris.insert(clips[index].uri).second)
            return invalid<void>("montage clip URI is duplicated", "clips[" + std::to_string(index) + "].uri");
    }
    for (std::size_t index = 0; index < timeline.animationSections.size(); ++index) {
        const auto& section = timeline.animationSections[index];
        if (!uris.contains(section.animationUri))
            return invalid<void>("animation section clip was not supplied",
                                 "animationSections[" + std::to_string(index) + "].animationUri");
        AnimClip*  clip  = nullptr;
        const auto asset = std::find_if(clips.begin(), clips.end(),
                                        [&](const auto& candidate) { return candidate.uri == section.animationUri; });
        if (asset != clips.end()) clip = asset->clip.get();
        const double sourceStart = section.sourceStart.seconds();
        const double sourceEnd   = section.sourceEnd.isZero() ? clip->getDuration() : section.sourceEnd.seconds();
        if (sourceStart >= clip->getDuration() || sourceEnd > clip->getDuration() || sourceEnd <= sourceStart)
            return invalid<void>("animation section trim is outside the supplied clip",
                                 "animationSections[" + std::to_string(index) + "].sourceStartNs");
    }

    timeline_      = std::move(timeline);
    clips_         = std::move(clips);
    activeSection_ = nullptr;
    player_->stop();
    time_                        = Duration::zero();
    lastTick_                    = SimulationTick::zero();
    hasLastTick_                 = false;
    started_                     = false;
    playing_                     = false;
    blendingOut_                 = false;
    weight_                      = 1.0;
    executionId_                 = {};
    rootMotionMask_.translationX = timeline_->montage.rootMotionHorizontal;
    rootMotionMask_.translationZ = timeline_->montage.rootMotionHorizontal;
    rootMotionMask_.translationY = timeline_->montage.rootMotionVertical;
    rootMotionMask_.rotation     = timeline_->montage.rootMotionRotation;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> MontagePlayer::reloadClips(IMontageClipProvider& provider) {
    if (!timeline_) return invalid<void>("montage has not been prepared", "montage");

    std::set<std::string>         seen;
    std::vector<MontageClipAsset> replacements;
    for (const auto& section : timeline_->animationSections) {
        if (!seen.insert(section.animationUri).second) continue;
        auto loaded = provider.load(section.animationUri, skeleton_);
        if (!loaded) return Result<void>::failure(loaded.status());
        replacements.push_back({section.animationUri, std::move(loaded).takeValue()});
    }

    MontagePlayer candidate(skeleton_);
    auto          validated = candidate.prepare(*timeline_, std::move(replacements));
    if (!validated) return validated;

    clips_ = std::move(candidate.clips_);
    player_->stop();
    activeSection_ = nullptr;
    if ((playing_ || blendingOut_) && time_ < timeline_->duration) {
        if (const auto* section = sectionAt(time_))
            activate(*section, Duration::fromNanoseconds(time_.nanoseconds() - section->start.nanoseconds()));
    }
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> MontagePlayer::replaceClip(std::string_view uri, std::unique_ptr<AnimClip> clip) {
    if (!timeline_) return invalid<void>("montage has not been prepared", "montage");
    if (uri.empty() || !clip) return invalid<void>("replacement montage clip is incomplete", "clip");
    if (!std::isfinite(clip->getDuration()) || clip->getDuration() <= 0.0f)
        return invalid<void>("replacement montage clip duration must be positive", "clip.duration");
    const auto asset = std::find_if(clips_.begin(), clips_.end(), [&](const auto& value) { return value.uri == uri; });
    if (asset == clips_.end()) return invalid<void>("replacement URI is not referenced by the montage", "uri");
    for (std::size_t index = 0; index < timeline_->animationSections.size(); ++index) {
        const auto& section = timeline_->animationSections[index];
        if (section.animationUri != uri) continue;
        const double sourceStart = section.sourceStart.seconds();
        const double sourceEnd   = section.sourceEnd.isZero() ? clip->getDuration() : section.sourceEnd.seconds();
        if (sourceStart >= clip->getDuration() || sourceEnd > clip->getDuration() || sourceEnd <= sourceStart)
            return invalid<void>("animation section trim is outside the replacement clip",
                                 "animationSections[" + std::to_string(index) + "].sourceStartNs");
    }

    asset->clip = std::move(clip);
    if ((playing_ || blendingOut_) && activeSection_ && activeSection_->animationUri == uri) {
        player_->stop();
        const auto* section = sectionAt(time_);
        activeSection_      = nullptr;
        if (section) activate(*section, Duration::fromNanoseconds(time_.nanoseconds() - section->start.nanoseconds()));
    }
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> MontagePlayer::play(action::ActionExecutionId executionId) {
    if (!timeline_) return invalid<void>("montage has not been prepared", "montage");
    if (executionId.isZero()) return invalid<void>("action execution id must not be zero", "executionId");
    player_->stop();
    activeSection_ = nullptr;
    time_          = Duration::zero();
    hasLastTick_   = false;
    started_       = false;
    playing_       = true;
    blendingOut_   = false;
    weight_        = timeline_->montage.defaultBlendIn.isZero() ? 1.0 : 0.0;
    executionId_   = executionId;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> MontagePlayer::rebindExecution(action::ActionExecutionId executionId) {
    if (!timeline_ || !playing_)
        return invalid<void>("montage must be prepared and playing before rebinding", "montage");
    if (executionId.isZero()) return invalid<void>("action execution id must not be zero", "executionId");
    executionId_ = executionId;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

void MontagePlayer::stop() noexcept {
    player_->stop();
    activeSection_ = nullptr;
    time_          = Duration::zero();
    hasLastTick_   = false;
    started_       = false;
    playing_       = false;
    blendingOut_   = false;
    weight_        = 0.0;
    executionId_   = {};
}

AnimClip* MontagePlayer::clipFor(std::string_view uri) const noexcept {
    const auto found = std::find_if(clips_.begin(), clips_.end(), [&](const auto& asset) { return asset.uri == uri; });
    return found == clips_.end() ? nullptr : found->clip.get();
}

const action::ActionAnimationSection* MontagePlayer::sectionAt(Duration time) const noexcept {
    if (!timeline_) return nullptr;
    for (const auto& section : timeline_->animationSections)
        if (time >= section.start && time < section.end) return &section;
    return nullptr;
}

void MontagePlayer::activate(const action::ActionAnimationSection& section, Duration localTime) {
    AnimClip*   clip            = clipFor(section.animationUri);
    const float authoredSeconds = static_cast<float>(
        Duration::fromNanoseconds(section.end.nanoseconds() - section.start.nanoseconds()).seconds());
    const float sourceStart = static_cast<float>(section.sourceStart.seconds());
    const float sourceEnd =
        section.sourceEnd.isZero() ? clip->getDuration() : static_cast<float>(section.sourceEnd.seconds());
    const float speed = (sourceEnd - sourceStart) / authoredSeconds;
    player_->setBlendCurve(section.blendCurve == action::ActionBlendCurve::Linear ? AnimBlendCurve::Linear
                                                                                  : AnimBlendCurve::EaseInOut);
    if (!player_->isPlaying())
        player_->play(clip);
    else
        player_->crossFade(clip, static_cast<float>(section.blendIn.seconds()));
    player_->setLoop(false);
    player_->setSpeed(speed);
    player_->setTime(sourceStart + static_cast<float>(localTime.seconds()) * speed);
    activeSection_ = &section;
}

void MontagePlayer::accumulateRootMotion(TransformTRS& total) const {
    total.px += player_->getRootMotionX();
    total.py += player_->getRootMotionY();
    total.pz += player_->getRootMotionZ();
    multiplyRotation(total, player_->getRootMotionRotationX(), player_->getRootMotionRotationY(),
                     player_->getRootMotionRotationZ(), player_->getRootMotionRotationW());
}

Result<MontageAdvance> MontagePlayer::present(const action::ActionAdvance& advance, SimulationTick tick) {
    if (!timeline_) return invalid<MontageAdvance>("montage has not been prepared", "montage");
    if (!playing_) return invalid<MontageAdvance>("montage is not playing", "montage");
    if (advance.id != executionId_)
        return invalid<MontageAdvance>("action advance belongs to another execution", "advance.id");
    if (advance.totalElapsed < time_ || advance.totalElapsed > timeline_->duration)
        return invalid<MontageAdvance>("action advance time is outside the presentation cursor",
                                       "advance.totalElapsed");
    if (hasLastTick_ && tick <= lastTick_)
        return Result<MontageAdvance>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "montage tick must advance monotonically", "tick"));

    MontageAdvance result;
    result.previous = time_;
    result.current  = advance.totalElapsed;

    if (!started_ && result.current == time_) {
        if (const auto* section = sectionAt(time_)) activate(*section, Duration::zero());
    }

    Duration cursor = time_;
    while (cursor < result.current) {
        const action::ActionAnimationSection* section = sectionAt(cursor);
        if (!section) {
            auto next = std::find_if(timeline_->animationSections.begin(), timeline_->animationSections.end(),
                                     [&](const auto& candidate) { return candidate.start > cursor; });
            cursor =
                next == timeline_->animationSections.end() ? result.current : std::min(next->start, result.current);
            activeSection_ = nullptr;
            if (gapPolicy_ == MontageGapPolicy::BindPose) player_->stop();
            continue;
        }
        if (activeSection_ != section)
            activate(*section, Duration::fromNanoseconds(cursor.nanoseconds() - section->start.nanoseconds()));
        const Duration sliceEnd = std::min(section->end, result.current);
        const Duration slice    = Duration::fromNanoseconds(sliceEnd.nanoseconds() - cursor.nanoseconds());
        player_->update(static_cast<float>(slice.seconds()));
        accumulateRootMotion(result.rootMotion);
        cursor = sliceEnd;
        if (cursor == section->end) activeSection_ = nullptr;
    }

    result.events               = advance.timelineEvents;
    result.activeBlocks         = activeBlocksAt(result.current);
    result.sectionId            = activeSection_ ? std::optional<LogicalId>(activeSection_->id) : std::nullopt;
    result.completed            = advance.phase == action::ActionPhase::Completed;
    const double currentSeconds = result.current.seconds();
    if (!timeline_->montage.defaultBlendIn.isZero()) {
        const double t = std::clamp(currentSeconds / timeline_->montage.defaultBlendIn.seconds(), 0.0, 1.0);
        weight_        = t * t * (3.0 - 2.0 * t);
    } else {
        weight_ = 1.0;
    }
    if (!timeline_->montage.looping && !timeline_->montage.defaultBlendOut.isZero()) {
        const double start = timeline_->duration.seconds() - timeline_->montage.defaultBlendOut.seconds() +
                             timeline_->montage.blendOutOffset.seconds();
        if (currentSeconds >= start) {
            const double t =
                std::clamp((currentSeconds - start) / timeline_->montage.defaultBlendOut.seconds(), 0.0, 1.0);
            weight_ = std::min(weight_, 1.0 - t * t * (3.0 - 2.0 * t));
        }
    }
    result.weight     = weight_;
    result.rootMotion = filtered(result.rootMotion, rootMotionMask_);
    if (receiver_) receiver_->applyMontageRootMotion(result.rootMotion);

    time_        = result.current;
    lastTick_    = tick;
    hasLastTick_ = true;
    started_     = true;
    if (result.completed) playing_ = false;
    return Result<MontageAdvance>::success(
        std::move(result), Status::success(result.completed ? StatusCode::Applied : StatusCode::Pending));
}

std::vector<action::ActionTimelineEvent> MontagePlayer::realignStateEvents(Duration target) const {
    std::vector<action::ActionTimelineEvent> events;
    for (const auto& track : timeline_->tracks) {
        if (track.muted) continue;
        for (const auto& state : track.states) {
            const bool wasActive = time_ >= state.start && time_ < state.end;
            const bool isActive  = target >= state.start && target < state.end;
            if (wasActive == isActive) continue;
            events.push_back(
                {isActive ? action::ActionTimelineEventKind::StateEnter : action::ActionTimelineEventKind::StateExit,
                 track.id, state.id, state.type, target, state.payload});
        }
    }
    return events;
}

std::vector<MontageActiveBlock> MontagePlayer::activeBlocksAt(Duration target) const {
    std::vector<MontageActiveBlock> blocks;
    for (const auto& track : timeline_->tracks) {
        if (track.muted) continue;
        for (const auto& state : track.states) {
            if (target < state.start || target >= state.end) continue;
            blocks.push_back({track.id, state.id, state.type,
                              Duration::fromNanoseconds(target.nanoseconds() - state.start.nanoseconds()),
                              Duration::fromNanoseconds(state.end.nanoseconds() - state.start.nanoseconds()),
                              state.payload});
        }
    }
    return blocks;
}

Result<MontageAdvance> MontagePlayer::jumpToTime(action::ActionExecutionId executionId, Duration target,
                                                 SimulationTick tick) {
    if (!timeline_) return invalid<MontageAdvance>("montage has not been prepared", "montage");
    if (!playing_) return invalid<MontageAdvance>("montage is not playing", "montage");
    if (executionId != executionId_) return invalid<MontageAdvance>("jump belongs to another execution", "executionId");
    if (target < Duration::zero() || target > timeline_->duration)
        return invalid<MontageAdvance>("montage jump is outside the timeline", "target");
    if (hasLastTick_ && tick <= lastTick_)
        return Result<MontageAdvance>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "montage tick must advance monotonically", "tick"));

    MontageAdvance result;
    result.previous     = time_;
    result.current      = target;
    result.events       = realignStateEvents(target);
    result.activeBlocks = activeBlocksAt(target);
    player_->stop();
    activeSection_ = nullptr;
    if (const auto* section = sectionAt(target)) {
        activate(*section, Duration::fromNanoseconds(target.nanoseconds() - section->start.nanoseconds()));
        result.sectionId = section->id;
    }
    time_        = target;
    lastTick_    = tick;
    hasLastTick_ = true;
    started_     = true;
    return Result<MontageAdvance>::success(std::move(result), Status::success(StatusCode::Applied));
}

Result<MontageAdvance> MontagePlayer::jumpToSection(action::ActionExecutionId executionId, std::size_t sectionIndex,
                                                    SimulationTick tick) {
    if (!timeline_) return invalid<MontageAdvance>("montage has not been prepared", "montage");
    auto range = timeline_->sectionRange(sectionIndex);
    if (!range) return Result<MontageAdvance>::failure(range.status());
    return jumpToTime(executionId, range.value().first, tick);
}

Result<MontageAdvance> MontagePlayer::evaluateSectionProgress(action::ActionExecutionId executionId,
                                                              std::size_t sectionIndex, double progress,
                                                              SimulationTick tick) {
    if (!std::isfinite(progress) || progress < 0.0 || progress > 1.0)
        return invalid<MontageAdvance>("physical section progress must be in [0, 1]", "progress");
    if (!timeline_) return invalid<MontageAdvance>("montage has not been prepared", "montage");
    auto range = timeline_->sectionRange(sectionIndex);
    if (!range) return Result<MontageAdvance>::failure(range.status());
    const auto     span   = range.value().second.nanoseconds() - range.value().first.nanoseconds();
    const Duration target = Duration::fromNanoseconds(range.value().first.nanoseconds() +
                                                      static_cast<std::int64_t>(std::llround(span * progress)));
    return jumpToTime(executionId, target, tick);
}

Result<std::size_t> MontagePlayer::physicalSectionIndex() const {
    if (!timeline_) return invalid<std::size_t>("montage has not been prepared", "montage");
    const auto found = std::upper_bound(timeline_->splitTimestamps.begin(), timeline_->splitTimestamps.end(), time_);
    return Result<std::size_t>::success(
        static_cast<std::size_t>(std::distance(timeline_->splitTimestamps.begin(), found)));
}

Result<double> MontagePlayer::physicalSectionProgress(std::size_t sectionIndex) const {
    if (!timeline_) return invalid<double>("montage has not been prepared", "montage");
    auto range = timeline_->sectionRange(sectionIndex);
    if (!range) return Result<double>::failure(range.status());
    const double span    = static_cast<double>(range.value().second.nanoseconds() - range.value().first.nanoseconds());
    const double elapsed = static_cast<double>(time_.nanoseconds() - range.value().first.nanoseconds());
    return Result<double>::success(std::clamp(elapsed / span, 0.0, 1.0));
}

Result<MontageAdvance> MontagePlayer::interrupt(SimulationTick tick) {
    if (!timeline_) return invalid<MontageAdvance>("montage has not been prepared", "montage");
    if (!playing_) return Result<MontageAdvance>::success(MontageAdvance{}, Status::success(StatusCode::NoOp));
    if (hasLastTick_ && tick <= lastTick_)
        return Result<MontageAdvance>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "montage tick must advance monotonically", "tick"));
    MontageAdvance result;
    result.previous  = time_;
    result.current   = time_;
    result.completed = true;
    for (const auto& track : timeline_->tracks) {
        if (track.muted) continue;
        for (const auto& state : track.states) {
            if (time_ < state.start || time_ >= state.end) continue;
            result.events.push_back(
                {action::ActionTimelineEventKind::StateExit, track.id, state.id, state.type, time_, state.payload});
        }
    }
    player_->stop();
    activeSection_ = nullptr;
    playing_       = false;
    blendingOut_   = false;
    weight_        = 0.0;
    result.weight  = 0.0;
    lastTick_      = tick;
    hasLastTick_   = true;
    return Result<MontageAdvance>::success(std::move(result), Status::success(StatusCode::Applied));
}

Result<MontageAdvance> MontagePlayer::beginBlendOut(Duration duration, SimulationTick tick) {
    if (duration < Duration::zero())
        return invalid<MontageAdvance>("montage blend-out duration must be non-negative", "duration");
    if (duration.isZero()) return interrupt(tick);
    if (!timeline_) return invalid<MontageAdvance>("montage has not been prepared", "montage");
    if (!playing_) return Result<MontageAdvance>::success(MontageAdvance{}, Status::success(StatusCode::NoOp));
    if (hasLastTick_ && tick <= lastTick_)
        return Result<MontageAdvance>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "montage tick must advance monotonically", "tick"));
    MontageAdvance result;
    result.previous = result.current = time_;
    result.weight                    = weight_;
    for (const auto& track : timeline_->tracks) {
        if (track.muted) continue;
        for (const auto& state : track.states)
            if (time_ >= state.start && time_ < state.end)
                result.events.push_back(
                    {action::ActionTimelineEventKind::StateExit, track.id, state.id, state.type, time_, state.payload});
    }
    blendingOut_         = true;
    blendOutDuration_    = duration;
    blendOutElapsed_     = Duration::zero();
    blendOutStartWeight_ = weight_;
    lastTick_            = tick;
    hasLastTick_         = true;
    return Result<MontageAdvance>::success(std::move(result), Status::success(StatusCode::Pending));
}

Result<MontageAdvance> MontagePlayer::advanceBlendOut(Duration delta, SimulationTick tick) {
    if (!blendingOut_) return invalid<MontageAdvance>("montage is not blending out", "montage");
    if (delta < Duration::zero())
        return invalid<MontageAdvance>("montage blend-out delta must be non-negative", "delta");
    if (tick <= lastTick_)
        return Result<MontageAdvance>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "montage tick must advance monotonically", "tick"));
    auto elapsed = blendOutElapsed_.tryAdd(delta);
    if (!elapsed) return Result<MontageAdvance>::failure(elapsed.status());
    blendOutElapsed_ = std::min(std::move(elapsed).takeValue(), blendOutDuration_);
    const double t   = std::clamp(blendOutElapsed_.seconds() / blendOutDuration_.seconds(), 0.0, 1.0);
    weight_          = blendOutStartWeight_ * (1.0 - t * t * (3.0 - 2.0 * t));
    MontageAdvance result;
    result.previous = result.current = time_;
    result.weight                    = weight_;
    result.completed                 = t >= 1.0;
    lastTick_                        = tick;
    if (result.completed) {
        player_->stop();
        activeSection_ = nullptr;
        blendingOut_   = false;
        playing_       = false;
    }
    return Result<MontageAdvance>::success(
        std::move(result), Status::success(result.completed ? StatusCode::Applied : StatusCode::Pending));
}

AnimPose& MontagePlayer::pose() { return *player_->getPose(); }

}  // namespace eve::animation
