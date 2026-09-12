#include "animation/editor/AnimationClipEditor.h"

#include "animation/AnimClip.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "editor/EditorProtocol.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <set>
#include <utility>

namespace eve::animation_editor {
namespace {

template <class T = void>
animation_editing::EditorResult<T> editorError(animation_editing::EditorStatus status, std::string rule,
                                               std::string message) {
    return eve::editing::failed<T>(status, animation_editing::RuleId(std::move(rule)), std::move(message));
}

animation_editing::EditorValue keyValue(const char* id, double time, double px, double py, double pz) {
    return animation_editing::EditorValue::Object{
        {"id", std::string(id)}, {"time", time}, {"px", px},     {"py", py},     {"pz", pz},
        {"rx", 0.0},             {"ry", 0.0},    {"rz", 0.0},    {"rw", 1.0},    {"sx", 1.0},
        {"sy", 1.0},             {"sz", 1.0}};
}

animation_editing::EditorValue trackValue(const char* id, const char* bone,
                                          animation_editing::EditorValue::Array keys) {
    return animation_editing::EditorValue::Object{
        {"id", std::string(id)}, {"bone", std::string(bone)}, {"keys", std::move(keys)}};
}

}  // namespace

AnimationClipEditor::AnimationClipEditor(std::string targetId)
    : target_(std::move(targetId)), authority_(&target_), transactions_(&authority_) {
    skeleton_ = {{"Hips", {}}, {"Spine", "Hips"}, {"Chest", "Spine"}, {"Head", "Chest"},
                 {"LeftArm", "Chest"}, {"RightArm", "Chest"}};
    seedPreviewClip();
    auto previewed = refreshPreview();
    if (!previewed.ok())
        previewed.ignore("animation clip editor keeps an empty overlay when the seeded preview is rejected");
}

animation_editing::EditorResult<void> AnimationClipEditor::loadRuntimeClip(const animation::AnimSkeleton& skeleton,
                                                                            const animation::AnimClip& clip) {
    if (skeleton.getBoneCount() <= 0 || clip.getDuration() <= 0.0f)
        return editorError(animation_editing::EditorStatus::Rejected, "editor.animation.runtime-source",
                           "Runtime skeleton and clip must contain bones and a positive duration");
    animation_editing::EditorValue::Array tracks;
    std::vector<SkeletonBone> candidateSkeleton;
    animation::AnimPose pose(skeleton.getBoneCount());
    for (int bone = 0; bone < skeleton.getBoneCount(); ++bone) {
        const std::string name = skeleton.getBoneName(bone);
        const int parent = skeleton.getParent(bone);
        candidateSkeleton.push_back({name, parent >= 0 ? skeleton.getBoneName(parent) : std::string{}});
        std::set<double> times;
        for (int key = 0; key < clip.getPositionKeyCount(bone); ++key) times.insert(clip.getPositionKeyTime(bone, key));
        for (int key = 0; key < clip.getRotationKeyCount(bone); ++key) times.insert(clip.getRotationKeyTime(bone, key));
        for (int key = 0; key < clip.getScaleKeyCount(bone); ++key) times.insert(clip.getScaleKeyTime(bone, key));
        if (times.empty()) times.insert(0.0);
        animation_editing::EditorValue::Array keys;
        int keyIndex = 0;
        for (const double time : times) {
            clip.sample(static_cast<float>(time), &pose, &skeleton);
            keys.push_back(animation_editing::EditorValue::Object{
                {"id", "runtime-key:" + std::to_string(bone) + ":" + std::to_string(keyIndex++)}, {"time", time},
                {"px", static_cast<double>(pose.getLocalPositionX(bone))}, {"py", static_cast<double>(pose.getLocalPositionY(bone))},
                {"pz", static_cast<double>(pose.getLocalPositionZ(bone))}, {"rx", static_cast<double>(pose.getLocalRotationX(bone))},
                {"ry", static_cast<double>(pose.getLocalRotationY(bone))}, {"rz", static_cast<double>(pose.getLocalRotationZ(bone))},
                {"rw", static_cast<double>(pose.getLocalRotationW(bone))}, {"sx", static_cast<double>(pose.getLocalScaleX(bone))},
                {"sy", static_cast<double>(pose.getLocalScaleY(bone))}, {"sz", static_cast<double>(pose.getLocalScaleZ(bone))}});
        }
        tracks.push_back(trackValue(("runtime-track:" + std::to_string(bone)).c_str(), name.c_str(), std::move(keys)));
    }
    animation_editing::EditorValue::Object root;
    root["schemaVersion"] = std::int64_t{1};
    root["settings"] = animation_editing::EditorValue::Object{{"duration", static_cast<double>(clip.getDuration())},
                                                               {"sampleRate", static_cast<double>(clip.getSampleRate())},
                                                               {"loop", clip.getLoop()}};
    root["tracks"] = std::move(tracks); root["events"] = animation_editing::EditorValue::Array{};
    root["masks"] = animation_editing::EditorValue::Array{};
    auto loaded = target_.loadSnapshot(animation_editing::EditorValue(std::move(root)));
    if (!loaded.ok()) return loaded;
    skeleton_ = std::move(candidateSkeleton);
    selectedBone_ = skeleton_.front().name; selectedKeyId_ = animation_editing::StableId();
    playhead_ = 0.0; playing_ = false;
    return refreshPreview();
}

animation_editing::EditorResult<void> AnimationClipEditor::writeRuntimeClip(
    animation::AnimClip& clip, const animation::AnimSkeleton& skeleton) const {
    animation_editing::AnimationClipRuntimeBuilder builder;
    auto built = builder.build(target_, &skeleton);
    if (!built.ok()) return animation_editing::EditorResult<void>::failure(built.status());
    std::unique_ptr<animation::AnimClip> candidate(std::move(built).takeValue());
    clip.adopt(*candidate);
    return eve::editing::applied<void>();
}

void AnimationClipEditor::seedPreviewClip() {
    animation_editing::EditorValue::Array tracks;
    tracks.push_back(trackValue("hips-track", "Hips",
                                {keyValue("hips-start", 0.0, 0.0, 0.0, 0.0),
                                 keyValue("hips-end", 2.0, 2.0, 0.0, 0.0)}));
    tracks.push_back(trackValue("spine-track", "Spine",
                                {keyValue("spine-start", 0.0, 0.0, 1.0, 0.0),
                                 keyValue("spine-end", 2.0, 2.0, 1.2, 0.0)}));
    tracks.push_back(trackValue("chest-track", "Chest", {keyValue("chest-start", 0.0, 0.0, 1.7, 0.0), keyValue("chest-end", 2.0, 2.0, 1.8, 0.0)}));
    tracks.push_back(trackValue("head-track", "Head", {keyValue("head-start", 0.0, 0.0, 2.25, 0.0), keyValue("head-end", 2.0, 2.0, 2.3, 0.0)}));
    tracks.push_back(trackValue("left-arm-track", "LeftArm", {keyValue("left-arm-start", 0.0, -0.55, 1.75, 0.0), keyValue("left-arm-end", 2.0, 1.45, 1.65, 0.0)}));
    tracks.push_back(trackValue("right-arm-track", "RightArm", {keyValue("right-arm-start", 0.0, 0.55, 1.75, 0.0), keyValue("right-arm-end", 2.0, 2.55, 1.85, 0.0)}));
    animation_editing::EditorValue::Array events;
    events.push_back(animation_editing::EditorValue::Object{{"id", std::string("footstep")},
                                                            {"time", 0.5},
                                                            {"name", std::string("footstep")},
                                                            {"payload", std::string("left")}});
    animation_editing::EditorValue::Array masks;
    masks.push_back(animation_editing::EditorValue::Object{{"bone", std::string("Hips")}, {"weight", 1.0}});
    masks.push_back(animation_editing::EditorValue::Object{{"bone", std::string("Spine")}, {"weight", 1.0}});
    animation_editing::EditorValue::Object root;
    root["schemaVersion"] = std::int64_t{1};
    root["settings"] =
        animation_editing::EditorValue::Object{{"duration", 2.0}, {"sampleRate", 30.0}, {"loop", true}};
    root["tracks"] = std::move(tracks);
    root["events"] = std::move(events);
    root["masks"]  = std::move(masks);
    auto loaded    = target_.loadSnapshot(animation_editing::EditorValue(std::move(root)));
    if (!loaded.ok())
        loaded.ignore("animation clip editor keeps defaults when the preview snapshot is rejected");
}

std::vector<std::string> AnimationClipEditor::skeletonBones() const {
    std::vector<std::string> result; result.reserve(skeleton_.size());
    for (const auto& bone : skeleton_) result.push_back(bone.name);
    return result;
}

std::string AnimationClipEditor::skeletonParent(const std::string& bone) const {
    for (const auto& entry : skeleton_) if (entry.name == bone) return entry.parent;
    return {};
}

animation_editing::EditorResult<void> AnimationClipEditor::configureWorkspace(
    editor::EditorWorkspace& workspace) const {
    editor::EditorWorkspace candidate = workspace;
    struct Panel {
        const char* id;
        const char* title;
        const char* region;
        const char* context;
        int         order;
    };
    constexpr Panel panels[] = {
        {"animation.skeleton", "Skeleton", "left", "list", 100},
        {"animation.preview", "Pose Preview", "center", "preview", 100},
        {"animation.inspector", "Clip Inspector", "right", "inspector", 100},
        {"animation.timeline", "Dope / Curve", "bottom", "timeline", 100},
    };
    for (const auto& panel : panels) {
        if (!candidate.registerPanel(panel.id, panel.title, panel.region, panel.order) ||
            !candidate.setPanelCapability(panel.id, "animation.clip") ||
            !candidate.setPanelContext(panel.id, panel.context))
            return editorError(animation_editing::EditorStatus::Rejected, "editor.animation.workspace-conflict",
                               "Could not install the animation clip workspace composition");
    }
    if (!candidate.activatePanel("animation.timeline"))
        return editorError(animation_editing::EditorStatus::Rejected, "editor.animation.workspace-activate",
                           "Could not activate the animation timeline panel");
    workspace = std::move(candidate);
    return eve::editing::applied<void>();
}

animation_editing::EditorResult<void> AnimationClipEditor::setViewport(float width, float rowHeight,
                                                                      float labelWidth) {
    if (!std::isfinite(width) || !std::isfinite(rowHeight) || !std::isfinite(labelWidth) || width < 8.0f ||
        rowHeight < 8.0f || labelWidth < 0.0f || labelWidth >= width)
        return editorError(animation_editing::EditorStatus::Rejected, "editor.animation.viewport",
                           "Dope-sheet viewport width, row height and label width are invalid");
    viewportWidth_ = width;
    rowHeight_     = rowHeight;
    labelWidth_    = labelWidth;
    return eve::editing::applied<void>();
}

animation_editing::EditorResult<void> AnimationClipEditor::seekSeconds(double seconds) {
    if (!std::isfinite(seconds))
        return editorError(animation_editing::EditorStatus::Rejected, "editor.animation.seek",
                           "Seek time must be finite");
    const double duration = target_.duration();
    playhead_             = duration <= 0.0 ? 0.0 : std::clamp(seconds, 0.0, duration);
    return refreshPreview();
}

animation_editing::EditorResult<void> AnimationClipEditor::seekX(float x) {
    if (!std::isfinite(x))
        return editorError(animation_editing::EditorStatus::Rejected, "editor.animation.seek-x",
                           "Dope-sheet seek requires a finite x");
    return seekSeconds(xToTime(x));
}

animation_editing::EditorResult<void> AnimationClipEditor::pointerDown(float x, float y) {
    if (!std::isfinite(x) || !std::isfinite(y))
        return editorError(animation_editing::EditorStatus::Rejected, "editor.animation.pointer",
                           "Pointer coordinates must be finite");
    const auto tracks = target_.tracks();
    if (tracks.empty()) return seekX(x);
    const int row = static_cast<int>(std::floor(y / rowHeight_));
    if (x < labelWidth_ && row >= 0 && static_cast<std::size_t>(row) < tracks.size())
        return selectBone(tracks[static_cast<std::size_t>(row)].bone);

    const auto keys = flattenKeys();
    int        best = -1;
    float      bestDistance = 8.0f;
    for (int i = 0; i < static_cast<int>(keys.size()); ++i) {
        const float dx = x - timeToX(keys[static_cast<std::size_t>(i)].time);
        const float dy = y - (static_cast<float>(keys[static_cast<std::size_t>(i)].row) * rowHeight_ + rowHeight_ * 0.5f);
        const float distance = std::hypot(dx, dy);
        if (distance < bestDistance) {
            bestDistance = distance;
            best         = i;
        }
    }
    if (best >= 0) {
        selectedKeyTrack_ = keys[static_cast<std::size_t>(best)].trackId;
        selectedKeyId_    = target_.tracks()[static_cast<std::size_t>(keys[static_cast<std::size_t>(best)].row)]
                                .keys[static_cast<std::size_t>(keys[static_cast<std::size_t>(best)].keyIndex)]
                                .id;
        return selectBone(keys[static_cast<std::size_t>(best)].bone);
    }
    selectedKeyId_ = animation_editing::StableId();
    return seekX(x);
}

animation_editing::EditorResult<void> AnimationClipEditor::selectBone(std::string bone) {
    if (bone.empty())
        return editorError(animation_editing::EditorStatus::Rejected, "editor.animation.select-bone",
                           "Bone name must not be empty");
    selectedBone_ = std::move(bone);
    return refreshPreview();
}

animation_editing::EditorResult<void> AnimationClipEditor::setMaskWeight(double weight) {
    return commit(target_.makeSetMask({selectedBone_, weight}), "Set mask " + selectedBone_);
}

animation_editing::EditorResult<void> AnimationClipEditor::setDuration(double duration) {
    return commit(target_.makeSetSettings(duration, target_.sampleRate(), target_.isLooping()), "Set clip duration");
}

animation_editing::EditorResult<void> AnimationClipEditor::setSampleRate(double sampleRate) {
    return commit(target_.makeSetSettings(target_.duration(), sampleRate, target_.isLooping()), "Set clip sample rate");
}

animation_editing::EditorResult<void> AnimationClipEditor::setLoop(bool loop) {
    return commit(target_.makeSetSettings(target_.duration(), target_.sampleRate(), loop), "Set clip loop");
}

animation_editing::EditorResult<void> AnimationClipEditor::moveSelectedKey(double time) {
    if (selectedKeyId_.empty())
        return editorError(animation_editing::EditorStatus::NotFound, "editor.animation.no-selected-key",
                           "No key is selected");
    if (!std::isfinite(time) || time < 0.0 || time > target_.duration())
        return editorError(animation_editing::EditorStatus::Rejected, "editor.animation.key-time",
                           "Key time must lie inside the clip duration");
    auto tracks = target_.tracks();
    for (auto& track : tracks) {
        if (track.id != selectedKeyTrack_) continue;
        const auto key = std::find_if(track.keys.begin(), track.keys.end(),
                                      [&](const auto& candidate) { return candidate.id == selectedKeyId_; });
        if (key == track.keys.end()) continue;
        key->time = time;
        return commit(target_.makeSetTrack(track), "Move key");
    }
    return editorError(animation_editing::EditorStatus::NotFound, "editor.animation.track-missing",
                       "Selected key track was not found");
}

animation_editing::AnimationTransformKey AnimationClipEditor::sampledSelectedTransform() const {
    animation_editing::AnimationTransformKey result;
    result.time = playhead_;
    for (const auto& bone : preview_.bones) {
        if (bone.bone != selectedBone_) continue;
        result.positionX = bone.positionX; result.positionY = bone.positionY; result.positionZ = bone.positionZ;
        result.rotationX = bone.rotationX; result.rotationY = bone.rotationY; result.rotationZ = bone.rotationZ; result.rotationW = bone.rotationW;
        result.scaleX = bone.scaleX; result.scaleY = bone.scaleY; result.scaleZ = bone.scaleZ;
        break;
    }
    return result;
}

animation_editing::EditorResult<void> AnimationClipEditor::keySelectedBone() {
    auto tracks = target_.tracks();
    for (auto& track : tracks) {
        if (track.bone != selectedBone_) continue;
        auto key = std::find_if(track.keys.begin(), track.keys.end(), [&](const auto& candidate) {
            return std::abs(candidate.time - playhead_) < 1e-6;
        });
        if (key == track.keys.end()) {
            auto inserted = sampledSelectedTransform();
            inserted.id = animation_editing::StableId("key." + selectedBone_ + "." + std::to_string(++txSequence_));
            track.keys.push_back(std::move(inserted));
            selectedKeyId_ = track.keys.back().id;
        } else {
            selectedKeyId_ = key->id;
        }
        selectedKeyTrack_ = track.id;
        return commit(target_.makeSetTrack(track), "Key " + selectedBone_);
    }
    return editorError(animation_editing::EditorStatus::NotFound, "editor.animation.track-missing",
                       "Selected bone has no editable transform track");
}

animation_editing::EditorResult<void> AnimationClipEditor::deleteSelectedKey() {
    if (selectedKeyId_.empty())
        return editorError(animation_editing::EditorStatus::NotFound, "editor.animation.no-selected-key", "No key is selected");
    auto tracks = target_.tracks();
    for (auto& track : tracks) {
        if (track.id != selectedKeyTrack_) continue;
        const auto before = track.keys.size();
        std::erase_if(track.keys, [&](const auto& key) { return key.id == selectedKeyId_; });
        if (track.keys.size() == before) break;
        selectedKeyId_ = animation_editing::StableId();
        return commit(target_.makeSetTrack(track), "Delete key");
    }
    return editorError(animation_editing::EditorStatus::NotFound, "editor.animation.key-missing", "Selected key no longer exists");
}

animation_editing::EditorResult<void> AnimationClipEditor::updateSelectedTransform(
    const animation_editing::AnimationTransformKey& value, std::string label) {
    auto tracks = target_.tracks();
    for (auto& track : tracks) {
        if (track.bone != selectedBone_) continue;
        auto key = std::find_if(track.keys.begin(), track.keys.end(), [&](const auto& candidate) {
            return std::abs(candidate.time - playhead_) < 1e-6;
        });
        if (key == track.keys.end()) {
            auto inserted = value;
            inserted.id = animation_editing::StableId("key." + selectedBone_ + "." + std::to_string(++txSequence_));
            inserted.time = playhead_;
            track.keys.push_back(std::move(inserted));
            selectedKeyId_ = track.keys.back().id;
        } else {
            auto replacement = value; replacement.id = key->id; replacement.time = key->time; *key = replacement;
            selectedKeyId_ = key->id;
        }
        selectedKeyTrack_ = track.id;
        return commit(target_.makeSetTrack(track), std::move(label));
    }
    return editorError(animation_editing::EditorStatus::NotFound, "editor.animation.track-missing", "Selected bone has no editable transform track");
}

animation_editing::EditorResult<void> AnimationClipEditor::setSelectedPosition(double x, double y, double z) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        return editorError(animation_editing::EditorStatus::Rejected, "editor.animation.position", "Position must be finite");
    auto value = sampledSelectedTransform(); value.positionX = x; value.positionY = y; value.positionZ = z;
    return updateSelectedTransform(value, "Set position " + selectedBone_);
}

