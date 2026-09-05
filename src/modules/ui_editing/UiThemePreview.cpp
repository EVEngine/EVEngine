#include "ui_editing/UiTheme.h"
#include "ui_editing/UiThemeTokens.h"

#include "ui/Theme.h"

namespace eve::ui_editing {
namespace {

EditorDiagnostic previewDiagnostic(const char* rule, std::string message) {
    return eve::editing::ruleDiagnostic(eve::DiagnosticCode::PreconditionViolation, RuleId(rule),
                                        DiagnosticSeverity::Error, std::move(message));
}

}  // namespace

UiThemePreviewSnapshot UiThemePreviewService::build(const UiThemeCatalogTarget& catalog, const ObjectId& themeId,
                                                    Revision expectedRevision) const {
    UiThemePreviewSnapshot snapshot;
    snapshot.documentRevision = catalog.revision();
    snapshot.themeId          = themeId;
    if (expectedRevision != catalog.revision()) {
        snapshot.status = EditorStatus::Conflict;
        snapshot.diagnostics.push_back(
            previewDiagnostic("editor.ui-theme.stale-preview", "Theme preview requires the current catalog revision"));
        return snapshot;
    }
    auto asset = catalog.theme(themeId);
    if (!asset.ok()) {
        snapshot.status = EditorStatus::NotFound;
        snapshot.diagnostics.insert(snapshot.diagnostics.end(), asset.status().diagnostics().begin(),
                                    asset.status().diagnostics().end());
        return snapshot;
    }
    snapshot.theme       = asset.value().tokens;
    snapshot.runtimeName = catalog.runtimeName(themeId);
    snapshot.status      = EditorStatus::Applied;
    return snapshot;
}

EditorResult<void> UiThemeRuntimePublisher::publish(const UiThemeCatalogTarget& catalog) const {
    auto asset = catalog.theme(catalog.activeId());
    if (!asset.ok()) return EditorResult<void>::failure(asset.status());
    auto diagnostics = validateThemeTokens(asset.value().tokens);
    if (!diagnostics.empty())
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.ui-theme.publish"),
                                          "Active theme tokens are invalid");
    ui::setGlobalTheme(asset.value().tokens, catalog.runtimeName(catalog.activeId()));
    return eve::editing::applied<void>();
}

}  // namespace eve::ui_editing
