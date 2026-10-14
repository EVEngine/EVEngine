#pragma once

#include "editing/EditorPackage.h"

namespace eve::fluids_editor {

/** @brief Incomplete fluids package: factory-only until commands/panels land. */
[[nodiscard]] editing::EditorPackage makeFluidsEditorPackage();

class FluidsEditorPackageProvider final : public editing::IEditorPackageProvider {
public:
    FluidsEditorPackageProvider();
    [[nodiscard]] std::string_view              ownerModule() const override;
    [[nodiscard]] const editing::EditorPackage& package() const override;

private:
    editing::EditorPackage package_;
};

}  // namespace eve::fluids_editor
