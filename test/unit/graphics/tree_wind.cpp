#include <algorithm>
#include <array>
#include <limits>
#include "graphics/Shader.h"
#include "graphics/TreeWind.h"
#include "graphics/VegetationWind.h"
#include "zeroerr/unittest.h"

namespace {
void declareTreeLayout(eve::graphics::Shader& shader) {
    shader.declareVec4("windGlobals");
    shader.declareVec2("windPhaseDistance");
    shader.declareVec3("windFlex");
    shader.declareVec3("windFrequency");
    shader.declareVec2("windSineTime");
    shader.declareVec2("treeDimensions");
}
std::array<float, 32> snapshot(const eve::graphics::Shader& shader) {
    std::array<float, 32> values{};
    std::copy_n(shader.pushConstantData(), 32, values.begin());
    return values;
}
}  // namespace

TEST_CASE("graphics.TreeWind.appliesPcgBendAndDimensions") {
    using namespace eve::graphics;
    Shader shader;
    declareTreeLayout(shader);
    VegetationWindState state{{1, -0.5F, 0.25F}, 0.8F, 0.17F};
    VegetationWindProfile vegetation;
    vegetation.enabled = true;
    TreeWindProfile tree{{4, 9}, 0.35F};
    REQUIRE(applyTreeWind(shader, state, vegetation, tree, 2).ok());
    auto packed = packVegetationWind(state, vegetation, 2);
    REQUIRE(packed.ok());
    CHECK(shader.usedFloats() == 16);
    CHECK(shader.pushConstantData()[0] == packed.value()[0]);
    CHECK(shader.pushConstantData()[6] == packed.value()[6] * tree.bendFactor);
    CHECK(shader.pushConstantData()[7] == packed.value()[7] * tree.bendFactor);
    CHECK(shader.pushConstantData()[8] == packed.value()[8] * tree.bendFactor);
    CHECK(shader.pushConstantData()[14] == 4);
    CHECK(shader.pushConstantData()[15] == 9);
}

TEST_CASE("graphics.TreeWind.validationAndSchemaFailuresAreAtomic") {
    using namespace eve::graphics;
    Shader shader;
    declareTreeLayout(shader);
    VegetationWindState state{{1, 0, 0}, 1, 0.2F};
    VegetationWindProfile vegetation;
    vegetation.enabled = true;
    TreeWindProfile tree{{2, 6}, 0.5F};
    REQUIRE(applyTreeWind(shader, state, vegetation, tree, 0).ok());
    const auto before = snapshot(shader);
    tree.widthHeight.y = 0;
    CHECK(!applyTreeWind(shader, state, vegetation, tree, 0).ok());
    CHECK(snapshot(shader) == before);
    tree = {{2, 6}, std::numeric_limits<float>::infinity()};
    CHECK(!applyTreeWind(shader, state, vegetation, tree, 0).ok());
    CHECK(snapshot(shader) == before);

    Shader wrong;
    wrong.declareVec4("windGlobals");
    wrong.declareVec2("windPhaseDistance");
    wrong.declareVec3("windFlex");
    wrong.declareVec3("windFrequency");
    wrong.declareVec2("wrongSineTime");
    wrong.declareVec2("treeDimensions");
    const auto wrongBefore = snapshot(wrong);
    tree = {{2, 6}, 0.5F};
    CHECK(!applyTreeWind(wrong, state, vegetation, tree, 0).ok());
    CHECK(snapshot(wrong) == wrongBefore);
}
