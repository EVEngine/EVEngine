#include "audio_editing/AudioTarget.h"

#include <cmath>
#include <utility>

namespace eve::audio_editing {
namespace {

template <class T>
EditorResult<T> snapshotError(EditorStatus status, const char* rule, std::string message) {
    return EditorResult<T>::error(status, RuleId(rule), std::move(message));
}

const EditorValue* field(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

}  // namespace

EditorValue AudioSourceTarget::snapshotValue() const {
    EditorValue::Object properties;
    for (const auto& [path, value] : values_) properties[path] = value;
    EditorValue::Object root;
    root["schemaVersion"] = 1;
    root["properties"] = EditorValue(std::move(properties));
    return EditorValue(std::move(root));
}

EditorResult<void> AudioSourceTarget::loadSnapshot(const EditorValue& snapshot) {
    const EditorValue* versionValue = field(snapshot, "schemaVersion");
    const EditorValue* propertiesValue = field(snapshot, "properties");
    const auto* version = versionValue ? versionValue->getIf<int64_t>() : nullptr;
    const auto* properties = propertiesValue ? propertiesValue->getIf<EditorValue::Object>() : nullptr;
    if (!version || *version != 1 || !properties)
        return snapshotError<void>(EditorStatus::Rejected, "editor.audio.snapshot-format",
                                   "Audio source snapshot requires schemaVersion 1 and properties");
    auto candidate = defaults();
    const PropertySchema schemaValue = sourceSchema();
    for (const auto& [path, value] : *properties) {
        auto descriptor = schemaValue.find(PropertyPath(path));
        if (!descriptor)
            return snapshotError<void>(EditorStatus::Unsupported, "editor.audio.snapshot-property",
                                       "Audio source snapshot contains unknown property: " + path);
        auto valid = validatePropertyValue(*descriptor, value);
        if (!valid.isAccepted()) return valid;
        candidate[path] = value;
    }
    values_ = std::move(candidate);
    ++revision_;
    dirty_.clear();
    return EditorResult<void>::applied();
}

std::vector<EditorDiagnostic> AudioSourceTarget::validate() const {
    std::vector<EditorDiagnostic> diagnostics;
    const auto text = [&](const char* path) { return *values_.at(path).getIf<std::string>(); };
    const auto number = [&](const char* path) { return *values_.at(path).getIf<double>(); };
    if (text("clip.asset").empty())
        diagnostics.push_back({RuleId("editor.audio.clip-required"), DiagnosticSeverity::Error,
                               "Audio source requires a clip asset"});
    if (number("spatial.reference-distance") > number("spatial.maximum-distance"))
        diagnostics.push_back({RuleId("editor.audio.attenuation-range"), DiagnosticSeverity::Error,
                               "Reference distance exceeds maximum attenuation distance"});
    const bool looping = *values_.at("play.loop").getIf<bool>();
    const double loopStart = number("play.loop-start");
    const double loopEnd = number("play.loop-end");
    if (looping && loopEnd > 0.0 && loopStart >= loopEnd)
        diagnostics.push_back({RuleId("editor.audio.loop-range"), DiagnosticSeverity::Error,
                               "Loop start must be earlier than loop end"});
    const auto& direction = *values_.at("spatial.direction").getIf<EditorValue::Array>();
    double lengthSquared = 0.0;
    for (const EditorValue& component : direction) {
        const double value = *component.getIf<double>();
        lengthSquared += value * value;
    }
    if (lengthSquared < 1e-12)
        diagnostics.push_back({RuleId("editor.audio.direction"), DiagnosticSeverity::Warning,
                               "Directional audio source has a zero direction vector"});
    if (text("mixer.bus").empty())
        diagnostics.push_back({RuleId("editor.audio.bus-required"), DiagnosticSeverity::Error,
                               "Audio source requires a mixer bus route"});
    return diagnostics;
}

}  // namespace eve::audio_editing
