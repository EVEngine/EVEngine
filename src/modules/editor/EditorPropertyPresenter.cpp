#include "editor/EditorPropertyPresenter.h"

namespace eve::editor {
namespace {

EditorValue selectionValue(const SelectionSnapshot& selection) {
    EditorValue::Array items;
    items.reserve(selection.items.size());
    for (const SelectionItem& item : selection.items) items.emplace_back(item.item.value());
    return EditorValue(std::move(items));
}

EditorResult<PropertyEditIntent> makeIntent(const PropertySchema& schema, const SelectionSnapshot& selection,
                                            const PropertyPath& path, const EditorValue& value, PropertySetMode mode,
                                            bool runtime) {
    auto descriptor = schema.find(path);
    if (!descriptor)
        return eve::editing::failed<PropertyEditIntent>(EditorStatus::NotFound, RuleId("editor.property.not-found"),
                                                       "Property is not present in the schema: " + path.value());
    if (runtime && (!hasPropertyFlag(descriptor->flags, PropertyFlag::Runtime) ||
                    hasPropertyFlag(descriptor->flags, PropertyFlag::EditorOnly)))
        return eve::editing::failed<PropertyEditIntent>(EditorStatus::Rejected, RuleId("editor.property.runtime-hidden"),
                                                       "Property is unavailable in runtime editing");
    EditorResult<void> validation = validatePropertyValue(*descriptor, value);
    if (!validation.ok()) return EditorResult<PropertyEditIntent>::failure(validation.status());

    EditorValue::Object payload;
    payload["path"]      = path.value();
    payload["value"]     = value;
    payload["selection"] = selectionValue(selection);
    switch (mode) {
        case PropertySetMode::Absolute: payload["mode"] = "absolute"; break;
        case PropertySetMode::Relative: payload["mode"] = "relative"; break;
        case PropertySetMode::Reset: payload["mode"] = "reset"; break;
    }
    return eve::editing::applied<PropertyEditIntent>(
        PropertyEditIntent{CommandId("editor.property.set"), EditorValue(std::move(payload))});
}

}  // namespace

PropertyPresentation DeveloperPropertyPresenter::present(const PropertySchema&    schema,
                                                         const SelectionSnapshot& selection,
                                                         const IPropertyProvider& provider) const {
    PropertyPresentation presentation;
    presentation.typeId        = schema.typeId;
    presentation.schemaVersion = schema.version;
    presentation.rows.reserve(schema.properties.size());
    for (const PropertyDescriptor& descriptor : schema.properties)
        presentation.rows.push_back({descriptor, provider.read(selection, descriptor.path)});
    return presentation;
}

EditorResult<PropertyEditIntent> DeveloperPropertyPresenter::editIntent(const PropertySchema&    schema,
                                                                        const SelectionSnapshot& selection,
                                                                        const PropertyPath&      path,
                                                                        const EditorValue&       value,
                                                                        PropertySetMode          mode) const {
    return makeIntent(schema, selection, path, value, mode, false);
}

PropertyPresentation RuntimePropertyPresenter::present(const PropertySchema& schema, const SelectionSnapshot& selection,
                                                       const IPropertyProvider& provider,
                                                       const HostProfile&       profile) const {
    PropertyPresentation presentation;
    presentation.typeId        = schema.typeId;
    presentation.schemaVersion = schema.version;
    if (!profile.hasFeatures(HostFeature::RuntimeWorld)) return presentation;
    for (const PropertyDescriptor& descriptor : schema.properties) {
        if (!hasPropertyFlag(descriptor.flags, PropertyFlag::Runtime) ||
            hasPropertyFlag(descriptor.flags, PropertyFlag::EditorOnly))
            continue;
        presentation.rows.push_back({descriptor, provider.read(selection, descriptor.path)});
    }
    return presentation;
}

EditorResult<PropertyEditIntent> RuntimePropertyPresenter::editIntent(
    const PropertySchema& schema, const SelectionSnapshot& selection, const PropertyPath& path,
    const EditorValue& value, const HostProfile& profile, PropertySetMode mode) const {
    if (!profile.hasFeatures(HostFeature::RuntimeWorld))
        return eve::editing::failed<PropertyEditIntent>(EditorStatus::Rejected,
                                                       RuleId("editor.property.runtime-feature-denied"),
                                                       "Host profile does not expose runtime world editing");
    return makeIntent(schema, selection, path, value, mode, true);
}

}  // namespace eve::editor
