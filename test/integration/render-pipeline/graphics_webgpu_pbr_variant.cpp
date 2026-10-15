#include "common/config.h"

#ifdef EVENGINE_WEBGPU

#include <cstdint>
#include <iostream>

#include "GraphicsParitySupport.h"
#include "graphics/PbrSurface.h"
#include "graphics/webgpu/Graphics.h"
#include "graphics/webgpu/PbrVariantPlan.h"
#include "graphics/webgpu/PbrVariantSource.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::graphics;
using namespace eve::graphics::webgpu;

namespace {
[[maybe_unused]] Texture* marker(std::uintptr_t value) { return reinterpret_cast<Texture*>(value); }

}


TEST_CASE("graphics.webgpu.pbrVariantSpecializationCoalescesEquivalentSamplers") {
    PbrSurface surface;
    surface.textures[std::size_t(PbrTextureSlot::BaseColor)].texture = marker(1);
    surface.textures[std::size_t(PbrTextureSlot::Normal)].texture    = marker(2);
    surface.vegetationDetail.value                                  = 1.f;
    surface.vegetationDetail.textures[0].texture                     = marker(3);
    surface.vegetationDetail.textures[1].texture                     = marker(4);
    auto plan = planPbrVariant(surface, false, false);
    REQUIRE(plan.ok());
    CHECK_EQ(plan.value().fragmentTextures, 4u);
    CHECK_EQ(plan.value().fragmentSamplers, 1u);
    auto specialized = specializePbrVariantSources(plan.value());
    CHECK(specialized.fragment.find("var map0_sampler") != std::string::npos);
    CHECK(specialized.fragment.find("var map2_sampler") == std::string::npos);
    CHECK(specialized.fragment.find("var detailMap0_sampler") == std::string::npos);
    CHECK(specialized.fragment.find("var detailMap1_sampler") == std::string::npos);

    using eve::graphics::parity_test::headlessGraphics;
    auto* base = headlessGraphics();
    auto* gfx  = dynamic_cast<eve::graphics::webgpu::Graphics*>(base);
    REQUIRE(gfx != nullptr);
    PbrVariantSources views{specialized.vertex, specialized.fragment};
    const auto compiled = gfx->debugValidatePbrVariantSources(views);
    if (!compiled.ok()) std::cerr << compiled.error()->message() << '\n';
    REQUIRE(compiled.ok());
    const auto linked = gfx->debugValidatePbrVariantPipeline(views);
    if (!linked.ok()) std::cerr << linked.error()->message() << '\n';
    REQUIRE(linked.ok());
}

TEST_CASE("graphics.webgpu.pbrVariantBaseSourcesCompileOnDawn") {
    using eve::graphics::parity_test::headlessGraphics;
    auto* base = headlessGraphics();
    auto* gfx  = dynamic_cast<eve::graphics::webgpu::Graphics*>(base);
    REQUIRE(gfx != nullptr);
    const auto compiled = gfx->debugValidatePbrVariantBaseSources();
    REQUIRE(compiled.ok());
}

TEST_CASE("graphics.webgpu.pbrVariantSpecializationRemovesUnreachableBindings") {
    PbrSurface surface;
    surface.textures[std::size_t(PbrTextureSlot::BaseColor)].texture = marker(1);
    auto plan = planPbrVariant(surface, false, false);
    REQUIRE(plan.ok());
    auto specialized = specializePbrVariantSources(plan.value());
    CHECK(specialized.fragment.find("var map0_image") != std::string::npos);
    CHECK(specialized.fragment.find("var map1_image") == std::string::npos);
    CHECK(specialized.fragment.find("map1_image") == std::string::npos);
    CHECK(specialized.fragment.find("environmentMap_image") == std::string::npos);
    CHECK(specialized.fragment.find("shadowMap_image") == std::string::npos);
    CHECK(specialized.vertex.find("vertexField_image") == std::string::npos);

    using eve::graphics::parity_test::headlessGraphics;
    auto* base = headlessGraphics();
    auto* gfx  = dynamic_cast<eve::graphics::webgpu::Graphics*>(base);
    REQUIRE(gfx != nullptr);
    PbrVariantSources views{specialized.vertex, specialized.fragment};
    const auto compiled = gfx->debugValidatePbrVariantSources(views);
    if (!compiled.ok()) std::cerr << compiled.error()->message() << '\n';
    REQUIRE(compiled.ok());
}

#endif
