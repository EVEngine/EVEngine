#include "action_editor/AnimationRootMotionPreviewSource.h"

#include "animation/AnimClip.h"
#include "animation/AnimClipRegistry.h"
#include "animation/AnimPose.h"
#include "common/Diagnostic.h"
#include "common/Exception.h"

#include <limits>
#include <string>

namespace eve::editor {
namespace {

Result<std::vector<action::ActionPreviewPoint3>> sampleError(DiagnosticCode code, std::string message,
                                                             std::string path = {}) {
    return Result<std::vector<action::ActionPreviewPoint3>>::failure(
        Diagnostic::error(code, std::move(message), std::move(path)));
}

}  // namespace

Result<std::vector<action::ActionPreviewPoint3>> AnimationRootMotionPreviewSource::sampleRootMotion(
    std::string_view animationUri, Duration duration, std::uint32_t sampleCount) const {
    if (animationUri.empty())
        return sampleError(DiagnosticCode::InvalidArgument, "Animation URI must not be empty", "animationUri");
    if (duration < Duration::zero())
        return sampleError(DiagnosticCode::InvalidArgument, "Preview duration must be non-negative", "duration");
    if (sampleCount < 2 || sampleCount > 1024)
        return sampleError(DiagnosticCode::InvalidArgument, "Root-motion sample count must be between 2 and 1024",
                           "sampleCount");
    if (rootBone_ > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
        return sampleError(DiagnosticCode::InvalidArgument, "Root-bone index exceeds the animation API range",
                           "rootBone");

    const std::string uri(animationUri);
    auto              clips = animation::AnimClipRegistry::findByPath(uri);
    if (clips.empty())
        return sampleError(DiagnosticCode::NotFound, "No registered animation clip matches the preview URI", uri);
    if (clips.size() != 1)
        return sampleError(DiagnosticCode::Conflict, "Preview URI resolves to more than one animation clip", uri);

    animation::AnimClip* clip = clips.front();
    if (!clip) return sampleError(DiagnosticCode::StaleHandle, "Animation registry returned a stale clip", uri);
    const int rootBone = static_cast<int>(rootBone_);
    if (rootBone >= clip->getTrackCount())
        return sampleError(DiagnosticCode::InvalidArgument, "Root-bone track is not present in the animation clip",
                           "rootBone");

    std::vector<action::ActionPreviewPoint3> path;
    path.reserve(sampleCount);
    animation::AnimPose pose;
    try {
        for (std::uint32_t index = 0; index < sampleCount; ++index) {
            const double alpha      = static_cast<double>(index) / static_cast<double>(sampleCount - 1);
            const float  sampleTime = static_cast<float>(duration.seconds() * alpha);
            clip->sample(sampleTime, &pose);
            const auto& root = pose.local(rootBone);
            path.push_back({root.px, root.py, root.pz});
        }
    } catch (const Exception&) {
        return sampleError(DiagnosticCode::Failed, "Animation clip failed while sampling root motion", uri);
    }
    return Result<std::vector<action::ActionPreviewPoint3>>::success(std::move(path));
}

}  // namespace eve::editor
