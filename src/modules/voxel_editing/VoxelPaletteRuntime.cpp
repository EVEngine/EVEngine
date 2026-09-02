#include <utility>
#include "voxel/CubeTypeRegistry.h"
#include "voxel_editing/VoxelPaletteTarget.h"
namespace eve::voxel_editing {
namespace {
template <class T>
EditorResult<T> fail(EditorStatus s, const char* r, std::string m) {
    return eve::editing::failed<T>(s, RuleId(r), std::move(m));
}
}  // namespace
VoxelPaletteRuntime::VoxelPaletteRuntime()  = default;
VoxelPaletteRuntime::~VoxelPaletteRuntime() = default;
EditorResult<std::vector<VoxelPalettePublishedEntry>> VoxelPaletteRuntime::publish(
    const VoxelPaletteTarget& document) {
    const auto diagnostics = document.validate();
    for (const auto& d : diagnostics)
        if (d.severity() == DiagnosticSeverity::Error)
            return EditorResult<std::vector<VoxelPalettePublishedEntry>>::failure(
                eve::Status(EditorStatus::Rejected, diagnostics));
    auto                                    candidate = std::make_unique<voxel::CubeTypeRegistry>();
    std::vector<VoxelPalettePublishedEntry> published;
    for (const auto& entry : document.entries()) {
        const int base = candidate->add(entry.type);
        if (base == 0)
            return fail<std::vector<VoxelPalettePublishedEntry>>(EditorStatus::Failed, "editor.voxel-palette.publish",
                                                                 "Voxel registry rejected a validated cube type");
        published.push_back({entry.id, entry.type.name, base, entry.type.directional ? 4 : 1});
    }
    registry_          = std::move(candidate);
    revision_          = document.revision();
    return eve::editing::applied<std::vector<VoxelPalettePublishedEntry>>(std::move(published), diagnostics);
}
}  // namespace eve::voxel_editing