animation_editing::EditorResult<void> AnimationClipEditor::setSelectedRotation(double xd, double yd, double zd) {
    if (!std::isfinite(xd) || !std::isfinite(yd) || !std::isfinite(zd))
        return editorError(animation_editing::EditorStatus::Rejected, "editor.animation.rotation", "Rotation must be finite");
    const double k = std::numbers::pi / 360.0;
    const double cx = std::cos(xd*k), sx = std::sin(xd*k), cy = std::cos(yd*k), sy = std::sin(yd*k), cz = std::cos(zd*k), sz = std::sin(zd*k);
    auto value = sampledSelectedTransform();
    value.rotationW = cx*cy*cz + sx*sy*sz; value.rotationX = sx*cy*cz - cx*sy*sz;
    value.rotationY = cx*sy*cz + sx*cy*sz; value.rotationZ = cx*cy*sz - sx*sy*cz;
    return updateSelectedTransform(value, "Set rotation " + selectedBone_);
}

animation_editing::EditorResult<void> AnimationClipEditor::setSelectedScale(double x, double y, double z) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || x <= 0.0 || y <= 0.0 || z <= 0.0)
        return editorError(animation_editing::EditorStatus::Rejected, "editor.animation.scale", "Scale must be positive and finite");
    auto value = sampledSelectedTransform(); value.scaleX = x; value.scaleY = y; value.scaleZ = z;
    return updateSelectedTransform(value, "Set scale " + selectedBone_);
}

