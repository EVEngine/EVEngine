#pragma once

#include "editing/EditorPackage.h"

namespace eve::gpuagents_editor {

/** @brief Incomplete GPU Agents package: factory-only until commands/panels land. */
[[nodiscard]] editing::EditorPackage makeGpuAgentsEditorPackage();

class GpuAgentsEditorPackageProvider final : public editing::IEditorPackageProvider {
public:
    GpuAgentsEditorPackageProvider();
    [[nodiscard]] std::string_view              ownerModule() const override;
    [[nodiscard]] const editing::EditorPackage& package() const override;

private:
    editing::EditorPackage package_;
};

}  // namespace eve::gpuagents_editor
