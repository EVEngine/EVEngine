#include "editor/presentation/WorkspacePresentation.h"

namespace eve::editor {
namespace {
Result<void> failure(EditorStatus status, std::string message) {
    return editing::failed<void>(status, RuleId("editor.presentation"), std::move(message));
}
}  // namespace

Result<void> WorkspacePresentation::stage(IWorkspacePresentationSource& source, PropertyPath path, EditorValue value) {
    if (draft_ && draft_->path != path)
        return failure(EditorStatus::Conflict, "Finish or cancel the active field before editing another field");
    const auto state = source.state();
    if (draft_ && (draft_->start.revision != state.revision || draft_->start.context != state.context)) {
        cancel();
        return failure(EditorStatus::Conflict, "Document or selection changed during editing");
    }
    const auto descriptor = source.schema().find(path);
    if (!descriptor) return failure(EditorStatus::NotFound, "Property is unavailable: " + path.value());
    if (hasPropertyFlag(descriptor->flags, PropertyFlag::ReadOnly))
        return failure(EditorStatus::Rejected, "Property is read-only: " + path.value());
    auto valid = validatePropertyValue(*descriptor, value);
    if (!valid.ok()) {
        cancel();
        return valid;
    }
    if (descriptor->type == PropertyType::Float)
        if (const auto* integer = value.getIf<std::int64_t>()) value = static_cast<double>(*integer);
    const auto current = source.read(path);
    if (current.state != PropertyReadState::Value)
        return failure(EditorStatus::Rejected, "Property has no single editable value: " + path.value());
    if (!draft_)
        draft_ = Draft{std::move(path), std::move(value), current.value, state};
    else
        draft_->value = std::move(value);
    return editing::applied<void>();
}

Result<void> WorkspacePresentation::commit(IWorkspacePresentationSource& source) {
    if (!draft_) return editing::noOp();
    Draft draft = std::move(*draft_);
    cancel();
    const auto state = source.state();
    if (draft.start.revision != state.revision || draft.start.context != state.context)
        return failure(EditorStatus::Conflict, "Document or selection changed during editing");
    if (draft.value == draft.original) return editing::noOp();
    const auto descriptor = source.schema().find(draft.path);
    if (!descriptor || hasPropertyFlag(descriptor->flags, PropertyFlag::ReadOnly))
        return failure(EditorStatus::Rejected, "Property became unavailable during editing");
    auto valid = validatePropertyValue(*descriptor, draft.value);
    if (!valid.ok()) return valid;
    return source.write(draft.path, draft.value);
}

void WorkspacePresentation::cancel() noexcept { draft_.reset(); }

Result<void> WorkspacePresentation::edit(IWorkspacePresentationSource& source, PropertyPath path, EditorValue value) {
    auto staged = stage(source, std::move(path), std::move(value));
    if (!staged.ok()) return staged;
    return commit(source);
}

Result<void> WorkspacePresentation::action(IWorkspacePresentationSource& source, std::string_view id) {
    cancel();
    const auto state = source.state();
    if ((id == "undo" && !state.canUndo) || (id == "redo" && !state.canRedo))
        return failure(EditorStatus::Rejected, "History action is unavailable");
    return source.action(id);
}

Value WorkspacePresentation::snapshot(const IWorkspacePresentationSource& source) const {
    const auto   state = source.state();
    Value::Array rows;
    for (const auto& descriptor : source.schema().properties) {
        const auto  current = source.read(descriptor.path);
        std::string kind;
        switch (descriptor.type) {
            case PropertyType::Bool: kind = "bool"; break;
            case PropertyType::Float:
            case PropertyType::Int: kind = "number"; break;
            case PropertyType::Enum: kind = "enum"; break;
            default: kind = "text"; break;
        }
        Value::Array choices;
        for (const auto& choice : descriptor.enumItems) choices.emplace_back(choice);
        const bool editable = !hasPropertyFlag(descriptor.flags, PropertyFlag::ReadOnly) &&
                              current.state == PropertyReadState::Value &&
                              (kind != "text" || descriptor.type == PropertyType::String ||
                               descriptor.type == PropertyType::AssetRef || descriptor.type == PropertyType::ObjectRef);
        EditorValue value = current.value;
        if (draft_ && draft_->path == descriptor.path) value = draft_->value;
        auto        projected = toPresentationValue(value);
        std::string display;
        if (current.state == PropertyReadState::Mixed)
            display = "Mixed values";
        else if (current.state != PropertyReadState::Value)
            display = "Unavailable";
        else if (const auto* text = projected.getIf<std::string>())
            display = *text;
        else {
            auto encoded = projected.toJson();
            display      = encoded.ok() ? encoded.value() : "Unavailable";
        }
        rows.emplace_back(Value::Object{{"path", descriptor.path.value()},
                                        {"label", descriptor.displayNameKey},
                                        {"kind", kind},
                                        {"value", std::move(projected)},
                                        {"display", std::move(display)},
                                        {"editable", editable},
                                        {"minimum", descriptor.numeric.minimum.value_or(-1000000.0)},
                                        {"maximum", descriptor.numeric.maximum.value_or(1000000.0)},
                                        {"choices", std::move(choices)}});
    }
    return Value::Object{{"revision", static_cast<std::int64_t>(state.revision)},
                         {"context", state.context},
                         {"canUndo", state.canUndo},
                         {"canRedo", state.canRedo},
                         {"playing", state.playing},
                         {"rows", std::move(rows)}};
}
}  // namespace eve::editor
