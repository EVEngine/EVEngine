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

TEST_CASE("graphics.raytracing.capabilityAbsentOrPresent") {
    // The optional module registers IRayTracing at static init when linked.
    // Either the provider is present (full/3d profiles) or absent (trimmed) —
    // both are supported states.
    auto* rt = eve::cap::query<IRayTracing>();
    if (!rt) {
        CHECK(true);  // trimmed build: absence is explicit and OK
        return;
    }
    // Provider linked: querying caps must not throw.
    const RayTracingCaps caps = rt->caps();
    (void)caps;
    CHECK(true);
}

TEST_CASE("graphics.raytracing.probeAndUnsupportedOps") {
    auto* gfx = Graphics::create();
    REQUIRE(gfx != nullptr);
    gfx->initHeadless(64, 64);

    const bool deviceRt = gfx->supportsRayTracing();
    CHECK_EQ(deviceRt, gfx->rayTracingCaps().rayTracingAvailable());

    auto* rt = eve::cap::query<IRayTracing>();
    if (!rt) {
        CHECK(!deviceRt);  // no provider ⇒ treat as unavailable
        return;
    }

    if (!rt->isAvailable()) {
        // Soft-fail contract: fallible ops return Unsupported, not throw.
        const float    positions[] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
        const uint32_t indices[]   = {0, 1, 2};
        auto           added       = rt->addTriangleMesh(positions, 3, indices, 3, glm::mat4(1.f));
        CHECK(!added.ok());
        CHECK_EQ(int(added.status().code()), int(eve::StatusCode::Unsupported));

        auto rebuilt = rt->rebuildScene();
        CHECK(!rebuilt.ok());
        CHECK_EQ(int(rebuilt.status().code()), int(eve::StatusCode::Unsupported));
        return;
    }

    // Hardware path: build a single triangle BLAS/TLAS.
    const float    positions[] = {-1.f, 0.f, -2.f, 1.f, 0.f, -2.f, 0.f, 1.f, -2.f};
    const uint32_t indices[]   = {0, 1, 2};
    auto           added       = rt->addTriangleMesh(positions, 3, indices, 3, glm::mat4(1.f));
    REQUIRE(added.ok());
    CHECK(added.value() == 0u);
    auto rebuilt = rt->rebuildScene();
    CHECK(rebuilt.ok());
    rt->clearScene();
}

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

TEST_CASE("graphics.raytracing.moduleScriptFacade") {
    auto* rtMod = eve::graphics::raytracing::RayTracing::create();
    REQUIRE(rtMod != nullptr);
    // Must not throw when RT is unavailable.
    (void)rtMod->isAvailable();
    (void)rtMod->caps();
}
