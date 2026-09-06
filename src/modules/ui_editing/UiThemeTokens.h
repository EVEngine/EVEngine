#pragma once

#include "ui_editing/UiTheme.h"

namespace eve::ui_editing {

EditorValue                     themeTokensValue(const ui::Theme& theme);
EditorResult<ui::Theme>         parseThemeTokens(const EditorValue& value);
[[nodiscard]] EditorResult<void> assignThemeToken(ui::Theme& theme, const PropertyPath& path, const EditorValue& value);
EditorValue                     readThemeToken(const ui::Theme& theme, const PropertyPath& path);
PropertySchema                  themeTokenSchema();
std::vector<EditorDiagnostic>   validateThemeTokens(const ui::Theme& theme);
ui::Theme                       themeFromPreset(UiThemeBasePreset preset);
std::string                     presetName(UiThemeBasePreset preset);
EditorResult<UiThemeBasePreset> parsePreset(const std::string& name);

}  // namespace eve::ui_editing
