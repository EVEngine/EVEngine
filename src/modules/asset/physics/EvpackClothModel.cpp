#include "asset/physics/EvpackClothModel.h"

#include "asset/RuntimeDefinition.h"

namespace eve::asset_physics {
namespace {

}  // namespace

Result<LoadedClothModel> EvpackClothModelLoader::load(const AssetRef&                  model,
                                                      const asset::EvpackCapabilities& capabilities,
                                                      std::uint64_t                    maximumDecodedBytes) const {
    auto payload = reader_.read(model, "eve.cloth-model/1", capabilities, maximumDecodedBytes);
    if (!payload) return Result<LoadedClothModel>::failure(payload.status());
    if (payload.value().chunks.size() != 1 || payload.value().chunks.front().kind != asset::EvpackChunkKind::Definition)
        return Result<LoadedClothModel>::failure(
            Diagnostic::error(DiagnosticCode::ParseError, "eve.cloth-model/1 must contain exactly one definition chunk",
                              {}, {}, "asset.physics.cloth-model"));

    asset::RuntimeDefinitionLimits limits;
    limits.maximumBytes = maximumDecodedBytes;
    auto definition     = asset::decodeRuntimeDefinition(payload.value().chunks.front().bytes, limits);
    if (!definition) return Result<LoadedClothModel>::failure(definition.status());
    auto decoded = physics::ClothModel::fromValue(definition.value());
    if (!decoded) return Result<LoadedClothModel>::failure(decoded.status());

    return Result<LoadedClothModel>::success(
        {model, std::move(decoded).takeValue(), std::move(payload).takeValue().variant});
}

}  // namespace eve::asset_physics
