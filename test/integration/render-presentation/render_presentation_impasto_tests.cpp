#include "graphics/Graphics.h"
#include "graphics/Shader.h"
#include "stylize/StyleShaders.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "stylize/StyleInstance.h"

#include <cmath>

TEST_CASE("stylize.impasto.independentMaterialContract") {
    using namespace eve::stylize;
    REQUIRE(styleSupports("impasto", "mesh"));
    REQUIRE(!styleSupports("impasto", "post"));
    REQUIRE(!styleSupports("impasto", "depth"));
    StyleInstance instance("impasto");
    REQUIRE_EQ(instance.getFloat("rimErosion"), 0.8f);
    instance.setFloat("strokeScale", 0.f);
    REQUIRE_EQ(instance.getFloat("strokeScale"), 0.1f);
    bool rejectedUnknown = false;
    try {
        instance.setFloat("unknownPaintParameter", 1.f);
    } catch (const std::exception&) {
        rejectedUnknown = true;
    }
    REQUIRE(rejectedUnknown);
    eve::graphics::Shader shader;
    bindMeshUniforms(&shader, "impasto");
    REQUIRE_EQ(shader.usedFloats(), styleParamCount("impasto"));
    REQUIRE_EQ(shader.usedFloats(), 32);
    REQUIRE_EQ(instance.getFloat("occlusionStrength"), 0.f);
    REQUIRE_EQ(instance.getFloat("packedBrush"), 0.f);
    REQUIRE_EQ(instance.getFloat("layer"), 0.f);
    REQUIRE(shader.usedFloats() <= int(eve::graphics::Shader::kMaxFloats));
    for (int index = 0; index < styleParamCount("impasto"); ++index) {
        const auto* param = styleParameterAt("impasto", index);
        REQUIRE(param != nullptr);
        float value = 0.f;
        REQUIRE_EQ(shader.getFromVar(param->id, &value, sizeof(value)), int(sizeof(value)));
        REQUIRE_EQ(value, param->defaultValue);
        REQUIRE(std::isfinite(value));
        REQUIRE(value >= param->minValue);
        REQUIRE(value <= param->maxValue);
    }
    // Existing watercolor recipe remains separate, including its original defaults.
    REQUIRE(styleSupports("watercolor", "post"));
    REQUIRE(!styleSupports("watercolor", "mesh"));
    REQUIRE_EQ(findStyleParameter("watercolor", "paperStrength")->defaultValue, 0.65f);
    REQUIRE(findStyleParameter("watercolor", "strokeScale") == nullptr);
}


TEST_CASE("stylize.impasto.realGpuPipeline") {
    auto*                       window   = eve::window::Window::create();
    auto*                       graphics = eve::graphics::Graphics::create();
    eve::window::WindowSettings settings;
    settings.width  = 160;
    settings.height = 120;
    REQUIRE(window->setWindowSettings(settings));
    auto* shader = eve::stylize::createMeshShader(graphics, "impasto");
    REQUIRE(shader != nullptr);
    REQUIRE(shader->gpuHandle != nullptr);
    REQUIRE_EQ(shader->getKind(), eve::graphics::Shader::Kind::eMesh3D);
    REQUIRE(shader->hasUniform("strokeScale"));
    REQUIRE(shader->hasUniform("rimErosion"));
}
