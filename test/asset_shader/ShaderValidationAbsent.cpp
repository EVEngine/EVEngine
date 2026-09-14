#include "asset/ShaderAsset.h"
#include "asset/graphics/ShaderAssetValidation.h"
int main() {
    auto result = eve::asset_graphics::validateShaderAssetGpu({});
    return !result && result.status().code() == eve::StatusCode::Unsupported ? 0 : 1;
}
