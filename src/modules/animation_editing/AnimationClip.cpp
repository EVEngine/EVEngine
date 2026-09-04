#include "animation_editing/AnimationClip.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <set>
#include <utility>

namespace eve::animation_editing {
namespace {

template <class T>
EditorResult<T> clipError(EditorStatus status, const char* rule, std::string message) {
    return eve::editing::failed<T>(status, RuleId(rule), std::move(message));
}

const EditorValue* field(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

const std::string* stringField(const EditorValue& value, const char* key) {
    const EditorValue* entry = field(value, key);
    return entry ? entry->getIf<std::string>() : nullptr;
}

bool readNumber(const EditorValue& value, const char* key, double& out) {
    const EditorValue* entry = field(value, key);
    if (!entry) return false;
    if (const auto* real = entry->getIf<double>()) {
        out = *real;
        return true;
    }
    if (const auto* integer = entry->getIf<std::int64_t>()) {
        out = static_cast<double>(*integer);
        return true;
    }
    return false;
}

EditorValue keyValue(const AnimationTransformKey& key) {
    return EditorValue::Object{{"id", key.id.value()}, {"time", key.time},
                               {"px", key.positionX}, {"py", key.positionY}, {"pz", key.positionZ},
                               {"rx", key.rotationX}, {"ry", key.rotationY}, {"rz", key.rotationZ},
                               {"rw", key.rotationW}, {"sx", key.scaleX}, {"sy", key.scaleY},
                               {"sz", key.scaleZ}};
}

EditorResult<AnimationTransformKey> parseKey(const EditorValue& value) {
    const auto* id = stringField(value, "id");
    double time = 0, px = 0, py = 0, pz = 0, rx = 0, ry = 0, rz = 0, rw = 0, sx = 0, sy = 0, sz = 0;
    if (!id || id->empty() || !readNumber(value, "time", time) || !readNumber(value, "px", px) ||
        !readNumber(value, "py", py) || !readNumber(value, "pz", pz) || !readNumber(value, "rx", rx) ||
        !readNumber(value, "ry", ry) || !readNumber(value, "rz", rz) || !readNumber(value, "rw", rw) ||
        !readNumber(value, "sx", sx) || !readNumber(value, "sy", sy) || !readNumber(value, "sz", sz))
        return clipError<AnimationTransformKey>(EditorStatus::Rejected, "editor.animation.invalid-key",
                                                "Transform key requires a stable id, time and complete TRS");
    const double values[]{time, px, py, pz, rx, ry, rz, rw, sx, sy, sz};
    for (double component : values)
        if (!std::isfinite(component))
            return clipError<AnimationTransformKey>(EditorStatus::Rejected, "editor.animation.nonfinite-key",
                                                    "Transform key components must be finite");
    const double length = std::sqrt(rx * rx + ry * ry + rz * rz + rw * rw);
    if (time < 0.0 || sx <= 0.0 || sy <= 0.0 || sz <= 0.0 || length < 1e-8)
        return clipError<AnimationTransformKey>(EditorStatus::Rejected, "editor.animation.invalid-key-range",
                                                "Key time must be non-negative, scale positive and rotation non-zero");
    return eve::editing::applied<AnimationTransformKey>({StableId(*id), time, px, py, pz,
                                                          rx / length, ry / length, rz / length, rw / length,
                                                          sx, sy, sz});
}

EditorValue trackValue(const AnimationBoneTrack& track) {
    EditorValue::Array keys;
    for (const auto& key : track.keys) keys.push_back(keyValue(key));
    return EditorValue::Object{{"id", track.id.value()}, {"bone", track.bone}, {"keys", std::move(keys)}};
}

EditorResult<AnimationBoneTrack> parseTrack(const EditorValue& value) {
    const auto* id = stringField(value, "id");
    const auto* bone = stringField(value, "bone");
    const EditorValue* keyEntry = field(value, "keys");
    const auto* keys = keyEntry ? keyEntry->getIf<EditorValue::Array>() : nullptr;
    if (!id || id->empty() || !bone || bone->empty() || !keys)
        return clipError<AnimationBoneTrack>(EditorStatus::Rejected, "editor.animation.invalid-track",
                                             "Bone track requires stable id, bone name and keys");
    AnimationBoneTrack result{StableId(*id), *bone, {}};
    std::set<StableId> ids;
    for (const auto& entry : *keys) {
        auto key = parseKey(entry);
        if (!key.ok() || !ids.insert(key.value().id).second)
            return clipError<AnimationBoneTrack>(EditorStatus::Rejected, "editor.animation.duplicate-key",
                                                 "Transform key ids must be unique inside a track");
        result.keys.push_back(std::move(key).value());
    }
    std::sort(result.keys.begin(), result.keys.end(), [](const auto& a, const auto& b) {
        return a.time == b.time ? a.id < b.id : a.time < b.time;
    });
    return eve::editing::applied<AnimationBoneTrack>(std::move(result));
}

EditorValue eventValue(const AnimationEventRecord& event) {
    return EditorValue::Object{{"id", event.id.value()}, {"time", event.time},
                               {"name", event.name}, {"payload", event.payload}};
}

EditorResult<AnimationEventRecord> parseEvent(const EditorValue& value) {
    const auto* id = stringField(value, "id");
    double time = 0;
    const auto* name = stringField(value, "name");
    const auto* payload = stringField(value, "payload");
    if (!id || id->empty() || !readNumber(value, "time", time) || !std::isfinite(time) || time < 0.0 ||
        !name || name->empty() || !payload)
        return clipError<AnimationEventRecord>(EditorStatus::Rejected, "editor.animation.invalid-event",
                                               "Event requires stable id, non-negative time, name and payload");
    return eve::editing::applied<AnimationEventRecord>({StableId(*id), time, *name, *payload});
}

EditorValue settingsValue(double duration, double sampleRate, bool loop) {
    return EditorValue::Object{{"duration", duration}, {"sampleRate", sampleRate}, {"loop", loop}};
}

DomainOperation operation(const char* type, const char* inverseType, const std::string& target,
                          EditorValue payload, EditorValue inverse, std::string object) {
    DomainOperation result;
    result.type = type; result.inverseType = inverseType; result.target = TargetId(target);
    result.payload = std::move(payload); result.inverse = std::move(inverse); result.hasInverse = true;
    result.affectedObjects.push_back({TargetId(target), std::move(object), 0});
    return result;
}

std::string normalized(std::string name) {
    std::string result;
    for (unsigned char c : name)
        if (std::isalnum(c)) result.push_back(static_cast<char>(std::tolower(c)));
    return result;
}

double mix(double a, double b, double amount) { return a + (b - a) * amount; }

EditorDiagnostic clipDiagnostic(const char* rule, DiagnosticSeverity severity, std::string message) {
    return eve::editing::ruleDiagnostic(eve::DiagnosticCode::PreconditionViolation, RuleId(rule), severity,
                                        std::move(message));
}

}  // namespace

AnimationClipDocumentTarget::AnimationClipDocumentTarget(std::string id) : id_(std::move(id)) {}

TargetDescriptor AnimationClipDocumentTarget::describe() const {
    TargetDescriptor result;
    result.id = TargetId(id_); result.type = "animation-clip-document"; result.revision = revision_;
    result.capabilities = {IAnimationClipEditTarget::editingCapabilityId(),
                           eve::editing::IEditingSnapshotProvider::editingCapabilityId()};
    return result;
}

void* AnimationClipDocumentTarget::queryCapability(const CapabilityId& capability) {
    if (capability == IAnimationClipEditTarget::editingCapabilityId())
        return static_cast<IAnimationClipEditTarget*>(this);
    if (capability == eve::editing::IEditingSnapshotProvider::editingCapabilityId())
        return static_cast<eve::editing::IEditingSnapshotProvider*>(this);
    return nullptr;
}

EditorResult<void> AnimationClipDocumentTarget::applyDomainOperation(const DomainOperation& op) {
    if (op.target != TargetId(id_))
        return clipError<void>(EditorStatus::Rejected, "editor.animation.target-mismatch", "Operation targets another clip");
    if (op.type == "animation.clip.settings.v1") {
        double duration = 0, rate = 0;
        const EditorValue* loopEntry = field(op.payload, "loop"); const auto* loop = loopEntry ? loopEntry->getIf<bool>() : nullptr;
        if (!readNumber(op.payload, "duration", duration) || !readNumber(op.payload, "sampleRate", rate) ||
            !loop || !std::isfinite(duration) || !std::isfinite(rate) || duration <= 0.0 || rate <= 0.0)
            return clipError<void>(EditorStatus::Rejected, "editor.animation.invalid-settings", "Duration and sample rate must be positive");
        duration_ = duration; sampleRate_ = rate; loop_ = *loop;
    } else if (op.type == "animation.clip.track.set.v1") {
        auto track = parseTrack(op.payload); if (!track.ok()) return clipError<void>(track.code(), "editor.animation.invalid-track", "Track payload is invalid");
        for (const auto& key : track.value().keys)
            if (key.time > duration_) return clipError<void>(EditorStatus::Rejected, "editor.animation.key-after-duration", "Track key exceeds clip duration");
        for (const auto& [id, current] : tracks_)
            if (id != track.value().id && current.bone == track.value().bone)
                return clipError<void>(EditorStatus::Conflict, "editor.animation.duplicate-bone-track", "A bone may have only one track");
        auto trackRecord = std::move(track).value(); tracks_[trackRecord.id] = std::move(trackRecord);
    } else if (op.type == "animation.clip.track.delete.v1") {
        auto track = parseTrack(op.payload); if (!track.ok() || !tracks_.erase(track.value().id))
            return clipError<void>(EditorStatus::NotFound, "editor.animation.track-not-found", "Bone track was not found");
    } else if (op.type == "animation.clip.event.set.v1") {
        auto event = parseEvent(op.payload); if (!event.ok()) return clipError<void>(event.code(), "editor.animation.invalid-event", "Event payload is invalid");
        if (event.value().time > duration_) return clipError<void>(EditorStatus::Rejected, "editor.animation.event-after-duration", "Event exceeds clip duration");
        auto eventRecord = std::move(event).value(); events_[eventRecord.id] = std::move(eventRecord);
    } else if (op.type == "animation.clip.event.delete.v1") {
        auto event = parseEvent(op.payload); if (!event.ok() || !events_.erase(event.value().id))
            return clipError<void>(EditorStatus::NotFound, "editor.animation.event-not-found", "Event was not found");
    } else if (op.type == "animation.clip.mask.set.v1") {
        const auto* bone = stringField(op.payload, "bone"); double weight = 0;
        if (!bone || bone->empty() || !readNumber(op.payload, "weight", weight) || !std::isfinite(weight) ||
            weight < 0.0 || weight > 1.0)
            return clipError<void>(EditorStatus::Rejected, "editor.animation.invalid-mask", "Mask weight must be in [0, 1]");
        mask_[*bone] = weight;
    } else {
        return clipError<void>(EditorStatus::Unsupported, "editor.animation.unsupported-operation", "Unsupported clip operation");
    }
    ++revision_; dirty_.include(0, 0); return eve::editing::applied<void>();
}

std::unique_ptr<IDomainOperationTarget> AnimationClipDocumentTarget::cloneDomainState() const {
    return std::make_unique<AnimationClipDocumentTarget>(*this);
}

EditorResult<void> AnimationClipDocumentTarget::commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) {
    auto* clip = dynamic_cast<AnimationClipDocumentTarget*>(candidate.get());
    if (!clip || clip->id_ != id_) return clipError<void>(EditorStatus::Rejected, "editor.animation.invalid-candidate", "Candidate is not this clip document");
    *this = std::move(*clip); return eve::editing::applied<void>();
}

EditorResult<DomainOperation> AnimationClipDocumentTarget::makeSetSettings(double duration, double sampleRate, bool loop) const {
    if (!std::isfinite(duration) || !std::isfinite(sampleRate) || duration <= 0.0 || sampleRate <= 0.0)
        return clipError<DomainOperation>(EditorStatus::Rejected, "editor.animation.invalid-settings", "Duration and sample rate must be positive");
    for (const auto& [id, track] : tracks_) { static_cast<void>(id); for (const auto& key : track.keys)
        if (key.time > duration) return clipError<DomainOperation>(EditorStatus::Rejected, "editor.animation.duration-truncates-key", "New duration would truncate a transform key"); }
    for (const auto& [id, event] : events_) { static_cast<void>(id); if (event.time > duration)
        return clipError<DomainOperation>(EditorStatus::Rejected, "editor.animation.duration-truncates-event", "New duration would truncate an event"); }
    return eve::editing::applied<DomainOperation>(operation("animation.clip.settings.v1", "animation.clip.settings.v1", id_,
        settingsValue(duration, sampleRate, loop), settingsValue(duration_, sampleRate_, loop_), "settings"));
}

EditorResult<DomainOperation> AnimationClipDocumentTarget::makeSetTrack(const AnimationBoneTrack& track) const {
    auto parsed = parseTrack(trackValue(track)); if (!parsed.ok()) return clipError<DomainOperation>(parsed.code(), "editor.animation.invalid-track", "Track is invalid");
    const auto old = tracks_.find(track.id); EditorValue inverse = old == tracks_.end() ? trackValue(track) : trackValue(old->second);
    return eve::editing::applied<DomainOperation>(operation("animation.clip.track.set.v1",
        old == tracks_.end() ? "animation.clip.track.delete.v1" : "animation.clip.track.set.v1", id_, trackValue(parsed.value()), std::move(inverse), track.id.value()));
}

EditorResult<DomainOperation> AnimationClipDocumentTarget::makeDeleteTrack(const StableId& id) const {
    const auto found = tracks_.find(id); if (found == tracks_.end()) return clipError<DomainOperation>(EditorStatus::NotFound, "editor.animation.track-not-found", "Bone track was not found");
    return eve::editing::applied<DomainOperation>(operation("animation.clip.track.delete.v1", "animation.clip.track.set.v1", id_, trackValue(found->second), trackValue(found->second), id.value()));
}

EditorResult<DomainOperation> AnimationClipDocumentTarget::makeSetEvent(const AnimationEventRecord& event) const {
    auto parsed = parseEvent(eventValue(event)); if (!parsed.ok()) return clipError<DomainOperation>(parsed.code(), "editor.animation.invalid-event", "Event is invalid");
    const auto old = events_.find(event.id); EditorValue inverse = old == events_.end() ? eventValue(event) : eventValue(old->second);
    return eve::editing::applied<DomainOperation>(operation("animation.clip.event.set.v1",
        old == events_.end() ? "animation.clip.event.delete.v1" : "animation.clip.event.set.v1", id_, eventValue(parsed.value()), std::move(inverse), event.id.value()));
}

EditorResult<DomainOperation> AnimationClipDocumentTarget::makeDeleteEvent(const StableId& id) const {
    const auto found = events_.find(id); if (found == events_.end()) return clipError<DomainOperation>(EditorStatus::NotFound, "editor.animation.event-not-found", "Event was not found");
    return eve::editing::applied<DomainOperation>(operation("animation.clip.event.delete.v1", "animation.clip.event.set.v1", id_, eventValue(found->second), eventValue(found->second), id.value()));
}

EditorResult<DomainOperation> AnimationClipDocumentTarget::makeSetMask(const AnimationMaskEntry& mask) const {
    if (mask.bone.empty() || !std::isfinite(mask.weight) || mask.weight < 0.0 || mask.weight > 1.0)
        return clipError<DomainOperation>(EditorStatus::Rejected, "editor.animation.invalid-mask", "Mask weight must be in [0, 1]");
    const auto old = mask_.find(mask.bone); const double oldWeight = old == mask_.end() ? 1.0 : old->second;
    return eve::editing::applied<DomainOperation>(operation("animation.clip.mask.set.v1", "animation.clip.mask.set.v1", id_,
        EditorValue::Object{{"bone", mask.bone}, {"weight", mask.weight}}, EditorValue::Object{{"bone", mask.bone}, {"weight", oldWeight}}, mask.bone));
}

std::vector<AnimationBoneTrack> AnimationClipDocumentTarget::tracks() const {
    std::vector<AnimationBoneTrack> result; for (const auto& [id, track] : tracks_) { static_cast<void>(id); result.push_back(track); } return result;
}

std::vector<AnimationEventRecord> AnimationClipDocumentTarget::events() const {
    std::vector<AnimationEventRecord> result; for (const auto& [id, event] : events_) { static_cast<void>(id); result.push_back(event); }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.time == b.time ? a.id < b.id : a.time < b.time; }); return result;
}

