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
Texture* marker(std::uintptr_t value) { return reinterpret_cast<Texture*>(value); }

}

TEST_CASE("graphics.webgpu.pbrVariantPlansOnlyReachableResources") {
    PbrSurface surface;
    auto       empty = planPbrVariant(surface, false, false);
    REQUIRE(empty.ok());
    CHECK_EQ(empty.value().fragmentTextures, 0u);
    CHECK_EQ(empty.value().vertexTextures, 0u);

    surface.textures[std::size_t(PbrTextureSlot::BaseColor)].texture = marker(1);
    surface.textures[std::size_t(PbrTextureSlot::Normal)].texture    = marker(2);
    surface.vegetationDetail.value                                  = 1.f;
    surface.vegetationDetail.textures[0].texture                     = marker(3);
    surface.vegetationDetail.textures[1].texture                     = marker(4);
    auto detail = planPbrVariant(surface, true, true);
    REQUIRE(detail.ok());
    CHECK_EQ(detail.value().canonicalTextureMask, 0b101u);
    CHECK_EQ(detail.value().detailTextureMask, 0b011u);
    CHECK_EQ(detail.value().fragmentTextures, 6u);
    CHECK_EQ(detail.value().fragmentSamplers, 3u);
    CHECK_EQ(detail.value().canonicalSamplerRepresentatives[0], 0u);
    CHECK_EQ(detail.value().canonicalSamplerRepresentatives[2], 0u);
    CHECK_EQ(detail.value().detailSamplerRepresentatives[0], 0u);
    CHECK_EQ(detail.value().detailSamplerRepresentatives[1], 0u);

    surface.vegetationDetail.maskMode            = 1;
    surface.vegetationDetail.textures[2].texture = marker(5);
    auto masked = planPbrVariant(surface, true, true);
    REQUIRE(masked.ok());
    CHECK_EQ(masked.value().detailTextureMask, 0b111u);
    CHECK_EQ(masked.value().fragmentTextures, 7u);
    CHECK_EQ(masked.value().fragmentSamplers, 3u);
}

TEST_CASE("graphics.webgpu.pbrVariantSeparatesVertexBudgetAndRejectsOverflow") {
    PbrSurface surface;
    surface.vegetationVertex.source  = PbrVegetationDeformationSource::GpuFields;
    surface.vegetationVertex.texture = marker(1);
    surface.vegetationMotion.mode    = PbrVegetationMotionMode::Object;
    surface.vegetationMotion.texture = marker(2);
    surface.vegetationMotion.noise   = marker(3);
    auto vertex = planPbrVariant(surface, false, false, {3, 3});
    REQUIRE(vertex.ok());
    CHECK_EQ(vertex.value().vertexTextures, 3u);
    CHECK_EQ(vertex.value().fragmentTextures, 0u);
    REQUIRE(!planPbrVariant(surface, false, false, {2, 3}).ok());

    for (std::size_t slot = 0; slot < std::size_t(PbrTextureSlot::Count); ++slot)
        surface.textures[slot].texture = marker(slot + 10);
    surface.vegetationExtras.texture = marker(30);
    surface.vegetationColors.texture = marker(31);
    REQUIRE(planPbrVariant(surface, true, true, {17, 16}).ok());
    surface.vegetationAlpha.enabled = true;
    surface.vegetationAlpha.noise   = marker(32);
    REQUIRE(planPbrVariant(surface, true, true, {17, 16}).ok());
    surface.vegetationDetail.value               = 1.f;
    surface.vegetationDetail.textures[0].texture = marker(33);
    surface.vegetationDetail.textures[1].texture = marker(34);
    auto overflow = planPbrVariant(surface, true, true, {17, 16});
    REQUIRE(!overflow.ok());
    CHECK_EQ(overflow.error()->code(), eve::DiagnosticCode::Unsupported);
}

TEST_CASE("graphics.webgpu.pbrVariantBaseSourcesCarryBothCanonicalStages") {
    const auto sources = pbrVariantBaseSources();
    CHECK(sources.vertex.starts_with("diagnostic(off, derivative_uniformity);"));
    CHECK(sources.vertex.find("struct Params") != std::string_view::npos);
    CHECK(sources.vertex.find("@vertex") != std::string_view::npos);
    CHECK(sources.vertex.find("vertexField_image") != std::string_view::npos);
    CHECK(sources.vertex.find("@binding(50u) var vertexField_image") != std::string_view::npos);
    CHECK(sources.fragment.starts_with("diagnostic(off, derivative_uniformity);"));
    CHECK(sources.fragment.find("struct Shadows") != std::string_view::npos);
    CHECK(sources.fragment.find("@fragment") != std::string_view::npos);
    CHECK(sources.fragment.find("detailMap2_image") != std::string_view::npos);
    CHECK(sources.fragment.find("vegetationFadeNoise_image") != std::string_view::npos);
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