animation_editing::EditorResult<void> AnimationClipEditor::commit(
    animation_editing::EditorResult<animation_editing::DomainOperation> operation, std::string label) {
    if (!operation.ok())
        return animation_editing::EditorResult<void>::failure(operation.status());
    editor::TransactionSpec spec;
    spec.id           = editor::TransactionId("animation.clip.tx." + std::to_string(++txSequence_));
    spec.label        = std::move(label);
    spec.target       = editor::TargetId(target_.targetId());
    spec.baseRevision = target_.revision();
    auto begun        = transactions_.begin(std::move(spec));
    if (!begun.ok())
        return editorError(begun.code(), "editor.animation.begin", "Could not begin the animation clip transaction");
    auto appended = transactions_.append(std::move(operation).takeValue());
    if (!appended.ok()) {
        auto discarded = transactions_.discard();
        if (!discarded.ok()) discarded.ignore("pending animation clip transaction already inactive");
        return animation_editing::EditorResult<void>::failure(appended.status());
    }
    auto committed = transactions_.commit();
    if (!committed.ok())
        return animation_editing::EditorResult<void>::failure(committed.status());
    return refreshPreview();
}

animation_editing::EditorResult<editor::TransactionReceipt> AnimationClipEditor::undo() {
    auto result = transactions_.undo();
    if (!result.ok()) return result;
    auto previewed = refreshPreview();
    if (!previewed.ok())
        return animation_editing::EditorResult<editor::TransactionReceipt>::failure(previewed.status());
    return result;
}