std::vector<EditorDiagnostic> AnimationClipDocumentTarget::validate(const std::vector<std::string>& bones) const {
    std::vector<EditorDiagnostic> result; const std::set<std::string> known(bones.begin(), bones.end());
    for (const auto& [id, track] : tracks_) { static_cast<void>(id);
        if (!known.empty() && !known.contains(track.bone)) result.push_back(clipDiagnostic("editor.animation.missing-track-bone", DiagnosticSeverity::Error, "Track references missing bone: " + track.bone));
        if (track.keys.empty()) result.push_back(clipDiagnostic("editor.animation.empty-track", DiagnosticSeverity::Warning, "Bone track has no keys: " + track.bone));
    }
    for (const auto& [bone, weight] : mask_) { static_cast<void>(weight); if (!known.empty() && !known.contains(bone))
        result.push_back(clipDiagnostic("editor.animation.missing-mask-bone", DiagnosticSeverity::Error, "Mask references missing bone: " + bone)); }
    return result;
}

AnimationClipPreview AnimationClipDocumentTarget::preview(double time, const std::vector<std::string>& bones) const {
    AnimationClipPreview result; result.documentRevision = revision_;
    if (!std::isfinite(time)) { result.diagnostics.push_back(clipDiagnostic("editor.animation.invalid-preview-time", DiagnosticSeverity::Error, "Preview time must be finite")); return result; }
    result.time = loop_ ? std::fmod(std::max(0.0, time), duration_) : std::clamp(time, 0.0, duration_);
    result.diagnostics = validate(bones);
    for (const auto& [id, track] : tracks_) { static_cast<void>(id); if (track.keys.empty()) continue;
        auto upper = std::upper_bound(track.keys.begin(), track.keys.end(), result.time, [](double t, const auto& key) { return t < key.time; });
        const auto& b = upper == track.keys.end() ? track.keys.back() : *upper;
        const auto& a = upper == track.keys.begin() ? track.keys.front() : *(upper - 1);
        const double amount = b.time > a.time ? (result.time - a.time) / (b.time - a.time) : 0.0;
        AnimationSampledBone sample; sample.bone = track.bone;
        sample.positionX = mix(a.positionX, b.positionX, amount); sample.positionY = mix(a.positionY, b.positionY, amount); sample.positionZ = mix(a.positionZ, b.positionZ, amount);
        sample.rotationX = mix(a.rotationX, b.rotationX, amount); sample.rotationY = mix(a.rotationY, b.rotationY, amount); sample.rotationZ = mix(a.rotationZ, b.rotationZ, amount); sample.rotationW = mix(a.rotationW, b.rotationW, amount);
        const double length = std::sqrt(sample.rotationX * sample.rotationX + sample.rotationY * sample.rotationY + sample.rotationZ * sample.rotationZ + sample.rotationW * sample.rotationW);
        if (length > 1e-8) { sample.rotationX /= length; sample.rotationY /= length; sample.rotationZ /= length; sample.rotationW /= length; }
        sample.scaleX = mix(a.scaleX, b.scaleX, amount); sample.scaleY = mix(a.scaleY, b.scaleY, amount); sample.scaleZ = mix(a.scaleZ, b.scaleZ, amount);
        const auto mask = mask_.find(track.bone); sample.maskWeight = mask == mask_.end() ? 1.0 : mask->second; result.bones.push_back(std::move(sample));
    }
    result.status = std::any_of(result.diagnostics.begin(), result.diagnostics.end(), [](const auto& d) { return d.severity() == DiagnosticSeverity::Error; }) ? EditorStatus::Failed : EditorStatus::Applied;
    return result;
}

