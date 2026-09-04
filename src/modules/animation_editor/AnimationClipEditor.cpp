#include "animation_editor/AnimationClipEditor.h"

#include "editor/EditorProtocol.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
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
    seedPreviewClip();
    auto previewed = refreshPreview();
    if (!previewed.ok())
        previewed.ignore("animation clip editor keeps an empty overlay when the seeded preview is rejected");
}

void AnimationClipEditor::seedPreviewClip() {
    animation_editing::EditorValue::Array tracks;
    tracks.push_back(trackValue("hips-track", "Hips",
                                {keyValue("hips-start", 0.0, 0.0, 0.0, 0.0),
                                 keyValue("hips-end", 2.0, 2.0, 0.0, 0.0)}));
    tracks.push_back(trackValue("spine-track", "Spine",
                                {keyValue("spine-start", 0.0, 0.0, 1.0, 0.0),
                                 keyValue("spine-end", 2.0, 2.0, 1.2, 0.0)}));
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
    return {"Hips", "Spine"};
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
        selectedKeyIndex_ = keys[static_cast<std::size_t>(best)].keyIndex;
        return selectBone(keys[static_cast<std::size_t>(best)].bone);
    }
    selectedKeyIndex_ = -1;
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
    return commit(target_.makeSetSettings(duration, target_.sampleRate(), target_.loop()), "Set clip duration");
}

animation_editing::EditorResult<void> AnimationClipEditor::setSampleRate(double sampleRate) {
    return commit(target_.makeSetSettings(target_.duration(), sampleRate, target_.loop()), "Set clip sample rate");
}

animation_editing::EditorResult<void> AnimationClipEditor::setLoop(bool loop) {
    return commit(target_.makeSetSettings(target_.duration(), target_.sampleRate(), loop), "Set clip loop");
}

animation_editing::EditorResult<void> AnimationClipEditor::moveSelectedKey(double time) {
    if (selectedKeyIndex_ < 0)
        return editorError(animation_editing::EditorStatus::NotFound, "editor.animation.no-selected-key",
                           "No key is selected");
    if (!std::isfinite(time) || time < 0.0 || time > target_.duration())
        return editorError(animation_editing::EditorStatus::Rejected, "editor.animation.key-time",
                           "Key time must lie inside the clip duration");
    auto tracks = target_.tracks();
    for (auto& track : tracks) {
        if (track.id != selectedKeyTrack_) continue;
        if (selectedKeyIndex_ >= static_cast<int>(track.keys.size()))
            return editorError(animation_editing::EditorStatus::NotFound, "editor.animation.key-missing",
                               "Selected key is no longer on the track");
        track.keys[static_cast<std::size_t>(selectedKeyIndex_)].time = time;
        return commit(target_.makeSetTrack(track), "Move key");
    }
    return editorError(animation_editing::EditorStatus::NotFound, "editor.animation.track-missing",
                       "Selected key track was not found");
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
        } else if (target_.loop()) {
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
        bone.parent     = sample.bone == "Spine" ? animation_editing::StableId("Hips")
                                                 : animation_editing::StableId();
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

bool AnimationClipEditor::trackSelected(int index) const { return trackBone(index) == selectedBone_; }

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

bool AnimationClipEditor::keySelected(int index) const {
    const auto keys = flattenKeys();
    if (index < 0 || static_cast<std::size_t>(index) >= keys.size()) return false;
    const auto& key = keys[static_cast<std::size_t>(index)];
    return selectedKeyIndex_ == key.keyIndex && selectedKeyTrack_ == key.trackId;
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