animation_editing::EditorResult<editor::TransactionReceipt> AnimationClipEditor::redo() {
    auto result = transactions_.redo();
    if (!result.ok()) return result;
    auto previewed = refreshPreview();
    if (!previewed.ok())
        return animation_editing::EditorResult<editor::TransactionReceipt>::failure(previewed.status());
    return result;
}

void AnimationClipEditor::play() noexcept { playing_ = true; }
void AnimationClipEditor::pause() noexcept { playing_ = false; }
void AnimationClipEditor::stop() noexcept {
    playing_  = false;
    playhead_ = 0.0;
}

animation_editing::EditorResult<void> AnimationClipEditor::update(double deltaSeconds) {
    if (playing_) {
        if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0)
            return editorError(animation_editing::EditorStatus::Rejected, "editor.animation.dt",
                               "Playback delta must be a non-negative finite time");
        playhead_ += deltaSeconds;
        const double duration = target_.duration();
        if (duration <= 0.0) {
            playhead_ = 0.0;
        } else if (target_.isLooping()) {
            playhead_ = std::fmod(playhead_, duration);
            if (playhead_ < 0.0) playhead_ += duration;
        } else if (playhead_ >= duration) {
            playhead_ = duration;
            playing_  = false;
        }
    }
    return refreshPreview();
}

