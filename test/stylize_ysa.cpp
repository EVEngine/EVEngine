#include <cmath>

#include "graphics/Graphics.h"
#include "graphics/Shader.h"
#include "stylize/StyleShaders.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("stylize.ysa.independentMaterialContract") {
    using namespace eve::stylize;
    REQUIRE(styleSupports("ysa", "mesh"));
    REQUIRE(!styleSupports("ysa", "post"));
    REQUIRE(!styleSupports("ysa", "cpu"));
    eve::graphics::Shader shader;
    bindMeshUniforms(&shader, "ysa");
    REQUIRE_EQ(shader.usedFloats(), styleParamCount("ysa"));
    REQUIRE(shader.usedFloats() <= int(eve::graphics::Shader::kMaxFloats));
    for (int i = 0; i < styleParamCount("ysa"); ++i) {
        const auto* parameter = styleParameterAt("ysa", i);
        REQUIRE(parameter != nullptr);
        float value = 0.f;
        REQUIRE_EQ(shader.getFromVar(parameter->id, &value, sizeof(value)), int(sizeof(value)));
        REQUIRE_EQ(value, parameter->defaultValue);
        REQUIRE(std::isfinite(value));
        REQUIRE(value >= parameter->minValue);
        REQUIRE(value <= parameter->maxValue);
    }
    // The new material must not alias the existing toon parameter layout.
    REQUIRE(findStyleParameter("ysa", "bands") == nullptr);
    REQUIRE(findStyleParameter("cartoon", "lightMidpoint") == nullptr);
    eve::graphics::Shader legacy;
    bindMeshUniforms(&legacy, "cartoon");
    float bands = 0.f;
    REQUIRE_EQ(legacy.getFromVar("bands", &bands, sizeof(bands)), int(sizeof(bands)));
    REQUIRE_EQ(bands, 4.f);
}

TEST_CASE("stylize.ysa.realGpuPipeline") {
    auto*                       window   = eve::window::Window::create();
    auto*                       graphics = eve::graphics::Graphics::create();
    eve::window::WindowSettings settings;
    settings.width  = 160;
    settings.height = 120;
    REQUIRE(window->setWindowSettings(settings));
    auto* shader = eve::stylize::createMeshShader(graphics, "ysa");
    REQUIRE(shader != nullptr);
    REQUIRE(shader->gpuHandle != nullptr);
    REQUIRE_EQ(shader->getKind(), eve::graphics::Shader::Kind::eMesh3D);
    REQUIRE(shader->hasUniform("lightMidpoint"));
    REQUIRE(shader->hasUniform("rimDynamic"));
}
