#include "common/Runtime.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <filesystem>
#include <fstream>
#include <sstream>

TEST_CASE("climbing.contactPresentation.fixedStepBlendReleasesAndRejectsAtomically") {
    const auto path = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path() /
                      "examples/climbing-motion-matching/main.nut";
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.is_open());
    std::ostringstream buffer;
    buffer << input.rdbuf();
    const auto source = buffer.str();
    // Execute the production functions without loading graphics or assets.
    const auto clampBegin = source.find("function clampf(");
    const auto clampEnd   = source.find("function edgePressed(", clampBegin);
    const auto begin      = source.find("function emptyContactBlend(");
    const auto end        = source.find("function configureContactPose(", begin);
    REQUIRE(clampBegin != std::string::npos);
    REQUIRE(clampEnd != std::string::npos);
    REQUIRE(begin != std::string::npos);
    REQUIRE(end != std::string::npos);
    eve::Runtime runtime(4096, ssq::Libs::ALL);
    runtime.runSource(source.substr(clampBegin, clampEnd - clampBegin) + source.substr(begin, end - begin));
    runtime.runSource(R"(
        local desired = emptyContactBlend();
        desired[0] = {weight=1.0, target={x=1.0, y=2.0, z=3.0}};
        desired[2] = {weight=1.0, target={x=0.0, y=0.5, z=0.2}};
        local initial = emptyContactBlend();
        local first = blendContactPose(initial, desired, 0.03);
        assert(abs(first[0].weight - 0.25) < 0.00001);
        assert(abs(first[2].weight - 0.25) < 0.00001);
        assert(initial[0].weight == 0.0);
        local split = blendContactPose(blendContactPose(initial, desired, 0.01), desired, 0.02);
        assert(abs(split[0].weight - first[0].weight) < 0.00001);
        local released = blendContactPose(first, emptyContactBlend(), 0.03);
        assert(released[0].weight == 0.0 && released[0].target == null);
        assert(released[2].weight == 0.0 && released[2].target == null);
        local malformed = emptyContactBlend();
        malformed[3].weight = 1.0;
        local rejected = false;
        try { blendContactPose(first, malformed, 0.03); }
        catch (error) { rejected = true; }
        assert(rejected && first[0].weight == 0.25);
        rejected = false;
        try { blendContactPose(first, desired, -0.01); }
        catch (error) { rejected = true; }
        assert(rejected && first[2].weight == 0.25);
    )");
}
