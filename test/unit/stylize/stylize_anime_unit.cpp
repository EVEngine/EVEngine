#include <cmath>
#include "graphics/Graphics.h"
#include "graphics/Shader.h"
#include "stylize/StyleShaders.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("stylize.anime.materialContract") {
    using namespace eve::stylize;
    REQUIRE(styleSupports("anime", "mesh"));
    REQUIRE(!styleSupports("anime", "post"));
    REQUIRE(!styleSupports("anime", "cpu"));
    eve::graphics::Shader shader;
    bindMeshUniforms(&shader, "anime");
    REQUIRE_EQ(shader.usedFloats(), styleParamCount("anime"));
    REQUIRE(shader.usedFloats() <= int(eve::graphics::Shader::kMaxFloats));
    for (int i = 0; i < styleParamCount("anime"); ++i) {
        const auto* param = styleParameterAt("anime", i);
        REQUIRE(param != nullptr);
        float value = 0.f;
        REQUIRE_EQ(shader.getFromVar(param->id, &value, sizeof(value)), int(sizeof(value)));
        REQUIRE_EQ(value, param->defaultValue);
        REQUIRE(value >= param->minValue);
        REQUIRE(value <= param->maxValue);
        REQUIRE(std::isfinite(value));
    }
    REQUIRE_EQ(findStyleParameter("anime", "skinDetail")->defaultValue, 1.f);
    REQUIRE(findStyleParameter("anime", "posterize") == nullptr);
}
