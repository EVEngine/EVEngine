#include "audio_editing/AudioTarget.h"

#include <tuple>
#include <utility>

namespace eve::audio_editing {
namespace {

template <class T>
EditorResult<T> audioError(EditorStatus status, const char* rule, std::string message) {
    return eve::editing::failed<T>(status, RuleId(rule), std::move(message));
}

const EditorValue* field(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

PropertyDescriptor property(const char* path, const char* label, const char* category,
                            PropertyType type, EditorValue value) {
    PropertyDescriptor result;
    result.path = PropertyPath(path);
    result.displayNameKey = label;
    result.category = category;
    result.type = type;
    result.flags = PropertyFlag::Runtime;
    result.defaultValue = std::move(value);
    return result;
}

}  // namespace

AudioSourceTarget::AudioSourceTarget(std::string id) : id_(std::move(id)), values_(defaults()) {}

TargetDescriptor AudioSourceTarget::describe() const {
    return {TargetId(id_), "audio-source", revision_, false,
            {editingCapabilityId(), IEditingSnapshotProvider::editingCapabilityId()}};
}

void* AudioSourceTarget::queryCapability(const CapabilityId& capability) {
    if (capability == editingCapabilityId()) return static_cast<IPropertyProvider*>(this);
    if (capability == IEditingSnapshotProvider::editingCapabilityId())
        return static_cast<IEditingSnapshotProvider*>(this);
    return nullptr;
}

EditorResult<void> AudioSourceTarget::applyDomainOperation(const DomainOperation& operation) {
    if (operation.target != TargetId(id_) || operation.type != "audio.source.property.set.v1")
        return audioError<void>(EditorStatus::Rejected, "editor.audio.operation",
                                "Operation is not valid for this audio source");
    const EditorValue* pathValue = field(operation.payload, "path");
    const EditorValue* value = field(operation.payload, "value");
    const auto* path = pathValue ? pathValue->getIf<std::string>() : nullptr;
    if (!path || !value)
        return audioError<void>(EditorStatus::Rejected, "editor.audio.payload",
                                "Audio source operation requires path and value");
    auto descriptor = sourceSchema().find(PropertyPath(*path));
    if (!descriptor)
        return audioError<void>(EditorStatus::Unsupported, "editor.audio.property",
                                "Unknown audio source property: " + *path);
    auto valid = validatePropertyValue(*descriptor, *value);
    if (!valid.ok()) return valid;
    values_[*path] = *value;
    ++revision_;
    dirty_.include(0, 0);
    return eve::editing::applied<void>();
}

std::unique_ptr<IDomainOperationTarget> AudioSourceTarget::cloneDomainState() const {
    return std::make_unique<AudioSourceTarget>(*this);
}

EditorResult<void> AudioSourceTarget::commitDomainState(
    std::unique_ptr<IDomainOperationTarget> candidate) {
    auto* typed = dynamic_cast<AudioSourceTarget*>(candidate.get());
    if (!typed || typed->id_ != id_)
        return audioError<void>(EditorStatus::Conflict, "editor.audio.candidate-mismatch",
                                "Audio source candidate belongs to another target");
    *this = *typed;
    return eve::editing::applied<void>();
}

eve::Result<eve::Revision> AudioSourceTarget::currentRevision(const SelectionSnapshot& selection) const {
    if (!selectionMatches(selection))
        return eve::Result<eve::Revision>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Selection does not belong to this audio source",
            "editor.audio.selection", {}, "editor.AudioSourceTarget"));
    return eve::Result<eve::Revision>::success(eve::Revision(revision_));
}

PropertySchema AudioSourceTarget::schema(const SelectionSnapshot&) const { return sourceSchema(); }

PropertyReadResult AudioSourceTarget::read(const SelectionSnapshot& selection,
                                           const PropertyPath& path) const {
    if (!selectionMatches(selection)) return {};
    const auto found = values_.find(path.value());
    return found == values_.end() ? PropertyReadResult{}
                                  : PropertyReadResult{PropertyReadState::Value, found->second, {}};
}

