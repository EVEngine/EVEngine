#include <array>
#include <limits>
#include "fluids/OceanHullMask.h"
#include "zeroerr/unittest.h"

using namespace eve::fluids;
TEST_CASE("fluids.ocean.hullMaskPoseBevelAndOrigin") {
    OceanHullVolume hull;
    hull.center     = {1e8 + 3, 4, -1e8 + 2};
    hull.rotation   = glm::angleAxis(0.5, glm::normalize(glm::dvec3(1, 2, 3)));
    hull.halfExtent = {2, 1, 4};
    hull.bevels     = {{1, 0, 1, 4}};
    std::array<OceanHullVolume, 2> volumes{hull, hull};
    volumes[1].center += glm::dvec3(20, 0, 0);
    OceanHullMaskBuilder builder(volumes);
    builder.origin = {1e8, 0, -1e8};
    auto frame     = builder.build();
    REQUIRE(frame.ok());
    CHECK(frame.value().size() == 2);
    auto world = [&](glm::dvec3 local) { return hull.center + hull.rotation * local; };
    CHECK(frame.value().contains(world({0, 0, 0})));
    CHECK(frame.value().contains(world({1, 0, 2})));
    CHECK(!frame.value().contains(world({1.5, 0, 3})));
    CHECK(!frame.value().contains(world({0, 1.1, 0})));
    CHECK(!frame.value().contains(world({0, 0, 4.1})));
    CHECK(frame.value().contains(volumes[1].center));
    CHECK(!frame.value().contains(hull.center + glm::dvec3(10, 0, 0)));
    volumes[0].center = {0, 0, 0};
    CHECK(frame.value().contains(hull.center));
    CHECK(!frame.value().contains({std::numeric_limits<double>::quiet_NaN(), 0, 0}));
    std::array<OceanHullVolume, 1> box{};
    auto                           simple = OceanHullMaskBuilder(box).build();
    REQUIRE(simple.ok());
    CHECK(simple.value().contains({1, 1, 1}));
    CHECK(!simple.value().contains({1.01, 1, 1}));
    CHECK(!simple.value().contains({1e308, 0, 0}));
    auto empty = OceanHullMaskBuilder({}).build();
    REQUIRE(empty.ok());
    CHECK(!empty.value().contains({0, 0, 0}));
}
TEST_CASE("fluids.ocean.hullMaskRejectsWholeInvalidBatch") {
    std::array<OceanHullVolume, 2> volumes{};
    auto                           check = [&]() { return OceanHullMaskBuilder(volumes).build().ok(); };
    REQUIRE(check());
    volumes[1].rotation = {0, 0, 0, 0};
    CHECK(!check());
    volumes[1]              = {};
    volumes[1].halfExtent.x = -1;
    CHECK(!check());
    volumes[1]        = {};
    volumes[1].bevels = {{0, 0, 0, 1}};
    CHECK(!check());
    volumes[1] = {};
    volumes[1].bevels.resize(11, {1, 0, 0, 1});
    CHECK(!check());
    volumes[1]          = {};
    volumes[1].center.x = std::numeric_limits<double>::infinity();
    CHECK(!check());
    std::vector<OceanHullVolume> tooMany(33);
    CHECK(!OceanHullMaskBuilder(tooMany).build().ok());
    volumes[1]        = {};
    volumes[1].bevels = {{2, 0, 0, 1}};
    auto normalized   = OceanHullMaskBuilder(std::span(volumes).subspan(1)).build();
    REQUIRE(normalized.ok());
    CHECK(normalized.value().contains({0.4, 0, 0}));
    CHECK(!normalized.value().contains({0.6, 0, 0}));
}