animation_editing::EditorResult<void> AnimationClipEditor::refreshPreview() {
    const auto sampled = target_.preview(playhead_, skeletonBones());
    if (sampled.documentRevision != target_.revision()) {
        return editorError(animation_editing::EditorStatus::Conflict, "editor.animation.stale-preview",
                           "Clip preview revision does not match the document");
    }
    if (sampled.status != animation_editing::EditorStatus::Applied)
        return editorError(sampled.status, "editor.animation.preview", "Could not sample the animation clip pose");

    std::vector<animation_editing::SkeletonOverlayBone> bones;
    bones.reserve(sampled.bones.size());
    for (const auto& sample : sampled.bones) {
        animation_editing::SkeletonOverlayBone bone;
        bone.id         = animation_editing::StableId(sample.bone);
        bone.parent     = animation_editing::StableId(skeletonParent(sample.bone));
        bone.name       = sample.bone;
        bone.position   = {sample.positionX, sample.positionY, sample.positionZ};
        bone.rotation   = {sample.rotationX, sample.rotationY, sample.rotationZ, sample.rotationW};
        bone.selected   = sample.bone == selectedBone_;
        bone.maskWeight = sample.maskWeight;
        bones.push_back(std::move(bone));
    }
    animation_editing::SkeletonOverlayOptions options;
    options.showAxes            = true;
    options.axesForSelectedOnly = true;
    options.showConstraints     = false;
    auto overlay = overlayBuilder_.build(target_.targetId().value(), animation_editing::Revision(target_.revision()),
                                         bones, options);
    if (overlay.status != eve::editing::Status::Applied)
        return editorError(overlay.status, "editor.animation.overlay",
                           "Could not rebuild the skeleton overlay from the sampled pose");
    preview_ = sampled;
    overlay_ = std::move(overlay);
    return eve::editing::applied<void>();
}

