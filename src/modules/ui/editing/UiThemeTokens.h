#pragma once

#include "ui/editing/UiTheme.h"

namespace eve::ui_editing {

/** @brief Theme tokens value. */
EditorValue                     themeTokensValue(const ui::Theme& theme);
/** @brief Parse theme tokens. */
Result<ui::Theme>         parseThemeTokens(const EditorValue& value);
/** @brief Assign theme token. */
[[nodiscard]] Result<void> assignThemeToken(ui::Theme& theme, const PropertyPath& path, const EditorValue& value);
/** @brief Reads theme token. */
EditorValue                     readThemeToken(const ui::Theme& theme, const PropertyPath& path);
/** @brief Theme token schema. */
PropertySchema                  themeTokenSchema();
/** @brief Validate theme tokens. */
std::vector<EditorDiagnostic>   validateThemeTokens(const ui::Theme& theme);
/** @brief Theme from preset. */
ui::Theme                       themeFromPreset(UiThemeBasePreset preset);
/** @brief Preset name. */
std::string                     presetName(UiThemeBasePreset preset);
/** @brief Parse preset. */
Result<UiThemeBasePreset> parsePreset(const std::string& name);

}  // namespace eve::ui_editing
