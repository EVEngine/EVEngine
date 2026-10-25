#include "animation/editor/AnimationClipPresentation.h"

#include <bit>

namespace eve::animation_editor {
editor::WorkspacePresentationState AnimationClipPresentation::state() const {
    const auto targetId = source_.target().targetId().value();
    return {source_.revision(),
            std::to_string(targetId.size()) + ":" + targetId + ":" + source_.selectedBone() + ":" +
                source_.selectedKeyId().value() + ":" +
                std::to_string(std::bit_cast<std::uint64_t>(source_.playhead())),
            source_.canUndo(), source_.canRedo(), source_.isPlaying()};
}
editor::PropertySchema AnimationClipPresentation::schema() const {
    editor::PropertySchema schema;
    schema.typeId = "animation.clip.presentation";
    auto number   = [&](std::string path, std::string label, double minimum, double maximum) {
        editor::PropertyDescriptor field;
        field.path            = editor::PropertyPath(std::move(path));
        field.displayNameKey  = std::move(label);
        field.type            = editor::PropertyType::Float;
        field.numeric.minimum = minimum;
        field.numeric.maximum = maximum;
        schema.properties.push_back(std::move(field));
    };
    number("duration", "Duration", 0.25, 8.0);
    number("sample-rate", "Sample rate", 1.0, 120.0);
    editor::PropertyDescriptor loop;
    loop.path           = editor::PropertyPath("loop");
    loop.displayNameKey = "Loop";
    loop.type           = editor::PropertyType::Bool;
    schema.properties.push_back(std::move(loop));
    number("mask", "Mask", 0.0, 1.0);
    number("key-time", "Key time", 0.0, source_.duration());
    if (!source_.hasSelectedKey()) schema.properties.back().flags = editor::PropertyFlag::ReadOnly;
    for (const auto& group : {std::string("position"), std::string("rotation"), std::string("scale")})
        for (const char axis : std::string("xyz")) {
            const double low  = group == "rotation" ? -180.0 : group == "scale" ? 0.05 : -4.0;
            const double high = group == "rotation" ? 180.0 : group == "scale" ? 3.0 : 4.0;
            number(group + "-" + axis, group + " " + axis, low, high);
        }
    return schema;
}
editor::PropertyReadResult AnimationClipPresentation::read(const editor::PropertyPath& path) const {
    const auto&         p = path.value();
    editor::EditorValue value;
    if (p == "duration")
        value = source_.duration();
    else if (p == "sample-rate")
        value = source_.sampleRate();
    else if (p == "loop")
        value = source_.isLooping();
    else if (p == "mask")
        value = source_.selectedMaskWeight();
    else if (p == "key-time")
        value = source_.selectedKeyTime();
    else if (p == "position-x")
        value = source_.selectedPositionX();
    else if (p == "position-y")
        value = source_.selectedPositionY();
    else if (p == "position-z")
        value = source_.selectedPositionZ();
    else if (p == "rotation-x")
        value = source_.selectedRotationX();
    else if (p == "rotation-y")
        value = source_.selectedRotationY();
    else if (p == "rotation-z")
        value = source_.selectedRotationZ();
    else if (p == "scale-x")
        value = source_.selectedScaleX();
    else if (p == "scale-y")
        value = source_.selectedScaleY();
    else if (p == "scale-z")
        value = source_.selectedScaleZ();
    else
        return {};
    return {editor::PropertyReadState::Value, std::move(value), {}};
}
editor::Result<void> AnimationClipPresentation::write(const editor::PropertyPath& path,
                                                      const editor::EditorValue&  value) {
    const auto descriptor = schema().find(path);
    if (!descriptor || editor::hasPropertyFlag(descriptor->flags, editor::PropertyFlag::ReadOnly))
        return editing::failed<void>(editor::EditorStatus::Rejected, editor::RuleId("editor.animation.property"),
                                     "Property is unavailable");
    auto valid = editor::validatePropertyValue(*descriptor, value);
    if (!valid.ok()) return valid;
    const auto& p = path.value();
    if (p == "loop") return source_.setLoop(*value.getIf<bool>());
    const double v = value.getIf<double>() ? *value.getIf<double>() : static_cast<double>(*value.getIf<std::int64_t>());
    if (p == "duration") return source_.setDuration(v);
    if (p == "sample-rate") return source_.setSampleRate(v);
    if (p == "mask") return source_.setMaskWeight(v);
    if (p == "key-time") return source_.moveSelectedKey(v);
    if (p.starts_with("position-"))
        return source_.setSelectedPosition(p == "position-x" ? v : source_.selectedPositionX(),
                                           p == "position-y" ? v : source_.selectedPositionY(),
                                           p == "position-z" ? v : source_.selectedPositionZ());
    if (p.starts_with("rotation-"))
        return source_.setSelectedRotation(p == "rotation-x" ? v : source_.selectedRotationX(),
                                           p == "rotation-y" ? v : source_.selectedRotationY(),
                                           p == "rotation-z" ? v : source_.selectedRotationZ());
    if (p.starts_with("scale-"))
        return source_.setSelectedScale(p == "scale-x" ? v : source_.selectedScaleX(),
                                        p == "scale-y" ? v : source_.selectedScaleY(),
                                        p == "scale-z" ? v : source_.selectedScaleZ());
    return editing::failed<void>(editor::EditorStatus::NotFound, editor::RuleId("editor.animation.property"),
                                 "Unknown property");
}
editor::Result<void> AnimationClipPresentation::action(std::string_view id) {
    if (id == "undo" || id == "redo") {
        auto result = id == "undo" ? source_.undo() : source_.redo();
        if (!result.ok()) return editor::Result<void>::failure(result.status());
        return editing::applied<void>();
    }
    if (id == "set-key") return source_.keySelectedBone();
    if (id == "delete-key") return source_.deleteSelectedKey();
    if (id == "play-pause") {
        if (source_.isPlaying())
            source_.pause();
        else
            source_.play();
    } else if (id == "stop")
        source_.stop();
    else
        return editing::failed<void>(editor::EditorStatus::NotFound, editor::RuleId("editor.animation.action"),
                                     "Unknown animation action");
    return editing::applied<void>();
}
}  // namespace eve::animation_editor
