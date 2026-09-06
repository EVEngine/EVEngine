#include "ui_editing/UiTheme.h"
#include "ui_editing/UiThemeTokens.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace eve::ui_editing {
namespace {

template <class T>
EditorResult<T> fail(EditorStatus status, const char* rule, std::string message) {
    return eve::editing::failed<T>(status, RuleId(rule), std::move(message));
}

}  // namespace

bool UiThemeCatalogTarget::matches(const SelectionSnapshot& selection) const {
    if (selection.items.empty()) return false;
    for (const auto& item : selection.items) {
        if (item.target != TargetId(id_) || item.type != "ui.theme" || !findTheme(ObjectId(item.item.value())))
            return false;
    }
    return true;
}

eve::Result<eve::Revision> UiThemeCatalogTarget::currentRevision(const SelectionSnapshot& selection) const {
    if (!matches(selection))
        return eve::Result<eve::Revision>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Theme selection mismatch", "editor.ui-theme.selection"));
    return eve::Result<eve::Revision>::success(eve::Revision(revision_));
}

PropertySchema UiThemeCatalogTarget::schema(const SelectionSnapshot&) const { return themeTokenSchema(); }

PropertyReadResult UiThemeCatalogTarget::read(const SelectionSnapshot& selection, const PropertyPath& path) const {
    if (!matches(selection) || !schema(selection).find(path)) return {};
    std::optional<EditorValue> common;
    for (const auto& item : selection.items) {
        const UiThemeAsset* asset = findTheme(ObjectId(item.item.value()));
        EditorValue         value = readThemeToken(asset->tokens, path);
        if (!common)
            common = value;
        else if (*common != value)
            return {PropertyReadState::Mixed, {}, {}};
    }
    return {PropertyReadState::Value, *common, {}};
}

EditorResult<DomainOperation> UiThemeCatalogTarget::makeSet(const SelectionSnapshot& selection,
                                                            const PropertyPath& path, const EditorValue& value,
                                                            PropertySetMode mode) const {
    if (mode == PropertySetMode::Reset) return makeReset(selection, path);
    auto descriptor = schema(selection).find(path);
    if (!matches(selection) || !descriptor || mode != PropertySetMode::Absolute ||
        !editing::validatePropertyValue(*descriptor, value).ok())
        return fail<DomainOperation>(EditorStatus::Rejected, "editor.ui-theme.set",
                                     "Theme property edit is invalid");
    auto candidate = *this;
    for (const auto& item : selection.items) {
        UiThemeAsset* asset = candidate.mutableTheme(ObjectId(item.item.value()));
        if (!asset)
            return fail<DomainOperation>(EditorStatus::NotFound, "editor.ui-theme.missing",
                                         "Theme asset does not exist");
        auto assigned = assignThemeToken(asset->tokens, path, value);
        if (!assigned.ok())
            return fail<DomainOperation>(EditorStatus::Rejected, "editor.ui-theme.token",
                                         "Theme token could not be assigned");
        if (asset->id.value() != "dark" && asset->id.value() != "light")
            asset->basePreset = UiThemeBasePreset::Custom;
    }
    auto diagnostics = candidate.validate();
    if (std::any_of(diagnostics.begin(), diagnostics.end(),
                    [](const EditorDiagnostic& item) { return item.severity() == DiagnosticSeverity::Error; }))
        return fail<DomainOperation>(EditorStatus::Rejected, "editor.ui-theme.invalid",
                                     "Theme property edit is out of range");
    return replacement(candidate.contentValue(), path.value());
}

EditorResult<DomainOperation> UiThemeCatalogTarget::makeReset(const SelectionSnapshot& selection,
                                                              const PropertyPath&      path) const {
    auto descriptor = schema(selection).find(path);
    if (!descriptor)
        return fail<DomainOperation>(EditorStatus::Unsupported, "editor.ui-theme.property",
                                     "Unknown theme property");
    return makeSet(selection, path, descriptor->defaultValue, PropertySetMode::Absolute);
}

}  // namespace eve::ui_editing