float AnimationClipEditor::layoutHeight() const noexcept {
    return static_cast<float>(target_.tracks().size()) * rowHeight_ + 28.0f;
}

float AnimationClipEditor::playheadX() const noexcept { return timeToX(playhead_); }

double AnimationClipEditor::selectedMaskWeight() const {
    for (const auto& sample : preview_.bones)
        if (sample.bone == selectedBone_) return sample.maskWeight;
    return 1.0;
}

double AnimationClipEditor::selectedKeyTime() const {
    for (const auto& track : target_.tracks()) for (const auto& key : track.keys) if (key.id == selectedKeyId_) return key.time;
    return playhead_;
}

double AnimationClipEditor::selectedPositionX() const { return sampledSelectedTransform().positionX; }
double AnimationClipEditor::selectedPositionY() const { return sampledSelectedTransform().positionY; }
double AnimationClipEditor::selectedPositionZ() const { return sampledSelectedTransform().positionZ; }
double AnimationClipEditor::selectedScaleX() const { return sampledSelectedTransform().scaleX; }
double AnimationClipEditor::selectedScaleY() const { return sampledSelectedTransform().scaleY; }
double AnimationClipEditor::selectedScaleZ() const { return sampledSelectedTransform().scaleZ; }
double AnimationClipEditor::selectedRotationX() const { const auto q=sampledSelectedTransform(); return std::atan2(2*(q.rotationW*q.rotationX+q.rotationY*q.rotationZ),1-2*(q.rotationX*q.rotationX+q.rotationY*q.rotationY))*180/std::numbers::pi; }
double AnimationClipEditor::selectedRotationY() const { const auto q=sampledSelectedTransform(); return std::asin(std::clamp(2*(q.rotationW*q.rotationY-q.rotationZ*q.rotationX),-1.0,1.0))*180/std::numbers::pi; }
double AnimationClipEditor::selectedRotationZ() const { const auto q=sampledSelectedTransform(); return std::atan2(2*(q.rotationW*q.rotationZ+q.rotationX*q.rotationY),1-2*(q.rotationY*q.rotationY+q.rotationZ*q.rotationZ))*180/std::numbers::pi; }
int AnimationClipEditor::boneCount() const noexcept { return static_cast<int>(skeletonBones().size()); }
std::string AnimationClipEditor::boneName(int index) const { const auto bones=skeletonBones(); return index >= 0 && static_cast<std::size_t>(index) < bones.size() ? bones[static_cast<std::size_t>(index)] : std::string{}; }
std::string AnimationClipEditor::boneParent(int index) const { return skeletonParent(boneName(index)); }

