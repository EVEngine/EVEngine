#pragma once
#include "common/Result.h"
#include "common/Value.h"
#include "procgen/MeshBuild.h"
namespace eve::procgen::artifact_mesh {
// Private artifact-store codec. All arguments borrowed synchronously; no callbacks.
bool                                     validMesh(const MeshBuild& mesh) noexcept;
eve::Value                               encodeMesh(const MeshBuild& mesh);
[[nodiscard]] Result<std::vector<float>> decodeColors(const eve::Value::Object& object, std::size_t vertices);
}  // namespace eve::procgen::artifact_mesh
