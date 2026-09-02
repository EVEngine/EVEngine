#include "audio_editing/AudioTarget.h"

#include "audio/Source.h"

#include <utility>

namespace eve::audio_editing {
namespace {

const EditorValue::Object* properties(const AudioSourceTarget& target, EditorValue& snapshot) {
    snapshot = target.snapshotValue();
    const auto* root = snapshot.getIf<EditorValue::Object>();
    if (!root) return nullptr;
    const auto found = root->find("properties");
    return found == root->end() ? nullptr : found->second.getIf<EditorValue::Object>();
}

template <class T>
const T* value(const EditorValue::Object& properties, const char* path) {
    const auto found = properties.find(path);
    return found == properties.end() ? nullptr : found->second.getIf<T>();
}

}  // namespace

EditorResult<void> AudioSourceRuntimeApplier::apply(const AudioSourceTarget& target,
                                                    audio::Source* source) const {
    if (!source)
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.audio.runtime-source"),
                                         "Live audio source is required");
    const auto diagnostics = target.validate();
    for (const EditorDiagnostic& diagnostic : diagnostics)
        if (diagnostic.severity() == DiagnosticSeverity::Error)
            return EditorResult<void>::failure(eve::Status(EditorStatus::Rejected, diagnostics));
    EditorValue snapshot;
    const auto* values = properties(target, snapshot);
    if (!values)
        return eve::editing::failed<void>(EditorStatus::Failed, RuleId("editor.audio.runtime-properties"),
                                         "Audio source properties are unavailable");
    source->setVolume(static_cast<float>(*value<double>(*values, "play.volume")));
    source->setPitch(static_cast<float>(*value<double>(*values, "play.pitch")));
    source->setLooping(*value<bool>(*values, "play.loop"));
    const auto& position = *value<EditorValue::Array>(*values, "spatial.position");
    const auto& velocity = *value<EditorValue::Array>(*values, "spatial.velocity");
    const auto& direction = *value<EditorValue::Array>(*values, "spatial.direction");
    source->setPosition(static_cast<float>(*position[0].getIf<double>()),
                        static_cast<float>(*position[1].getIf<double>()),
                        static_cast<float>(*position[2].getIf<double>()));
    source->setVelocity(static_cast<float>(*velocity[0].getIf<double>()),
                        static_cast<float>(*velocity[1].getIf<double>()),
                        static_cast<float>(*velocity[2].getIf<double>()));
    source->setDirection(static_cast<float>(*direction[0].getIf<double>()),
                         static_cast<float>(*direction[1].getIf<double>()),
                         static_cast<float>(*direction[2].getIf<double>()));
    source->setRelative(*value<bool>(*values, "spatial.relative"));
    source->setAttenuationDistances(
        static_cast<float>(*value<double>(*values, "spatial.reference-distance")),
        static_cast<float>(*value<double>(*values, "spatial.maximum-distance")));
    return eve::editing::applied<void>();
}

EditorResult<void> AudioSourceRuntimeSink::publish(const AudioSourceTarget& candidate) {
    return AudioSourceRuntimeApplier().apply(candidate, source_);
}

}  // namespace eve::audio_editing
