#include <limits>
#include "graphics/Grass.h"
#include "graphics/VegetationWind.h"
#include "zeroerr/unittest.h"
TEST_CASE("graphics.VegetationWind.packetControlsAndExplicitTime") {
    eve::graphics::VegetationWindState   state{{1, -0.5F, 0.2F}, 0.8F, 0.17F};
    eve::graphics::VegetationWindProfile profile;
    auto                                 disabled = eve::graphics::packVegetationWind(state, profile, 2.0);
    REQUIRE(disabled.ok());
    CHECK(disabled.value()[5] == 0);
    profile.enabled   = true;
    profile.billboard = true;
    profile.alphaTest = false;
    auto packet       = eve::graphics::packVegetationWind(state, profile, 2.0);
    REQUIRE(packet.ok());
    const auto& data = packet.value();
    CHECK(data[0] == state.direction.x);
    CHECK(data[1] == state.direction.y);
    CHECK(data[2] == state.direction.z);
    CHECK(data[3] == state.strength);
    CHECK(data[4] == state.phase);
    CHECK(data[5] == -100);
    CHECK(data[6] == profile.flex.x);
    CHECK(data[7] == profile.flex.y);
    CHECK(data[8] == 0);
    CHECK(data[9] == profile.frequency.x);
    CHECK(data[10] == profile.frequency.y);
    CHECK(data[11] == profile.frequency.z);
    CHECK(std::abs(data[12] - 0.479425539F) < 0.000001F);
    CHECK(std::abs(data[13] - 0.909297427F) < 0.000001F);
    profile.billboard = false;
    profile.alphaTest = true;
    auto complete     = eve::graphics::packVegetationWind(state, profile, 2.0);
    REQUIRE(complete.ok());
    CHECK(complete.value()[5] == 100);
    CHECK(complete.value()[8] == profile.flex.z);
}
TEST_CASE("graphics.VegetationWind.packetRejectsInvalidDisabledInputs") {
    eve::graphics::VegetationWindState   state;
    eve::graphics::VegetationWindProfile profile;
    CHECK(!eve::graphics::packVegetationWind(state, profile, std::numeric_limits<double>::infinity()).ok());
    profile.maximumDistance = 0;
    CHECK(!eve::graphics::packVegetationWind(state, profile, 0).ok());
    profile.maximumDistance = 100;
    profile.frequency.x     = std::numeric_limits<float>::quiet_NaN();
    CHECK(!eve::graphics::packVegetationWind(state, profile, 0).ok());
    profile.frequency.x = 0.25F;
    state.strength      = 2;
    CHECK(!eve::graphics::packVegetationWind(state, profile, 0).ok());
    CHECK(!profile.enabled);
    CHECK(state.strength == 2);
}
TEST_CASE("graphics.Grass.independentBillboardDimensions") {
    std::vector<eve::graphics::grass::Point> points(1);
    points[0].position   = {3, 4, 5};
    points[0].id         = 11;
    points[0].scale      = 2;
    points[0].widthScale = 0.25F;
    const auto dense     = eve::graphics::grass::buildBillboards(points, 1, 1, false);
    const auto dark      = eve::graphics::grass::buildBillboards(points, 1, 1, true);
    for (int i = 0; i < 4; ++i) {
        CHECK(dense.posXYZ[size_t(i) * 3] == 3);
        CHECK(dense.posXYZ[size_t(i) * 3 + 1] == 4);
        CHECK(dense.nrmXYZ[size_t(i) * 3] == 11);
        CHECK(dense.nrmXYZ[size_t(i) * 3 + 1] == 2);
        CHECK(dense.nrmXYZ[size_t(i) * 3 + 2] == -0.25F);
        CHECK(dark.nrmXYZ[size_t(i) * 3 + 2] == 1.25F);
    }
    points[0].widthScale  = 0;
    const auto legacy     = eve::graphics::grass::buildBillboards(points, 1, 1, false);
    const auto legacyDark = eve::graphics::grass::buildBillboards(points, 1, 1, true);
    CHECK(legacy.nrmXYZ[2] == 0);
    CHECK(legacyDark.nrmXYZ[2] == 1);
    CHECK(legacy.uvST == dense.uvST);
    CHECK(legacy.indices == dense.indices);
    points[0].tint           = {0.2F, 0.4F, 0.8F};
    const auto     tinted    = eve::graphics::grass::buildBillboards(points, 1, 1, false);
    const uint32_t packedRgb = (51u << 16) | (102u << 8) | 204u;
    for (int i = 0; i < 4; ++i) CHECK(tinted.nrmXYZ[size_t(i) * 3] == -float(packedRgb + 1u));
}