AnimationRetargetPreview AnimationClipDocumentTarget::previewRetarget(const std::vector<std::string>& targetBones) const {
    AnimationRetargetPreview result; result.documentRevision = revision_; std::set<std::string> used;
    for (const auto& [id, track] : tracks_) { static_cast<void>(id); auto exact = std::find(targetBones.begin(), targetBones.end(), track.bone);
        auto found = exact; if (found == targetBones.end()) found = std::find_if(targetBones.begin(), targetBones.end(), [&](const auto& target) { return normalized(target) == normalized(track.bone); });
        if (found == targetBones.end() || used.contains(*found)) result.unmatchedSourceBones.push_back(track.bone); else { result.mapping[track.bone] = *found; used.insert(*found); }
    }
    for (const auto& target : targetBones) if (!used.contains(target)) result.unmatchedTargetBones.push_back(target);
    if (!result.unmatchedSourceBones.empty()) result.diagnostics.push_back(clipDiagnostic("editor.animation.unmatched-source-bones", DiagnosticSeverity::Warning, "Some source tracks cannot be mapped"));
    if (!result.unmatchedTargetBones.empty()) result.diagnostics.push_back(clipDiagnostic("editor.animation.unmatched-target-bones", DiagnosticSeverity::Info, "Some target bones remain at bind pose"));
    result.status = EditorStatus::Applied; return result;
}

