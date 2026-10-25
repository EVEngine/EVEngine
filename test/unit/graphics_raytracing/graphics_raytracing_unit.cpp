#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "graphics/RenderControl.h"

namespace {
using eve::graphics::RenderControl;

// Feature contract: RTX is opt-in, recognized even without a Graphics backend,
// and enabling it requests the depth and normal GBuffer dependency.
TEST_CASE("graphics.raytracing.spec.rtx_feature_dependencies") {
    RenderControl control;
    CHECK(control.supports("rtx"));
    CHECK(!control.isEnabled("rtx"));
    control.disable("gbuffer");
    CHECK(!control.isEnabled("gbuffer"));
    control.enable("rtx");
    CHECK(control.isEnabled("rtx"));
    CHECK(control.isEnabled("gbuffer"));
    control.disable("rtx");
    CHECK(!control.isEnabled("rtx"));
    REQUIRE(control.isEnabled("gbuffer"));
}

// Dependency contract: disabling GBuffer evicts RTX because the reflection
// path cannot consume depth and normals; enabling RTX again restores it.
TEST_CASE("graphics.raytracing.spec.gbuffer_disable_evicts_rtx") {
    RenderControl control;
    control.enable("rtx");
    control.disable("gbuffer");
    CHECK(!control.isEnabled("gbuffer"));
    CHECK(!control.isEnabled("rtx"));
    control.enable("rtx");
    CHECK(control.isEnabled("gbuffer"));
    REQUIRE(control.isEnabled("rtx"));
}

// Unknown feature names have no effect on the requested RTX state.
TEST_CASE("graphics.raytracing.spec.unknown_features_do_not_mutate_rtx") {
    RenderControl control;
    CHECK(!control.supports("raytrace"));
    control.enable("raytrace");
    CHECK(!control.isEnabled("raytrace"));
    CHECK(!control.isEnabled("rtx"));
    control.enable("rtx");
    control.disable("raytrace");
    REQUIRE(control.isEnabled("rtx"));
}
}  // namespace
