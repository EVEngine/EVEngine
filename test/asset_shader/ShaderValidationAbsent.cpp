// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "role": "compile-contract",
  "default": "module-composition",
  "module": "asset_graphics"
}
*/
// clang-format on

#include "asset/ShaderAsset.h"
#include "asset/graphics/ShaderAssetValidation.h"
int main() {
    auto result = eve::asset_graphics::validateShaderAssetGpu({});
    return !result && result.status().code() == eve::StatusCode::Unsupported ? 0 : 1;
}