int AnimationClipEditor::trackCount() const { return static_cast<int>(target_.tracks().size()); }

std::string AnimationClipEditor::trackBone(int index) const {
    const auto tracks = target_.tracks();
    if (index < 0 || static_cast<std::size_t>(index) >= tracks.size()) return {};
    return tracks[static_cast<std::size_t>(index)].bone;
}

std::string AnimationClipEditor::trackId(int index) const {
    const auto tracks = target_.tracks();
    if (index < 0 || static_cast<std::size_t>(index) >= tracks.size()) return {};
    return tracks[static_cast<std::size_t>(index)].id.value();
}

bool AnimationClipEditor::isTrackSelected(int index) const { return trackBone(index) == selectedBone_; }

int AnimationClipEditor::keyCount() const { return static_cast<int>(flattenKeys().size()); }

float AnimationClipEditor::keyX(int index) const {
    const auto keys = flattenKeys();
    if (index < 0 || static_cast<std::size_t>(index) >= keys.size()) return 0.0f;
    return timeToX(keys[static_cast<std::size_t>(index)].time);
}

float AnimationClipEditor::keyY(int index) const {
    const auto keys = flattenKeys();
    if (index < 0 || static_cast<std::size_t>(index) >= keys.size()) return 0.0f;
    return static_cast<float>(keys[static_cast<std::size_t>(index)].row) * rowHeight_ + rowHeight_ * 0.5f;
}

bool AnimationClipEditor::isKeySelected(int index) const {
    const auto keys = flattenKeys();
    if (index < 0 || static_cast<std::size_t>(index) >= keys.size()) return false;
    const auto& key = keys[static_cast<std::size_t>(index)];
    const auto tracks = target_.tracks();
    return selectedKeyTrack_ == key.trackId && tracks[static_cast<std::size_t>(key.row)].keys[static_cast<std::size_t>(key.keyIndex)].id == selectedKeyId_;
}

