#pragma once

#include "editing/EditorPackage.h"

namespace eve::action_editor {

/** @brief Build the Action editor package snapshot (panels, commands, target types). */
[[nodiscard]] editing::EditorPackage makeActionEditorPackage();

/** @brief Capability provider advertising makeActionEditorPackage(). */
class ActionEditorPackageProvider final : public editing::IEditorPackageProvider {
public:
    ActionEditorPackageProvider();

    [[nodiscard]] std::string_view              ownerModule() const override;
    [[nodiscard]] const editing::EditorPackage& package() const override;

private:
    editing::EditorPackage package_;
};

}  // namespace eve::action_editor
