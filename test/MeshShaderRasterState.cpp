#include "graphics/MeshShaderRasterState.h"
#include <limits>
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("graphics.mesh raster state admits source overlays and rejects invalid snapshots") {
    using namespace eve::graphics;
    MeshShaderRasterState state;
    REQUIRE(validateMeshShaderRasterState(state).ok());
    state.depthCompare   = MeshDepthCompare::Always;
    state.colorWriteMask = 7;
    REQUIRE(validateMeshShaderRasterState(state).ok());
    state.depthCompare      = MeshDepthCompare::LessEqual;
    state.depthBiasConstant = -1;
    state.depthBiasSlope    = -1;
    REQUIRE(validateMeshShaderRasterState(state).ok());
    state.colorWriteMask = 16;
    REQUIRE(!validateMeshShaderRasterState(state).ok());
    state.colorWriteMask = 15;
    state.depthBiasSlope = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(!validateMeshShaderRasterState(state).ok());
    state.depthBiasSlope = 0;
    state.depthCompare   = static_cast<MeshDepthCompare>(999);
    REQUIRE(!validateMeshShaderRasterState(state).ok());
}
