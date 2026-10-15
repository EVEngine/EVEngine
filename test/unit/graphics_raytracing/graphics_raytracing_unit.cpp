#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Capability.h"
#include "common/Diagnostic.h"
#include "common/Status.h"
#include "graphics/Graphics.h"
#include "graphics/IRayTracing.h"
#include "graphics/RayTracingCaps.h"
#include "graphics/RenderControl.h"
#include "graphics/raytracing/RayTracingModule.h"

#include <glm/gtc/matrix_transform.hpp>
#include <vector>

using namespace eve::graphics;


TEST_CASE("graphics.raytracing.renderControlFeature") {
    RenderControl rc;
    CHECK(rc.supports("rtx"));
    CHECK(!rc.isEnabled("rtx"));
    rc.enable("rtx");
    CHECK(rc.isEnabled("rtx"));
    CHECK(rc.isEnabled("gbuffer"));  // rtx implies gbuffer
    rc.disable("rtx");
    CHECK(!rc.isEnabled("rtx"));
}
