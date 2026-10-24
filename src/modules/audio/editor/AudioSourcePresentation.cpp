#include "audio/editor/AudioSourcePresentation.h"

namespace eve::audio_editor {
editor::WorkspacePresentationState AudioSourcePresentation::state() const {
    return {source_.revision(), source_.target().targetId().value(), source_.canUndo(), source_.canRedo(),
            source_.isPlaying()};
}
editor::PropertySchema AudioSourcePresentation::schema() const {
    auto schema = source_.target().schema({});
    for (auto& field : schema.properties) {
        // This tone-preview host has no clip resolver or mixer routing provider.
        if (field.path.value() == "clip.asset" || field.path.value() == "clip.mode" ||
            field.path.value() == "play.autoplay" || field.path.value() == "mixer.bus")
            field.flags = field.flags | editor::PropertyFlag::ReadOnly;
        // UI labels are a projection; constraints remain the domain schema's truth.
        const auto separator = field.path.value().find_last_of('.');
        field.displayNameKey = field.path.value().substr(separator + 1);
        if (field.path.value() == "play.loop-start" || field.path.value() == "play.loop-end")
            field.numeric.maximum = source_.duration();
    }
    return schema;
}
editor::PropertyReadResult AudioSourcePresentation::read(const editor::PropertyPath& path) const {
    return source_.read(path.value());
}
editor::EditorResult<void> AudioSourcePresentation::write(const editor::PropertyPath& path,
                                                          const editor::EditorValue&  value) {
    const auto descriptor = schema().find(path);
    if (!descriptor || editor::hasPropertyFlag(descriptor->flags, editor::PropertyFlag::ReadOnly))
        return editing::failed<void>(editor::EditorStatus::Rejected, editor::RuleId("editor.audio.property"),
                                     "Property is unavailable in this preview host");
    return source_.setProperty(path.value(), value);
}
editor::EditorResult<void> AudioSourcePresentation::action(std::string_view id) {
    if (id == "undo" || id == "redo") {
        auto result = id == "undo" ? source_.undo() : source_.redo();
        if (!result.ok()) return editor::EditorResult<void>::failure(result.status());
        return editing::applied<void>();
    }
    if (id == "play-pause") return source_.isPlaying() ? source_.pause() : source_.play();
    if (id == "stop") return source_.stop();
    return editing::failed<void>(editor::EditorStatus::NotFound, editor::RuleId("editor.audio.action"),
                                 "Unknown audio action");
}
}  // namespace eve::audio_editor