TEST_CASE("graphics.VegetationWind.phaseAndSmoothing") {
    eve::graphics::VegetationWindState state;
    REQUIRE(eve::graphics::advanceVegetationWind(state, {1, 0, 0}, 1, 4).ok());
    CHECK(state.direction.x == 1);
    CHECK(state.direction.y == -0.5F);
    CHECK(state.strength == 1);
    CHECK(std::abs(state.phase - 0.4F) < 0.000001F);
    state.phase       = 99.9F;
    state.updatePhase = 99.9F;
    REQUIRE(eve::graphics::advanceVegetationWind(state, {1, 0, 0}, 1, 4).ok());
    CHECK(std::abs(state.phase - 0.3F) < 0.00001F);
    const auto saved = state;
    CHECK(!eve::graphics::advanceVegetationWind(state, {1, 0, 0}, 1, -1).ok());
    CHECK(state.phase == saved.phase);
}
TEST_CASE("graphics.VegetationWind.rootDistanceAndBillboardGolden") {
    eve::graphics::VegetationWindInput input;
    eve::graphics::VegetationWindState wind;
    wind.direction = {1, 0, 0};
    wind.strength  = 1;
    auto root      = eve::graphics::evaluateVegetationWind(input, wind);
    REQUIRE(root.ok());
    CHECK(root.value() == glm::vec3(0));
    input.position  = {0, 1, 0};
    input.billboard = true;
    input.flex      = {1, 0, 0};
    // phase=0, sine time=0: gust=(0.5+1)*0.7=1.05; source volume normalization is 1/(1+1.05^2).
    auto bent = eve::graphics::evaluateVegetationWind(input, wind);
    REQUIRE(bent.ok());
    CHECK(std::abs(bent.value().x - float(1.05 / 2.1025)) < 0.000001F);
    CHECK(std::abs(bent.value().y - float(1 / 2.1025)) < 0.000001F);
    input.cameraPosition = {100, 0, 0};
    auto far             = eve::graphics::evaluateVegetationWind(input, wind);
    REQUIRE(far.ok());
    CHECK(far.value() == input.position);
    input.objectToWorld = glm::mat3(0);
    CHECK(!eve::graphics::evaluateVegetationWind(input, wind).ok());
}

TEST_CASE("graphics.VegetationWind.branchLeafAndScaledTransformGolden") {
    eve::graphics::VegetationWindInput input;
    input.position            = {0.2F, 0.8F, 0.1F};
    input.worldOffset         = {3, 4, 5};
    input.cameraPosition      = input.worldOffset;
    input.objectToWorld[0][0] = 2;
    input.objectToWorld[1][1] = 3;
    input.objectToWorld[2][2] = 0.5F;
    input.sinTimeQuarter      = 0.3F;
    input.sinTimeFull         = -0.2F;
    eve::graphics::VegetationWindState state;
    state.direction            = {1, -0.5F, 0.2F};
    state.strength             = 0.8F;
    state.phase                = 0.17F;
    const glm::vec3 expected[] = {{0.535718825F, 0.604993208F, 0.383200569F},
                                  {0.664822256F, 0.513757707F, 0.489274523F},
                                  {0.664999698F, 0.513719712F, 0.491201362F}};
    for (int i = 0; i < 3; ++i) {
        input.billboard = i == 0;
        input.alphaTest = i != 1;
        auto result     = eve::graphics::evaluateVegetationWind(input, state);
        REQUIRE(result.ok());
        for (int axis = 0; axis < 3; ++axis) CHECK(std::abs(result.value()[axis] - expected[i][axis]) < 0.000002F);
    }
}
