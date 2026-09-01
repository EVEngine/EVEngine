#include "audio_editing/AudioTarget.h"

#include <utility>

namespace eve::audio_editing {
namespace {

template <class T>
EditorResult<T> mixerError(EditorStatus status, const char* rule, std::string message) {
    return eve::editing::failed<T>(status, RuleId(rule), std::move(message));
}

const EditorValue* field(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

}  // namespace

AudioMixerTarget::AudioMixerTarget(std::string id) : id_(std::move(id)) {
    buses_.emplace(ObjectId("master"), AudioBusSnapshot{ObjectId("master"), {}, "Master"});
}

TargetDescriptor AudioMixerTarget::describe() const {
    return {TargetId(id_), "audio-mixer", revision_, false,
            {CapabilityId("eve.editor.target.audio-mixer")}};
}

void* AudioMixerTarget::queryCapability(const CapabilityId& capability) {
    return capability == CapabilityId("eve.editor.target.audio-mixer") ? this : nullptr;
}

EditorResult<void> AudioMixerTarget::applyDomainOperation(const DomainOperation& operation) {
    if (operation.target != TargetId(id_))
        return mixerError<void>(EditorStatus::Rejected, "editor.audio.mixer-target",
                                "Mixer operation targets another document");
    auto parsed = parseBus(operation.payload);
    if (!parsed.ok())
        return mixerError<void>(EditorStatus::Rejected, "editor.audio.mixer-payload",
                                "Mixer operation contains an invalid bus");
    if (operation.type == "audio.bus.create.v1") {
        if (buses_.contains(parsed.value().id) ||
            (!parsed.value().parent.empty() && !buses_.contains(parsed.value().parent)))
            return mixerError<void>(EditorStatus::Conflict, "editor.audio.bus-create",
                                    "Mixer bus already exists or its parent is missing");
        buses_.emplace(parsed.value().id, parsed.value());
    } else if (operation.type == "audio.bus.delete.v1") {
        if (parsed.value().id == ObjectId("master") || !buses_.contains(parsed.value().id) ||
            !children(parsed.value().id).empty())
            return mixerError<void>(EditorStatus::Rejected, "editor.audio.bus-delete",
                                    "Master, missing, or non-leaf mixer bus cannot be deleted");
        buses_.erase(parsed.value().id);
    } else if (operation.type == "audio.bus.replace.v1") {
        if (!buses_.contains(parsed.value().id) || parsed.value().id == ObjectId("master") ||
            !buses_.contains(parsed.value().parent) ||
            wouldCycle(parsed.value().id, parsed.value().parent))
            return mixerError<void>(EditorStatus::Rejected, "editor.audio.bus-replace",
                                    "Mixer bus replacement produces an invalid hierarchy");
        buses_[parsed.value().id] = parsed.value();
    } else {
        return mixerError<void>(EditorStatus::Unsupported, "editor.audio.mixer-operation",
                                "Unsupported mixer operation: " + operation.type);
    }
    ++revision_;
    dirty_.include(0, 0);
    return eve::editing::applied<void>();
}

std::unique_ptr<IDomainOperationTarget> AudioMixerTarget::cloneDomainState() const {
    return std::make_unique<AudioMixerTarget>(*this);
}

EditorResult<void> AudioMixerTarget::commitDomainState(
    std::unique_ptr<IDomainOperationTarget> candidate) {
    auto* typed = dynamic_cast<AudioMixerTarget*>(candidate.get());
    if (!typed || typed->id_ != id_)
        return mixerError<void>(EditorStatus::Conflict, "editor.audio.mixer-candidate-mismatch",
                                "Mixer candidate belongs to another target");
    *this = *typed;
    return eve::editing::applied<void>();
}

EditorResult<AudioBusSnapshot> AudioMixerTarget::bus(const ObjectId& id) const {
    const auto found = buses_.find(id);
    if (found == buses_.end())
        return mixerError<AudioBusSnapshot>(EditorStatus::NotFound, "editor.audio.bus-not-found",
                                            "Mixer bus does not exist: " + id.value());
    return eve::editing::applied<AudioBusSnapshot>(found->second);
}

std::vector<ObjectId> AudioMixerTarget::children(const ObjectId& parent) const {
    std::vector<ObjectId> result;
    for (const auto& [id, bus] : buses_)
        if (bus.parent == parent) result.push_back(id);
    return result;
}

EditorResult<DomainOperation> AudioMixerTarget::makeCreate(AudioBusSnapshot bus) const {
    if (bus.id.empty() || bus.id == ObjectId("master") || bus.name.empty() ||
        bus.volume < 0.0 || buses_.contains(bus.id) || !buses_.contains(bus.parent))
        return mixerError<DomainOperation>(EditorStatus::Rejected, "editor.audio.bus-create",
                                           "Mixer bus id/name/volume/parent is invalid");
    DomainOperation operation;
    operation.type = "audio.bus.create.v1";
    operation.inverseType = "audio.bus.delete.v1";
    operation.target = TargetId(id_);
    operation.payload = busValue(bus);
    operation.inverse = operation.payload;
    operation.hasInverse = true;
    operation.affectedObjects.push_back({TargetId(id_), bus.id.value(), 0});
    return eve::editing::applied<DomainOperation>(std::move(operation));
}

EditorResult<DomainOperation> AudioMixerTarget::makeDelete(const ObjectId& id) const {
    const auto found = buses_.find(id);
    if (found == buses_.end() || id == ObjectId("master") || !children(id).empty())
        return mixerError<DomainOperation>(EditorStatus::Rejected, "editor.audio.bus-delete",
                                           "Only an existing leaf bus can be deleted");
    DomainOperation operation;
    operation.type = "audio.bus.delete.v1";
    operation.inverseType = "audio.bus.create.v1";
    operation.target = TargetId(id_);
    operation.payload = busValue(found->second);
    operation.inverse = operation.payload;
    operation.hasInverse = true;
    operation.affectedObjects.push_back({TargetId(id_), id.value(), 0});
    return eve::editing::applied<DomainOperation>(std::move(operation));
}

EditorResult<DomainOperation> AudioMixerTarget::makeReplace(AudioBusSnapshot changed) const {
    const auto found = buses_.find(changed.id);
    if (found == buses_.end() || changed.id == ObjectId("master") || changed.name.empty() ||
        changed.volume < 0.0 || !buses_.contains(changed.parent) ||
        wouldCycle(changed.id, changed.parent))
        return mixerError<DomainOperation>(EditorStatus::Rejected, "editor.audio.bus-replace",
                                           "Mixer bus settings or hierarchy are invalid");
    DomainOperation operation;
    operation.type = "audio.bus.replace.v1";
    operation.inverseType = operation.type;
    operation.target = TargetId(id_);
    operation.payload = busValue(changed);
    operation.inverse = busValue(found->second);
    operation.hasInverse = true;
    operation.affectedObjects.push_back({TargetId(id_), changed.id.value(), 0});
    operation.affectedProperties = {"bus"};
    operation.mergeKey = "audio.bus:" + changed.id.value();
    return eve::editing::applied<DomainOperation>(std::move(operation));
}

bool AudioMixerTarget::wouldCycle(const ObjectId& id, const ObjectId& parent) const {
    ObjectId ancestor = parent;
    while (!ancestor.empty()) {
        if (ancestor == id) return true;
        const auto found = buses_.find(ancestor);
        if (found == buses_.end()) return false;
        ancestor = found->second.parent;
    }
    return false;
}

EditorValue AudioMixerTarget::busValue(const AudioBusSnapshot& bus) {
    EditorValue::Object value;
    value["id"] = bus.id.value();
    value["parent"] = bus.parent.value();
    value["name"] = bus.name;
    value["volume"] = bus.volume;
    value["mute"] = bus.mute;
    value["solo"] = bus.solo;
    value["effects"] = bus.effects;
    return EditorValue(std::move(value));
}

EditorResult<AudioBusSnapshot> AudioMixerTarget::parseBus(const EditorValue& value) {
    const auto* id = field(value, "id") ? field(value, "id")->getIf<std::string>() : nullptr;
    const auto* parent = field(value, "parent") ? field(value, "parent")->getIf<std::string>() : nullptr;
    const auto* name = field(value, "name") ? field(value, "name")->getIf<std::string>() : nullptr;
    const auto* volume = field(value, "volume") ? field(value, "volume")->getIf<double>() : nullptr;
    const auto* mute = field(value, "mute") ? field(value, "mute")->getIf<bool>() : nullptr;
    const auto* solo = field(value, "solo") ? field(value, "solo")->getIf<bool>() : nullptr;
    const EditorValue* effects = field(value, "effects");
    if (!id || id->empty() || !parent || !name || name->empty() || !volume || *volume < 0.0 ||
        !mute || !solo || !effects || effects->type() != EditorValue::Type::Array)
        return mixerError<AudioBusSnapshot>(EditorStatus::Rejected, "editor.audio.bus-value",
                                            "Mixer bus value is invalid");
    return eve::editing::applied<AudioBusSnapshot>(
        {ObjectId(*id), ObjectId(*parent), *name, *volume, *mute, *solo, *effects});
}

EditorValue AudioMixerTarget::snapshotValue() const {
    EditorValue::Array buses;
    for (const auto& [id, bus] : buses_) {
        (void)id;
        buses.push_back(busValue(bus));
    }
    EditorValue::Object root;
    root["schemaVersion"] = 1;
    root["buses"] = EditorValue(std::move(buses));
    return EditorValue(std::move(root));
}

EditorResult<void> AudioMixerTarget::loadSnapshot(const EditorValue& snapshot) {
    const EditorValue* versionValue = field(snapshot, "schemaVersion");
    const EditorValue* busesValue = field(snapshot, "buses");
    const auto* version = versionValue ? versionValue->getIf<int64_t>() : nullptr;
    const auto* buses = busesValue ? busesValue->getIf<EditorValue::Array>() : nullptr;
    if (!version || *version != 1 || !buses)
        return mixerError<void>(EditorStatus::Rejected, "editor.audio.mixer-snapshot",
                                "Mixer snapshot requires schemaVersion 1 and buses");
    std::map<ObjectId, AudioBusSnapshot> candidate;
    for (const EditorValue& value : *buses) {
        auto parsed = parseBus(value);
        if (!parsed.ok() || !candidate.emplace(parsed.value().id, parsed.value()).second)
            return mixerError<void>(EditorStatus::Rejected, "editor.audio.mixer-snapshot-bus",
                                    "Mixer snapshot contains an invalid or duplicate bus");
    }
    const auto master = candidate.find(ObjectId("master"));
    if (master == candidate.end() || !master->second.parent.empty())
        return mixerError<void>(EditorStatus::Rejected, "editor.audio.mixer-master",
                                "Mixer snapshot requires one root master bus");
    for (const auto& [id, bus] : candidate) {
        if (id == ObjectId("master")) continue;
        if (!candidate.contains(bus.parent))
            return mixerError<void>(EditorStatus::Rejected, "editor.audio.mixer-parent",
                                    "Mixer snapshot contains a missing parent bus");
        ObjectId ancestor = bus.parent;
        while (ancestor != ObjectId("master")) {
            if (ancestor == id)
                return mixerError<void>(EditorStatus::Rejected, "editor.audio.mixer-cycle",
                                        "Mixer snapshot contains a routing cycle");
            const auto found = candidate.find(ancestor);
            if (found == candidate.end() || found->second.parent.empty())
                return mixerError<void>(EditorStatus::Rejected, "editor.audio.mixer-root",
                                        "Every mixer bus must route to master");
            ancestor = found->second.parent;
        }
    }
    buses_ = std::move(candidate);
    ++revision_;
    dirty_.clear();
    return eve::editing::applied<void>();
}

}  // namespace eve::audio_editing
