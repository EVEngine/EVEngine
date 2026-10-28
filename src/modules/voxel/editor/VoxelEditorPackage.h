#pragma once

#include "editing/EditorPackage.h"

namespace eve::voxel_editor {

/** @brief Build the Voxel editor package snapshot. */
[[nodiscard]] editing::EditorPackage makeVoxelEditorPackage();

class VoxelEditorPackageProvider final : public editing::IEditorPackageProvider {
public:
    VoxelEditorPackageProvider();
    [[nodiscard]] std::string_view              ownerModule() const override;
    [[nodiscard]] const editing::EditorPackage& package() const override;

private:
    editing::EditorPackage package_;
};

}  // namespace eve::voxel_editor