EditorResult<DomainOperation> AudioSourceTarget::makeSet(const SelectionSnapshot& selection,
                                                          const PropertyPath& path,
                                                          const EditorValue& value,
                                                          PropertySetMode mode) const {
    if (!selectionMatches(selection))
        return audioError<DomainOperation>(EditorStatus::Rejected, "editor.audio.selection",
                                           "Selection does not belong to this audio source");
    if (mode == PropertySetMode::Reset) return makeReset(selection, path);
    if (mode != PropertySetMode::Absolute)
        return audioError<DomainOperation>(EditorStatus::Unsupported, "editor.audio.property-mode",
                                           "Audio source properties require absolute assignment");
    auto descriptor = sourceSchema().find(path);
    if (!descriptor)
        return audioError<DomainOperation>(EditorStatus::Unsupported, "editor.audio.property",
                                           "Unknown audio source property: " + path.value());
    auto valid = validatePropertyValue(*descriptor, value);
    if (!valid.ok()) return EditorResult<DomainOperation>::failure(valid.status());
    auto payload = [&](const EditorValue& assigned) {
        EditorValue::Object object;
        object["path"] = path.value();
        object["value"] = assigned;
        return EditorValue(std::move(object));
    };
    DomainOperation operation;
    operation.type = "audio.source.property.set.v1";
    operation.target = TargetId(id_);
    operation.payload = payload(value);
    operation.inverse = payload(values_.at(path.value()));
    operation.hasInverse = true;
    operation.affectedProperties.push_back(path.value());
    operation.mergeKey = "audio.source:" + id_ + ":" + path.value();
    return eve::editing::applied<DomainOperation>(std::move(operation));
}

EditorResult<DomainOperation> AudioSourceTarget::makeReset(const SelectionSnapshot& selection,
                                                            const PropertyPath& path) const {
    auto descriptor = sourceSchema().find(path);
    if (!descriptor)
        return audioError<DomainOperation>(EditorStatus::Unsupported, "editor.audio.property",
                                           "Unknown audio source property: " + path.value());
    return makeSet(selection, path, descriptor->defaultValue, PropertySetMode::Absolute);
}

PropertySchema AudioSourceTarget::sourceSchema() {
    PropertySchema result;
    result.typeId = "audio.source";
    auto asset = property("clip.asset", "editor.audio.clip", "clip", PropertyType::AssetRef, "");
    asset.assetTypeFilters = {"audio", "sound"};
    result.properties.push_back(std::move(asset));
    auto mode = property("clip.mode", "editor.audio.mode", "clip", PropertyType::Enum, "static");
    mode.enumItems = {"static", "stream"};
    result.properties.push_back(std::move(mode));
    for (const auto& [path, label, value] : {
             std::tuple{"play.autoplay", "editor.audio.autoplay", false},
             {"play.loop", "editor.audio.loop", false},
             {"spatial.relative", "editor.audio.relative", false}})
        result.properties.push_back(property(path, label, path[0] == 'p' ? "playback" : "spatial",
                                             PropertyType::Bool, value));
    auto numeric = [&](const char* path, const char* label, const char* category,
                       double value, double minimum, double maximum) {
        auto descriptor = property(path, label, category, PropertyType::Float, value);
        descriptor.numeric.minimum = minimum;
        descriptor.numeric.maximum = maximum;
        descriptor.numeric.step = 0.01;
        result.properties.push_back(std::move(descriptor));
    };
    numeric("play.volume", "editor.audio.volume", "playback", 1.0, 0.0, 16.0);
    numeric("play.pitch", "editor.audio.pitch", "playback", 1.0, 0.01, 16.0);
    numeric("play.loop-start", "editor.audio.loop-start", "playback", 0.0, 0.0, 86400.0);
    numeric("play.loop-end", "editor.audio.loop-end", "playback", 0.0, 0.0, 86400.0);
    numeric("spatial.reference-distance", "editor.audio.reference-distance", "spatial", 1.0, 0.0, 1000000.0);
    numeric("spatial.maximum-distance", "editor.audio.maximum-distance", "spatial", 10000.0, 0.0, 1000000.0);
    for (const auto& [path, label, value] : {
             std::tuple{"spatial.position", "editor.audio.position", EditorValue::Array{0.0, 0.0, 0.0}},
             {"spatial.velocity", "editor.audio.velocity", EditorValue::Array{0.0, 0.0, 0.0}},
             {"spatial.direction", "editor.audio.direction", EditorValue::Array{0.0, 0.0, -1.0}}})
        result.properties.push_back(property(path, label, "spatial", PropertyType::Vec3, value));
    result.properties.push_back(property("mixer.bus", "editor.audio.bus", "routing",
                                         PropertyType::ObjectRef, "master"));
    return result;
}

std::map<std::string, EditorValue> AudioSourceTarget::defaults() {
    std::map<std::string, EditorValue> result;
    for (const PropertyDescriptor& descriptor : sourceSchema().properties)
        result.emplace(descriptor.path.value(), descriptor.defaultValue);
    return result;
}

bool AudioSourceTarget::selectionMatches(const SelectionSnapshot& selection) const {
    return selection.items.size() == 1 && selection.items.front().target == TargetId(id_);
}

}  // namespace eve::audio_editing