int AnimationClipEditor::eventCount() const { return static_cast<int>(target_.events().size()); }

float AnimationClipEditor::eventX(int index) const {
    const auto events = target_.events();
    if (index < 0 || static_cast<std::size_t>(index) >= events.size()) return 0.0f;
    return timeToX(events[static_cast<std::size_t>(index)].time);
}

std::string AnimationClipEditor::eventName(int index) const {
    const auto events = target_.events();
    if (index < 0 || static_cast<std::size_t>(index) >= events.size()) return {};
    return events[static_cast<std::size_t>(index)].name;
}

int AnimationClipEditor::primitiveCount() const { return static_cast<int>(overlay_.primitives.size()); }

std::string AnimationClipEditor::primitiveKind(int index) const {
    const auto* primitive = primitiveAt(index);
    return primitive ? primitive->kind : std::string{};
}

float AnimationClipEditor::primitiveX(int index) const {
    const auto* primitive = primitiveAt(index);
    return primitive ? static_cast<float>(primitive->position[0]) : 0.0f;
}

float AnimationClipEditor::primitiveY(int index) const {
    const auto* primitive = primitiveAt(index);
    return primitive ? static_cast<float>(primitive->position[1]) : 0.0f;
}

float AnimationClipEditor::primitiveDirX(int index) const {
    const auto* primitive = primitiveAt(index);
    return primitive ? static_cast<float>(primitive->direction[0]) : 0.0f;
}

float AnimationClipEditor::primitiveDirY(int index) const {
    const auto* primitive = primitiveAt(index);
    return primitive ? static_cast<float>(primitive->direction[1]) : 0.0f;
}

float AnimationClipEditor::primitiveLength(int index) const {
    const auto* primitive = primitiveAt(index);
    return primitive ? static_cast<float>(primitive->length) : 0.0f;
}

float AnimationClipEditor::primitiveRadius(int index) const {
    const auto* primitive = primitiveAt(index);
    return primitive ? static_cast<float>(primitive->radius) : 0.0f;
}

float AnimationClipEditor::primitiveR(int index) const {
    const auto* primitive = primitiveAt(index);
    return primitive ? static_cast<float>(primitive->color[0]) : 0.0f;
}

float AnimationClipEditor::primitiveG(int index) const {
    const auto* primitive = primitiveAt(index);
    return primitive ? static_cast<float>(primitive->color[1]) : 0.0f;
}

float AnimationClipEditor::primitiveB(int index) const {
    const auto* primitive = primitiveAt(index);
    return primitive ? static_cast<float>(primitive->color[2]) : 0.0f;
}

float AnimationClipEditor::timeToX(double time) const {
    const double duration = target_.duration();
    const float  usable   = std::max(1.0f, viewportWidth_ - labelWidth_);
    if (duration <= 0.0) return labelWidth_;
    return labelWidth_ + static_cast<float>(std::clamp(time / duration, 0.0, 1.0)) * usable;
}

double AnimationClipEditor::xToTime(float x) const {
    const float usable = std::max(1.0f, viewportWidth_ - labelWidth_);
    const float t      = std::clamp((x - labelWidth_) / usable, 0.0f, 1.0f);
    return static_cast<double>(t) * target_.duration();
}

const eve::editing::GizmoPrimitive* AnimationClipEditor::primitiveAt(int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= overlay_.primitives.size()) return nullptr;
    return &overlay_.primitives[static_cast<std::size_t>(index)];
}

std::vector<AnimationClipEditor::TimelineKey> AnimationClipEditor::flattenKeys() const {
    std::vector<TimelineKey> keys;
    const auto               tracks = target_.tracks();
    for (int row = 0; row < static_cast<int>(tracks.size()); ++row) {
        const auto& track = tracks[static_cast<std::size_t>(row)];
        for (int i = 0; i < static_cast<int>(track.keys.size()); ++i) {
            TimelineKey key;
            key.trackId  = track.id;
            key.bone     = track.bone;
            key.keyIndex = i;
            key.time     = track.keys[static_cast<std::size_t>(i)].time;
            key.row      = row;
            keys.push_back(std::move(key));
        }
    }
    return keys;
}

}  // namespace eve::animation_editor