EditorValue AnimationClipDocumentTarget::snapshotValue() const {
    EditorValue::Array tracks; for (const auto& [id, track] : tracks_) { static_cast<void>(id); tracks.push_back(trackValue(track)); }
    EditorValue::Array events; for (const auto& event : this->events()) events.push_back(eventValue(event));
    EditorValue::Array masks; for (const auto& [bone, weight] : mask_) masks.push_back(EditorValue::Object{{"bone", bone}, {"weight", weight}});
    return EditorValue::Object{{"schemaVersion", int64_t{1}}, {"settings", settingsValue(duration_, sampleRate_, loop_)}, {"tracks", std::move(tracks)}, {"events", std::move(events)}, {"masks", std::move(masks)}};
}

EditorResult<void> AnimationClipDocumentTarget::loadSnapshot(const EditorValue& snapshot) {
    const EditorValue* versionEntry = field(snapshot, "schemaVersion"); const auto* version = versionEntry ? versionEntry->getIf<int64_t>() : nullptr;
    const EditorValue* settings = field(snapshot, "settings"); const EditorValue* tracksEntry = field(snapshot, "tracks"); const EditorValue* eventsEntry = field(snapshot, "events"); const EditorValue* masksEntry = field(snapshot, "masks");
    const auto* tracks = tracksEntry ? tracksEntry->getIf<EditorValue::Array>() : nullptr; const auto* events = eventsEntry ? eventsEntry->getIf<EditorValue::Array>() : nullptr; const auto* masks = masksEntry ? masksEntry->getIf<EditorValue::Array>() : nullptr;
    if (!version || *version != 1 || !settings || !tracks || !events || !masks) return clipError<void>(EditorStatus::Unsupported, "editor.animation.invalid-snapshot", "Clip snapshot schema is unsupported");
    double duration = 0, rate = 0;
    const EditorValue* loopEntry = field(*settings, "loop"); const auto* loop = loopEntry ? loopEntry->getIf<bool>() : nullptr;
    if (!readNumber(*settings, "duration", duration) || !readNumber(*settings, "sampleRate", rate) || !loop)
        return clipError<void>(EditorStatus::Rejected, "editor.animation.invalid-settings", "Clip snapshot settings are incomplete");
    AnimationClipDocumentTarget candidate(id_); auto settingsOp = candidate.makeSetSettings(duration, rate, *loop);
    if (!settingsOp.ok() || !candidate.applyDomainOperation(settingsOp.value()).ok()) return clipError<void>(EditorStatus::Rejected, "editor.animation.invalid-settings", "Clip snapshot settings are invalid");
    for (const auto& entry : *tracks) { auto parsed = parseTrack(entry); if (!parsed.ok()) return clipError<void>(EditorStatus::Rejected, "editor.animation.invalid-track", "Clip snapshot contains invalid track"); auto op = candidate.makeSetTrack(parsed.value()); if (!op.ok() || !candidate.applyDomainOperation(op.value()).ok()) return clipError<void>(EditorStatus::Rejected, "editor.animation.invalid-track", "Clip snapshot track cannot be applied"); }
    for (const auto& entry : *events) { auto parsed = parseEvent(entry); if (!parsed.ok()) return clipError<void>(EditorStatus::Rejected, "editor.animation.invalid-event", "Clip snapshot contains invalid event"); auto op = candidate.makeSetEvent(parsed.value()); if (!op.ok() || !candidate.applyDomainOperation(op.value()).ok()) return clipError<void>(EditorStatus::Rejected, "editor.animation.invalid-event", "Clip snapshot event cannot be applied"); }
    for (const auto& entry : *masks) { const auto* bone = stringField(entry, "bone"); double weight = 0; if (!bone || !readNumber(entry, "weight", weight)) return clipError<void>(EditorStatus::Rejected, "editor.animation.invalid-mask", "Clip snapshot contains invalid mask"); auto op = candidate.makeSetMask({*bone, weight}); if (!op.ok() || !candidate.applyDomainOperation(op.value()).ok()) return clipError<void>(EditorStatus::Rejected, "editor.animation.invalid-mask", "Clip snapshot mask cannot be applied"); }
    candidate.revision_ = revision_ + 1; candidate.dirty_.include(0, 0); *this = std::move(candidate); return eve::editing::applied<void>();
}

EditorResult<AnimationBoneTrack> parseAnimationBoneTrack(const EditorValue& value) { return parseTrack(value); }
EditorResult<AnimationEventRecord> parseAnimationEventRecord(const EditorValue& value) { return parseEvent(value); }

}  // namespace eve::animation_editing
