#pragma once

#include "ui/editing/UiTheme.h"

namespace eve::ui_editing {

EditorValue                     themeTokensValue(const ui::Theme& theme);
Result<ui::Theme>         parseThemeTokens(const EditorValue& value);
[[nodiscard]] Result<void> assignThemeToken(ui::Theme& theme, const PropertyPath& path, const EditorValue& value);
EditorValue                     readThemeToken(const ui::Theme& theme, const PropertyPath& path);
PropertySchema                  themeTokenSchema();
std::vector<EditorDiagnostic>   validateThemeTokens(const ui::Theme& theme);
ui::Theme                       themeFromPreset(UiThemeBasePreset preset);
std::string                     presetName(UiThemeBasePreset preset);
Result<UiThemeBasePreset> parsePreset(const std::string& name);

}  // namespace eve::ui_editing
