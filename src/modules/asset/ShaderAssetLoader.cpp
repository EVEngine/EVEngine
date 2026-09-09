#include "asset/EvpackResourceReader.h"
#include "asset/RuntimeDefinition.h"
#include "asset/ShaderAsset.h"

namespace eve::asset {
Result<ShaderAsset> loadShaderAsset(const EvpackResourceReader& reader, const AssetRef& asset,
                                    const EvpackCapabilities& capabilities) {
    if (capabilities.graphics != "vulkan")
        return Result<ShaderAsset>::failure(
            Diagnostic::error(DiagnosticCode::Unsupported, "eve.shader/1 requires Vulkan", {}, {}, "asset.shader"));
    auto payload = reader.read(asset, "eve.shader/1", capabilities, 8 * 1024 * 1024);
    if (!payload) return Result<ShaderAsset>::failure(payload.status());
    if (payload.value().chunks.size() != 1 || payload.value().chunks.front().kind != EvpackChunkKind::Definition)
        return Result<ShaderAsset>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                              "eve.shader/1 requires one self-contained definition", {},
                                                              {}, "asset.shader"));
    RuntimeDefinitionLimits limits;
    limits.maximumBytes  = 8 * 1024 * 1024;
    limits.maximumValues = 600000;
    auto definition      = decodeRuntimeDefinition(payload.value().chunks.front().bytes, limits);
    if (!definition) return Result<ShaderAsset>::failure(definition.status());
    return decodeShaderAsset(definition.value());
}
}  // namespace eve::asset
